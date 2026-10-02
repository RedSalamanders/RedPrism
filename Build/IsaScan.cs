using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;

namespace RedPrismBuild
{
    public sealed class IsaScanResult
    {
        public long Instructions;
        public long VectorInstructions;
        public long EvexInstructions;
        public long RuntimeInstructions;
        public long TierInstructions;
        public long TierEvexInstructions;
        public long UndecodedPrefixes;
        public long InlineTableVectorData;
        public readonly List<string> Violations = new List<string>();
    }

    // Build-time parser, kept out of the product. All addresses are checked against the actual PE and its
    // matching CodeView GUID/age. PDB section offsets are DECIMAL, disassembly addresses are hexadecimal.
    public static class IsaScanner
    {
        private sealed class Section
        {
            public uint Rva, Raw, Size;
            public bool Executable;
        }
        private sealed class Contribution
        {
            public ulong Begin, End;
            public int Module;
        }
        private sealed class Function
        {
            public ulong Begin, End;
            public ulong ContributionBegin, ContributionEnd;
            public int Module;
            public string Name;
        }
        private sealed class DecodedInstruction
        {
            public int Length;
            public bool Unknown;
            public int Module;
        }
        private sealed class Module
        {
            public string Object, Library;
        }
        private sealed class InlineTable
        {
            public ulong Begin, End;
            public ulong ContributionBegin, ContributionEnd;
            public int Module;
            public readonly HashSet<ulong> PendingTargets = new HashSet<ulong>();
            public readonly List<ulong> Targets = new List<ulong>();
        }
        private static readonly Regex ModuleLine = new Regex(@"^\s*Mod (\d+) \| `(.+)`:", RegexOptions.Compiled);
        private static readonly Regex ObjectLine = new Regex(@"^\s*Obj: `(.+)`:", RegexOptions.Compiled);
        private static readonly Regex ContributionLine = new Regex(@"^\s*SC\[.+?\]\s*\| mod = (\d+), (\d+):(\d+), size = (\d+),", RegexOptions.Compiled);
        private static readonly Regex FunctionLine = new Regex(@"\| S_[GL]PROC32(?:_ID)? \[size = \d+\] `(.+)`", RegexOptions.Compiled);
        private static readonly Regex FunctionAddress = new Regex(@"addr = (\d+):(\d+), code size = (\d+)", RegexOptions.Compiled);
        private static readonly Regex DataLine = new Regex(@"\| S_LDATA32 \[size = 16\] ``$", RegexOptions.Compiled);
        private static readonly Regex DataAddress = new Regex(@"type = <no type>, addr = (\d+):(\d+)$", RegexOptions.Compiled);
        private static readonly Regex InstructionLine = new Regex(@"^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s+)+)\s*(\S.*)$", RegexOptions.Compiled);
        private static readonly Regex TierObject = new Regex(@"^Kernels_(Avx2|Avx512)_[A-Za-z][A-Za-z0-9_]*\.obj$", RegexOptions.Compiled);

        private static uint Offset(uint rva, List<Section> sections, int length)
        {
            foreach (Section section in sections)
                if (rva >= section.Rva && (ulong)rva < (ulong)section.Rva + section.Size)
                {
                    ulong offset = (ulong)section.Raw + rva - section.Rva;
                    if (offset >= (ulong)length) break;
                    return checked((uint)offset);
                }
            throw new InvalidDataException("PE RVA is not backed by file bytes.");
        }
        private static ulong Address(int section, ulong offset, ulong imageBase, List<Section> sections)
        {
            if (section < 1 || section > sections.Count || offset > sections[section - 1].Size)
                throw new InvalidDataException("PDB contribution is outside the PE sections.");
            return checked(imageBase + sections[section - 1].Rva + offset);
        }
        private static int FindContribution(List<Contribution> contributions, ulong address)
        {
            int lo = 0, hi = contributions.Count - 1;
            while (lo <= hi)
            {
                int mid = lo + (hi - lo) / 2;
                if (contributions[mid].Begin > address) hi = mid - 1;
                else lo = mid + 1;
            }
            return hi >= 0 && address < contributions[hi].End ? hi : -1;
        }
        private static bool Contains(string[] values, string value)
        {
            foreach (string candidate in values)
                if (String.Equals(candidate, value, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }
        private static bool IsLegacyPrefix(byte value)
        {
            return value == 0x26 || value == 0x2e || value == 0x36 || value == 0x3e || value == 0x64 ||
                   value == 0x65 || value == 0x66 || value == 0x67 || value == 0xf0 || value == 0xf2 || value == 0xf3;
        }

        private static bool StartsWith(byte[] bytes, params byte[] prefix)
        {
            if (bytes.Length < prefix.Length) return false;
            for (int i = 0; i < prefix.Length; ++i) if (bytes[i] != prefix[i]) return false;
            return true;
        }

        private static bool MatchesPe(byte[] pe, List<Section> sections, ulong imageBase,
            ulong address, byte[] encoded)
        {
            if (address < imageBase || address - imageBase > UInt32.MaxValue) return false;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return false; }
            if (raw > pe.Length - encoded.Length) return false;
            for (int i = 0; i < encoded.Length; ++i) if (pe[raw + i] != encoded[i]) return false;
            return true;
        }

        private static bool MatchesDecodedPe(byte[] pe, List<Section> sections, ulong imageBase,
            ulong address, byte[] encoded, List<Contribution> contributions,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts)
        {
            int owner = FindContribution(contributions, address);
            if (owner < 0 || !IsDecodedBoundary(address, contributions[owner].Module, contributions,
                decoded, decodedStarts) || decoded[address].Length != encoded.Length) return false;
            int next = decodedStarts.BinarySearch(address) + 1;
            if (next < decodedStarts.Count && decodedStarts[next] < address + (ulong)encoded.Length) return false;
            return MatchesPe(pe, sections, imageBase, address, encoded);
        }

        private static bool IsTableData(InlineTable table, List<InlineTable> tables, ulong address, string[] bytes, byte[] pe,
                                        List<Section> sections, ulong imageBase)
        {
            if (table.PendingTargets.Count != 0 || address < table.Begin || address >= table.End) return false;
            ulong end = checked(address + (ulong)bytes.Length);
            if (end <= table.End) return true;
            // One decoded row can straddle two independently proved adjacent DWORD tables (OnCommand ASan).
            // Extend only over an exact touching table in the same contribution whose dispatch and all target
            // boundaries were proved. Missing proof, an intervening byte, or a different contribution stops it.
            ulong provedEnd = table.End;
            while (provedEnd < end)
            {
                InlineTable adjacent = null;
                foreach (InlineTable candidate in tables)
                    if (candidate.Begin == provedEnd && candidate.End > provedEnd && candidate.Module == table.Module &&
                        candidate.ContributionBegin == table.ContributionBegin &&
                        candidate.ContributionEnd == table.ContributionEnd && candidate.PendingTargets.Count == 0)
                    { adjacent = candidate; break; }
                if (adjacent == null) break;
                provedEnd = adjacent.End;
            }
            // Linear disassembly can consume INT3 alignment after a proven table (observed in DxUi.Menu).
            // Only a row starting in table data, no longer than an x64 instruction, may cross that end. Verify
            // every reported byte against the PE and require the complete outside suffix to be 0xCC. A vector
            // starting at/after the table, a nonpadding suffix or an unproved table remains a normal violation.
            if (bytes.Length > 15 || address < imageBase || address - imageBase > UInt32.MaxValue) return false;
            uint rva = checked((uint)(address - imageBase));
            foreach (Section section in sections)
                if (rva >= section.Rva && checked(end - imageBase) <= (ulong)section.Rva + section.Size)
                {
                    int raw = checked((int)Offset(rva, sections, pe.Length));
                    if (raw > pe.Length - bytes.Length) return false;
                    for (int i = 0; i < bytes.Length; ++i)
                        if (pe[raw + i] != Byte.Parse(bytes[i], NumberStyles.HexNumber) ||
                            (address + (ulong)i >= provedEnd && pe[raw + i] != 0xcc)) return false;
                    return true;
                }
            return false;
        }

        // The observed inlined clipboard switch hoists LEA r8,imagebase across this exact eight-instruction
        // prelude. Each instruction preserves R8; do not skip arbitrary instructions or accept a partial form.
        private static bool R8Prelude(byte[] bytes, int step)
        {
            switch (step)
            {
                case 0: return bytes.Length == 2 && StartsWith(bytes, 0x8b, 0xd0); // mov edx,eax
                case 1: return bytes.Length == 4 && StartsWith(bytes, 0x89, 0x44, 0x24); // stack store
                case 2: return bytes.Length == 3 && StartsWith(bytes, 0x44, 0x8b, 0xe0); // mov r12d,eax
                case 3: return bytes.Length == 2 && StartsWith(bytes, 0x85, 0xc0); // test eax,eax
                case 4: return bytes.Length == 6 && StartsWith(bytes, 0x0f, 0x84); // je rel32
                case 5: return bytes.Length == 3 && StartsWith(bytes, 0x8d, 0x40); // lea eax,[rax+imm8]
                case 6: return bytes.Length == 3 && StartsWith(bytes, 0x83, 0xf8); // cmp eax,imm8
                case 7: return bytes.Length == 2 && bytes[0] == 0x77; // ja rel8
                default: return false;
            }
        }

        // Recognize only MSVC's observed image-relative DWORD switch-table dispatch. This is data proof,
        // not a procedure/object exemption: a decoded LEA of the image base, optional byte-index load,
        // DWORD indexed load, ADD and JMP must be contiguous; intervening forms are R8Prelude above and the exact
        // MOVZX EAX,DL in DxUi typography's dispatch. The latter and the RDX-base/RCX-target form in WIC
        // orientation dispatch require the anonymous same-owner PDB data start. The observed Menu form uses
        // R9 as image base, one EAX byte-index load, an R8D DWORD load, ADD R8,R9 and JMP R8; it requires that
        // same PDB proof too. No other R9 sequence or intervening instruction is accepted. RunFilter's RDI-indexed
        // RDX-base/EAX-target form has the same exact PDB/RET/target proof and PE-checked contiguous dispatch.
        // That form also requires an exact PDB data start. The table follows RET plus at most three alignment
        // bytes, belongs to that contribution, and is bounded by the next same-owner anonymous data symbol when
        // present. Every entry must name a decoded instruction boundary; backward entries also need one exact
        // same-owner PDB procedure containing both the dispatch and target. Unrecognized forms remain gated.
        private static InlineTable ReadInlineTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures, ulong imageBase,
            ulong dispatch, ulong afterJump, uint tableRva, bool requireDataSymbol, bool signedByteIndex)
        {
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong begin = checked(imageBase + tableRva);
            int dataOwner;
            // Release std::variant uses index -1 for its default case: MOVSX RAX,byte [RBP+disp8/disp32] immediately
            // precedes LEA RCX,imagebase, and the DWORD load is biased by exactly one entry. Prove the complete
            // table, default included, from the exact anonymous same-owner PDB symbol; never skip an arbitrary
            // range before a table load or weaken normal dispatch/target bounds.
            if (signedByteIndex)
            {
                if (begin < imageBase + 4 || !dataSymbols.TryGetValue(begin - 4, out dataOwner) ||
                    dataOwner != owner.Module)
                    return null;
                begin -= 4;
                tableRva -= 4;
                requireDataSymbol = true;
            }
            if ((begin & 3) != 0 || begin <= afterJump || begin + 8 > owner.End || afterJump > owner.End)
                return null;
            int raw = checked((int)Offset(tableRva, sections, pe.Length));
            bool afterReturn = raw >= 1 && pe[raw - 1] == 0xc3;
            afterReturn |= raw >= 2 && pe[raw - 2] == 0xc3 && pe[raw - 1] == 0x90;
            afterReturn |= raw >= 3 && pe[raw - 3] == 0xc3 && pe[raw - 2] == 0x66 && pe[raw - 1] == 0x90;
            afterReturn |= raw >= 4 && pe[raw - 4] == 0xc3 && pe[raw - 3] == 0x0f &&
                           pe[raw - 2] == 0x1f && pe[raw - 1] == 0x00;
            if (requireDataSymbol && (!dataSymbols.TryGetValue(begin, out dataOwner) || dataOwner != owner.Module))
                return null;
            if (!afterReturn && (!dataSymbols.TryGetValue(begin, out dataOwner) || dataOwner != owner.Module)) return null;
            ulong nextDataSymbol = 0;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < owner.End &&
                    (nextDataSymbol == 0 || symbol.Key < nextDataSymbol)) nextDataSymbol = symbol.Key;
            var table = new InlineTable { Begin = begin, End = begin, Module = owner.Module,
                ContributionBegin = owner.Begin, ContributionEnd = owner.End };
            for (int entry = 0; entry < 4096 && table.End + 4 <= owner.End && raw + 4 <= pe.Length; ++entry, raw += 4)
            {
                if (entry != 0 && dataSymbols.ContainsKey(table.End)) break;
                if (nextDataSymbol != 0 && table.End + 4 > nextDataSymbol) return null;
                ulong target = checked(imageBase + BitConverter.ToUInt32(pe, raw));
                if (target >= begin || (target < afterJump && !SameProcedure(procedures, owner.Module, dispatch, target)))
                {
                    if (nextDataSymbol != 0) return null;
                    break;
                }
                table.PendingTargets.Add(target);
                table.Targets.Add(target);
                table.End += 4;
            }
            if (nextDataSymbol != 0 && table.End != nextDataSymbol) return null;
            return table.End - table.Begin >= 8 ? table : null;
        }

