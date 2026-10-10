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
            public uint Rva, Raw, Size, RawSize;
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
            public bool VerifyReportedBytes;
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
            if (table.VerifyReportedBytes && !MatchesPe(pe, sections, imageBase, address,
                Array.ConvertAll(bytes, b => Byte.Parse(b, NumberStyles.HexNumber)))) return false;
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

        // Observed Release variant move: RBP+disp32 signed byte, RDX image base, biased DWORD table.
        // Prove all five contiguous rows against the PE; the first/default entry belongs to the exact
        // anonymous PDB table start one DWORD before the load. This is data proof, never an ISA exemption.
        private static InlineTable ReadSignedRdxDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 8 || dispatch - imageBase > UInt32.MaxValue - 19 ||
                afterJump != dispatch + 19) return null;
            ulong prefix = dispatch - 8;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 27 || !StartsWithAt(pe, raw, 0x48, 0x0f, 0xbe, 0x85) ||
                !StartsWithAt(pe, raw + 8, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 11) != (long)imageBase ||
                !StartsWithAt(pe, raw + 15, 0x8b, 0x84, 0x82) ||
                BitConverter.ToUInt32(pe, raw + 18) != tableRva ||
                !StartsWithAt(pe, raw + 22, 0x48, 0x03, 0xc2) ||
                !StartsWithAt(pe, raw + 25, 0xff, 0xe0)) return null;
            int[] starts = { 0, 8, 15, 22, 25 };
            int[] lengths = { 8, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            InlineTable table = ReadInlineTable(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, afterJump, tableRva, true, true);
            if (table == null) return null;
            foreach (ulong target in table.Targets)
                if (target < afterJump || !IsDecodedBoundary(target, table.Module, contributions, decoded, decodedStarts))
                    return null;
            return table;
        }

        // DxUi's ASan grid switch ends with six DWORDs inside a much larger contribution. Its next
        // anonymous data symbol belongs to another procedure. Application::EditUnderWay uses the same
        // terminal dispatch with a long JA; EndCanvasEdit uses its disp32 stack form. Prove only these
        // exact stack guards and RCX dispatches, every reported/PE byte and the exact terminal procedure end.
        // WindowHost also emits a disp8 MOV EAX index (zero extended), short JA and RET directly before data.
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
                    imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows, true, true) ??
                ReadGuardedTerminalDwordTableForm(pe, sections, contributions, dataSymbols, procedures,
                    imageBase, dispatch, afterJump, tableRva, decoded, decodedStarts, guardRows, false, false, true);
        }

        private static InlineTable ReadGuardedTerminalDwordTableForm(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, ulong afterJump, uint tableRva,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> guardRows, bool longJa, bool wideFrame = false, bool unsignedIndex = false)
        {
            if (unsignedIndex && (longJa || wideFrame)) return null;
            int stackLength = wideFrame ? 8 : 5;
            int indexLength = unsignedIndex ? 4 : stackLength;
            int guardLength = stackLength + indexLength + (longJa ? 6 : 2);
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
                (unsignedIndex ? !StartsWithAt(pe, raw + indexStart, 0x8b, 0x44, 0x24) :
                 !StartsWithAt(pe, raw + indexStart, 0x48, 0x63, wideFrame ? (byte)0x84 : (byte)0x44, 0x24)) ||
                (wideFrame ? BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + indexStart + 4) :
                 pe[raw + 3] != pe[raw + indexStart + (unsignedIndex ? 3 : 4)]) ||
                !StartsWithAt(pe, raw + guardLength, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + guardLength + 3) != (long)imageBase ||
                !StartsWithAt(pe, raw + guardLength + 7, 0x8b, 0x84, 0x81) ||
                BitConverter.ToUInt32(pe, raw + guardLength + 10) != tableRva ||
                !StartsWithAt(pe, raw + guardLength + 14, 0x48, 0x03, 0xc1) ||
                !StartsWithAt(pe, raw + guardLength + 17, 0xff, 0xe0)) return null;
            uint maximum = pe[raw + stackLength - 1];
            if (maximum == 0 || maximum >= 128) return null; // CMP imm8 must be nonnegative
            int[] starts = { 0, stackLength, indexStart, guardLength, guardLength + 7, guardLength + 14, guardLength + 17 };
            int[] lengths = { stackLength, branchLength, indexLength, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong end = checked(begin + 4 * (maximum + 1));
            if (end > owner.End) return null;
            if (unsignedIndex && (!guardRows.TryGetValue(begin - 1, out byte[] terminal) ||
                terminal.Length != 1 || terminal[0] != 0xc3 ||
                !MatchesDecodedPe(pe, sections, imageBase, begin - 1, terminal, contributions, decoded, decodedStarts)))
                return null;
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
                if (defaultAddress >= 0 && (longJa || unsignedIndex
                    ? (ulong)defaultAddress >= (unsignedIndex ? afterJump : procedure.Begin) && (ulong)defaultAddress < begin &&
                      IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)
                    : table.Targets.Contains((ulong)defaultAddress))) return table;
            }
            return null;
        }

        // ASan's size_t variant switch compares a QWORD stack slot, then uses RAX as image base and
        // RCX as both table index and target. Accept only the observed seven contiguous PE/listing rows,
        // same stack slot, bounded unsigned guard, anonymous same-owner table and exact terminal procedure.
        private static InlineTable ReadGuardedQwordStackRaxTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            return ReadGuardedStackRaxTableForm(pe, sections, contributions, dataSymbols, procedures,
                imageBase, dispatch, decoded, decodedStarts, guardRows, false) ??
                ReadGuardedStackRaxTableForm(pe, sections, contributions, dataSymbols, procedures,
                    imageBase, dispatch, decoded, decodedStarts, guardRows, true);
        }

        // The second exact form is DWORD [RSP] CMP/short JA, RAX image base, and unsigned ECX [RSP].
        // Both forms retain the same terminal PDB, target, whole-body and byte/listing proofs below.
        private static InlineTable ReadGuardedStackRaxTableForm(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, bool zeroStackDword)
        {
            int prefixLength = zeroStackDword ? 6 : 15;
            int dispatchLength = zeroStackDword ? 22 : 27;
            if (dispatch < imageBase + (ulong)prefixLength || dispatch - imageBase > UInt32.MaxValue) return null;
            ulong prefix = dispatch - (ulong)prefixLength, afterJump = checked(dispatch + (ulong)dispatchLength);
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, dispatch) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            if (afterJump > owner.End) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (zeroStackDword)
            {
                if (raw > pe.Length - 28 || !StartsWithAt(pe, raw, 0x83, 0x3c, 0x24) ||
                    pe[raw + 4] != 0x77 || !StartsWithAt(pe, raw + 6, 0x48, 0x8d, 0x05) ||
                    (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 9) != (long)imageBase ||
                    !StartsWithAt(pe, raw + 13, 0x8b, 0x0c, 0x24) ||
                    !StartsWithAt(pe, raw + 16, 0x8b, 0x8c, 0x88) ||
                    !StartsWithAt(pe, raw + 23, 0x48, 0x03, 0xc8) ||
                    !StartsWithAt(pe, raw + 26, 0xff, 0xe1)) return null;
            }
            else
            {
                if (raw > pe.Length - 42 || !StartsWithAt(pe, raw, 0x48, 0x83, 0xbc, 0x24) ||
                    !StartsWithAt(pe, raw + 9, 0x0f, 0x87) ||
                    !StartsWithAt(pe, raw + 15, 0x48, 0x8d, 0x05) ||
                    (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 18) != (long)imageBase ||
                    !StartsWithAt(pe, raw + 22, 0x48, 0x8b, 0x8c, 0x24) ||
                    BitConverter.ToUInt32(pe, raw + 4) != BitConverter.ToUInt32(pe, raw + 26) ||
                    !StartsWithAt(pe, raw + 30, 0x8b, 0x8c, 0x88) ||
                    !StartsWithAt(pe, raw + 37, 0x48, 0x03, 0xc8) ||
                    !StartsWithAt(pe, raw + 40, 0xff, 0xe1)) return null;
            }
            uint maximum = pe[raw + (zeroStackDword ? 3 : 8)];
            if (maximum == 0 || maximum >= 128) return null; // sign-extended CMP imm8 must be nonnegative
            int[] starts = zeroStackDword ? new int[] { 0, 4, 6, 13, 16, 23, 26 } : new int[] { 0, 9, 15, 22, 30, 37, 40 };
            int[] lengths = zeroStackDword ? new int[] { 4, 2, 7, 3, 7, 3, 2 } : new int[] { 9, 6, 7, 8, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            uint tableRva = BitConverter.ToUInt32(pe, raw + (zeroStackDword ? 19 : 33));
            ulong begin = checked(imageBase + tableRva), end = checked(begin + 4 * (maximum + 1));
            long fallback = zeroStackDword ? checked((long)prefix + 6 + unchecked((sbyte)pe[raw + 5])) :
                checked((long)prefix + 15 + BitConverter.ToInt32(pe, raw + 11));
            if ((begin & 3) != 0 || begin <= afterJump || end > owner.End ||
                fallback < (long)afterJump || fallback >= (long)begin ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
            if (tableRaw > pe.Length - checked((int)(end - begin)) || begin < imageBase + 1) return null;
            ulong returnAddress = begin - 1;
            if (!guardRows.TryGetValue(returnAddress, out byte[] terminal) || terminal.Length != 1 || terminal[0] != 0xc3 ||
                !MatchesDecodedPe(pe, sections, imageBase, returnAddress, terminal, contributions, decoded, decodedStarts))
            {
                // MSVC emits either one-byte NOP or this exact three-byte alignment NOP after RET.
                // Every terminal row must be decoded, nonoverlapping and byte-identical to the PE;
                // the padded path below additionally validates the complete procedure's control flow.
                if (begin >= imageBase + 2 &&
                    guardRows.TryGetValue(begin - 2, out byte[] singleRet) && singleRet.Length == 1 && singleRet[0] == 0xc3 &&
                    MatchesDecodedPe(pe, sections, imageBase, begin - 2, singleRet, contributions, decoded, decodedStarts) &&
                    guardRows.TryGetValue(begin - 1, out byte[] singleNop) && singleNop.Length == 1 && singleNop[0] == 0x90 &&
                    MatchesDecodedPe(pe, sections, imageBase, begin - 1, singleNop, contributions, decoded, decodedStarts))
                    returnAddress = begin - 2;
                else if (begin < imageBase + 4 ||
                    !guardRows.TryGetValue(begin - 4, out terminal) || terminal.Length != 1 || terminal[0] != 0xc3 ||
                    !MatchesDecodedPe(pe, sections, imageBase, begin - 4, terminal, contributions, decoded, decodedStarts) ||
                    !guardRows.TryGetValue(begin - 3, out byte[] padding) || padding.Length != 3 ||
                    !StartsWith(padding, 0x0f, 0x1f, 0x00) ||
                    !MatchesDecodedPe(pe, sections, imageBase, begin - 3, padding, contributions, decoded, decodedStarts))
                    return null;
                else
                    returnAddress = begin - 4;
            }
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end ||
                    !IsDecodedBoundary(procedure.Begin, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                bool valid = true;
                for (uint entry = 0; entry <= maximum && valid; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    valid = target >= procedure.Begin && target <= returnAddress && (target < prefix || target >= afterJump) &&
                            IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                    table.Targets.Add(target);
                }
                if (!valid || !table.Targets.Contains((ulong)fallback)) continue;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                ulong nextAddress = procedure.Begin;
                bool padded = returnAddress != begin - 1;
                for (int i = first; i >= 0 && i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (padded)
                    {
                        if (address != nextAddress || instruction.Unknown || instruction.Module != owner.Module ||
                            instruction.Length <= 0 || instruction.Length > 15 || address + (ulong)instruction.Length > begin ||
                            !guardRows.TryGetValue(address, out byte[] bodyRow) || bodyRow.Length != instruction.Length ||
                            !MatchesDecodedPe(pe, sections, imageBase, address, bodyRow, contributions, decoded, decodedStarts))
                        { valid = false; break; }
                        nextAddress = address + (ulong)instruction.Length;
                    }
                    if (instruction.Unknown || instruction.Module != owner.Module) continue;
                    int source = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length));
                    if (source > pe.Length - instruction.Length) { valid = false; break; }
                    int opcodeOffset = 0;
                    if (padded)
                        while (opcodeOffset < instruction.Length && (IsLegacyPrefix(pe[source + opcodeOffset]) ||
                               (pe[source + opcodeOffset] >= 0x40 && pe[source + opcodeOffset] <= 0x4f))) ++opcodeOffset;
                    if (opcodeOffset == instruction.Length) continue;
                    byte opcode = pe[source + opcodeOffset];
                    long target;
                    if (opcode == 0xeb || (opcode >= 0x70 && opcode <= 0x7f) ||
                        (padded && opcode >= 0xe0 && opcode <= 0xe3))
                    {
                        if (instruction.Length != opcodeOffset + 2) { valid = false; break; }
                        target = (long)address + instruction.Length + unchecked((sbyte)pe[source + opcodeOffset + 1]);
                    }
                    else if (opcode == 0xe9 || opcode == 0xe8)
                    {
                        if (instruction.Length != opcodeOffset + 5) { valid = false; break; }
                        target = (long)address + instruction.Length + BitConverter.ToInt32(pe, source + opcodeOffset + 1);
                    }
                    else if (opcode == 0x0f && opcodeOffset + 1 < instruction.Length &&
                             pe[source + opcodeOffset + 1] >= 0x80 && pe[source + opcodeOffset + 1] <= 0x8f)
                    {
                        if (instruction.Length != opcodeOffset + 6) { valid = false; break; }
                        target = (long)address + instruction.Length + BitConverter.ToInt32(pe, source + opcodeOffset + 2);
                    }
                    else continue;
                    if ((target > (long)prefix && target < (long)afterJump &&
                         (address < prefix || address >= afterJump)) ||
                        (target > (long)returnAddress && target < (long)end)) { valid = false; break; }
                }
                if (valid && (!padded || nextAddress == begin)) return table;
            }
            return null;
        }

        // Release's R9 byte switch has an exact terminal DWORD table, with a cold CALL/INT3 tail after RET.
        // Classify data only: corroborate the dispatch, anonymous PDB start, exact procedure end and every
        // target, with uninterrupted known decoding before the table so missing edges cannot disappear.
        // The byte CMP establishes the observed table extent, not a general proof of R9's upper
        // bits or the compiler's entire control flow. Reject direct entries into the dispatch/table except
        // the observed identical CMP/JNE to JA, whose comparison flags are preserved by that branch.
        // The observed message switch hoists R9=imagebase. Prove the exact guarded R15-257 encoding and
        // the anonymous table extent. A reverse CFG proof, specialized to each admitted message value,
        // must find an intact image-base definition on every feasible incoming path. Unknown flag/data
        // definitions and register effects are refused; a same-procedure LEA alone is never sufficient.
        private static InlineTable ReadGuardedR9HoistedDirectTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong load, Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts,
            Dictionary<ulong, byte[]> rows)
        {
            if (load < imageBase + 16) return null;
            ulong prefix = load - 16;
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 29 ||
                !StartsWithAt(pe, raw, 0x41, 0x8d, 0x87, 0xff, 0xfe, 0xff, 0xff) ||
                !StartsWithAt(pe, raw + 7, 0x83, 0xf8) || pe[raw + 9] < 1 || pe[raw + 9] > 127 ||
                !StartsWithAt(pe, raw + 10, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 16, 0x41, 0x8b, 0x8c, 0x81) ||
                !StartsWithAt(pe, raw + 24, 0x49, 0x03, 0xc9) ||
                !StartsWithAt(pe, raw + 27, 0xff, 0xe1)) return null;
            int maximum = pe[raw + 9];
            ulong afterJump = prefix + 29;
            ulong begin = checked(imageBase + BitConverter.ToUInt32(pe, raw + 20));
            ulong end = checked(begin + 4ul * (ulong)(maximum + 1));
            long fallback = checked((long)prefix + 16 + BitConverter.ToInt32(pe, raw + 12));
            int[] starts = { 0, 7, 10, 16, 24, 27 };
            int[] lengths = { 7, 3, 6, 8, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
                if (!rows.TryGetValue(prefix + (ulong)starts[i], out byte[] bytes) || bytes.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, prefix + (ulong)starts[i], bytes,
                        contributions, decoded, decodedStarts)) return null;
            if ((begin & 3) != 0 || begin <= afterJump || end > owner.End ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End < end) continue;
                ulong next = procedure.End, codeEnd = procedure.End;
                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                    if (symbol.Value == owner.Module && symbol.Key >= procedure.Begin && symbol.Key < procedure.End)
                    {
                        codeEnd = Math.Min(codeEnd, symbol.Key);
                        if (symbol.Key > begin) next = Math.Min(next, symbol.Key);
                    }
                if (next != end || codeEnd < afterJump || fallback < (long)procedure.Begin ||
                    (ulong)fallback >= codeEnd || ((ulong)fallback >= prefix && (ulong)fallback < afterJump) ||
                    !IsDecodedBoundary((ulong)fallback, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true };
                int tableRaw;
                try { tableRaw = checked((int)Offset(checked((uint)(begin - imageBase)), sections, pe.Length)); }
                catch (InvalidDataException) { continue; }
                if (tableRaw > pe.Length - 4 * (maximum + 1)) continue;
                bool valid = true;
                for (int i = 0; i <= maximum; ++i)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + 4 * i));
                    if (target < procedure.Begin || target >= codeEnd || (target >= prefix && target < afterJump) ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    table.Targets.Add(target);
                }
                if (!valid || !table.Targets.Contains((ulong)fallback)) continue;
                for (int value = 257; value <= 257 + maximum && valid; ++value)
                    valid = ProveHoistedR9(pe, sections, contributions, dataSymbols, imageBase, procedure,
                        codeEnd, prefix, afterJump, begin, end, value, decoded, decodedStarts, rows);
                if (valid) return table;
            }
            return null;
        }

        // This observed Release worker loop hoists the nonvolatile R14 base across an exact,
        // bounded loop. Prove the complete producer/guard/cases/back edge, not an arbitrary
        // same-procedure LEA. Outside entries cannot bypass its producer or guard. The only
        // call in the loop targets outside this procedure and preserves R14 by the x64 ABI.
        private static InlineTable ReadGuardedR14LoopTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong load, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> rows)
        {
            if (load < imageBase + 46 || load - imageBase > UInt32.MaxValue - 86) return null;
            ulong prefix = load - 46, loopEnd = prefix + 132;
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, loopEnd - 1) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            int raw;
            try { raw = checked((int)Offset((uint)(prefix - imageBase), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 132) return null;
            int[] starts = { 0, 7, 15, 23, 27, 30, 32, 36, 38, 41, 44, 46, 54, 57, 59, 61, 63,
                68, 70, 75, 78, 80, 83, 85, 89, 93, 95, 98, 101, 106, 108, 112, 120, 127, 130 };
            byte[][] encodings = {
                new byte[]{0x4c,0x8d,0x35,0,0,0,0},
                new byte[]{0x44,0x8b,0xac,0x24,0x08,0x02,0,0},
                new byte[]{0x44,0x8b,0xa4,0x24,0x10,0x02,0,0},
                new byte[]{0x0f,0x1f,0x40,0}, new byte[]{0x83,0x3b,2}, new byte[]{0x75,0x58},
                new byte[]{0x44,0x39,0x63,0x34}, new byte[]{0x75,0x52}, new byte[]{0x8b,0x43,0x18},
                new byte[]{0x83,0xf8,5}, new byte[]{0x77,0x18}, new byte[]{0x41,0x8b,0x8c,0x86,0,0,0,0},
                new byte[]{0x49,0x03,0xce}, new byte[]{0xff,0xe1}, new byte[]{0x33,0xc0}, new byte[]{0xeb,0x0c},
                new byte[]{0xb8,1,0,0,0}, new byte[]{0xeb,5}, new byte[]{0xb8,2,0,0,0},
                new byte[]{0x41,0x3b,0xc5}, new byte[]{0x75,0x28}, new byte[]{0x48,0x85,0xff},
                new byte[]{0x74,0x0a}, new byte[]{0x48,0x8b,0x47,0x10}, new byte[]{0x48,0x39,0x43,0x10},
                new byte[]{0x73,0x19}, new byte[]{0x48,0x8b,0xd3}, new byte[]{0x48,0x8b,0xce},
                new byte[]{0xe8,0,0,0,0}, new byte[]{0x84,0xc0}, new byte[]{0x48,0x0f,0x44,0xfb},
                new byte[]{0x48,0x89,0xbc,0x24,0x20,0x01,0,0}, new byte[]{0x48,0x81,0xc3,0xd0,0,0,0},
                new byte[]{0x49,0x3b,0xdf}, new byte[]{0x75,0x97} };
            for (int i = 0; i < starts.Length; ++i)
            {
                int at = starts[i];
                for (int b = 0; b < encodings[i].Length; ++b)
                    if (!((at == 0 && b >= 3) || (at == 46 && b >= 4) || (at == 101 && b >= 1)) &&
                        pe[raw + at + b] != encodings[i][b]) return null;
                if (!rows.TryGetValue(prefix + (ulong)at, out byte[] reported) || reported.Length != encodings[i].Length ||
                    !MatchesDecodedPe(pe, sections, imageBase, prefix + (ulong)at, reported,
                        contributions, decoded, decodedStarts)) return null;
            }
            if ((long)prefix + 7 + BitConverter.ToInt32(pe, raw + 3) != (long)imageBase) return null;
            uint tableRva = BitConverter.ToUInt32(pe, raw + 50);
            ulong begin = checked(imageBase + tableRva), end = begin + 24;
            long callee = (long)prefix + 106 + BitConverter.ToInt32(pe, raw + 102);
            if ((begin & 3) != 0 || begin < loopEnd + 7 || end > owner.End ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int tableRaw;
            try { tableRaw = checked((int)Offset(tableRva, sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (tableRaw < 7 || tableRaw > pe.Length - 24 || pe[tableRaw - 7] != 0xe9 ||
                !StartsWithAt(pe, tableRaw - 2, 0x66, 0x90)) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end ||
                    (callee >= (long)procedure.Begin && callee < (long)end)) continue;
                bool valid = true;
                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                    if (symbol.Value == owner.Module && symbol.Key >= procedure.Begin && symbol.Key < end && symbol.Key != begin)
                    { valid = false; break; }
                if (!valid) continue;
                long terminalTarget = (long)begin - 2 + BitConverter.ToInt32(pe, tableRaw - 6);
                if (terminalTarget < (long)procedure.Begin || terminalTarget >= (long)prefix ||
                    !IsDecodedBoundary((ulong)terminalTarget, owner.Module, contributions, decoded, decodedStarts)) continue;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) continue;
                ulong next = procedure.Begin;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (address != next || instruction.Unknown || instruction.Module != owner.Module ||
                        instruction.Length <= 0 || instruction.Length > 15 || address + (ulong)instruction.Length > begin ||
                        !rows.TryGetValue(address, out byte[] body) || body.Length != instruction.Length ||
                        !MatchesDecodedPe(pe, sections, imageBase, address, body, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    next = address + (ulong)instruction.Length;
                    int op = 0;
                    while (op < body.Length && (IsLegacyPrefix(body[op]) || (body[op] >= 0x40 && body[op] <= 0x4f))) ++op;
                    if (op == body.Length) continue;
                    long target;
                    bool call = false;
                    if (body[op] == 0xeb || (body[op] >= 0x70 && body[op] <= 0x7f) || (body[op] >= 0xe0 && body[op] <= 0xe3))
                    {
                        if (body.Length != op + 2) { valid = false; break; }
                        target = (long)next + unchecked((sbyte)body[op + 1]);
                    }
                    else if (body[op] == 0xe9 || body[op] == 0xe8)
                    {
                        if (body.Length != op + 5) { valid = false; break; }
                        target = (long)next + BitConverter.ToInt32(body, op + 1);
                        call = body[op] == 0xe8;
                    }
                    else if (body[op] == 0x0f && op + 1 < body.Length && body[op + 1] >= 0x80 && body[op + 1] <= 0x8f)
                    {
                        if (body.Length != op + 6) { valid = false; break; }
                        target = (long)next + BitConverter.ToInt32(body, op + 2);
                    }
                    else
                    {
                        if ((body[op] == 0xff && op + 1 < body.Length &&
                             ((body[op + 1] & 0x38) == 0x20 || (body[op + 1] & 0x38) == 0x28) && address != prefix + 57) ||
                            (body[op] == 0xff && op + 1 < body.Length && (body[op + 1] & 0x38) == 0x18) ||
                            body[op] == 0x9a || body[op] == 0xea || body[op] == 0xca || body[op] == 0xcb || body[op] == 0xcf)
                            valid = false;
                        if (!valid) break;
                        continue;
                    }
                    if ((target > (long)prefix && target < (long)loopEnd &&
                         (address < prefix || address >= loopEnd)) ||
                        (target >= (long)begin - 2 && target < (long)end) ||
                        (call && target >= (long)procedure.Begin && target < (long)end))
                    { valid = false; break; }
                }
                if (!valid || next != begin || !rows.TryGetValue(begin - 7, out byte[] terminal) || terminal.Length != 5 ||
                    !rows.TryGetValue(begin - 2, out byte[] padding) || padding.Length != 2) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true };
                for (int entry = 0; entry < 6; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + 4 * entry));
                    if ((target != prefix + 59 && target != prefix + 63) ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    table.Targets.Add(target);
                }
                if (!valid) continue;
                int tableFirst = decodedStarts.BinarySearch(begin);
                if (tableFirst < 0) continue;
                next = begin;
                for (int i = tableFirst; i < decodedStarts.Count && decodedStarts[i] < end; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (address != next || instruction.Unknown || instruction.Module != owner.Module ||
                        instruction.Length <= 0 || instruction.Length > 15 ||
                        !rows.TryGetValue(address, out byte[] reported) || reported.Length != instruction.Length ||
                        !MatchesPe(pe, sections, imageBase, address, reported) ||
                        (i + 1 < decodedStarts.Count && decodedStarts[i + 1] < address + (ulong)instruction.Length))
                    { valid = false; break; }
                    next = address + (ulong)instruction.Length;
                }
                if (valid && next >= end) return table;
            }
            return null;
        }

        private sealed class R9FlowEdge
        {
            public ulong Source, Destination;
            public bool Conditional, Taken;
            public R9FlowSwitch Switch;
        }

        private sealed class R9FlowSwitch
        {
            public ulong Guard, End;
            public int Bias;
            public ulong[] Targets;
        }

        private static bool ProveHoistedR9(byte[] pe, List<Section> sections, List<Contribution> contributions,
            Dictionary<ulong, int> symbols, ulong imageBase, Function procedure, ulong codeEnd,
            ulong guard, ulong guardEnd, ulong tableBegin, ulong tableEnd, int message,
            Dictionary<ulong, DecodedInstruction> decoded, List<ulong> decodedStarts, Dictionary<ulong, byte[]> rows)
        {
            var parents = new Dictionary<ulong, List<R9FlowEdge>>();
            var previous = new Dictionary<ulong, ulong>();
            var body = new List<ulong>();
            var switches = new List<R9FlowSwitch>();
            var calls = new HashSet<ulong>();
            var terminalPadding = new HashSet<ulong>();
            int first = decodedStarts.BinarySearch(procedure.Begin);
            if (first < 0) return false;
            ulong prior = 0;
            for (int i = first; i < decodedStarts.Count && decodedStarts[i] < codeEnd; ++i)
            {
                ulong address = decodedStarts[i];
                if (body.Count >= 16384 || !rows.TryGetValue(address, out byte[] bytes) ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, bytes, contributions, decoded, decodedStarts))
                    return false;
                if (prior != 0) previous[address] = prior;
                prior = address;
                body.Add(address);
                parents[address] = new List<R9FlowEdge>();
            }
            bool Add(ulong source, long destination, bool conditional, bool taken, R9FlowSwitch routing = null)
            {
                if (destination < (long)procedure.Begin || destination >= (long)codeEnd ||
                    !parents.TryGetValue((ulong)destination, out List<R9FlowEdge> incoming)) return false;
                incoming.Add(new R9FlowEdge { Source = source, Destination = (ulong)destination,
                    Conditional = conditional, Taken = taken, Switch = routing });
                return true;
            }
            foreach (ulong address in body)
            {
                byte[] bytes = rows[address];
                ulong following = address + (ulong)bytes.Length;
                bool branch = false, conditional = false, call = false;
                long target = 0;
                if (bytes.Length == 2 && (bytes[0] == 0xeb || (bytes[0] >= 0x70 && bytes[0] <= 0x7f)))
                { branch = true; conditional = bytes[0] != 0xeb; target = (long)following + unchecked((sbyte)bytes[1]); }
                else if (bytes.Length == 5 && (bytes[0] == 0xe9 || bytes[0] == 0xe8))
                { branch = true; call = bytes[0] == 0xe8; target = (long)following + BitConverter.ToInt32(bytes, 1); }
                else if (bytes.Length == 6 && bytes[0] == 0x0f && bytes[1] >= 0x80 && bytes[1] <= 0x8f)
                { branch = true; conditional = true; target = (long)following + BitConverter.ToInt32(bytes, 2); }
                if (branch)
                {
                    if (call && target >= 0) calls.Add((ulong)target);
                    if ((target > (long)guard && target < (long)guardEnd &&
                         (address < guard || address >= guardEnd)) ||
                        (call && target == (long)guard) || (target >= (long)tableBegin && target < (long)tableEnd))
                        return false;
                    if (!call && !Add(address, target, conditional, true)) return false;
                    if ((conditional || call) && !Add(address, (long)following, conditional, false)) return false;
                    continue;
                }
                if (bytes[0] == 0xc3 || (bytes.Length == 3 && bytes[0] == 0xc2) ||
                    (bytes.Length == 1 && bytes[0] == 0xcc)) continue;
                if (bytes.Length == 2 && bytes[0] == 0xff && (bytes[1] == 0xe1 || bytes[1] == 0xe2))
                {
                    if (!TryR9FlowTargets(pe, sections, symbols, imageBase, procedure, codeEnd,
                        address, rows, previous, out R9FlowSwitch routing)) return false;
                    switches.Add(routing);
                    foreach (ulong destination in new HashSet<ulong>(routing.Targets))
                        if (!Add(address, (long)destination, false, true, routing)) return false;
                    continue;
                }
                // Other indirect jumps have no proved local target set. Do not infer a reaching definition.
                if ((bytes[0] == 0xff && bytes.Length >= 2 && (bytes[1] & 0x38) == 0x20) ||
                    (bytes.Length >= 3 && bytes[0] >= 0x40 && bytes[0] <= 0x4f && bytes[1] == 0xff &&
                     (bytes[2] & 0x38) == 0x20)) return false;
                if (following == codeEnd &&
                    ((bytes.Length == 1 && bytes[0] == 0x90) ||
                     (bytes.Length == 2 && StartsWith(bytes, 0x66, 0x90)) ||
                     (bytes.Length == 3 && StartsWith(bytes, 0x0f, 0x1f, 0x00))))
                { terminalPadding.Add(address); continue; }
                if (!Add(address, (long)following, false, false)) return false;
            }
            // Padding after unconditional transfers has no executable predecessor. Keep only syntactically
            // reachable nodes; branch conditions are still an overapproximation at this stage.
            var children = new Dictionary<ulong, List<ulong>>();
            foreach (ulong address in body) children[address] = new List<ulong>();
            foreach (KeyValuePair<ulong, List<R9FlowEdge>> item in parents)
                foreach (R9FlowEdge edge in item.Value) children[edge.Source].Add(item.Key);
            var reachable = new HashSet<ulong>();
            var pending = new Stack<ulong>();
            pending.Push(procedure.Begin);
            while (pending.Count != 0)
            {
                ulong address = pending.Pop();
                if (!reachable.Add(address)) continue;
                foreach (ulong child in children[address]) pending.Push(child);
            }
            if (!reachable.Contains(guard)) return false;
            foreach (ulong address in terminalPadding) if (reachable.Contains(address)) return false;
            foreach (List<R9FlowEdge> incoming in parents.Values)
                incoming.RemoveAll(edge => !reachable.Contains(edge.Source));
            foreach (R9FlowSwitch routing in switches)
            {
                if (calls.Contains(routing.Guard)) return false;
                foreach (ulong address in body)
                    if (address > routing.Guard && address < routing.End)
                    {
                        if (!previous.TryGetValue(address, out ulong preceding) || parents[address].Count != 1 ||
                            parents[address][0].Source != preceding || parents[address][0].Taken) return false;
                    }
            }
            bool TrustedPrevious(ulong at, out ulong preceding)
            {
                preceding = 0;
                return previous.TryGetValue(at, out preceding) && parents[at].Count == 1 &&
                    parents[at][0].Source == preceding && !parents[at][0].Taken;
            }
            bool Feasible(R9FlowEdge edge)
            {
                if (edge.Switch != null)
                {
                    uint index = unchecked((uint)(message + edge.Switch.Bias));
                    return index < edge.Switch.Targets.Length && edge.Switch.Targets[index] == edge.Destination;
                }
                if (!edge.Conditional) return true;
                ulong comparison;
                if (!TrustedPrevious(edge.Source, out comparison)) return true;
                byte[] cmp = rows[comparison];
                // Consecutive JA/JE consumers share the immediately preceding CMP; neither changes flags.
                if ((cmp.Length == 2 && cmp[0] >= 0x70 && cmp[0] <= 0x7f) ||
                    (cmp.Length == 6 && cmp[0] == 0x0f && cmp[1] >= 0x80 && cmp[1] <= 0x8f))
                {
                    if (!TrustedPrevious(comparison, out comparison)) return true;
                    cmp = rows[comparison];
                }
                uint left, right;
                if (cmp.Length == 7 && StartsWith(cmp, 0x41, 0x81, 0xff))
                { left = (uint)message; right = BitConverter.ToUInt32(cmp, 3); }
                else if ((cmp.Length == 3 && StartsWith(cmp, 0x83, 0xf8)) ||
                         (cmp.Length == 5 && cmp[0] == 0x3d))
                {
                    if (!TrustedPrevious(comparison, out ulong index)) return true;
                    byte[] lea = rows[index];
                    int bias;
                    if (lea.Length == 4 && StartsWith(lea, 0x41, 0x8d, 0x47)) bias = unchecked((sbyte)lea[3]);
                    else if (lea.Length == 7 && StartsWith(lea, 0x41, 0x8d, 0x87)) bias = BitConverter.ToInt32(lea, 3);
                    else return true;
                    left = unchecked((uint)(message + bias));
                    right = cmp.Length == 3 ? unchecked((uint)(int)(sbyte)cmp[2]) : BitConverter.ToUInt32(cmp, 1);
                }
                else return true;
                byte code = (byte)(rows[edge.Source][rows[edge.Source][0] == 0x0f ? 1 : 0] & 0x0f);
                bool taken;
                if (code == 7) taken = left > right;
                else if (code == 6) taken = left <= right;
                else if (code == 4) taken = left == right;
                else if (code == 5) taken = left != right;
                else return true;
                return taken == edge.Taken;
            }
            var active = new HashSet<ulong>();
            // 0: no feasible path, 1: all feasible paths preserve a proved definition, 2: unsafe/unknown.
            var memo = new Dictionary<ulong, int>();
            int Walk(ulong address, int depth)
            {
                if (depth > 256) return 2;
                if (memo.TryGetValue(address, out int known)) return known;
                if (!active.Add(address)) return 2;
                byte[] bytes = rows[address];
                int valid;
                if (bytes.Length == 7 && StartsWith(bytes, 0x4c, 0x8d, 0x0d))
                    valid = (long)address + 7 + BitConverter.ToInt32(bytes, 3) == (long)imageBase ? 1 : 2;
                else
                {
                    valid = PreservesR9AndR15(bytes) ? 0 : 2;
                    if (valid == 0)
                    {
                        foreach (R9FlowEdge edge in parents[address])
                            if (Feasible(edge))
                            {
                                int incoming = Walk(edge.Source, depth + 1);
                                valid = Math.Max(valid, incoming);
                                if (valid == 2) break;
                            }
                        if (address == procedure.Begin) valid = 2;
                    }
                }
                active.Remove(address);
                memo[address] = valid;
                return valid;
            }
            return Walk(guard, 0) == 1;
        }

        private static bool PreservesR9AndR15(byte[] bytes)
        {
            return (bytes.Length == 1 && bytes[0] == 0x90) ||
                (bytes.Length == 3 && StartsWith(bytes, 0x0f, 0x1f, 0x00)) ||
                (bytes.Length == 4 && StartsWith(bytes, 0x0f, 0x1f, 0x40, 0x00)) ||
                (bytes.Length == 7 && StartsWith(bytes, 0x49, 0x8d, 0x95)) ||
                (bytes.Length == 4 && StartsWith(bytes, 0x48, 0x89, 0x55)) ||
                (bytes.Length == 7 && StartsWith(bytes, 0x41, 0x81, 0xff)) ||
                (bytes.Length == 4 && StartsWith(bytes, 0x41, 0x8d, 0x47)) ||
                (bytes.Length == 7 && StartsWith(bytes, 0x41, 0x8d, 0x87)) ||
                (bytes.Length == 3 && StartsWith(bytes, 0x83, 0xf8)) ||
                (bytes.Length == 5 && bytes[0] == 0x3d) ||
                (bytes.Length == 6 && StartsWith(bytes, 0x41, 0xb8)) ||
                (bytes.Length == 9 && StartsWith(bytes, 0x41, 0x0f, 0xb6, 0x84, 0x01)) ||
                (bytes.Length == 8 && StartsWith(bytes, 0x41, 0x8b, 0x8c, 0x81)) ||
                (bytes.Length == 3 && StartsWith(bytes, 0x49, 0x03, 0xc9)) ||
                (bytes.Length == 7 && StartsWith(bytes, 0x4c, 0x8d, 0x05)) ||
                (bytes.Length == 8 && StartsWith(bytes, 0x41, 0x8b, 0x94, 0x80)) ||
                (bytes.Length == 3 && StartsWith(bytes, 0x49, 0x03, 0xd0)) ||
                (bytes.Length == 2 && (bytes[0] == 0xeb || (bytes[0] >= 0x70 && bytes[0] <= 0x7f) ||
                                      StartsWith(bytes, 0xff, 0xe1) || StartsWith(bytes, 0xff, 0xe2))) ||
                (bytes.Length == 5 && bytes[0] == 0xe9) ||
                (bytes.Length == 6 && bytes[0] == 0x0f && bytes[1] >= 0x80 && bytes[1] <= 0x8f);
        }

        private static bool TryR9FlowTargets(byte[] pe, List<Section> sections, Dictionary<ulong, int> symbols,
            ulong imageBase, Function procedure, ulong codeEnd, ulong jump, Dictionary<ulong, byte[]> rows,
            Dictionary<ulong, ulong> previous, out R9FlowSwitch routing)
        {
            routing = null;
            bool r8 = rows[jump][1] == 0xe2;
            if (!previous.TryGetValue(jump, out ulong add) || rows[add].Length != 3 || add + 3 != jump ||
                !(r8 ? StartsWith(rows[add], 0x49, 0x03, 0xd0) : StartsWith(rows[add], 0x49, 0x03, 0xc9)) ||
                !previous.TryGetValue(add, out ulong load) || rows[load].Length != 8 || load + 8 != add ||
                !(r8 ? StartsWith(rows[load], 0x41, 0x8b, 0x94, 0x80) : StartsWith(rows[load], 0x41, 0x8b, 0x8c, 0x81)))
                return false;
            ulong at = load;
            if (!previous.TryGetValue(at, out ulong before)) return false;
            if (r8)
            {
                byte[] image = rows[before];
                if (image.Length != 7 || !StartsWith(image, 0x4c, 0x8d, 0x05) || before + 7 != at ||
                    (long)before + 7 + BitConverter.ToInt32(image, 3) != (long)imageBase) return false;
                at = before;
                if (!previous.TryGetValue(at, out before)) return false;
            }
            ulong byteBegin = 0;
            if (!r8 && rows[before].Length == 9 && StartsWith(rows[before], 0x41, 0x0f, 0xb6, 0x84, 0x01) && before + 9 == at)
            {
                byteBegin = checked(imageBase + BitConverter.ToUInt32(rows[before], 5));
                at = before;
                if (!previous.TryGetValue(at, out before)) return false;
            }
            byte[] ja = rows[before];
            if (ja.Length != 6 || !StartsWith(ja, 0x0f, 0x87) || before + 6 != at) return false;
            long fallback = (long)before + 6 + BitConverter.ToInt32(ja, 2);
            at = before;
            if (!previous.TryGetValue(at, out before)) return false;
            byte[] cmp = rows[before];
            uint maximum;
            if (cmp.Length == 3 && StartsWith(cmp, 0x83, 0xf8) && cmp[2] <= 127) maximum = cmp[2];
            else if (cmp.Length == 5 && cmp[0] == 0x3d) maximum = BitConverter.ToUInt32(cmp, 1);
            else return false;
            if (maximum == 0 || maximum >= 4096 || before + (ulong)cmp.Length != at) return false;
            at = before;
            if (!previous.TryGetValue(at, out before)) return false;
            byte[] lea = rows[before];
            int bias;
            if (lea.Length == 4 && StartsWith(lea, 0x41, 0x8d, 0x47)) bias = unchecked((sbyte)lea[3]);
            else if (lea.Length == 7 && StartsWith(lea, 0x41, 0x8d, 0x87)) bias = BitConverter.ToInt32(lea, 3);
            else return false;
            if (before + (ulong)lea.Length != at) return false;
            ulong guard = before;
            ulong begin = checked(imageBase + BitConverter.ToUInt32(rows[load], 4));
            if ((begin & 3) != 0 || begin < codeEnd || !symbols.TryGetValue(begin, out int owner) || owner != procedure.Module)
                return false;
            ulong end = procedure.End;
            foreach (KeyValuePair<ulong, int> symbol in symbols)
                if (symbol.Value == owner && symbol.Key > begin && symbol.Key < end) end = symbol.Key;
            if (end <= begin || end - begin < 8 || end - begin > 4096 * 4 || ((end - begin) & 3) != 0) return false;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(begin - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return false; }
            int count = checked((int)((end - begin) / 4));
            if (raw > pe.Length - 4 * count) return false;
            var entries = new ulong[count];
            for (int i = 0; i < count; ++i)
            {
                ulong target = checked(imageBase + BitConverter.ToUInt32(pe, raw + 4 * i));
                if (target < procedure.Begin || target >= codeEnd || !rows.ContainsKey(target) ||
                    (target >= guard && target < jump + 2)) return false;
                entries[i] = target;
            }
            if (fallback < (long)procedure.Begin || fallback >= (long)codeEnd ||
                !rows.ContainsKey((ulong)fallback) || (fallback >= (long)guard && fallback < (long)jump + 2)) return false;
            var targets = new ulong[maximum + 1];
            if (byteBegin == 0)
            {
                if (count != targets.Length) return false;
                Array.Copy(entries, targets, count);
            }
            else
            {
                if (!symbols.TryGetValue(byteBegin, out int byteOwner) || byteOwner != owner || byteBegin < codeEnd ||
                    byteBegin + maximum + 1 > procedure.End) return false;
                foreach (KeyValuePair<ulong, int> symbol in symbols)
                    if (symbol.Key > byteBegin && symbol.Key < byteBegin + maximum + 1) return false;
                int byteRaw;
                try { byteRaw = checked((int)Offset(checked((uint)(byteBegin - imageBase)), sections, pe.Length)); }
                catch (InvalidDataException) { return false; }
                if (byteRaw > pe.Length - targets.Length) return false;
                for (int i = 0; i < targets.Length; ++i)
                {
                    if (pe[byteRaw + i] >= entries.Length) return false;
                    targets[i] = entries[pe[byteRaw + i]];
                }
            }
            routing = new R9FlowSwitch { Guard = guard, End = jump + 2, Bias = bias, Targets = targets };
            return true;
        }


        // The observed terminal enum switch uses R13 as index and an immediately preceding unsigned
        // guard to establish its DWORD extent. This recognizes data, not a general upper-bit proof for
        // R13. Require the exact six rows, PDB terminal bound, full preceding code and every target.
        private static InlineTable ReadGuardedR13TerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> rows)
        {
            if (dispatch < imageBase + 10 || dispatch - imageBase > UInt32.MaxValue - 20) return null;
            ulong prefix = dispatch - 10, afterJump = dispatch + 20;
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, afterJump - 1) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            int raw;
            try { raw = checked((int)Offset((uint)(prefix - imageBase), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 30 || !StartsWithAt(pe, raw, 0x41, 0x83, 0xfd) ||
                pe[raw + 3] < 1 || pe[raw + 3] > 127 || !StartsWithAt(pe, raw + 4, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 10, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 13) != (long)imageBase ||
                !StartsWithAt(pe, raw + 17, 0x42, 0x8b, 0x8c, 0xaa) ||
                !StartsWithAt(pe, raw + 25, 0x48, 0x03, 0xca, 0xff, 0xe1)) return null;
            int[] starts = { 0, 4, 10, 17, 25, 28 };
            int[] lengths = { 4, 6, 7, 8, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
                if (!rows.TryGetValue(prefix + (ulong)starts[i], out byte[] row) || row.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, prefix + (ulong)starts[i], row,
                        contributions, decoded, decodedStarts)) return null;
            uint maximum = pe[raw + 3], tableRva = BitConverter.ToUInt32(pe, raw + 21);
            ulong begin = checked(imageBase + tableRva), end = begin + 4 * (maximum + 1);
            long fallback = (long)prefix + 10 + BitConverter.ToInt32(pe, raw + 6);
            if ((begin & 3) != 0 || begin <= afterJump || end > owner.End ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            int tableRaw;
            try { tableRaw = checked((int)Offset(tableRva, sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
            // This form additionally proves the current table listing itself. A corrupt, duplicated or
            // overlapping decoder row must not supply data status to the other reported vector rows.
            // The final scalar row may consume bytes past the table, as linear disassembly does in the
            // observed image; those bytes must also match PE, and IsTableData still gates a crossing vector.
            int tableFirst = decodedStarts.BinarySearch(begin);
            if (tableFirst < 0) return null;
            ulong tableNext = begin;
            for (int i = tableFirst; i < decodedStarts.Count && decodedStarts[i] < end; ++i)
            {
                ulong address = decodedStarts[i];
                DecodedInstruction instruction = decoded[address];
                if (address != tableNext || instruction.Unknown || instruction.Module != owner.Module ||
                    instruction.Length <= 0 || instruction.Length > 15 ||
                    !rows.TryGetValue(address, out byte[] reported) || reported.Length != instruction.Length ||
                    !MatchesPe(pe, sections, imageBase, address, reported)) return null;
                tableNext = address + (ulong)instruction.Length;
            }
            if (tableNext < end) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end) continue;
                ulong codeEnd = begin;
                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                    if (symbol.Value == owner.Module && symbol.Key >= procedure.Begin && symbol.Key < codeEnd)
                        codeEnd = symbol.Key;
                if (codeEnd <= afterJump || fallback < (long)afterJump || fallback >= (long)codeEnd ||
                    !IsDecodedBoundary((ulong)fallback, owner.Module, contributions, decoded, decodedStarts)) continue;
                // An adjacent earlier anonymous DWORD table is data too only with valid body targets and
                // a decoded RET/alignment boundary. Never treat an arbitrary missing code span as data.
                if ((codeEnd & 3) != 0 || ((begin - codeEnd) & 3) != 0) continue;
                bool valid = true;
                for (ulong at = codeEnd; at < begin && valid; at += 4)
                {
                    int dataRaw = checked((int)Offset(checked((uint)(at - imageBase)), sections, pe.Length));
                    if (dataRaw > pe.Length - 4) { valid = false; break; }
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, dataRaw));
                    valid = target >= procedure.Begin && target < codeEnd &&
                        !(target >= prefix && target < afterJump) &&
                        IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                }
                if (!valid) continue;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) continue;
                ulong next = procedure.Begin;
                ulong ret = codeEnd - 1;
                if (rows.TryGetValue(codeEnd - 3, out byte[] nop) && nop.Length == 3 &&
                    StartsWith(nop, 0x0f, 0x1f, 0x00)) ret = codeEnd - 4;
                if (!rows.TryGetValue(ret, out byte[] terminal) || terminal.Length != 1 || terminal[0] != 0xc3) continue;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < codeEnd; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (address != next || instruction.Unknown || instruction.Module != owner.Module ||
                        instruction.Length <= 0 || instruction.Length > 15 || address + (ulong)instruction.Length > codeEnd ||
                        !rows.TryGetValue(address, out byte[] body) || body.Length != instruction.Length ||
                        !MatchesDecodedPe(pe, sections, imageBase, address, body, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    next = address + (ulong)instruction.Length;
                    int op = 0;
                    while (op < body.Length && (IsLegacyPrefix(body[op]) || (body[op] >= 0x40 && body[op] <= 0x4f))) ++op;
                    if (op == body.Length) continue;
                    long target;
                    if (body[op] == 0xeb || (body[op] >= 0x70 && body[op] <= 0x7f) || (body[op] >= 0xe0 && body[op] <= 0xe3))
                    {
                        if (body.Length != op + 2) { valid = false; break; }
                        target = (long)address + body.Length + unchecked((sbyte)body[op + 1]);
                    }
                    else if (body[op] == 0xe9 || body[op] == 0xe8)
                    {
                        if (body.Length != op + 5) { valid = false; break; }
                        target = (long)address + body.Length + BitConverter.ToInt32(body, op + 1);
                    }
                    else if (body[op] == 0x0f && op + 1 < body.Length && body[op + 1] >= 0x80 && body[op + 1] <= 0x8f)
                    {
                        if (body.Length != op + 6) { valid = false; break; }
                        target = (long)address + body.Length + BitConverter.ToInt32(body, op + 2);
                    }
                    else continue;
                    if ((target > (long)prefix && target < (long)afterJump) ||
                        (target >= (long)ret + 1 && target < (long)end)) { valid = false; break; }
                }
                if (!valid || next != codeEnd) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true };
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < afterJump || target >= codeEnd ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) { valid = false; break; }
                    table.Targets.Add(target);
                }
                if (valid && table.Targets.Contains((ulong)fallback)) return table;
            }
            return null;
        }

        private static InlineTable ReadGuardedR9ByteSwitchTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 10 || dispatch - imageBase > UInt32.MaxValue) return null;
            ulong prefix = dispatch - 10, afterJump = checked(dispatch + 20);
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, dispatch) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            if (afterJump > owner.End) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 30 || !StartsWithAt(pe, raw, 0x41, 0x80, 0xf9) ||
                !StartsWithAt(pe, raw + 4, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 10, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 13) != (long)imageBase ||
                !StartsWithAt(pe, raw + 17, 0x42, 0x8b, 0x84, 0x8a) ||
                !StartsWithAt(pe, raw + 25, 0x48, 0x03, 0xc2, 0xff, 0xe0)) return null;
            uint maximum = pe[raw + 3];
            if (maximum == 0 || maximum >= 128) return null;
            int[] starts = { 0, 4, 10, 17, 25, 28 };
            int[] lengths = { 4, 6, 7, 8, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            uint tableRva = BitConverter.ToUInt32(pe, raw + 21);
            ulong begin = checked(imageBase + tableRva), end = checked(begin + 4 * (maximum + 1));
            long fallback = checked((long)prefix + 10 + BitConverter.ToInt32(pe, raw + 6));
            if ((begin & 3) != 0 || begin < afterJump + 8 || end > owner.End ||
                fallback < (long)afterJump || fallback >= (long)begin - 8 ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            bool rawBacked = false;
            foreach (Section section in sections)
                if (tableRva >= section.Rva && end - imageBase <= (ulong)section.Rva + section.RawSize)
                    rawBacked = true;
            if (!rawBacked) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            int tableRaw = checked((int)Offset(tableRva, sections, pe.Length));
            if (tableRaw < 8 || tableRaw > pe.Length - checked((int)(end - begin)) ||
                !StartsWithAt(pe, tableRaw - 8, 0xc3, 0xe8) ||
                !StartsWithAt(pe, tableRaw - 2, 0xcc, 0x90)) return null;
            int[] tailStarts = { -8, -7, -2, -1 };
            int[] tailLengths = { 1, 5, 1, 1 };
            for (int i = 0; i < tailStarts.Length; ++i)
            {
                ulong address = (ulong)((long)begin + tailStarts[i]);
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != tailLengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end ||
                    !IsDecodedBoundary(procedure.Begin, owner.Module, contributions, decoded, decodedStarts) ||
                    !IsDecodedBoundary((ulong)fallback, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                bool valid = true;
                for (uint entry = 0; entry <= maximum && valid; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    valid = target >= procedure.Begin && target < begin - 8 && (target < prefix || target >= afterJump) &&
                            IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                    table.Targets.Add(target);
                }
                if (!valid) continue;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                ulong nextAddress = procedure.Begin;
                for (int i = first; i >= 0 && i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (address != nextAddress || instruction.Unknown || instruction.Module != owner.Module ||
                        instruction.Length <= 0 || instruction.Length > 15 || address + (ulong)instruction.Length > begin ||
                        !guardRows.TryGetValue(address, out byte[] bodyRow) || bodyRow.Length != instruction.Length ||
                        !MatchesDecodedPe(pe, sections, imageBase, address, bodyRow, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    nextAddress = address + (ulong)instruction.Length;
                    int source = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length));
                    if (source > pe.Length - instruction.Length) { valid = false; break; }
                    int opcodeOffset = 0;
                    while (opcodeOffset < instruction.Length && (IsLegacyPrefix(pe[source + opcodeOffset]) ||
                           (pe[source + opcodeOffset] >= 0x40 && pe[source + opcodeOffset] <= 0x4f))) ++opcodeOffset;
                    // LLVM prints LOCK as its own one-byte row before XADD. Its contiguous successor
                    // is still checked and any direct transfer there is inspected; a prefix-only row
                    // has no relative displacement to interpret.
                    if (opcodeOffset == instruction.Length) continue;
                    byte opcode = pe[source + opcodeOffset];
                    long target;
                    if (opcode == 0xeb || (opcode >= 0x70 && opcode <= 0x7f) || (opcode >= 0xe0 && opcode <= 0xe3))
                    {
                        if (instruction.Length != opcodeOffset + 2) { valid = false; break; }
                        target = (long)address + instruction.Length + unchecked((sbyte)pe[source + opcodeOffset + 1]);
                    }
                    else if (opcode == 0xe9 || opcode == 0xe8)
                    {
                        if (instruction.Length != opcodeOffset + 5) { valid = false; break; }
                        target = (long)address + instruction.Length + BitConverter.ToInt32(pe, source + opcodeOffset + 1);
                    }
                    else if (opcode == 0x0f && opcodeOffset + 1 < instruction.Length &&
                             pe[source + opcodeOffset + 1] >= 0x80 && pe[source + opcodeOffset + 1] <= 0x8f)
                    {
                        if (instruction.Length != opcodeOffset + 6) { valid = false; break; }
                        target = (long)address + instruction.Length + BitConverter.ToInt32(pe, source + opcodeOffset + 2);
                    }
                    else continue;
                    bool intoGuard = target > (long)prefix && target < (long)afterJump &&
                                     (address < prefix || address >= afterJump);
                    if (target >= (long)begin && target < (long)end) { valid = false; break; }
                    if (!intoGuard) continue;
                    ulong comparison = address >= 4 ? address - 4 : 0;
                    if (target != (long)prefix + 4 || instruction.Length != 6 || pe[source] != 0x0f || pe[source + 1] != 0x85 ||
                        comparison < procedure.Begin ||
                        !guardRows.TryGetValue(comparison, out byte[] cmp) || cmp.Length != 4 ||
                        !StartsWith(cmp, 0x41, 0x80, 0xf9) || cmp[3] != maximum ||
                        !MatchesDecodedPe(pe, sections, imageBase, comparison, cmp, contributions, decoded, decodedStarts) ||
                        !guardRows.TryGetValue(address, out byte[] branch) || branch.Length != 6 ||
                        !MatchesDecodedPe(pe, sections, imageBase, address, branch, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                }
                if (valid && nextAddress == begin) return table;
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

        // std::format's terminal switch zero-extends a byte into EAX before three exact register moves.
        // The AL guard therefore bounds the whole RAX index. Prove the complete procedure body so a
        // missing row or direct transfer cannot silently bypass that reaching definition or the guard.
        private static InlineTable ReadGuardedR10EcxTerminalDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 20 || dispatch - imageBase > UInt32.MaxValue - 20) return null;
            ulong prefix = dispatch - 20, afterJump = dispatch + 20;
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, afterJump - 1) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            int[] starts = { 0, 3, 6, 9, 12, 14, 20, 27, 35, 38 };
            int[] lengths = { 3, 3, 3, 3, 2, 6, 7, 8, 3, 2 };
            byte[][] encodings = {
                new byte[]{0x0f,0xb6,0x01}, new byte[]{0x4d,0x8b,0xf8}, new byte[]{0x4c,0x8b,0xf2},
                new byte[]{0x4c,0x8b,0xc1}, new byte[]{0x3c}, new byte[]{0x0f,0x87},
                new byte[]{0x4c,0x8d,0x15}, new byte[]{0x41,0x8b,0x8c,0x82},
                new byte[]{0x49,0x03,0xca}, new byte[]{0xff,0xe1}
            };
            if (raw > pe.Length - 40) return null;
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!StartsWithAt(pe, raw + starts[i], encodings[i]) ||
                    !guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            if ((long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 23) != (long)imageBase) return null;
            uint maximum = pe[raw + 13];
            if (maximum == 0 || maximum >= 128) return null;
            uint tableRva = BitConverter.ToUInt32(pe, raw + 31);
            ulong begin = checked(imageBase + tableRva), end = checked(begin + 4 * (maximum + 1));
            long fallback = (long)prefix + 20 + BitConverter.ToInt32(pe, raw + 16);
            if ((begin & 3) != 0 || begin < afterJump + 8 || end > owner.End ||
                fallback < (long)afterJump || fallback >= (long)begin - 8 ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            int tableRaw;
            try { tableRaw = checked((int)Offset(tableRva, sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (tableRaw < 8 || tableRaw > pe.Length - checked((int)(end - begin)) ||
                pe[tableRaw - 8] != 0xe8 || !StartsWithAt(pe, tableRaw - 3, 0xcc, 0x66, 0x90)) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End != end ||
                    !IsDecodedBoundary((ulong)fallback, owner.Module, contributions, decoded, decodedStarts)) continue;
                // All body rows, including CALL/INT3/NOP at the table boundary, must agree with PE bytes.
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) return null;
                ulong next = procedure.Begin;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (address != next || instruction.Unknown || instruction.Module != owner.Module ||
                        instruction.Length <= 0 || instruction.Length > 15 || address + (ulong)instruction.Length > begin ||
                        !guardRows.TryGetValue(address, out byte[] body) || body.Length != instruction.Length ||
                        !MatchesDecodedPe(pe, sections, imageBase, address, body, contributions, decoded, decodedStarts)) return null;
                    next = address + (ulong)instruction.Length;
                    int op = 0;
                    while (op < body.Length && (IsLegacyPrefix(body[op]) || (body[op] >= 0x40 && body[op] <= 0x4f))) ++op;
                    if (op == body.Length) continue;
                    long target;
                    if (body[op] == 0xeb || (body[op] >= 0x70 && body[op] <= 0x7f) || (body[op] >= 0xe0 && body[op] <= 0xe3))
                    {
                        if (body.Length != op + 2) return null;
                        target = (long)address + body.Length + unchecked((sbyte)body[op + 1]);
                    }
                    else if (body[op] == 0xe9 || body[op] == 0xe8)
                    {
                        if (body.Length != op + 5) return null;
                        target = (long)address + body.Length + BitConverter.ToInt32(body, op + 1);
                    }
                    else if (body[op] == 0x0f && op + 1 < body.Length && body[op + 1] >= 0x80 && body[op + 1] <= 0x8f)
                    {
                        if (body.Length != op + 6) return null;
                        target = (long)address + body.Length + BitConverter.ToInt32(body, op + 2);
                    }
                    else continue;
                    if ((target > (long)prefix && target < (long)afterJump) ||
                        (target >= (long)begin - 3 && target < (long)end)) return null;
                }
                if (next != begin || !guardRows.TryGetValue(begin - 8, out byte[] call) || call.Length != 5 ||
                    !guardRows.TryGetValue(begin - 3, out byte[] trap) || trap.Length != 1 ||
                    !guardRows.TryGetValue(begin - 2, out byte[] nop) || nop.Length != 2) return null;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true };
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < afterJump || target >= begin - 8 ||
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

        // The ASan zero-extending stack remaps use exactly a 21-byte imm8 or 24-byte imm32 guard,
        // followed by the same 27-byte RCX dispatch. The imm8 is sign extended (bound 1..127);
        // the imm32 bound is 1..4095. Both byte counts end exactly at their owning procedure, inside
        // a larger contribution. A later data symbol cannot rescue a malformed guard. Corroborate
        // all eight rows against PE bytes and nonoverlapping boundaries.
        private static ulong CompactRcxByteIndexEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, ulong imageBase, ulong dispatch,
            ulong begin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, out bool candidate)
        {
            candidate = false;
            if (dispatch < imageBase + 21 || dispatch - imageBase > UInt32.MaxValue) return 0;
            bool wideImmediate = false;
            if (dispatch >= imageBase + 24)
            {
                ulong widePrefix = dispatch - 24;
                int wideRaw;
                try { wideRaw = checked((int)Offset(checked((uint)(widePrefix - imageBase)), sections, pe.Length)); }
                catch (InvalidDataException) { return 0; }
                wideImmediate = StartsWithAt(pe, wideRaw, 0x81, 0xbc, 0x24) ||
                    (guardRows.TryGetValue(widePrefix, out byte[] wideRow) && wideRow.Length == 11 &&
                     StartsWith(wideRow, 0x81, 0xbc, 0x24));
            }
            int compareLength = wideImmediate ? 11 : 8;
            int indexStart = compareLength + 6;
            int guardLength = indexStart + 7;
            ulong prefix = dispatch - (ulong)guardLength;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - (guardLength + 27)) return 0;
            candidate = wideImmediate || StartsWithAt(pe, raw + indexStart, 0x8b, 0x84, 0x24);
            if (guardRows.TryGetValue(prefix + (ulong)indexStart, out byte[] indexRow) && indexRow.Length == 7 &&
                StartsWith(indexRow, 0x8b, 0x84, 0x24)) candidate = true;
            if (!candidate || begin != dwordTable.End ||
                !StartsWithAt(pe, raw, wideImmediate ? (byte)0x81 : (byte)0x83, 0xbc, 0x24) ||
                !StartsWithAt(pe, raw + compareLength, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + indexStart, 0x8b, 0x84, 0x24) ||
                BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + indexStart + 3) ||
                !StartsWithAt(pe, raw + guardLength, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + guardLength + 3) != (long)imageBase ||
                !StartsWithAt(pe, raw + guardLength + 7, 0x0f, 0xb6, 0x84, 0x01) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + guardLength + 11)) != begin ||
                !StartsWithAt(pe, raw + guardLength + 15, 0x8b, 0x84, 0x81) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + guardLength + 18)) != dwordTable.Begin ||
                !StartsWithAt(pe, raw + guardLength + 22, 0x48, 0x03, 0xc1, 0xff, 0xe0)) return 0;
            uint maximum = wideImmediate ? BitConverter.ToUInt32(pe, raw + 7) : pe[raw + 7];
            if (maximum == 0 || maximum >= (wideImmediate ? 4096U : 128U)) return 0;
            int[] starts = { 0, compareLength, indexStart, guardLength, guardLength + 7,
                guardLength + 15, guardLength + 22, guardLength + 25 };
            int[] lengths = { compareLength, 6, 7, 7, 8, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return 0;
            }
            long defaultAddress = (long)prefix + compareLength + 6 + BitConverter.ToInt32(pe, raw + compareLength + 2);
            if (defaultAddress < 0 || !dwordTable.Targets.Contains((ulong)defaultAddress)) return 0;
            ulong end = checked(begin + maximum + 1);
            if (wideImmediate)
                foreach (Function entry in procedures)
                    if (entry.Module == dwordTable.Module && entry.ContributionBegin == dwordTable.ContributionBegin &&
                        entry.ContributionEnd == dwordTable.ContributionEnd &&
                        entry.Begin >= dwordTable.Begin && entry.Begin < end) return 0;
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

        // ASan accessibility uses the compact disp32 guard with MOVSXD instead of MOV EAX. The
        // unsigned guard limits every surviving index to 0..maximum (maximum 1..127), so sign extension cannot produce a
        // negative table offset. Corroborate the complete eight-row dispatch and exact remap count. An
        // interior remap additionally needs the next same-owner anonymous symbol at its DWORD-aligned
        // end and exact compiler NOP alignment bytes. That padding receives no table exemption.
        private static ulong CompactSignedRcxByteIndexEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, Dictionary<ulong, int> dataSymbols,
            ulong imageBase, ulong dispatch,
            ulong begin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, out bool candidate)
        {
            candidate = false;
            if (dispatch < imageBase + 22 || dispatch - imageBase > UInt32.MaxValue) return 0;
            ulong prefix = dispatch - 22;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - 49) return 0;
            candidate = StartsWithAt(pe, raw + 14, 0x48, 0x63, 0x84, 0x24);
            if (guardRows.TryGetValue(prefix + 14, out byte[] indexRow) && indexRow.Length == 8 &&
                StartsWith(indexRow, 0x48, 0x63, 0x84, 0x24)) candidate = true;
            // The established imm32/MOVSXD form has the same final index row, but its comparison
            // starts three bytes earlier. Let its existing proof handle that distinct encoding.
            if (dispatch >= imageBase + 25 && raw >= 3 &&
                guardRows.TryGetValue(prefix - 3, out byte[] longComparison) && longComparison.Length == 11 &&
                StartsWith(longComparison, 0x81, 0xbc, 0x24) &&
                MatchesDecodedPe(pe, sections, imageBase, prefix - 3, longComparison,
                    contributions, decoded, decodedStarts))
            {
                candidate = false;
                return 0;
            }
            if (!candidate || begin != dwordTable.End ||
                !StartsWithAt(pe, raw, 0x83, 0xbc, 0x24) ||
                !StartsWithAt(pe, raw + 8, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 14, 0x48, 0x63, 0x84, 0x24) ||
                BitConverter.ToUInt32(pe, raw + 3) != BitConverter.ToUInt32(pe, raw + 18) ||
                !StartsWithAt(pe, raw + 22, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 25) != (long)imageBase ||
                !StartsWithAt(pe, raw + 29, 0x0f, 0xb6, 0x84, 0x01) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + 33)) != begin ||
                !StartsWithAt(pe, raw + 37, 0x8b, 0x84, 0x81) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + 40)) != dwordTable.Begin ||
                !StartsWithAt(pe, raw + 44, 0x48, 0x03, 0xc1, 0xff, 0xe0)) return 0;
            uint maximum = pe[raw + 7];
            if (maximum == 0 || maximum >= 128) return 0;
            int[] starts = { 0, 8, 14, 22, 29, 37, 44, 47 };
            int[] lengths = { 8, 6, 8, 7, 8, 7, 3, 2 };
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
                    dispatch >= dwordTable.Begin || dwordTable.Begin < procedure.Begin || procedure.End < end) continue;
                if (procedure.End != end)
                {
                    ulong nextSymbol = 0;
                    foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                        if (symbol.Value == dwordTable.Module && symbol.Key > begin && symbol.Key < procedure.End &&
                            (nextSymbol == 0 || symbol.Key < nextSymbol)) nextSymbol = symbol.Key;
                    ulong alignedEnd = checked((end + 3) & ~3UL);
                    if (nextSymbol == 0 || nextSymbol != alignedEnd) continue;
                    int paddingLength = checked((int)(alignedEnd - end));
                    byte[] padding = paddingLength == 1 ? new byte[] { 0x90 } :
                        paddingLength == 2 ? new byte[] { 0x66, 0x90 } :
                        paddingLength == 3 ? new byte[] { 0x0f, 0x1f, 0x00 } : new byte[0];
                    if (paddingLength != 0 && !MatchesPe(pe, sections, imageBase, end, padding)) continue;
                }
                bool allTargetsInside = true;
                foreach (ulong target in dwordTable.Targets)
                    if (target < procedure.Begin || target >= dwordTable.Begin) { allTargetsInside = false; break; }
                if (allTargetsInside) return end;
            }
            return 0;
        }

        // ASan menu/color-picker tracing uses a disp8 stack slot with an imm32 or sign-extended imm8
        // unsigned guard and a zero-extending
        // EAX load. Its remap ends at the exact procedure inside a larger contribution. Corroborate all
        // eight rows; a later anonymous data symbol cannot rescue a broken or missing guard.
        private static ulong Disp8RcxByteIndexEnd(byte[] pe, List<Section> sections,
            List<Contribution> contributions, List<Function> procedures, ulong imageBase, ulong dispatch,
            ulong begin, InlineTable dwordTable, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, out bool candidate)
        {
            candidate = false;
            if (dispatch < imageBase + 18 || dispatch - imageBase > UInt32.MaxValue) return 0;
            ulong prefix = dispatch - 18;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return 0; }
            if (raw > pe.Length - 45) return 0;
            bool shortImmediate = StartsWithAt(pe, raw + 3, 0x83, 0x7c, 0x24) ||
                (guardRows.TryGetValue(prefix + 3, out byte[] shortRow) && shortRow.Length == 5 &&
                 StartsWith(shortRow, 0x83, 0x7c, 0x24));
            if (shortImmediate) { prefix += 3; raw += 3; }
            int comparisonLength = shortImmediate ? 5 : 8;
            int indexOffset = comparisonLength + 6;
            int dispatchOffset = indexOffset + 4;
            byte comparisonOpcode = shortImmediate ? (byte)0x83 : (byte)0x81;
            candidate = StartsWithAt(pe, raw + indexOffset, 0x8b, 0x44, 0x24) ||
                StartsWithAt(pe, raw, comparisonOpcode, 0x7c, 0x24);
            if (guardRows.TryGetValue(prefix + (ulong)indexOffset, out byte[] indexRow) && indexRow.Length == 4 &&
                StartsWith(indexRow, 0x8b, 0x44, 0x24)) candidate = true;
            if (guardRows.TryGetValue(prefix, out byte[] comparisonRow) && comparisonRow.Length == comparisonLength &&
                StartsWith(comparisonRow, comparisonOpcode, 0x7c, 0x24)) candidate = true;
            if (!candidate || begin != dwordTable.End ||
                !StartsWithAt(pe, raw, comparisonOpcode, 0x7c, 0x24) ||
                !StartsWithAt(pe, raw + comparisonLength, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + indexOffset, 0x8b, 0x44, 0x24) || pe[raw + 3] != pe[raw + indexOffset + 3] ||
                !StartsWithAt(pe, raw + dispatchOffset, 0x48, 0x8d, 0x0d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + dispatchOffset + 3) != (long)imageBase ||
                !StartsWithAt(pe, raw + dispatchOffset + 7, 0x0f, 0xb6, 0x84, 0x01) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + dispatchOffset + 11)) != begin ||
                !StartsWithAt(pe, raw + dispatchOffset + 15, 0x8b, 0x84, 0x81) ||
                checked(imageBase + BitConverter.ToUInt32(pe, raw + dispatchOffset + 18)) != dwordTable.Begin ||
                !StartsWithAt(pe, raw + dispatchOffset + 22, 0x48, 0x03, 0xc1, 0xff, 0xe0)) return 0;
            // Opcode 83 sign extends its immediate. Negative encoded values cannot establish a bounded
            // unsigned index range, even when the same byte interpreted unsigned would be small.
            if (shortImmediate && pe[raw + 4] >= 128) return 0;
            uint maximum = shortImmediate ? pe[raw + 4] : BitConverter.ToUInt32(pe, raw + 4);
            if (maximum == 0 || maximum >= 4096) return 0;
            int[] starts = { 0, comparisonLength, indexOffset, dispatchOffset, dispatchOffset + 7,
                dispatchOffset + 15, dispatchOffset + 22, dispatchOffset + 25 };
            int[] lengths = { comparisonLength, 6, 4, 7, 8, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return 0;
            }
            long defaultAddress = (long)prefix + indexOffset + BitConverter.ToInt32(pe, raw + comparisonLength + 2);
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
                if (compactEnd != 0)
                {
                    guardedEnd = compactEnd;
                    dwordTable.VerifyReportedBytes = true;
                }
                ulong compactSignedEnd = CompactSignedRcxByteIndexEnd(pe, sections, contributions, procedures, dataSymbols,
                    imageBase, dispatch, begin, dwordTable, decoded, decodedStarts, guardRows,
                    out bool compactSignedCandidate);
                if (compactSignedCandidate && compactSignedEnd == 0) return null;
                if (compactSignedEnd != 0)
                {
                    guardedEnd = compactSignedEnd;
                    dwordTable.VerifyReportedBytes = true;
                }
                ulong disp8End = Disp8RcxByteIndexEnd(pe, sections, contributions, procedures, imageBase,
                    dispatch, begin, dwordTable, decoded, decodedStarts, guardRows, out bool disp8Candidate);
                if (disp8Candidate && disp8End == 0) return null;
                if (disp8End != 0)
                {
                    guardedEnd = disp8End;
                    dwordTable.VerifyReportedBytes = true;
                }
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
                ContributionBegin = dwordTable.ContributionBegin, ContributionEnd = dwordTable.ContributionEnd,
                VerifyReportedBytes = dwordTable.VerifyReportedBytes };
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
            Dictionary<ulong, byte[]> guardRows, out InlineTable byteTable, bool rdxForm = false)
        {
            byteTable = null;
            if (dispatch < imageBase + 8 || dispatch - imageBase > UInt32.MaxValue || afterJump != dispatch + (rdxForm ? 27UL : 29UL) ||
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
            if (rdxForm)
            {
                // The observed Release variant destructor uses the same one-byte bias through RDX.
                // This exact contiguous MOVSX/LEA/MOVZX/MOV/ADD/JMP form has its own PE/PDB proof.
                if (raw > pe.Length - 35 || !StartsWithAt(pe, raw, 0x48, 0x0f, 0xbe, 0x85) ||
                    !StartsWithAt(pe, raw + 8, 0x48, 0x8d, 0x15) ||
                    (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 11) != (long)imageBase ||
                    !StartsWithAt(pe, raw + 15, 0x0f, 0xb6, 0x84, 0x02) ||
                    BitConverter.ToUInt32(pe, raw + 19) != byteLoadRva ||
                    !StartsWithAt(pe, raw + 23, 0x8b, 0x8c, 0x82) ||
                    BitConverter.ToUInt32(pe, raw + 26) != dwordTableRva ||
                    !StartsWithAt(pe, raw + 30, 0x48, 0x03, 0xca) ||
                    !StartsWithAt(pe, raw + 33, 0xff, 0xe1)) return null;
            }
            else if (raw > pe.Length - 37 ||
                !StartsWithAt(pe, raw, 0x48, 0x0f, 0xbe, 0x81) ||
                !StartsWithAt(pe, raw + 8, 0x4c, 0x8d, 0x05) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 11) != (long)imageBase ||
                !StartsWithAt(pe, raw + 15, 0x41, 0x0f, 0xb6, 0x84, 0x00) ||
                BitConverter.ToUInt32(pe, raw + 20) != byteLoadRva ||
                !StartsWithAt(pe, raw + 24, 0x41, 0x8b, 0x94, 0x80) ||
                BitConverter.ToUInt32(pe, raw + 28) != dwordTableRva ||
                !StartsWithAt(pe, raw + 32, 0x49, 0x03, 0xd0) ||
                !StartsWithAt(pe, raw + 35, 0xff, 0xe2)) return null;
            int[] starts = rdxForm ? new int[] { 0, 8, 15, 23, 30, 33 } : new int[] { 0, 8, 15, 24, 32, 35 };
            int[] lengths = rdxForm ? new int[] { 8, 7, 8, 7, 3, 2 } : new int[] { 8, 7, 9, 8, 3, 2 };
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
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = rdxForm };
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
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = rdxForm };
                return table;
            }
            return null;
        }

        // Release variant retirement hoists the nonvolatile R15 image base and uses a signed
        // RBP+disp32 byte index, biased by one byte for the valueless case. Prove all adjacent
        // DWORD/byte pairs from anonymous same-owner symbols and the exact procedure end.
        // A complete PE-matched body/CFG must carry the image-base definition into every
        // dispatch; neither a procedure name nor a preceding LEA grants an exemption.
        private static List<InlineTable> ReadHoistedR15SignedTables(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> symbols, List<Function> procedures,
            ulong imageBase, ulong candidate, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> rows)
        {
            int ownerIndex = FindContribution(contributions, candidate);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || candidate < procedure.Begin || candidate >= procedure.End)
                    continue;
                bool backed = false;
                foreach (Section section in sections)
                {
                    ulong sectionBegin = imageBase + section.Rva;
                    ulong backedEnd = sectionBegin + Math.Min(section.Size, section.RawSize);
                    if (section.Executable && procedure.Begin >= sectionBegin && procedure.End <= backedEnd &&
                        (ulong)section.Raw + procedure.End - sectionBegin <= (ulong)pe.Length) backed = true;
                }
                if (!backed) continue;
                var starts = new List<ulong>();
                foreach (KeyValuePair<ulong, int> symbol in symbols)
                    if (symbol.Value == owner.Module && symbol.Key >= procedure.Begin && symbol.Key < procedure.End)
                        starts.Add(symbol.Key);
                starts.Sort();
                if (starts.Count < 2 || starts.Count > 16 || (starts.Count & 1) != 0) continue;
                ulong codeEnd = starts[0];
                ulong byteExtent = procedure.End - starts[starts.Count - 1];
                if (byteExtent == 0 || byteExtent > 128) continue;
                var tables = new List<InlineTable>();
                var targets = new Dictionary<ulong, List<ulong>>();
                bool valid = true;
                for (int pair = 0; pair < starts.Count && valid; pair += 2)
                {
                    ulong begin = starts[pair], bytesBegin = starts[pair + 1];
                    ulong end = pair + 2 < starts.Count ? starts[pair + 2] : procedure.End;
                    if ((begin & 3) != 0 || bytesBegin <= begin || ((bytesBegin - begin) & 3) != 0 ||
                        bytesBegin - begin < 8 || bytesBegin - begin > 512 || end <= bytesBegin ||
                        end - bytesBegin < byteExtent || end - bytesBegin > byteExtent + 3 ||
                        (pair + 2 < starts.Count && end != ((bytesBegin + byteExtent + 3) & ~3UL)))
                    { valid = false; break; }
                    int raw, byteRaw;
                    try
                    {
                        raw = checked((int)Offset(checked((uint)(begin - imageBase)), sections, pe.Length));
                        byteRaw = checked((int)Offset(checked((uint)(bytesBegin - imageBase)), sections, pe.Length));
                    }
                    catch (InvalidDataException) { valid = false; break; }
                    int count = checked((int)((bytesBegin - begin) / 4));
                    int length = checked((int)(end - bytesBegin));
                    if (raw > pe.Length - 4 * count || byteRaw > pe.Length - length) { valid = false; break; }
                    var entries = new List<ulong>();
                    for (int i = 0; i < count && valid; ++i)
                    {
                        ulong target = checked(imageBase + BitConverter.ToUInt32(pe, raw + 4 * i));
                        valid = target >= procedure.Begin && target < codeEnd &&
                            IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                        entries.Add(target);
                    }
                    for (int i = 0; i < length && valid; ++i)
                        valid = (ulong)i < byteExtent ? pe[byteRaw + i] < count : pe[byteRaw + i] == 0x90;
                    targets[begin] = entries;
                    tables.Add(new InlineTable { Begin = begin, End = bytesBegin, Module = owner.Module,
                        ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true });
                    tables.Add(new InlineTable { Begin = bytesBegin, End = end, Module = owner.Module,
                        ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true });
                }
                if (!valid) continue;
                // Data must follow the observed terminal INT3 + three-byte alignment NOP.
                if (!rows.TryGetValue(codeEnd - 4, out byte[] trap) || trap.Length != 1 || trap[0] != 0xcc ||
                    !rows.TryGetValue(codeEnd - 3, out byte[] padding) || padding.Length != 3 ||
                    !StartsWith(padding, 0x0f, 0x1f, 0x00)) continue;
                var body = new List<ulong>();
                ulong at = procedure.Begin;
                while (at < codeEnd && body.Count < 16384)
                {
                    if (!rows.TryGetValue(at, out byte[] row) ||
                        !MatchesDecodedPe(pe, sections, imageBase, at, row, contributions, decoded, decodedStarts))
                    { valid = false; break; }
                    body.Add(at);
                    at += (ulong)row.Length;
                }
                if (!valid || at != codeEnd) continue;
                var dispatches = new Dictionary<ulong, List<ulong>>();
                var protectedRows = new Dictionary<ulong, ulong>();
                var used = new HashSet<ulong>();
                foreach (ulong address in body)
                {
                    byte[] row = rows[address];
                    if (row.Length != 8 || !StartsWith(row, 0x48, 0x0f, 0xbe, 0x85)) continue;
                    ulong loadByte = address + 8, loadDword = address + 17, add = address + 25, jump = address + 28;
                    if (!rows.TryGetValue(loadByte, out byte[] byteLoad) || byteLoad.Length != 9 ||
                        !StartsWith(byteLoad, 0x41, 0x0f, 0xb6, 0x84, 0x07) ||
                        !rows.TryGetValue(loadDword, out byte[] dwordLoad) || dwordLoad.Length != 8 ||
                        !StartsWith(dwordLoad, 0x41, 0x8b, 0x8c, 0x87) ||
                        !rows.TryGetValue(add, out byte[] addition) || addition.Length != 3 ||
                        !StartsWith(addition, 0x49, 0x03, 0xcf) ||
                        !rows.TryGetValue(jump, out byte[] indirect) || indirect.Length != 2 ||
                        !StartsWith(indirect, 0xff, 0xe1)) continue;
                    ulong table = checked(imageBase + BitConverter.ToUInt32(dwordLoad, 4));
                    int pair = starts.IndexOf(table);
                    if (pair < 0 || (pair & 1) != 0 ||
                        checked(imageBase + BitConverter.ToUInt32(byteLoad, 5)) != starts[pair + 1] + 1) continue;
                    dispatches[jump] = targets[table];
                    used.Add(table);
                    foreach (ulong protectedRow in new ulong[] { loadByte, loadDword, add, jump })
                        protectedRows[protectedRow] = address;
                }
                if (used.Count != starts.Count / 2 || !protectedRows.ContainsKey(candidate)) continue;
                var edges = new Dictionary<ulong, List<ulong>>();
                foreach (ulong address in body) edges[address] = new List<ulong>();
                bool Edge(ulong source, long destination)
                {
                    if (destination < 0 || !edges.ContainsKey((ulong)destination)) return false;
                    if (protectedRows.TryGetValue((ulong)destination, out ulong start) &&
                        (source < start || source >= start + 30)) return false;
                    edges[source].Add((ulong)destination);
                    return true;
                }
                foreach (ulong address in body)
                {
                    byte[] row = rows[address];
                    ulong following = address + (ulong)row.Length;
                    bool branch = false, conditional = false, call = false;
                    long target = 0;
                    if (row.Length == 2 && (row[0] == 0xeb || (row[0] >= 0x70 && row[0] <= 0x7f)))
                    { branch = true; conditional = row[0] != 0xeb; target = (long)following + unchecked((sbyte)row[1]); }
                    else if (row.Length == 5 && (row[0] == 0xe9 || row[0] == 0xe8))
                    { branch = true; call = row[0] == 0xe8; target = (long)following + BitConverter.ToInt32(row, 1); }
                    else if (row.Length == 6 && row[0] == 0x0f && row[1] >= 0x80 && row[1] <= 0x8f)
                    { branch = true; conditional = true; target = (long)following + BitConverter.ToInt32(row, 2); }
                    if (branch)
                    {
                        if (target >= (long)codeEnd && target < (long)procedure.End) { valid = false; break; }
                        if (call && target >= (long)procedure.Begin && target < (long)codeEnd)
                        { valid = false; break; } // Local subroutines need a separate return/ABI proof.
                        if (!call && !Edge(address, target)) { valid = false; break; }
                        if ((call || conditional) && !Edge(address, (long)following)) { valid = false; break; }
                    }
                    else if (dispatches.TryGetValue(address, out List<ulong> destinations))
                    {
                        foreach (ulong destination in destinations) if (!Edge(address, (long)destination)) valid = false;
                    }
                    else if ((row.Length == 1 && row[0] == 0xc3) || (row.Length == 3 && row[0] == 0xc2) ||
                        (row.Length == 1 && row[0] == 0xcc) || address == codeEnd - 3) { }
                    else if (UnmodelledR15Transfer(row)) valid = false;
                    else if (!Edge(address, (long)following)) valid = false;
                    if (!valid) break;
                }
                if (!valid) continue;
                var states = new Dictionary<ulong, int>();
                var pending = new Queue<ulong>();
                states[procedure.Begin] = 1; // Unknown base at entry; bit 2 means the exact image base.
                pending.Enqueue(procedure.Begin);
                while (pending.Count != 0)
                {
                    ulong address = pending.Dequeue();
                    int effect = R15Effect(rows[address], address, imageBase);
                    int state = effect == 1 ? 2 : effect == 2 ? 1 : states[address];
                    foreach (ulong destination in edges[address])
                    {
                        states.TryGetValue(destination, out int before);
                        if ((before | state) == before) continue;
                        states[destination] = before | state;
                        pending.Enqueue(destination);
                    }
                }
                foreach (ulong jump in dispatches.Keys)
                    if (!states.TryGetValue(jump - 28, out int state) || state != 2) valid = false;
                if (valid) return tables;
            }
            return null;
        }

        private static bool UnmodelledR15Transfer(byte[] bytes)
        {
            int at = 0;
            while (at < bytes.Length && IsLegacyPrefix(bytes[at])) ++at;
            if (at < bytes.Length && bytes[at] >= 0x40 && bytes[at] <= 0x4f) ++at;
            if (at >= bytes.Length) return false;
            byte opcode = bytes[at++];
            if ((opcode >= 0xe0 && opcode <= 0xe3) || (opcode >= 0xe8 && opcode <= 0xeb) ||
                (opcode >= 0x70 && opcode <= 0x7f) || opcode == 0xc2 || opcode == 0xc3 ||
                opcode == 0xca || opcode == 0xcb || opcode == 0x9a) return true;
            if (at >= bytes.Length) return false;
            if (opcode == 0x0f && bytes[at] >= 0x80 && bytes[at] <= 0x8f) return true;
            return opcode == 0xff && ((bytes[at] & 0x38) == 0x20 || (bytes[at] & 0x38) == 0x28);
        }

        // Conservative register-effect proof, using encoded operands, not disassembly prose.
        // Unknown effects lose the definition. Calls rely on the Windows x64 nonvolatile ABI;
        // local subroutine calls are refused by the CFG above.
        private static int R15Effect(byte[] bytes, ulong address, ulong imageBase)
        {
            // LLVM reports LOCK as its own PE-matched row; the following opcode still
            // receives the register-effect check. The prefix itself writes no register.
            if (bytes.Length == 1 && bytes[0] == 0xf0) return 0;
            if (bytes.Length == 7 && StartsWith(bytes, 0x4c, 0x8d, 0x3d))
                return (long)address + 7 + BitConverter.ToInt32(bytes, 3) == (long)imageBase ? 1 : 2;
            int at = 0;
            while (at < bytes.Length && IsLegacyPrefix(bytes[at])) ++at;
            if (at >= bytes.Length) return 2;
            if (bytes[at] == 0xc4 || bytes[at] == 0xc5 || bytes[at] == 0x62) return 2;
            if (bytes[at] < 0x40 || bytes[at] > 0x4f) return 0;
            byte rex = bytes[at++];
            if (at >= bytes.Length) return 2;
            byte opcode = bytes[at++];
            if ((opcode >= 0x40 && opcode <= 0x4f) || opcode == 0xc4 || opcode == 0xc5 || opcode == 0x62)
                return 2; // Repeated REX or vector prefixes need their own encoded-effect proof.
            if (opcode >= 0x50 && opcode <= 0x57) return 0;
            if ((opcode >= 0x58 && opcode <= 0x5f) || (opcode >= 0xb0 && opcode <= 0xbf) ||
                (opcode >= 0x90 && opcode <= 0x97))
                return (rex & 1) != 0 && (opcode & 7) == 7 ? 2 : 0;
            if (opcode == 0x0f)
            {
                if (at >= bytes.Length) return 2;
                opcode = bytes[at++];
                if (at >= bytes.Length) return 2;
                byte operand = bytes[at];
                // MOVSX/MOVZX read r/m; vector, multi-map and unfamiliar forms are conservative.
                if (opcode != 0xbe && opcode != 0xbf && opcode != 0xb6 && opcode != 0xb7)
                    return (rex & 5) != 0 ? 2 : 0;
                return (rex & 4) != 0 && ((operand >> 3) & 7) == 7 ? 2 : 0;
            }
            if (at >= bytes.Length) return 2;
            byte modrm = bytes[at];
            bool reg15 = (rex & 4) != 0 && ((modrm >> 3) & 7) == 7;
            bool rm15 = (rex & 1) != 0 && (modrm & 0xc7) == 0xc7;
            if (reg15 && opcode != 0x89 && opcode != 0x88 && opcode != 0x39 && opcode != 0x3b &&
                opcode != 0x85 && opcode != 0x84) return 2;
            if (rm15 && opcode != 0x8b && opcode != 0x8a && opcode != 0x03 && opcode != 0x0b &&
                opcode != 0x13 && opcode != 0x1b && opcode != 0x23 && opcode != 0x2b && opcode != 0x33 &&
                opcode != 0x3b && opcode != 0x39 && opcode != 0x85 && opcode != 0x84) return 2;
            return 0;
        }

        // OnCommand's unsigned RBX command switch uses RDI as image base. Prove the exact 43-byte
        // guard/dispatch and adjacent PDB-owned tables; an unknown shape stays executable for the ISA gate.
        private static InlineTable ReadGuardedRdiBaseByteRemap(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows, out InlineTable byteTable)
        {
            byteTable = null;
            if (dispatch < imageBase + 16 || dispatch - imageBase > UInt32.MaxValue) return null;
            ulong prefix = dispatch - 16, afterJump = checked(dispatch + 27);
            int ownerIndex = FindContribution(contributions, prefix);
            if (ownerIndex < 0 || FindContribution(contributions, dispatch) != ownerIndex) return null;
            Contribution owner = contributions[ownerIndex];
            if (afterJump > owner.End) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 43 ||
                !StartsWithAt(pe, raw, 0x8d, 0x43, 0x9c) || !StartsWithAt(pe, raw + 3, 0x3d) ||
                !StartsWithAt(pe, raw + 8, 0x0f, 0x87) || !StartsWithAt(pe, raw + 14, 0x48, 0x98) ||
                !StartsWithAt(pe, raw + 16, 0x48, 0x8d, 0x3d) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 19) != (long)imageBase ||
                !StartsWithAt(pe, raw + 23, 0x0f, 0xb6, 0x84, 0x07) ||
                !StartsWithAt(pe, raw + 31, 0x8b, 0x8c, 0x87) ||
                !StartsWithAt(pe, raw + 38, 0x48, 0x03, 0xcf) || !StartsWithAt(pe, raw + 41, 0xff, 0xe1)) return null;
            uint maximum = BitConverter.ToUInt32(pe, raw + 4);
            if (maximum < 1 || maximum >= 4096) return null;
            int[] starts = { 0, 3, 8, 14, 16, 23, 31, 38, 41 };
            int[] lengths = { 3, 5, 6, 2, 7, 8, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            ulong dwordBegin = checked(imageBase + BitConverter.ToUInt32(pe, raw + 34));
            ulong byteBegin = checked(imageBase + BitConverter.ToUInt32(pe, raw + 27));
            ulong byteEnd = checked(byteBegin + maximum + 1);
            long branch = checked((long)prefix + 14 + BitConverter.ToInt32(pe, raw + 10));
            if (branch < (long)afterJump || (ulong)branch >= dwordBegin ||
                (dwordBegin & 3) != 0 || dwordBegin <= afterJump || byteBegin <= dwordBegin ||
                ((byteBegin - dwordBegin) & 3) != 0 || byteBegin - dwordBegin < 8 ||
                byteBegin - dwordBegin > 4096 * 4 || byteEnd > owner.End ||
                !dataSymbols.TryGetValue(dwordBegin, out int dwordOwner) || dwordOwner != owner.Module ||
                !dataSymbols.TryGetValue(byteBegin, out int byteOwner) || byteOwner != owner.Module) return null;
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Key > dwordBegin && symbol.Key < byteEnd && symbol.Key != byteBegin) return null;
            int entryCount = checked((int)((byteBegin - dwordBegin) / 4));
            int dwordRaw, byteRaw;
            try
            {
                dwordRaw = checked((int)Offset(checked((uint)(dwordBegin - imageBase)), sections, pe.Length));
                byteRaw = checked((int)Offset(checked((uint)(byteBegin - imageBase)), sections, pe.Length));
            }
            catch (InvalidDataException) { return null; }
            if (dwordRaw > pe.Length - 4 * entryCount || byteRaw > pe.Length - checked((int)maximum + 1)) return null;
            for (int i = 0; i <= maximum; ++i)
                if (pe[byteRaw + i] >= entryCount) return null;
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || procedure.Begin > prefix || procedure.End < byteEnd) continue;
                ulong boundary = procedure.End;
                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                    if (symbol.Value == owner.Module && symbol.Key > byteBegin && symbol.Key < boundary) boundary = symbol.Key;
                if (boundary < byteEnd || boundary - byteEnd > 3 || (boundary & 3) != 0) continue;
                int padding = checked((int)(boundary - byteEnd));
                int paddingRaw = checked(byteRaw + (int)maximum + 1);
                if (paddingRaw > pe.Length - padding ||
                    (padding == 1 && pe[paddingRaw] != 0x90 && pe[paddingRaw] != 0xcc) ||
                    (padding == 2 && !StartsWithAt(pe, paddingRaw, 0x66, 0x90)) ||
                    (padding == 3 && !StartsWithAt(pe, paddingRaw, 0x0f, 0x1f, 0x00))) continue;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) continue;
                bool guarded = true;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < dwordBegin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (instruction.Unknown || instruction.Module != owner.Module) continue;
                    int source = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length));
                    if (source > pe.Length - instruction.Length) { guarded = false; break; }
                    long target;
                    if (instruction.Length == 2 && (pe[source] == 0xeb || (pe[source] >= 0x70 && pe[source] <= 0x7f)))
                        target = (long)address + 2 + unchecked((sbyte)pe[source + 1]);
                    else if (instruction.Length == 5 && (pe[source] == 0xe9 || pe[source] == 0xe8))
                        target = (long)address + 5 + BitConverter.ToInt32(pe, source + 1);
                    else if (instruction.Length == 6 && pe[source] == 0x0f && pe[source + 1] >= 0x80 && pe[source + 1] <= 0x8f)
                        target = (long)address + 6 + BitConverter.ToInt32(pe, source + 2);
                    else continue;
                    if ((target > (long)prefix && target < (long)afterJump &&
                         (address < prefix || address >= afterJump)) ||
                        (target >= (long)dwordBegin && target < (long)byteEnd)) { guarded = false; break; }
                }
                if (!guarded) continue;
                var table = new InlineTable { Begin = dwordBegin, End = byteBegin, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                bool valid = true;
                for (int entry = 0; entry < entryCount && valid; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, dwordRaw + 4 * entry));
                    valid = target >= procedure.Begin && target < dwordBegin &&
                            (target < prefix || target >= afterJump) &&
                            IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts);
                    table.Targets.Add(target);
                }
                if (!valid || !table.Targets.Contains((ulong)branch)) continue;
                byteTable = new InlineTable { Begin = byteBegin, End = byteEnd, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End };
                return table; // Guard-derived data ends before alignment; padding itself receives no exemption.
            }
            return null;
        }

        // Release FilterSettings switches sign-extend EDX to RSI before an unsigned ESI guard and an
        // RDX/RSI DWORD dispatch. Prove the full observed producer/prelude as well: a 32-bit guard alone
        // cannot bound an arbitrary 64-bit RSI. Every row, target and terminal PDB extent must agree.
        private static InlineTable ReadGuardedRsiDwordTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, List<Function> procedures,
            ulong imageBase, ulong dispatch, Dictionary<ulong, DecodedInstruction> decoded,
            List<ulong> decodedStarts, Dictionary<ulong, byte[]> guardRows)
        {
            if (dispatch < imageBase + 36 || dispatch - imageBase > UInt32.MaxValue) return null;
            int ownerIndex = FindContribution(contributions, dispatch);
            if (ownerIndex < 0) return null;
            Contribution owner = contributions[ownerIndex];
            ulong prefix = dispatch - 36, afterJump = dispatch + 19;
            if (prefix < owner.Begin || afterJump > owner.End) return null;
            int raw;
            try { raw = checked((int)Offset(checked((uint)(prefix - imageBase)), sections, pe.Length)); }
            catch (InvalidDataException) { return null; }
            if (raw > pe.Length - 55 || !StartsWithAt(pe, raw, 0x48, 0x63, 0xf2) ||
                !StartsWithAt(pe, raw + 3, 0x48, 0x8b, 0xf9) ||
                !StartsWithAt(pe, raw + 6, 0xc7, 0x44, 0x24, 0x20, 0, 0, 0, 0) ||
                pe[raw + 14] != 0xe8 || !StartsWithAt(pe, raw + 19, 0xc7, 0x44, 0x24, 0x20, 1, 0, 0, 0) ||
                !StartsWithAt(pe, raw + 27, 0x83, 0xfe) || !StartsWithAt(pe, raw + 30, 0x0f, 0x87) ||
                !StartsWithAt(pe, raw + 36, 0x48, 0x8d, 0x15) ||
                (long)dispatch + 7 + BitConverter.ToInt32(pe, raw + 39) != (long)imageBase ||
                !StartsWithAt(pe, raw + 43, 0x8b, 0x8c, 0xb2) ||
                !StartsWithAt(pe, raw + 50, 0x48, 0x03, 0xca) || !StartsWithAt(pe, raw + 53, 0xff, 0xe1)) return null;
            uint maximum = pe[raw + 29];
            if (maximum == 0 || maximum >= 128) return null;
            uint tableRva = BitConverter.ToUInt32(pe, raw + 46);
            ulong begin = checked(imageBase + tableRva), end = checked(begin + 4 * (maximum + 1));
            if ((begin & 3) != 0 || begin <= afterJump || end > owner.End ||
                !dataSymbols.TryGetValue(begin, out int dataOwner) || dataOwner != owner.Module) return null;
            int[] starts = { 0, 3, 6, 14, 19, 27, 30, 36, 43, 50, 53 };
            int[] lengths = { 3, 3, 8, 5, 8, 3, 6, 7, 7, 3, 2 };
            for (int i = 0; i < starts.Length; ++i)
            {
                ulong address = prefix + (ulong)starts[i];
                if (!guardRows.TryGetValue(address, out byte[] reported) || reported.Length != lengths[i] ||
                    !MatchesDecodedPe(pe, sections, imageBase, address, reported, contributions, decoded, decodedStarts))
                    return null;
            }
            foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                if (symbol.Value == owner.Module && symbol.Key > begin && symbol.Key < end) return null;
            long defaultAddress = (long)prefix + 36 + BitConverter.ToInt32(pe, raw + 32);
            foreach (Function procedure in procedures)
            {
                if (procedure.Module != owner.Module || procedure.ContributionBegin != owner.Begin ||
                    procedure.ContributionEnd != owner.End || prefix < procedure.Begin || procedure.End != end ||
                    defaultAddress < (long)afterJump || defaultAddress >= (long)begin ||
                    !IsDecodedBoundary((ulong)defaultAddress, owner.Module, contributions, decoded, decodedStarts)) continue;
                var table = new InlineTable { Begin = begin, End = end, Module = owner.Module,
                    ContributionBegin = owner.Begin, ContributionEnd = owner.End, VerifyReportedBytes = true };
                int tableRaw;
                try { tableRaw = checked((int)Offset(tableRva, sections, pe.Length)); }
                catch (InvalidDataException) { return null; }
                if (tableRaw > pe.Length - checked((int)(end - begin))) return null;
                if (tableRaw < 3 || !StartsWithAt(pe, tableRaw - 3, 0xcc, 0x66, 0x90) ||
                    !guardRows.TryGetValue(begin - 3, out byte[] terminal) || terminal.Length != 1 ||
                    !MatchesDecodedPe(pe, sections, imageBase, begin - 3, terminal, contributions, decoded, decodedStarts) ||
                    !guardRows.TryGetValue(begin - 2, out byte[] padding) || padding.Length != 2 ||
                    !MatchesDecodedPe(pe, sections, imageBase, begin - 2, padding, contributions, decoded, decodedStarts))
                    return null;
                int first = decodedStarts.BinarySearch(procedure.Begin);
                if (first < 0) continue;
                for (int i = first; i < decodedStarts.Count && decodedStarts[i] < begin; ++i)
                {
                    ulong address = decodedStarts[i];
                    DecodedInstruction instruction = decoded[address];
                    if (instruction.Unknown || instruction.Module != owner.Module) return null;
                    int at = checked((int)Offset(checked((uint)(address - imageBase)), sections, pe.Length));
                    long target;
                    if (instruction.Length == 2 && (pe[at] == 0xeb || (pe[at] >= 0x70 && pe[at] <= 0x7f)))
                        target = (long)address + 2 + unchecked((sbyte)pe[at + 1]);
                    else if (instruction.Length == 5 && (pe[at] == 0xe9 || pe[at] == 0xe8))
                        target = (long)address + 5 + BitConverter.ToInt32(pe, at + 1);
                    else if (instruction.Length == 6 && pe[at] == 0x0f && pe[at + 1] >= 0x80 && pe[at + 1] <= 0x8f)
                        target = (long)address + 6 + BitConverter.ToInt32(pe, at + 2);
                    else continue;
                    if ((target > (long)prefix && target < (long)afterJump &&
                         (address < prefix || address >= afterJump)) ||
                        (target >= (long)begin && target < (long)end)) return null;
                }
                for (uint entry = 0; entry <= maximum; ++entry)
                {
                    ulong target = checked(imageBase + BitConverter.ToUInt32(pe, tableRaw + checked((int)(4 * entry))));
                    if (target < afterJump || target >= begin ||
                        !IsDecodedBoundary(target, owner.Module, contributions, decoded, decodedStarts)) return null;
                    table.Targets.Add(target);
                }
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
                    RawSize = BitConverter.ToUInt32(pe, at + 16),
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

            // Bind the PDB before interpreting its offsets in this image. An unrelated PDB can have different
            // section sizes, so contribution mapping is not a valid way to diagnose an identity mismatch.
            bool guidMatch = false, ageMatch = false;
            foreach (string line in File.ReadLines(metadata))
            {
                Match match = Regex.Match(line, @"^\s*GUID: (\{.+\})$");
                if (match.Success) guidMatch = Guid.Parse(match.Groups[1].Value) == guid;
                match = Regex.Match(line, @"^\s*Age: (\d+)$");
                if (match.Success) ageMatch = UInt32.Parse(match.Groups[1].Value) == age;
            }
            if (!guidMatch || !ageMatch) throw new InvalidDataException("PDB GUID/age does not match the linked image.");

            var modules = new Dictionary<int, Module>();
            var contributions = new List<Contribution>();
            int module = -1;
            bool inContributions = false;
            foreach (string line in File.ReadLines(metadata))
            {
                Match match;
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
            var rdiBaseCandidates = new List<ulong>();
            var raxStackCandidates = new List<ulong>();
            var rsiDwordCandidates = new List<ulong>();
            var r13TerminalCandidates = new List<ulong>();
            var raxPaddingBodyRanges = new List<InlineTable>();
            var r9ByteCandidates = new List<ulong>();
            var r9HoistedCandidates = new List<ulong>();
            var r14LoopCandidates = new List<ulong>();
            var r15SignedCandidates = new List<ulong>();
            var r9BodyRanges = new List<InlineTable>();
            foreach (string line in disassemblyLines)
            {
                Match candidate = InstructionLine.Match(line);
                if (!candidate.Success) continue;
                string[] candidateBytes = candidate.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                if (candidateBytes.Length == 8 && candidateBytes[0] == "41" && candidateBytes[1] == "8b" &&
                    candidateBytes[2] == "8c" && candidateBytes[3] == "86")
                {
                    ulong address = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    r14LoopCandidates.Add(address);
                    int index = FindContribution(contributions, address);
                    if (index >= 0)
                        foreach (Function procedure in procedures)
                            if (procedure.Module == contributions[index].Module &&
                                procedure.ContributionBegin == contributions[index].Begin &&
                                procedure.ContributionEnd == contributions[index].End &&
                                procedure.Begin <= address && address < procedure.End)
                                r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = procedure.End });
                }
                if (candidateBytes.Length == 8 && candidateBytes[0] == "42" && candidateBytes[1] == "8b" &&
                    candidateBytes[2] == "8c" && candidateBytes[3] == "aa")
                {
                    ulong load = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    if (load >= imageBase + 17) r13TerminalCandidates.Add(load - 7);
                    int index = FindContribution(contributions, load);
                    if (index >= 0)
                        foreach (Function procedure in procedures)
                            if (procedure.Module == contributions[index].Module &&
                                procedure.ContributionBegin == contributions[index].Begin &&
                                procedure.ContributionEnd == contributions[index].End &&
                                procedure.Begin <= load && load < procedure.End)
                            {
                                // Retain the complete candidate procedure, including its terminal table rows.
                                r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = procedure.End });
                            }
                }
                if (candidateBytes.Length == 9 && candidateBytes[0] == "41" && candidateBytes[1] == "0f" &&
                    candidateBytes[2] == "b6" && candidateBytes[3] == "84" && candidateBytes[4] == "07")
                {
                    ulong address = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    r15SignedCandidates.Add(address);
                    int index = FindContribution(contributions, address);
                    if (index >= 0)
                        foreach (Function procedure in procedures)
                            if (procedure.Module == contributions[index].Module &&
                                procedure.ContributionBegin == contributions[index].Begin &&
                                procedure.ContributionEnd == contributions[index].End &&
                                procedure.Begin <= address && address < procedure.End)
                            {
                                ulong end = procedure.End;
                                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                                    if (symbol.Value == procedure.Module && symbol.Key >= procedure.Begin && symbol.Key < end)
                                        end = symbol.Key;
                                r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = end });
                            }
                }
                if (candidateBytes.Length == 8 && candidateBytes[0] == "41" && candidateBytes[1] == "8b" &&
                    candidateBytes[2] == "8c" && candidateBytes[3] == "81")
                {
                    ulong address = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    r9HoistedCandidates.Add(address);
                    int index = FindContribution(contributions, address);
                    if (index >= 0)
                        foreach (Function procedure in procedures)
                            if (procedure.Module == contributions[index].Module &&
                                procedure.ContributionBegin == contributions[index].Begin &&
                                procedure.ContributionEnd == contributions[index].End &&
                                procedure.Begin <= address && address < procedure.End)
                            {
                                ulong end = procedure.End;
                                foreach (KeyValuePair<ulong, int> symbol in dataSymbols)
                                    if (symbol.Value == procedure.Module && symbol.Key >= procedure.Begin && symbol.Key < end)
                                        end = symbol.Key;
                                r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = end });
                            }
                }
                if (candidateBytes.Length == 4 && candidateBytes[0] == "41" && candidateBytes[1] == "80" && candidateBytes[2] == "f9")
                {
                    ulong comparison = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    rdxGuardCandidates.Add(comparison);
                    rdxGuardCandidates.Add(comparison + 4);
                }
                if (candidateBytes.Length == 7 && candidateBytes[0] == "48" && candidateBytes[1] == "8d" && candidateBytes[2] == "05")
                {
                    ulong address = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    if (address >= imageBase + 15)
                    {
                        raxStackCandidates.Add(address);
                        foreach (int offset in new int[] { -15, -6, -2, 0, 7, 10, 15, 17, 20, 22, 25 })
                            rdxGuardCandidates.Add(checked((ulong)((long)address + offset)));
                        try
                        {
                            foreach (int loadOffset in new int[] { 15, 10 })
                            {
                                int at = checked((int)Offset(checked((uint)(address + (ulong)loadOffset - imageBase)), sections, pe.Length));
                                if (at <= pe.Length - 7 && StartsWithAt(pe, at, 0x8b, 0x8c, 0x88))
                                {
                                    ulong table = checked(imageBase + BitConverter.ToUInt32(pe, at + 3));
                                    if (table > imageBase) rdxGuardCandidates.Add(table - 1);
                                    if (table >= imageBase + 2) rdxGuardCandidates.Add(table - 2);
                                    if (table >= imageBase + 4)
                                    {
                                        rdxGuardCandidates.Add(table - 4);
                                        rdxGuardCandidates.Add(table - 3);
                                        int tableRaw = checked((int)Offset(checked((uint)(table - imageBase)), sections, pe.Length));
                                        if ((tableRaw >= 2 && StartsWithAt(pe, tableRaw - 2, 0xc3, 0x90)) ||
                                            (tableRaw >= 4 && StartsWithAt(pe, tableRaw - 4, 0xc3, 0x0f, 0x1f, 0x00)))
                                            foreach (Function procedure in procedures)
                                                if (procedure.Begin <= address - (ulong)(loadOffset == 10 ? 6 : 15) && procedure.End > table)
                                                    raxPaddingBodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = table });
                                    }
                                }
                            }
                        }
                        catch (InvalidDataException) { /* Unmapped candidate; no terminal row retained. */ }
                    }
                }
                if (candidateBytes.Length == 7 && candidateBytes[0] == "48" && candidateBytes[1] == "8d" && candidateBytes[2] == "3d")
                {
                    ulong address = UInt64.Parse(candidate.Groups[1].Value, NumberStyles.HexNumber);
                    if (address >= imageBase + 16)
                    {
                        rdiBaseCandidates.Add(address);
                        foreach (int offset in new int[] { -16, -13, -8, -2, 0, 7, 15, 22, 25 })
                            rdxGuardCandidates.Add(checked((ulong)((long)address + offset)));
                    }
                }
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
                if (encoded[0] == 0x48 && encoded[2] == 0x15 && candidateAddress >= imageBase + 36)
                {
                    rsiDwordCandidates.Add(candidateAddress);
                    foreach (int offset in new int[] { -36, -33, -30, -22, -17, -9, -6, 0, 7, 14, 17 })
                        rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                    try
                    {
                        int loadRaw = checked((int)Offset(checked((uint)(candidateAddress + 7 - imageBase)), sections, pe.Length));
                        if (loadRaw <= pe.Length - 7 && StartsWithAt(pe, loadRaw, 0x8b, 0x8c, 0xb2))
                        {
                            ulong tableBegin = checked(imageBase + BitConverter.ToUInt32(pe, loadRaw + 3));
                            if (tableBegin >= imageBase + 3)
                                foreach (int offset in new int[] { -3, -2 })
                                    rdxGuardCandidates.Add(checked((ulong)((long)tableBegin + offset)));
                        }
                    }
                    catch (InvalidDataException) { /* Not a mapped canonical load; retain no terminal rows. */ }
                }
                if (r10Candidate)
                {
                    r10BaseCandidates.Add(candidateAddress);
                    for (int offset = 0; offset < 118; ++offset) rdxGuardCandidates.Add(candidateAddress + (ulong)offset);
                    if (candidateAddress >= imageBase + 20)
                        foreach (int offset in new int[]{-20,-17,-14,-11,-8,-6})
                            rdxGuardCandidates.Add((ulong)((long)candidateAddress + offset));
                    try
                    {
                        int at = checked((int)Offset(checked((uint)(candidateAddress + 7 - imageBase)), sections, pe.Length));
                        if (at <= pe.Length - 8 && StartsWithAt(pe, at, 0x41, 0x8b, 0x8c, 0x82))
                        {
                            ulong table = checked(imageBase + BitConverter.ToUInt32(pe, at + 4));
                            int candidateOwner = FindContribution(contributions, candidateAddress);
                            if (candidateOwner >= 0)
                                foreach (Function procedure in procedures)
                                    if (procedure.Module == contributions[candidateOwner].Module &&
                                        procedure.ContributionBegin == contributions[candidateOwner].Begin &&
                                        procedure.ContributionEnd == contributions[candidateOwner].End &&
                                        procedure.Begin <= candidateAddress && table < procedure.End &&
                                        dataSymbols.TryGetValue(table, out int tableOwner) && tableOwner == procedure.Module)
                                        r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = table });
                        }
                    }
                    catch (InvalidDataException) { /* Unmapped candidate; retain no procedure body. */ }
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
                if (candidateBytes[2] == "15")
                {
                    r9ByteCandidates.Add(candidateAddress);
                    foreach (int offset in new int[] { -10, -8, -6, 0, 7, 15, 18 })
                        rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                    try
                    {
                        int at = checked((int)Offset(checked((uint)(candidateAddress + 7 - imageBase)), sections, pe.Length));
                        if (at <= pe.Length - 8 && StartsWithAt(pe, at, 0x42, 0x8b, 0x84, 0x8a))
                        {
                            ulong table = checked(imageBase + BitConverter.ToUInt32(pe, at + 4));
                            if (table >= imageBase + 8)
                                foreach (int offset in new int[] { -8, -7, -2, -1 })
                                    rdxGuardCandidates.Add((ulong)((long)table + offset));
                            int candidateOwner = FindContribution(contributions, candidateAddress);
                            if (candidateOwner >= 0)
                                foreach (Function procedure in procedures)
                                    if (procedure.Module == contributions[candidateOwner].Module &&
                                        procedure.ContributionBegin == contributions[candidateOwner].Begin &&
                                        procedure.ContributionEnd == contributions[candidateOwner].End &&
                                        procedure.Begin <= candidateAddress && table < procedure.End &&
                                        dataSymbols.TryGetValue(table, out int tableOwner) && tableOwner == procedure.Module)
                                        r9BodyRanges.Add(new InlineTable { Begin = procedure.Begin, End = table });
                        }
                    }
                    catch (InvalidDataException) { /* Unmapped candidate; no terminal row retained. */ }
                }
                if (candidateBytes[2] == "0d")
                {
                    int[] offsets = { -25, -22, -21, -18, -17, -16, -15, -13, -12, -11, -10, -7, -6, -5, -4, 0, 7, 14, 15, 17, 22, 25 };
                    foreach (int offset in offsets) rdxGuardCandidates.Add(checked((ulong)((long)candidateAddress + offset)));
                    try
                    {
                        uint rva = checked((uint)(candidateAddress - imageBase));
                        int raw = checked((int)Offset(rva, sections, pe.Length));
                        if (raw <= pe.Length - 14 && StartsWithAt(pe, raw + 7, 0x8b, 0x84, 0x81))
                        {
                            ulong table = checked(imageBase + BitConverter.ToUInt32(pe, raw + 10));
                            if (table > imageBase) rdxGuardCandidates.Add(table - 1);
                        }
                    }
                    catch (InvalidDataException) { /* Unmapped candidate; no terminal row retained. */ }
                    continue;
                }
                // Exact byte-index guard plus its four dispatch rows. Keep only candidates, not all encodings.
                int[] byteOffsets = { -11, -8, -5, -4, -2, 0, 7, 14, 15, 17, 22, 25 };
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
                    bool r9Body = false;
                    foreach (InlineTable range in r9BodyRanges)
                        if (rowAddress >= range.Begin && rowAddress < range.End) { r9Body = true; break; }
                    foreach (InlineTable range in raxPaddingBodyRanges)
                        if (rowAddress >= range.Begin && rowAddress < range.End) { r9Body = true; break; }
                    if (r9Body || (rowBytes.Length == 11 && rowBytes[0] == "81" && rowBytes[1] == "bc" && rowBytes[2] == "24") ||
                        (rowBytes.Length == 6 && rowBytes[0] == "0f" && rowBytes[1] == "87") ||
                        (rowBytes.Length == 8 && rowBytes[0] == "48" && rowBytes[1] == "63" &&
                         rowBytes[2] == "84" && rowBytes[3] == "24") || rdxGuardCandidates.Contains(rowAddress))
                        guardRows.Add(rowAddress, Array.ConvertAll(rowBytes, b => Byte.Parse(b, NumberStyles.HexNumber)));
                }
                else if (rowOwner >= 0) decoded[rowAddress].Unknown = true;
            }
            decodedStarts.Sort();
            foreach (ulong address in r14LoopCandidates)
            {
                InlineTable table = ReadGuardedR14LoopTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in r13TerminalCandidates)
            {
                InlineTable table = ReadGuardedR13TerminalDwordTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in rsiDwordCandidates)
            {
                InlineTable table = ReadGuardedRsiDwordTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in r15SignedCandidates)
            {
                List<InlineTable> tables = ReadHoistedR15SignedTables(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (tables != null) inlineTables.AddRange(tables);
            }
            foreach (ulong address in r9HoistedCandidates)
            {
                InlineTable table = ReadGuardedR9HoistedDirectTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in rdiBaseCandidates)
            {
                InlineTable byteTable;
                InlineTable table = ReadGuardedRdiBaseByteRemap(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows, out byteTable);
                if (table != null) { inlineTables.Add(table); inlineTables.Add(byteTable); }
            }
            foreach (ulong address in r10BaseCandidates)
            {
                InlineTable table = ReadGuardedR10AlTerminalDwordTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows) ??
                    ReadGuardedR10EcxTerminalDwordTable(pe, sections, contributions, dataSymbols,
                        procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in raxStackCandidates)
            {
                InlineTable table = ReadGuardedQwordStackRaxTable(pe, sections, contributions, dataSymbols,
                    procedures, imageBase, address, decoded, decodedStarts, guardRows);
                if (table != null) inlineTables.Add(table);
            }
            foreach (ulong address in r9ByteCandidates)
            {
                InlineTable table = ReadGuardedR9ByteSwitchTable(pe, sections, contributions, dataSymbols,
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
            bool rdxSignedByteBeforeLea = false, rdxSignedRemap = false, rdxSignedDword = false;
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
                    { byteIndexTableRva = BitConverter.ToUInt32(encoded, 4); hasByteIndexTable = true; rdxRemap = true; rdxSignedRemap = rdxSignedByteBeforeLea; dispatchStage = 2; }
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
                    else if (rdxSignedByteBeforeLea && rdxBase && rdxBaseMatchesPe && dispatchStage == 1 &&
                        encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x84, 0x82) &&
                        MatchesDecodedPe(pe, sections, imageBase, address, encoded, contributions, decoded, decodedStarts))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 0; rdxSignedDword = true; dispatchStage = 3; }
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
                         (rdxBase && (rdxByteGuard || rdxRdiIndex || rdxSignedDword) && StartsWith(encoded, 0x48, 0x03, 0xc2) &&
                          MatchesDecodedPe(pe, sections, imageBase, address, encoded, contributions, decoded, decodedStarts)) ||
                         (r9Base && StartsWith(encoded, 0x4d, 0x03, 0xc1)) ||
                         (r8Base && encoded[0] == 0x49 && encoded[1] == 0x03 && encoded[2] == 0xc0 + 8 * targetRegister)))
                        dispatchStage = 4;
                    else if (dispatchStage == 4 &&
                        ((r9Base && encoded.Length == 3 && StartsWith(encoded, 0x41, 0xff, 0xe0)) ||
                         (!r9Base && encoded.Length == 2 && encoded[0] == 0xff && encoded[1] == 0xe0 + targetRegister &&
                          (!(rdxRemap || rdxRdiIndex || rdxSignedDword) || MatchesDecodedPe(pe, sections, imageBase, address, encoded,
                            contributions, decoded, decodedStarts)))))
                    {
                        InlineTable provenByteIndexTable = null;
                        InlineTable table = rdxSignedRemap
                            ? ReadSignedBiasedByteIndexTables(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), byteIndexTableRva, tableRva, decoded,
                                decodedStarts, guardRows, out provenByteIndexTable, true)
                            : biasedSignedDispatch
                            ? ReadSignedBiasedByteIndexTables(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), byteIndexTableRva, tableRva, decoded,
                                decodedStarts, guardRows, out provenByteIndexTable)
                            : rdxSignedDword
                            ? ReadSignedRdxDwordTable(pe, sections, contributions, dataSymbols, procedures, imageBase,
                                dispatchBegin, checked(address + (ulong)encoded.Length), tableRva, decoded, decodedStarts, guardRows)
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
                        if (table != null && hasByteIndexTable && !biasedSignedDispatch && !rdxSignedRemap)
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
                      rdxRemap = false; rdxByteGuard = false; rdxSignedRemap = false; rdxSignedDword = false;
                      rdxSignedByteBeforeLea = rdxBase && previousSignedByteIndex && address == precedingEnd;
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