        // DxUi's ASan grid switch ends with six DWORDs inside a much larger contribution. Its next
        // anonymous data symbol belongs to another procedure. Application::EditUnderWay uses the same
        // terminal dispatch with a long JA; EndCanvasEdit uses its disp32 stack form. Prove only these
        // exact stack guards and RCX dispatches, every reported/PE byte and the exact terminal procedure end.
        private static InlineTable ReadGuardedTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            return ReadGuardedTerminalDwordTableForm(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows, false) ??
                ReadGuardedTerminalDwordTableForm(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows, true) ??
                ReadGuardedTerminalDwordTableForm(pe, sections, contributions, dataSymbols, procedures,
                    imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows, true, true);
        }

        private static InlineTable ReadGuardedTerminalDwordTableForm(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows, bool longJa, bool wideFrame = false)
        {
            int stackLength = wideFrame ? 8 : 5;
            int guardLength = 2 * stackLength + (longJa ? 6 : 2);
            int branchLength = longJa ? 6 : 2;
            int indexStart = stackLength + branchLength;
            if (dispatch < imageBase + (ulong)guardLength || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + 19)
                return null;
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong begin = checked(imageBase + tableRva), prefix = dispatch - (ulong)guardLength;
            if ((begin & 3) != 0 || begin <= afterJump ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - (guardLength + 19) ||
                !StartsWithAt(pe, raw, 0x83, wideFrame ? (byte)0xbc : (byte)0x7c, 0x24) ||
                (longJa ? !StartsWithAt(pe, raw + stackLength, 0x0f, 0x87) : pe[raw + stackLength] != 0x77) ||
                !StartsWithAt(pe, raw + indexStart, 0x48, 0x63, wideFrame ? (byte)0x84 : (byte)0x44, 0x24) ||
                (wideFrame ? BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + indexStart + 4) :
                 pe[raw + 3] != pe[raw + indexStart + 4]) ||
                !StartsWithAt(pe, raw + guardLength, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + guardLength + 3) != (long)imageBase ||
                !StartsWithAt(pe, raw + guardLength + 7, 0x8b, 0x84, 0x81) ||
                BitConverter.ToUInt32(pe, raw + guardLength + 10) != tableRva ||
                !StartsWithAt(pe, raw + guardLength + 14, 0x48, 0x03, 0xc1) ||
                !StartsWithAt(pe, raw + guardLength + 17, 0xff, 0xe0)) return null;
            uint maximum = pe[raw + stackLength - 1];
            if (maximum == 0 || maximum >= 128) return null; // CMP imm8 must be nonnegative
            int[] starts = { 0, stackLength, indexStart, guardLength, guardLength + 7, guardLength + 14, guardLength + 17 };
            int[] lengths = { stackLength, branchLength, stackLength, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong end = checked(begin + 4 * (maximum + 1));
            if (end > owner.End) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            long defaultAddress = longJa ? (long)prefix + stackLength + 6 + BitConverter.ToInt32(pe, raw + stackLength + 2) :
                (long)prefix + stackLength + 2 + unchecked((sbyte)pe[raw + stackLength + 1]);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || prefix < procedure.Begin || begin < procedure.Begin ||
                    afterJump > begin || procedure.End != end) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
                if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < procedure.Begin || target >= begin ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
                if (defaultAddress >= 0 && (longJa
                    ? (ulong)defaultAddress >= procedure.Begin && (ulong)defaultAddress < begin &&
                      IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)
                    : table.Targets.Contains((ulong)defaultAddress))) return table;
            }
            return null;
        }

        // Release may inline KernelIsaName as an unsigned byte switch. Prove only the observed ECX index
        // and RDX image-base dispatch, with exact guard-derived table size and terminal procedure end.
        // This is data proof; no object/procedure exemption or new runtime allowlist entry is involved.
        private static InlineTable ReadGuardedRdxTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            return ReadGuardedRdxEcxTerminalDwordTable(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows) ??
                ReadGuardedRdxAlTerminalDwordTable(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows);
        }

        private static InlineTable ReadGuardedRdxEcxTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 8 || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + 19)
                return null;
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong begin = checked(imageBase + tableRva), prefix = dispatch - 8;
            if ((begin & 3) != 0 || begin <= afterJump ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 27 ||
                !StartsWithAt(pe, raw, 0x0f, 0xb6, 0x08, 0x80, 0xf9) || pe[raw + 6] != 0x77 ||
                !StartsWithAt(pe, raw + 8, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 11) != (long)imageBase ||
                !StartsWithAt(pe, raw + 15, 0x8b, 0x84, 0x8a) ||
                BitConverter.ToUInt32(pe, raw + 18) != tableRva ||
                !StartsWithAt(pe, raw + 22, 0x48, 0x03, 0xc2, 0xff, 0xe0)) return null;
            uint maximum = pe[raw + 5];
            if (maximum == 0 || maximum >= 128) return null; // bounded observed form, not an arbitrary byte range
            int[] starts = { 0, 3, 6, 8, 15, 22, 25 };
            int[] lengths = { 3, 3, 2, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong end = checked(begin + 4 * (maximum + 1));
            if (end > owner.End) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            long defaultAddress = (long)prefix + 8 + unchecked((sbyte)pe[raw + 7]);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || prefix < procedure.Begin || begin < procedure.Begin ||
                    afterJump > begin || procedure.End != end || defaultAddress < (long)procedure.Begin ||
                    defaultAddress >= (long)begin ||
                    !IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
                if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < procedure.Begin || target >= begin ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
                return table;
            }
            return null;
        }

        // Release ClipboardPasteProbeJob zero-extends its status into EAX, compares AL, then indexes
        // a terminal DWORD table through RDX+4*RAX. Require that exact contiguous RSI+disp32 load,
        // unsigned byte guard, corroborated PE/listing rows and same-owner PDB start/end. Every default
        // and table target is a decoded boundary inside the procedure; no instruction exemption is added.
        private static InlineTable ReadGuardedRdxAlTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 11 || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + 19)
                return null;
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong begin = checked(imageBase + tableRva), prefix = dispatch - 11;
            if ((begin & 3) != 0 || begin <= afterJump ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 30 ||
                !StartsWithAt(pe, raw, 0x0f, 0xb6, 0x86) || pe[raw + 7] != 0x3c || pe[raw + 9] != 0x77 ||
                !StartsWithAt(pe, raw + 11, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 14) != (long)imageBase ||
                !StartsWithAt(pe, raw + 18, 0x8b, 0x84, 0x82) ||
                BitConverter.ToUInt32(pe, raw + 21) != tableRva ||
                !StartsWithAt(pe, raw + 25, 0x48, 0x03, 0xc2, 0xff, 0xe0)) return null;
            uint maximum = pe[raw + 8];
            if (maximum == 0 || maximum >= 128) return null; // bounded observed form, not an arbitrary byte range
            int[] starts = { 0, 7, 9, 11, 18, 25, 28 };
            int[] lengths = { 7, 2, 2, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong end = checked(begin + 4 * (maximum + 1));
            if (end > owner.End) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            long defaultAddress = (long)prefix + 11 + unchecked((sbyte)pe[raw + 10]);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || prefix < procedure.Begin || begin < procedure.Begin ||
                    afterJump > begin || procedure.End != end || defaultAddress < (long)procedure.Begin ||
                    defaultAddress >= (long)begin ||
                    !IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
                bool afterReturn = tableRaw >= 1 && pe[tableRaw - 1] == 0xc3;
                afterReturn |= tableRaw >= 2 && pe[tableRaw - 2] == 0xc3 && pe[tableRaw - 1] == 0x90;
                afterReturn |= tableRaw >= 3 && pe[tableRaw - 3] == 0xc3 && pe[tableRaw - 2] == 0x66 && pe[tableRaw - 1] == 0x90;
                afterReturn |= tableRaw >= 4 && pe[tableRaw - 4] == 0xc3 && pe[tableRaw - 3] == 0x0f &&
                               pe[tableRaw - 2] == 0x1f && pe[tableRaw - 1] == 0x00;
                if (!afterReturn) return null;
                if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < procedure.Begin || target >= begin ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
                return table;
            }
            return null;
        }

        // The host continuation switch keeps the image base in R10 across a bounded, non-clobbering prelude.
        // Prove every row, branch boundary, terminal PDB extent and DWORD target; no runtime code is exempted.
        private static InlineTable ReadGuardedR10AlTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong baseAddress, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (baseAddress < imageBase || baseAddress - imageBase > UInt32.MaxValue - 118) return null;
            int ownerIndex = FindContribution(contributions, baseAddress);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            int raw;
            try { raw = checked((int)Offset((uint)(baseAddress - imageBase), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 118 || !StartsWithAt(pe, raw, 0x4c, 0x8d, 0x15) ||
                (long)baseAddress + 7 + BitConverter.ToInt32(pe, raw + 3) != (long)imageBase) return null;
            int[] starts = { 0, 7, 14, 21, 28, 35, 41, 44, 46, 50, 52, 55, 62, 69, 71, 74, 79, 85, 90, 97, 99, 105, 113, 116 };
            int[] lengths = { 7, 7, 7, 7, 7, 6, 3, 2, 4, 2, 3, 7, 7, 2, 3, 5, 6, 5, 7, 2, 6, 8, 3, 2 };
            byte[][] prefixes = {
                new byte[]{0x4c,0x8d,0x15}, new byte[]{0x4c,0x8d,0x8b}, new byte[]{0x4c,0x8d,0x83},
                new byte[]{0x48,0x8d,0x93}, new byte[]{0x80,0xbb}, new byte[]{0x0f,0x84},
                new byte[]{0x48,0x85,0xff}, new byte[]{0x74}, new byte[]{0x41,0x80,0x3f},
                new byte[]{0x75}, new byte[]{0x4c,0x8b,0xef}, new byte[]{0x48,0x8d,0xb3},
                new byte[]{0x4c,0x8d,0xb3}, new byte[]{0xeb}, new byte[]{0x4d,0x8b,0xeb},
                new byte[]{0xbe}, new byte[]{0x41,0xbe}, new byte[]{0x4c,0x89,0x74,0x24},
                new byte[]{0x0f,0xb6,0x83}, new byte[]{0x3c}, new byte[]{0x0f,0x87},
                new byte[]{0x41,0x8b,0x84,0x82}, new byte[]{0x49,0x03,0xc2}, new byte[]{0xff,0xe0}
            };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = baseAddress + (ulong)starts[i];
                if (!StartsWithAt(pe, raw + starts[i], prefixes[i]) ||
                    !guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            uint maximum = pe[raw + 98];
            if (maximum == 0 || maximum >= 128) return null;
            uint tableRva = BitConverter.ToUInt32(pe, raw + 109);
            ulong begin = checked(imageBase + tableRva), end = checked(begin + 4 * (maximum + 1));
            ulong afterJump = baseAddress + 118;
            long defaultAddress = (long)baseAddress + 105 + BitConverter.ToInt32(pe, raw + 101);
            if ((begin & 3) != 0 || begin <= afterJump || end > owner.End ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || baseAddress < procedure.Begin || procedure.End != end ||
                    defaultAddress < (long)afterJump || defaultAddress >= (long)begin ||
                    !IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)) continue;
                int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
                if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
                bool terminal = false;
                int[] tailStarts = null, tailLengths = null;
                if (tableRaw >= 3 && StartsWithAt(pe, tableRaw - 3, 0xc3, 0x66, 0x90))
                { tailStarts = new int[]{-3,-2}; tailLengths = new int[]{1,2}; }
                else if (tableRaw >= 9 && pe[tableRaw - 9] == 0xc3 && pe[tableRaw - 8] == 0xe8 &&
                         StartsWithAt(pe, tableRaw - 3, 0xcc, 0x66, 0x90))
                { tailStarts = new int[]{-9,-8,-3,-2}; tailLengths = new int[]{1,5,1,2}; }
                if (tailStarts != null)
                {
                    terminal = true;
                    for (int i = 0; i < tailStarts.Length; ++i)
                    {
                        ulong address = (ulong)((long)begin + tailStarts[i]);
                        if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != tailLengths[i] ||
                            !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                        { terminal = false; break; }
                    }
                }
                if (!terminal) return null;
                // The exact prelude contains no call or R10 write. Its branch exits must also be decoded.
                foreach (int branch in new int[]{35,44,50,69})
                {
                    long target = branch == 35
                        ? (long)baseAddress + branch + 6 + BitConverter.ToInt32(pe, raw + branch + 2)
                        : (long)baseAddress + branch + 2 + unchecked((sbyte)pe[raw + branch + 1]);
                    if (target < (long)procedure.Begin || target >= (long)begin ||
                        !IsDecodedBoundary((ulong)target, owner.Module, contributions, decoded, decodedStarts)) return null;
                }
                // Reject any direct entry that bypasses the base definition or the unsigned guard, including
                // entries from other parts of this procedure. The terminal table itself is excluded from code.
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) return null;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (instruction.Unknown || instruction.Module != owner.Module) continue;
                    int source = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length));
                    if (source > pe.Length - instruction.Length) return null;
                    long target;
                    if (instruction.Length == 2 && (pe[source] == 0xeb || (pe[source] >= 0x70 && pe[source] <= 0x7f)))
                        target = (long)address + 2 + unchecked((sbyte)pe[source + 1]);
                    else if (instruction.Length == 5 && (pe[source] == 0xe9 || pe[source] == 0xe8))
                        target = (long)address + 5 + BitConverter.ToInt32(pe, source + 1);
                    else if (instruction.Length == 6 && pe[source] == 0x0f && pe[source + 1] >= 0x80 && pe[source + 1] <= 0x8f)
                        target = (long)address + 6 + BitConverter.ToInt32(pe, source + 2);
                    else continue;
                    if (target > (long)baseAddress && target < (long)afterJump &&
                        (address < baseAddress || address >= afterJump ||
                         !IsDecodedBoundary((ulong)target, owner.Module, contributions, decoded, decodedStarts))) return null;
                }
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < procedure.Begin || target >= begin || (target >= baseAddress && target < afterJump) ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
                return table;
            }
            return null;
        }

        // Keyboard command resolution emits a CL-bounded R8/ECX switch. Its exact unsigned load/guard/re-extension
        // and contiguous dispatch prove a terminal DWORD table; no module or instruction is exempted.
        private static InlineTable ReadGuardedR8ClTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 16 || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + 20)
                return null;
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong begin = checked(imageBase + tableRva), prefix = dispatch - 16;
            if ((begin & 3) != 0 || begin <= afterJump ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 36 ||
                !StartsWithAt(pe, raw, 0x0f, 0xb6, 0x4b) ||
                !StartsWithAt(pe, raw + 4, 0x80, 0xf9) || !StartsWithAt(pe, raw + 7, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 13, 0x0f, 0xb6, 0xc9) ||
                !StartsWithAt(pe, raw + 16, 0x4c, 0x8d, 0x05) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 19) != (long)imageBase ||
                !StartsWithAt(pe, raw + 23, 0x41, 0x8b, 0x8c, 0x88) ||
                BitConverter.ToUInt32(pe, raw + 27) != tableRva ||
                !StartsWithAt(pe, raw + 31, 0x49, 0x03, 0xc8, 0xff, 0xe1)) return null;
            uint maximum = pe[raw + 6];
            if (maximum == 0 || maximum >= 128) return null;
            int[] starts = { 0, 4, 7, 13, 16, 23, 31, 34 };
            int[] lengths = { 4, 3, 6, 3, 7, 8, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong end = checked(begin + 4 * (maximum + 1));
            if (end > owner.End) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            long defaultAddress = (long)prefix + 13 + BitConverter.ToInt32(pe, raw + 9);
            int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
            // The observed tail jumps back into the epilogue, followed by one NOP and the exact data symbol.
            if (tableRaw < 3 || tableRaw > pe.Length - checked((int)(end - begin)) ||
                pe[tableRaw - 3] != 0xeb || pe[tableRaw - 1] != 0x90 ||
                !guardRows.TryGetValue(begin - 3, out byte[] tailRow) || tailRow.Length != 2 ||
                !MatchesDecodedPe(pe, sections, imageBase, begin - 3, tailRow, contributions, decoded, decodedStarts) ||
                !guardRows.TryGetValue(begin - 1, out byte[] paddingRow) || paddingRow.Length != 1 ||
                !MatchesDecodedPe(pe, sections, imageBase, begin - 1, paddingRow, contributions, decoded, decodedStarts)) return null;
            long tailTarget = (long)begin - 1 + unchecked((sbyte)pe[tableRaw - 2]);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || prefix < procedure.Begin || begin < procedure.Begin ||
                    procedure.End != end || defaultAddress < (long)procedure.Begin || defaultAddress >= (long)begin ||
                    tailTarget < (long)procedure.Begin || tailTarget >= (long)begin - 3 ||
                    !IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts) ||
                    !IsDecodedBoundary((ulong)tailTarget, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < procedure.Begin || target >= begin ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
                return table;
            }
            return null;
        }

        private static bool StartsWithAt(byte[] bytes, int offset, params byte[] prefix)
        {
            if (offset < 0 || offset > bytes.Length - prefix.Length) return false;
            for (int i = 0; i < prefix.Length; ++i) if (bytes[offset + i] != prefix[i]) return false;
            return true;
        }

        // ASan's OnCommand remap ends at its procedure, inside a larger contribution. Bound that form
        // from its exact unsigned stack-index guard, not from an unrelated later anonymous data symbol.
        private static ulong GuardedByteIndexEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, ulong imageBase, ulong dispatch,
            ulong begin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 25 || dispatch - imageBase > UInt32.MaxValue) return 0;
            ulong prefix = dispatch - 25;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - 25 ||
                pe[raw] != 0x81 || pe[raw + 1] != 0xbc || pe[raw + 2] != 0x24 ||
                pe[raw + 11] != 0x0f || pe[raw + 12] != 0x87 ||
                pe[raw + 17] != 0x48 || pe[raw + 18] != 0x63 ||
                pe[raw + 19] != 0x84 || pe[raw + 20] != 0x24 ||
                BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + 21)) return 0;
            uint maximum = BitConverter.ToUInt32(pe, raw + 7);
            if (maximum >= 4096) return 0;
            long defaultAddress = (long)prefix + 17 + BitConverter.ToInt32(pe, raw + 13);
            if (defaultAddress < 0 || !dwordTable.Targets.Contains((ulong)defaultAddress)) return 0;
            int[] starts = { 0, 11, 17 };
            int[] lengths = { 11, 6, 8 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!IsDecodedBoundary(address, dwordTable.Module, contributions, decoded, decodedStarts) ||
                    decoded[address].Length != lengths[i]) return 0;
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i]) return 0;
                for (int offset = 0; offset < reported.Length; ++offset)
                    if (reported[offset] != pe[raw + starts[i] + offset]) return 0;
                int next = decodedStarts.BinarySearch(address) + 1;
                if (next < decodedStarts.Count && decodedStarts[next] < address + (ulong)lengths[i]) return 0;
            }
            ulong end = checked(begin + maximum + 1);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != dwordTable.Module ||
                    procedure.ContributionBegin != dwordTable.ContributionBegin ||
                    procedure.ContributionEnd != dwordTable.ContributionEnd ||
                    prefix < procedure.Begin || dispatch >= procedure.End || procedure.End != end ||
                    dwordTable.Begin < procedure.Begin || dwordTable.End > begin || begin >= end) continue;
                bool allTargetsInside = true;
                foreach (ulong target in dwordTable.Targets)
                    if (target < procedure.Begin || target >= procedure.End) { allTargetsInside = false; break; }
                if (allTargetsInside) return end;
            }
            return 0;
        }

        // The compact ASan stack remap has a 21-byte guard and the exact 27-byte RCX dispatch.
        // Its imm8 is sign extended: only 1..127 is an unsigned upper bound. The byte count ends
        // exactly at this procedure, inside a larger contribution; another data symbol cannot rescue
        // a malformed guard. Corroborate all eight rows against PE bytes and nonoverlapping boundaries.
        private static ulong CompactRcxByteIndexEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, ulong imageBase, ulong dispatch,
            ulong begin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, out bool candidate)
        {
            candidate = false;
            if (dispatch < imageBase + 21 || dispatch - imageBase > UInt32.MaxValue) return 0;
            ulong prefix = dispatch - 21;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - 48) return 0;
            candidate = StartsWithAt(pe, raw + 14, 0x8b, 0x84, 0x24);
            if (guardRows.TryGetValue(prefix + 14, out byte[] indexRow) && indexRow.Length == 7 &&
                StartsWith(indexRow, 0x8b, 0x84, 0x24)) candidate = true;
            if (!candidate || begin != dwordTable.End ||
                !StartsWithAt(pe, raw, 0x83, 0xbc, 0x24) ||
                !StartsWithAt(pe, raw + 8, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 14, 0x8b, 0x84, 0x24) ||
                BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + 17) ||
                !StartsWithAt(pe, raw + 21, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 24) != (long)imageBase ||
                !StartsWithAt(pe, raw + 28, 0x0f, 0xb6, 0x84, 0x01) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + 32)) != begin ||
                !StartsWithAt(pe, raw + 36, 0x8b, 0x84, 0x81) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + 39)) != dwordTable.Begin ||
                !StartsWithAt(pe, raw + 43, 0x48, 0x03, 0xc1, 0xff, 0xe0)) return 0;
            uint maximum = pe[raw + 7];
            if (maximum == 0 || maximum >= 128) return 0;
            int[] starts = { 0, 8, 14, 21, 28, 36, 43, 46 };
            int[] lengths = { 8, 6, 7, 7, 8, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return 0;
            }
            long defaultAddress = (long)prefix + 14 + BitConverter.ToInt32(pe, raw + 10);
            if (defaultAddress < 0 || !dwordTable.Targets.Contains((ulong)defaultAddress)) return 0;
            ulong end = checked(begin + maximum + 1);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != dwordTable.Module ||
                    procedure.ContributionBegin != dwordTable.ContributionBegin ||
                    procedure.ContributionEnd != dwordTable.ContributionEnd || prefix < procedure.Begin ||
                    dispatch >= dwordTable.Begin || dwordTable.Begin < procedure.Begin || procedure.End != end) continue;
                bool allTargetsInside = true;
                foreach (ulong target in dwordTable.Targets)
                    if (target < procedure.Begin || target >= dwordTable.Begin) { allTargetsInside = false; break; }
                if (allTargetsInside) return end;
            }
            return 0;
        }

        // Prove the observed switch remap: LEA EAX,[RDX+disp32], CMP EAX,imm32,
        // unsigned JA to the same default target used by the DWORD switch, then CDQE and
        // the image-base dispatch. This bounds the byte table to max+1 entries. Both PE
        // bytes and decoded/listed boundaries must agree; disassembly text is not evidence.
        private static ulong RdxRemapEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, ulong imageBase,
            ulong dispatch, ulong byteTableBegin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 19 || dispatch - imageBase > UInt32.MaxValue) return 0;
            ulong prefix = dispatch - 19;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - 19 || pe[raw] != 0x8d || pe[raw + 1] != 0x82 ||
                pe[raw + 6] != 0x3d || pe[raw + 11] != 0x0f || pe[raw + 12] != 0x87 ||
                pe[raw + 17] != 0x48 || pe[raw + 18] != 0x98) return 0;
            uint maximum = BitConverter.ToUInt32(pe, raw + 7);
            if (maximum >= 4096) return 0;
            long defaultAddress = (long)prefix + 17 + BitConverter.ToInt32(pe, raw + 13);
            if (defaultAddress < 0 || !dwordTable.Targets.Contains((ulong)defaultAddress)) return 0;
            int[] starts = { 0, 6, 11, 17 };
            int[] lengths = { 6, 5, 6, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!IsDecodedBoundary(address, dwordTable.Module, contributions, decoded, decodedStarts) ||
                    decoded[address].Length != lengths[i] ||
                    !guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i]) return 0;
                for (int offset = 0; offset < reported.Length; ++offset)
                    if (reported[offset] != pe[raw + starts[i] + offset]) return 0;
                int next = decodedStarts.BinarySearch(address) + 1;
                if (next < decodedStarts.Count && decodedStarts[next] < address + (ulong)lengths[i]) return 0;
            }
            ulong end = checked(byteTableBegin + maximum + 1);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != dwordTable.Module || prefix < procedure.Begin || dispatch >= procedure.End ||
                    procedure.ContributionBegin != dwordTable.ContributionBegin ||
                    procedure.ContributionEnd != dwordTable.ContributionEnd || end > procedure.End ||
                    dwordTable.Begin < procedure.Begin || dwordTable.End > byteTableBegin) continue;
                bool allTargetsInside = true;
                foreach (ulong target in dwordTable.Targets)
                    if (target < procedure.Begin || target >= procedure.End) { allTargetsInside = false; break; }
                if (allTargetsInside) return end;
            }
            return 0;
        }

        private static InlineTable ProveByteIndexTable(byte[] pe, List<Section> sections,
            Dictionary<ulong, int> dataSymbols, List<Contribution> contributions, List<Function> procedures,
            ulong imageBase, ulong dispatch, uint byteTableRva, InlineTable dwordTable, bool guardedRcxForm,
            bool guardedRdxRemap,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            ulong begin = checked(imageBase + byteTableRva);
            int owner = FindContribution(contributions, begin);
            if (owner < 0 || contributions[owner].Module != dwordTable.Module ||
                !dataSymbols.TryGetValue(begin, out int symbolOwner) || symbolOwner != dwordTable.Module)
                return null;
            ulong end = 0;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == dwordTable.Module && symbol.Key > begin &&
                    symbol.Key < dwordTable.ContributionEnd && (end == 0 || symbol.Key < end))
                    end = symbol.Key;
            ulong guardedEnd = guardedRcxForm ? GuardedByteIndexEnd(pe, sections, contributions, procedures,
                imageBase, dispatch, begin, dwordTable, decoded, decodedStarts, guardRows) : 0;
            if (guardedRcxForm)
            {
                ulong compactEnd = CompactRcxByteIndexEnd(pe, sections, contributions, procedures, imageBase,
                    dispatch, begin, dwordTable, decoded, decodedStarts, guardRows, out bool compactCandidate);
                if (compactCandidate && compactEnd == 0) return null;
                if (compactEnd != 0) guardedEnd = compactEnd;
            }
            if (guardedRdxRemap)
                guardedEnd = RdxRemapEnd(pe, sections, contributions, procedures, imageBase,
                    dispatch, begin, dwordTable, decoded, decodedStarts, guardRows);
            // This newly recognized dispatch is accepted only with its exact range guard. A later anonymous
            // data symbol or procedure-end fallback cannot rescue a malformed/missing guard.
            if (guardedRdxRemap && guardedEnd == 0) return null;
            if (guardedEnd != 0)
            {
                // An earlier symbol contradicts the encoded index count. A later function's data cannot
                // extend this range. The guard also requires every target to lie in this exact procedure.
                if (end != 0 && end < guardedEnd) return null;
                end = guardedEnd;
            }
            if (end == 0)
            {
                // The observed Debug terminal byte table has no following data symbol. Its exact owning PDB
                // procedure and executable contribution end at the same byte; neither alone extends data.
                foreach (Function procedure in procedures)
                    if (procedure.Module == dwordTable.Module && dispatch >= procedure.Begin && dispatch < procedure.End &&
                        procedure.ContributionBegin == dwordTable.ContributionBegin &&
                        procedure.ContributionEnd == dwordTable.ContributionEnd &&
                        procedure.End == dwordTable.ContributionEnd && dwordTable.Begin >= procedure.Begin &&
                        dwordTable.End <= begin && begin >= procedure.Begin && begin < procedure.End)
                    { end = procedure.End; break; }
            }
            if (end == 0 || end <= begin || end - begin > 4096 || begin < dwordTable.End) return null;
            ulong rangeEnd = checked(begin + (end - begin));
            if (rangeEnd > dwordTable.ContributionEnd || begin < imageBase ||
                begin - imageBase > UInt32.MaxValue || end - imageBase > UInt32.MaxValue)
                return null;
            uint rva = checked((uint)(begin - imageBase));
            int raw;
            try { raw = checked((int)Offset(rva, sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            int length = checked((int)(end - begin));
            if (raw > pe.Length - length) return null;
            int entryCount = checked((int)((dwordTable.End - dwordTable.Begin) / 4));
            // Observed DWORD alignment after a byte table is exactly 90, 66 90 or 0F 1F 00. Accept only the
            // complete 1-3 byte suffix ending at the proved aligned boundary. All preceding bytes are indices;
            // no arbitrary instruction, changed suffix or repeated padding can shorten index validation.
            int indexLength = length;
            if (guardedEnd == 0 && (end & 3) == 0)
            {
                if (length >= 3 && pe[raw + length - 3] == 0x0f && pe[raw + length - 2] == 0x1f &&
                    pe[raw + length - 1] == 0x00) indexLength -= 3;
                else if (length >= 2 && pe[raw + length - 2] == 0x66 && pe[raw + length - 1] == 0x90)
                    indexLength -= 2;
                else if (length >= 1 && pe[raw + length - 1] == 0x90) indexLength -= 1;
            }
            if (indexLength == 0) return null;
            for (int i = 0; i < indexLength; ++i)
                if (pe[raw + i] >= entryCount) return null;
            return new InlineTable { Begin = begin, End = end, Module = dwordTable.Module,
                ContributionBegin = dwordTable.ContributionBegin, ContributionEnd = dwordTable.ContributionEnd };
        }

        // The observed Release std::variant destruction dispatch: the index is a signed byte read from the object, so the
        // byte table is biased by one entry (index -1, a valueless variant, addresses its first byte), and both tables are
        // the last bytes of the procedure. Exactly these six contiguous instructions, 37 bytes:
        //   MOVSX RAX,byte [RCX+disp32]; LEA R8,imagebase; MOVZX EAX,byte [R8+RAX+byteTable+1];
        //   MOV EDX,dword [R8+4*RAX+dwordTable]; ADD RDX,R8; JMP RDX
        // Every row must agree with the PE bytes, have its reported length and overlap no other decoded row. The DWORD table
        // starts at the exact anonymous same-owner PDB data symbol of its load. The byte table starts at the exact anonymous
        // same-owner PDB data symbol one byte before its load displacement (the displacement itself proves nothing), which is
        // also where the DWORD table ends. The byte table ends at the end its owning PDB procedure shares with its executable
        // contribution, with no other data symbol inside either table, and holds only indices below the DWORD entry count.
        // Every entry names a decoded instruction boundary of that procedure before the tables. A different bias, register,
        // displacement width, table layout or procedure bound receives no exemption; nothing is exempted by address,
        // object or procedure name.
        private static InlineTable ReadSignedBiasedByteIndexTables(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint byteLoadRva, uint dwordTableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows, out InlineTable byteTable)
        {
            byteTable = null;
            if (dispatch < imageBase + 8 || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + 29 ||
                byteLoadRva == 0)
                return null;
            ulong prefix = dispatch - 8;
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, dispatch) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            if (afterJump > owner.End) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 37 ||
                !StartsWithAt(pe, raw, 0x48, 0x0f, 0xbe, 0x81) ||
                !StartsWithAt(pe, raw + 8, 0x4c, 0x8d, 0x05) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 11) != (long)imageBase ||
                !StartsWithAt(pe, raw + 15, 0x41, 0x0f, 0xb6, 0x84, 0x00) ||
                BitConverter.ToUInt32(pe, raw + 20) != byteLoadRva ||
                !StartsWithAt(pe, raw + 24, 0x41, 0x8b, 0x94, 0x80) ||
                BitConverter.ToUInt32(pe, raw + 28) != dwordTableRva ||
                !StartsWithAt(pe, raw + 32, 0x49, 0x03, 0xd0) ||
                !StartsWithAt(pe, raw + 35, 0xff, 0xe2)) return null;
            int[] starts = { 0, 8, 15, 24, 32, 35 };
            int[] lengths = { 8, 7, 9, 8, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong dwordBegin = checked(imageBase + dwordTableRva);
            ulong byteBegin = checked(imageBase + byteLoadRva - 1);
            ulong end = owner.End;
            if ((dwordBegin & 3) != 0 || dwordBegin <= afterJump || byteBegin <= dwordBegin ||
                ((byteBegin - dwordBegin) & 3) != 0 || byteBegin - dwordBegin < 8 || byteBegin >= end ||
                end - byteBegin > 4096 ||
                !dataSymbols.TryGetValue(dwordBegin, out int dwordOwner) || dwordOwner != owner.Module ||
                !dataSymbols.TryGetValue(byteBegin, out int byteOwner) || byteOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Key > dwordBegin && symbol.Key < end && symbol.Key != byteBegin) return null;
            int entryCount = checked((int)((byteBegin - dwordBegin) / 4));
            int byteLength = checked((int)(end - byteBegin));
            int dwordRaw, byteRaw;
            try
            {
                dwordRaw = checked((int)Offset(dwordTableRva, sections, pe.Length));
                byteRaw = checked((int)Offset(checked((uint)(byteBegin - imageBase)), sections, pe.Length));
            }
            catch (InvalidDataException) { return null; }
            if (dwordRaw > pe.Length - 4 * entryCount || byteRaw > pe.Length - byteLength) return null;
            for (int i = 0; i < byteLength; ++i)
                if (pe[byteRaw + i] >= entryCount) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end) continue;
                var table = new InlineTable { Begin = dwordBegin, End = byteBegin, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                bool valid = true;
                for (int entry = 0; entry < entryCount && valid; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, dwordRaw + 4 * entry));
                    valid = target >= procedure.Begin && target < dwordBegin &&
                            IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                    table.Targets.Add(target);
                }
                if (!valid) continue;
                byteTable = new InlineTable { Begin = byteBegin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                return table;
            }
            return null;
        }

        private static bool IsDecodedBoundary(ulong target, int module, List<Contribution> contributions,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts)
        {
            int owner = FindContribution(contributions, target);
            DecodedInstruction instruction;
            if (owner < 0 || contributions[owner].Module != module ||
                !decoded.TryGetValue(target, out instruction) || instruction.Unknown || instruction.Module != module ||
                checked(target + (ulong)instruction.Length) > contributions[owner].End)
                return false;
            int position = decodedStarts.BinarySearch(target);
            position = position >= 0 ? position - 1 : ~position - 1;
            if (position >= 0 && decoded.TryGetValue(decodedStarts[position], out DecodedInstruction prior) &&
                checked(decodedStarts[position] + (ulong)prior.Length) > target) return false;
            return true;
        }

        private static bool SameProcedure(List<Function> procedures, int module, ulong dispatch, ulong target)
        {
            foreach (Function procedure in procedures)
                if (procedure.Module == module && dispatch >= procedure.Begin && dispatch < procedure.End &&
                    target >= procedure.Begin && target < procedure.End)
                    return true;
            return false;
        }

        private static bool ProcedureCoversBackwardTable(List<Function> procedures, InlineTable table,
            ulong dispatch, ulong afterJump)
        {
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != table.Module || dispatch < procedure.Begin || dispatch >= procedure.End ||
                    procedure.ContributionBegin != table.ContributionBegin ||
                    procedure.ContributionEnd != table.ContributionEnd ||
                    table.Begin < procedure.Begin || table.End > procedure.End) continue;
                bool containsEveryBackwardTarget = true;
                foreach (ulong target in table.Targets)
                    if (target < afterJump && (target < procedure.Begin || target >= procedure.End))
                    { containsEveryBackwardTarget = false; break; }
                if (containsEveryBackwardTarget) return true;
            }
            return false;
        }

        public static IsaScanResult Scan(string binary, string metadata, string symbols, string disassembly,
                                         string[] runtimeObjects, string[] runtimeLibraries, string[] runtimeFunctions)
        {
            byte[] pe = File.ReadAllBytes(binary);
            if (pe.Length < 64 || BitConverter.ToUInt16(pe, 0) != 0x5a4d)
                throw new InvalidDataException("Not a PE image.");
            int header = BitConverter.ToInt32(pe, 0x3c);
            if (header < 64 || header > pe.Length - 264 || BitConverter.ToUInt32(pe, header) != 0x4550 ||
                BitConverter.ToUInt16(pe, header + 4) != 0x8664 || BitConverter.ToUInt16(pe, header + 24) != 0x20b)
                throw new InvalidDataException("ISA scan requires an x64 PE32+ image.");
            int optional = header + 24;
            ulong imageBase = BitConverter.ToUInt64(pe, optional + 24);
            int sectionHeader = optional + BitConverter.ToUInt16(pe, header + 20);
            int sectionCount = BitConverter.ToUInt16(pe, header + 6);
            var sections = new List<Section>();
            for (int i = 0; i < sectionCount; ++i)
            {
                int at = checked(sectionHeader + i * 40);
                sections.Add(new Section { Rva = BitConverter.ToUInt32(pe, at + 12),
                    Raw = BitConverter.ToUInt32(pe, at + 20),
                    Size = Math.Max(BitConverter.ToUInt32(pe, at + 8), BitConverter.ToUInt32(pe, at + 16)),
                    Executable = (BitConverter.ToUInt32(pe, at + 36) & 0x20000000) != 0 });
            }
            uint debugRva = BitConverter.ToUInt32(pe, optional + 112 + 6 * 8);
            uint debugSize = BitConverter.ToUInt32(pe, optional + 116 + 6 * 8);
            if (debugRva == 0 || debugSize < 28) throw new InvalidDataException("PE has no CodeView identity.");
            int debug = checked((int)Offset(debugRva, sections, pe.Length));
            Guid guid = Guid.Empty;
            uint age = 0;
            for (int offset = 0; offset + 28 <= debugSize; offset += 28)
            {
                int at = checked(debug + offset);
                if (BitConverter.ToUInt32(pe, at + 12) != 2) continue;
                int cv = checked((int)BitConverter.ToUInt32(pe, at + 24));
                if (BitConverter.ToUInt32(pe, cv) != 0x53445352) continue;
                byte[] identity = new byte[16];
                Array.Copy(pe, cv + 4, identity, 0, 16);
                guid = new Guid(identity);
                age = BitConverter.ToUInt32(pe, cv + 20);
                break;
            }
            if (guid == Guid.Empty) throw new InvalidDataException("PE has no RSDS identity.");

            var modules = new Dictionary<int, Module>();
            var contributions = new List<Contribution>();
            int module = -1;
            bool inContributions = false, guidMatch = false, ageMatch = false;
            foreach (string line in File.ReadLines(metadata))
            {
                Match match = Regex.Match(line, @"^\s*GUID: (\{.+\})$");
                if (match.Success) guidMatch = Guid.Parse(match.Groups[1].Value) == guid;
                match = Regex.Match(line, @"^\s*Age: (\d+)$");
                if (match.Success) ageMatch = UInt32.Parse(match.Groups[1].Value) == age;
                if (line.Trim() == "Section Contributions") inContributions = true;
                if (!inContributions)
                {
                    match = ModuleLine.Match(line);
                    if (match.Success)
                    {
                        module = Int32.Parse(match.Groups[1].Value);
                        modules.Add(module, new Module { Object = match.Groups[2].Value });
                    }
                    match = ObjectLine.Match(line);
                    if (match.Success && module >= 0) modules[module].Library = match.Groups[1].Value;
                    continue;
                }
                match = ContributionLine.Match(line);
                if (!match.Success) continue;
                int index = Int32.Parse(match.Groups[2].Value);
                if (index == 65535) continue;
                ulong start = UInt64.Parse(match.Groups[3].Value);
                ulong size = UInt64.Parse(match.Groups[4].Value);
                if (size == 0) continue;
                ulong begin = Address(index, start, imageBase, sections);
                ulong end = Address(index, checked(start + size), imageBase, sections);
                if (sections[index - 1].Executable)
                    contributions.Add(new Contribution { Module = Int32.Parse(match.Groups[1].Value), Begin = begin, End = end });
            }
            if (!guidMatch || !ageMatch) throw new InvalidDataException("PDB GUID/age does not match the linked image.");
            if (modules.Count == 0 || contributions.Count == 0) throw new InvalidDataException("Missing PDB module/contribution data.");
            contributions.Sort((a, b) => a.Begin.CompareTo(b.Begin));
            for (int i = 1; i < contributions.Count; ++i)
                if (contributions[i].Begin < contributions[i - 1].End)
                    throw new InvalidDataException("Overlapping PDB contributions cannot be attributed safely.");

            var functions = new List<Function>();
            var procedures = new List<Function>();
            var dataSymbols = new Dictionary<ulong, int>();
            string function = null;
            bool dataSymbol = false;
            module = -1;
            foreach (string line in File.ReadLines(symbols))
            {
                Match match = ModuleLine.Match(line);
                if (match.Success) { module = Int32.Parse(match.Groups[1].Value); function = null; dataSymbol = false; }
                if (dataSymbol)
                {
                    match = DataAddress.Match(line.Trim());
                    if (match.Success && module >= 0)
                    {
                        int index = Int32.Parse(match.Groups[1].Value);
                        if (index >= 1 && index <= sections.Count && sections[index - 1].Executable)
                        {
                            ulong address = Address(index, UInt64.Parse(match.Groups[2].Value), imageBase, sections);
                            int owner = FindContribution(contributions, address);
                            if (owner >= 0 && contributions[owner].Module == module) dataSymbols[address] = module;
                        }
                    }
                }
                dataSymbol = DataLine.IsMatch(line.Trim());
                match = FunctionLine.Match(line);
                if (match.Success) function = match.Groups[1].Value;
                else if (function != null)
                {
                    match = FunctionAddress.Match(line);
                    Module functionOwner;
                    if (match.Success && modules.TryGetValue(module, out functionOwner))
                    {
                        int index = Int32.Parse(match.Groups[1].Value);
                        ulong start = UInt64.Parse(match.Groups[2].Value);
                        ulong size = UInt64.Parse(match.Groups[3].Value);
                        if (index >= 1 && index <= sections.Count && start <= sections[index - 1].Size &&
                            size != 0 && size <= sections[index - 1].Size - start)
                        {
                            ulong begin = Address(index, start, imageBase, sections);
                            ulong end = Address(index, checked(start + size), imageBase, sections);
                            int owner = FindContribution(contributions, begin);
                            if (owner >= 0 && contributions[owner].Module == module && end <= contributions[owner].End)
                            {
                                var procedure = new Function { Module = module, Name = function, Begin = begin, End = end,
                                    ContributionBegin = contributions[owner].Begin, ContributionEnd = contributions[owner].End };
                                procedures.Add(procedure);
                                if (Array.IndexOf(runtimeFunctions, Path.GetFileName(functionOwner.Object) + "!" + function) >= 0)
                                    functions.Add(procedure);
                            }
                        }
                    }
                    function = null;
                }
            }

            var result = new IsaScanResult();
            var inlineTables = new List<InlineTable>();
            var decoded = new Dictionary<ulong, DecodedInstruction>();
            var decodedStarts = new List<ulong>();
            var guardRows = new Dictionary<ulong, byte[]>();
            string[] disassemblyLines = File.ReadAllLines(disassembly);
            var rdxGuardCandidates = new HashSet<ulong>();
            var r10BaseCandidates = new List<ulong>();
            foreach (string line in disassemblyLines)
            {
                Match candidate = InstructionLine.Match(line);
                if (!candidate.Success) continue;
                string[] candidateBytes = candidate.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                bool r10Candidate = candidateBytes.Length == 7 && candidateBytes[0] == "4c" && candidateBytes[1] == "8d" &&
                                    candidateBytes[2] == "15";
                bool r8Candidate = candidateBytes.Length == 7 && candidateBytes[0] == "4c" && candidateBytes[1] == "8d" &&
                                   candidateBytes[2] == "05";
                if (!r10Candidate && !r8Candidate && (candidateBytes.Length != 7 || candidateBytes[0] != "48" || candidateBytes[1] != "8d" ||
                    (candidateBytes[2] != "15" && candidateBytes[2] != "0d"))) continue;
                ulong candidateAddress = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                byte[] encoded = Array.ConvertAll(candidateBytes, b => Byte.Parse(b, NumberStyles.HexNumber));
                if (checked((long)candidateAddress + 7 + BitConverter.ToInt32(encoded, 3)) != (long)imageBase ||
                    candidateAddress < imageBase + 19) continue;
                if (r10Candidate)
                {
                    r10BaseCandidates.Add(candidateAddress);
                    for (int offset = 0; offset < 118; ++offset) rdxGuardCandidates.Add(candidateAddress + (ulong)offset);
                    try
                    {
                        int loadRaw = checked((int)Offset(checked((uint)(candidateAddress + 105 - imageBase)), sections, pe.Length));
                        if (loadRaw <= pe.Length - 8 && StartsWithAt(pe, loadRaw, 0x41, 0x8b, 0x84, 0x82))
                        {
                            ulong tableBegin = checked(imageBase + BitConverter.ToUInt32(pe, loadRaw + 4));
                            if (tableBegin >= imageBase + 9)
                                foreach (int offset in new int[]{-9,-8,-3,-2})
                                    rdxGuardCandidates.Add((ulong)((long)tableBegin + offset));
                        }
                    }
                    catch (InvalidDataException) { /* Not a mapped canonical load; no terminal rows retained. */ }
                    continue;
                }
                if (r8Candidate)
                {
                    // The signed biased byte-index dispatch: the MOVSX ahead of the LEA and the four rows after it.
                    int[] r8Offsets = { -16, -12, -9, -8, -3, 0, 7, 15, 16, 18, 24, 27 };
                    foreach (int offset in r8Offsets) rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                    // Retain only the observed terminal jump/NOP rows named by a canonical R8/ECX load.
                    try
                    {
                        int loadRaw = checked((int)Offset(checked((uint)(candidateAddress + 7 - imageBase)), sections, pe.Length));
                        if (loadRaw <= pe.Length - 8 && StartsWithAt(pe, loadRaw, 0x41, 0x8b, 0x8c, 0x88))
                        {
                            ulong tableBegin = checked(imageBase + BitConverter.ToUInt32(pe, loadRaw + 4));
                            if (tableBegin >= imageBase + 3)
                            {
                                rdxGuardCandidates.Add(tableBegin - 3);
                                rdxGuardCandidates.Add(tableBegin - 1);
                            }
                        }
                    }
                    catch (InvalidDataException) { /* Not a mapped canonical load; no terminal rows retained. */ }
                    continue;
                }
                if (candidateBytes[2] == "0d")
                {
                    int[] offsets = { -25, -22, -21, -17, -16, -13, -12, -11, -7, -5, 0, 7, 14, 15, 17, 22, 25 };
                    foreach (int offset in offsets) rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                    continue;
                }
                // Exact byte-index guard plus its four dispatch rows. Keep only candidates, not all encodings.
                int[] byteOffsets = { -11, -8, -5, -4, -2, 0, 7, 14, 17 };
                foreach (int offset in byteOffsets) rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                ulong guard = candidateAddress - 19;
                rdxGuardCandidates.Add(guard);
                rdxGuardCandidates.Add(guard + 6);
                rdxGuardCandidates.Add(guard + 11);
                rdxGuardCandidates.Add(guard + 17);
            }
            foreach (string line in disassemblyLines)
            {
                Match row = InstructionLine.Match(line);
                if (!row.Success) continue;
                string[] rowBytes = row.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                ulong rowAddress = UInt64.Parse(row.Groups[1].Value, NumberStyles.HexNumber);
                int rowOwner = FindContribution(contributions, rowAddress);
                if (rowOwner >= 0 && !decoded.ContainsKey(rowAddress))
                {
                    decoded.Add(rowAddress, new DecodedInstruction { Length = rowBytes.Length,
                        Unknown = row.Groups[3].Value.StartsWith("<unknown>", StringComparison.Ordinal),
                        Module = contributions[rowOwner].Module });
                    decodedStarts.Add(rowAddress);
                    // Retain the stack guard encodings and four rows preceding a candidate RDX image base,
                    // not millions of unrelated encodings. Each new proof checks actual PE bytes as well.
                    if ((rowBytes.Length == 11 && rowBytes[0] == "81" && rowBytes[1] == "bc" && rowBytes[2] == "24") ||
                        (rowBytes.Length == 6 && rowBytes[0] == "0f" && rowBytes[1] == "87") ||
                        (rowBytes.Length == 8 && rowBytes[0] == "48" && rowBytes[1] == "63" &&
                         rowBytes[2] == "84" && rowBytes[3] == "24") || rdxGuardCandidates.Contains(rowAddress))
                        guardRows.Add(rowAddress, Array.ConvertAll(rowBytes, b => Byte.Parse(b, NumberStyles.HexNumber)));
                }
                else if (rowOwner >= 0) decoded[rowAddress].Unknown = true;
            }
            decodedStarts.Sort();
            foreach (ulong address in r10BaseCandidates)
            {
                InlineTable table = ReadGuardedR10AlTerminalDwordTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            int dispatchStage = 0;
            bool r8Base = false;
            bool raxBase = false;
            bool rdxBase = false;
            bool rdxRemap = false;
            bool rdxByteGuard = false;
            bool r8ClGuard = false;
            bool rdxRdiIndex = false;
            bool rdxBaseMatchesPe = false;
            bool r9Base = false;
            int indexRegister = 0, targetRegister = 0;
            int preludeStep = 0;
            ulong firstPreludeTarget = 0, secondPreludeTarget = 0;
            ulong dispatchBegin = 0, precedingEnd = 0;
            uint tableRva = 0;
            bool previousSignedByteIndex = false, signedByteIndex = false;
            bool dlByteIndex = false;
            bool hasByteIndexTable = false;
            uint byteIndexTableRva = 0;
            bool previousRcxSignedByte = false, rcxSignedByteBeforeLea = false, biasedSignedDispatch = false;
            foreach (string line in disassemblyLines)
            {
                Match match = InstructionLine.Match(line);
                if (!match.Success) continue;
                ++result.Instructions;
                string[] bytes = match.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                ulong address = UInt64.Parse(match.Groups[1].Value, NumberStyles.HexNumber);
                if (!match.Groups[3].Value.StartsWith("<unknown>", StringComparison.Ordinal))
                    foreach (InlineTable table in inlineTables) table.PendingTargets.Remove(address);
                // Only a handful of possible dispatch instructions need byte-array parsing. Keep the
                // ordinary instruction scan cheap even for multi-million-instruction Debug images.
                if (dispatchStage != 0 || (bytes.Length == 7 && bytes[1] == "8d" &&
                    ((bytes[0] == "48" && (bytes[2] == "0d" || bytes[2] == "05" || bytes[2] == "15")) ||
                     (bytes[0] == "4c" && (bytes[2] == "05" || bytes[2] == "0d")))))
                {
                    byte[] encoded = Array.ConvertAll(bytes, b => Byte.Parse(b, NumberStyles.HexNumber));
                    if (address != precedingEnd) dispatchStage = 0;
                    if (r8Base && dispatchStage == 1 && R8Prelude(encoded, preludeStep))
                    {
                        if (preludeStep == 4)
                            firstPreludeTarget = checked((ulong)((long)address + encoded.Length + BitConverter.ToInt32(encoded, 2)));
                        else if (preludeStep == 7)
                            secondPreludeTarget = checked((ulong)((long)address + encoded.Length + unchecked((sbyte)encoded[1])));
                        ++preludeStep;
                    }
                    else if (raxBase && dispatchStage == 1 &&
                        ((encoded.Length == 7 && StartsWith(encoded, 0x48, 0x8b, 0x8d)) ||
                         (encoded.Length == 5 && StartsWith(encoded, 0x48, 0x8b, 0x4c, 0x24))))
                        dispatchStage = 2; // exact RBP+disp32 or RSP+disp8 RCX load; RAX image base is unchanged
                    else if (!r8Base && !raxBase && !rdxBase && !r9Base && !signedByteIndex && dispatchStage == 1 && encoded.Length == 3 &&
                             StartsWith(encoded, 0x0f, 0xb6, 0xc2))
                    { dlByteIndex = true; dispatchStage = 2; } // MOVZX EAX,DL preserves the RCX image base
                    else if (!r8Base && !raxBase && !rdxBase && !r9Base && dispatchStage == 1 && encoded.Length == 8 && StartsWith(encoded, 0x0f, 0xb6, 0x84, 0x01))
                    { byteIndexTableRva = BitConverter.ToUInt32(encoded, 4); hasByteIndexTable = true; dispatchStage = 2; }
                    else if (rdxBase && rdxBaseMatchesPe && dispatchStage == 1 && encoded.Length == 8 &&
                        StartsWith(encoded, 0x0f, 0xb6, 0x84, 0x02) && MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts))
                    { byteIndexTableRva = BitConverter.ToUInt32(encoded, 4); hasByteIndexTable = true; rdxRemap = true; dispatchStage = 2; }
                    else if (r9Base && dispatchStage == 1 && encoded.Length == 9 && StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x84, 0x01))
                    { byteIndexTableRva = BitConverter.ToUInt32(encoded, 5); hasByteIndexTable = true; dispatchStage = 2; }
                    else if (r8Base && dispatchStage == 1 && preludeStep == 0 && encoded.Length == 9 &&
                        (StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x84, 0x00) || StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x8c, 0x08)))
                    { indexRegister = encoded[3] == 0x84 ? 0 : 1; byteIndexTableRva = BitConverter.ToUInt32(encoded, 5);
                      hasByteIndexTable = true; dispatchStage = 2; }
                    else if (raxBase && dispatchStage == 2 && encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x8c, 0x88))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 1; dispatchStage = 3; }
                    else if (rdxBase && dispatchStage == (rdxRemap ? 2 : 1) && encoded.Length == 7 &&
                        StartsWith(encoded, 0x8b, 0x8c, 0x82) && (!rdxRemap || MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts)))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 1; dispatchStage = 3; }
                    else if (rdxBase && rdxBaseMatchesPe && dispatchStage == 1 && encoded.Length == 7 &&
                        StartsWith(encoded, 0x8b, 0x84, 0xba) && MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 0; rdxRdiIndex = true; dispatchStage = 3; }
                    else if (rdxBase && rdxBaseMatchesPe && dispatchStage == 1 && encoded.Length == 7 &&
                        (StartsWith(encoded, 0x8b, 0x84, 0x8a) || StartsWith(encoded, 0x8b, 0x84, 0x82)) && MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 0; rdxByteGuard = true; dispatchStage = 3; }
                    else if (r9Base && dispatchStage == 2 && encoded.Length == 8 && StartsWith(encoded, 0x45, 0x8b, 0x84, 0x81))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = 8; dispatchStage = 3; }
                    else if (!r8Base && !raxBase && !rdxBase && !r9Base && (dispatchStage == 1 || dispatchStage == 2) && encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x84, 0x81))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 0; dispatchStage = 3; }
                    else if (rcxSignedByteBeforeLea && r8Base && dispatchStage == 2 && preludeStep == 0 && indexRegister == 0 &&
                        hasByteIndexTable && encoded.Length == 8 && StartsWith(encoded, 0x41, 0x8b, 0x94, 0x80))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = 2; biasedSignedDispatch = true; dispatchStage = 3; }
                    else if (r8Base && dispatchStage == 1 && preludeStep == 0 && !hasByteIndexTable &&
                        encoded.Length == 8 && StartsWith(encoded, 0x41, 0x8b, 0x8c, 0x88) &&
                        MatchesDecodedPe(pe, sections, imageBase, address, encoded, contributions, decoded, decodedStarts))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = 1; r8ClGuard = true; dispatchStage = 3; }
                    else if (r8Base && (dispatchStage == 1 || dispatchStage == 2) &&
                        (preludeStep == 0 || preludeStep == 8) && encoded.Length == 8 &&
                        encoded[0] == 0x41 && encoded[1] == 0x8b &&
                        ((indexRegister == 0 && (encoded[2] == 0x84 || encoded[2] == 0x8c) && encoded[3] == 0x80) ||
                         (indexRegister == 1 && encoded[2] == 0x94 && encoded[3] == 0x88)))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = (encoded[2] >> 3) & 7; dispatchStage = 3; }
                    else if (dispatchStage == 3 && encoded.Length == 3 &&
                        ((!r8Base && !raxBase && !rdxBase && !r9Base && StartsWith(encoded, 0x48, 0x03, 0xc1)) ||
                         (raxBase && StartsWith(encoded, 0x48, 0x03, 0xc8)) ||
                         (rdxBase && StartsWith(encoded, 0x48, 0x03, 0xca) &&
                          (!rdxRemap || MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts))) ||
                         (rdxBase && (rdxByteGuard || rdxRdiIndex) && StartsWith(encoded, 0x48, 0x03, 0xc2) &&
                          MatchesDecodedPe(pe, sections, imageBase, address, encoded, contributions, decoded, decodedStarts)) ||
                         (r9Base && StartsWith(encoded, 0x4d, 0x03, 0xc1)) ||
                         (r8Base && encoded[0] == 0x49 && encoded[1] == 0x03 && encoded[2] == 0xc0 + 8 * targetRegister)))
                        dispatchStage = 4;
                    else if (dispatchStage == 4 &&
                        ((r9Base && encoded.Length == 3 && StartsWith(encoded, 0x41, 0xff, 0xe0)) ||
                         (!r9Base && encoded.Length == 2 && encoded[0] == 0xff && encoded[1] == 0xe0 + targetRegister &&
                          (!(rdxRemap || rdxRdiIndex) || MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts)))))
                    {
                        InlineTable provenByteIndexTable = null;
                        InlineTable table = biasedSignedDispatch
                            ? ReadSignedBiasedByteIndexTables(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), byteIndexTableRva, tableRva, decoded,
                                decodedStarts, guardRows, out provenByteIndexTable)
                            : r8ClGuard
                            ? ReadGuardedR8ClTerminalDwordTable(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), tableRva, decoded, decodedStarts, guardRows)
                            : rdxByteGuard
                            ? ReadGuardedRdxTerminalDwordTable(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), tableRva, decoded, decodedStarts, guardRows)
                            : ReadInlineTable(pe, sections, contributions, dataSymbols, procedures, imageBase, dispatchBegin,
                            checked(address + (ulong)encoded.Length), tableRva,
                            preludeStep != 0 || raxBase || rdxBase || r9Base || dlByteIndex || hasByteIndexTable, signedByteIndex);
                        if (table == null && !r8Base && !raxBase && !rdxBase && !r9Base &&
                            !dlByteIndex && !hasByteIndexTable && !signedByteIndex)
                            table = ReadGuardedTerminalDwordTable(pe, sections, contributions, dataSymbols, procedures,
                                imageBase, dispatchBegin, checked(address + (ulong)encoded.Length), tableRva,
                                decoded, decodedStarts, guardRows);
                        if (table != null && preludeStep != 0)
                        {
                            ulong afterJump = checked(address + 2);
                            if (preludeStep != 8 || firstPreludeTarget < afterJump || firstPreludeTarget >= table.Begin ||
                                secondPreludeTarget < afterJump || secondPreludeTarget >= table.Begin)
                                table = null;
                            else
                            {
                                table.PendingTargets.Add(firstPreludeTarget);
                                table.PendingTargets.Add(secondPreludeTarget);
                                table.Targets.Add(firstPreludeTarget);
                                table.Targets.Add(secondPreludeTarget);
                            }
                        }
                        if (table != null && hasByteIndexTable && !biasedSignedDispatch)
                        {
                            provenByteIndexTable = ProveByteIndexTable(pe, sections, dataSymbols,
                                contributions, procedures, imageBase, dispatchBegin, byteIndexTableRva, table,
                                !r8Base && !raxBase && !rdxBase && !r9Base && !signedByteIndex,
                                rdxRemap, decoded, decodedStarts, guardRows);
                            if (provenByteIndexTable == null) table = null;
                        }
                        if (table != null)
                            foreach (ulong branchTarget in table.Targets)
                                if (!IsDecodedBoundary(branchTarget, table.Module, contributions, decoded, decodedStarts))
                                { table = null; break; }
                        if (table != null && !ProcedureCoversBackwardTable(procedures, table, dispatchBegin,
                            checked(address + (ulong)encoded.Length)))
                            foreach (ulong branchTarget in table.Targets)
                                if (branchTarget < checked(address + (ulong)encoded.Length)) { table = null; break; }
                        if (table != null) table.PendingTargets.Clear();
                        if (table != null)
                        {
                            if (provenByteIndexTable != null) inlineTables.Add(provenByteIndexTable);
                            inlineTables.Add(table);
                        }
                        dispatchStage = 0;
                    }
                    else dispatchStage = 0;
                    if (encoded.Length == 7 && (StartsWith(encoded, 0x48, 0x8d, 0x0d) || StartsWith(encoded, 0x4c, 0x8d, 0x05) || StartsWith(encoded, 0x4c, 0x8d, 0x0d) ||
                        StartsWith(encoded, 0x48, 0x8d, 0x05) || StartsWith(encoded, 0x48, 0x8d, 0x15)) &&
                        checked((long)address + 7 + BitConverter.ToInt32(encoded, 3)) == (long)imageBase)
                    { dispatchBegin = address; dispatchStage = 1; r8Base = encoded[0] == 0x4c && encoded[2] == 0x05;
                      raxBase = encoded[0] == 0x48 && encoded[2] == 0x05; indexRegister = 0;
                      rdxBase = encoded[0] == 0x48 && encoded[2] == 0x15;
                      rdxBaseMatchesPe = rdxBase && MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                          contributions, decoded, decodedStarts);
                      r9Base = encoded[0] == 0x4c && encoded[2] == 0x0d;
                      hasByteIndexTable = false; byteIndexTableRva = 0;
                      rdxRemap = false; rdxByteGuard = false;
                      r8ClGuard = false;
                      rdxRdiIndex = false;
                      signedByteIndex = !r8Base && !raxBase && !rdxBase && !r9Base && previousSignedByteIndex && address == precedingEnd;
                      dlByteIndex = false;
                      rcxSignedByteBeforeLea = r8Base && previousRcxSignedByte && address == precedingEnd;
                      biasedSignedDispatch = false;
                      preludeStep = 0; firstPreludeTarget = 0; secondPreludeTarget = 0; }
                }
                previousSignedByteIndex = (bytes.Length == 5 || bytes.Length == 8) && bytes[0] == "48" &&
                                          bytes[1] == "0f" && bytes[2] == "be" &&
                                          ((bytes.Length == 5 && bytes[3] == "45") ||
                                           (bytes.Length == 8 && bytes[3] == "85"));
                previousRcxSignedByte = bytes.Length == 8 && bytes[0] == "48" && bytes[1] == "0f" && bytes[2] == "be" &&
                                        bytes[3] == "81";
                precedingEnd = checked(address + (ulong)bytes.Length);
                int prefix = 0;
                byte opcode = Byte.Parse(bytes[prefix], NumberStyles.HexNumber);
                while (IsLegacyPrefix(opcode) && ++prefix < bytes.Length)
                    opcode = Byte.Parse(bytes[prefix], NumberStyles.HexNumber);
                if (opcode != 0xc4 && opcode != 0xc5 && opcode != 0x62) continue;
                bool inlineData = false;
                foreach (InlineTable table in inlineTables)
                    if (IsTableData(table, inlineTables, address, bytes, pe, sections, imageBase))
                    { inlineData = true; break; }
                if (inlineData) { ++result.InlineTableVectorData; continue; }
                // Linear disassembly can encounter inline jump tables. An undecoded data byte is not an
                // instruction. Report it separately instead of exempting any decoded instruction.
                if (match.Groups[3].Value.StartsWith("<unknown>", StringComparison.Ordinal))
                { ++result.UndecodedPrefixes; continue; }
                ++result.VectorInstructions;
                if (opcode == 0x62) ++result.EvexInstructions;
                int contributionIndex = FindContribution(contributions, address);
                if (contributionIndex < 0)
                {
                    result.Violations.Add(String.Format("0x{0:x}: no PDB contribution: {1}", address, match.Groups[3].Value));
                    continue;
                }
                Contribution contribution = contributions[contributionIndex];
                Module owner;
                if (!modules.TryGetValue(contribution.Module, out owner))
                    throw new InvalidDataException("Contribution refers to an absent module.");
                string objectName = Path.GetFileName(owner.Object);
                Match tier = TierObject.Match(objectName);
                bool wholeInstruction = checked(address + (ulong)bytes.Length) <= contribution.End;
                if (wholeInstruction && tier.Success && (opcode != 0x62 || tier.Groups[1].Value == "Avx512"))
                {
                    ++result.TierInstructions;
                    if (opcode == 0x62) ++result.TierEvexInstructions;
                    continue;
                }
                if (wholeInstruction && Contains(runtimeObjects, objectName) &&
                    Contains(runtimeLibraries, Path.GetFileName(owner.Library ?? "")))
                { ++result.RuntimeInstructions; continue; }
                bool runtimeFunction = false;
                foreach (Function candidate in functions)
                    if (candidate.Module == contribution.Module && address >= candidate.Begin &&
                        address + (ulong)bytes.Length <= candidate.End && candidate.Begin >= contribution.Begin &&
                        candidate.End <= contribution.End)
                    { runtimeFunction = true; break; }
                if (wholeInstruction && runtimeFunction) { ++result.RuntimeInstructions; continue; }
                result.Violations.Add(String.Format("0x{0:x}: {1}: {2}", address, owner.Object, match.Groups[3].Value));
            }
            if (result.Instructions == 0) throw new InvalidDataException("No decoded executable instructions.");
            return result;
        }
    }
}
