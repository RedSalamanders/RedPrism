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
            public int Module;
            public string Name;
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
        // same PDB proof too. No other R9 sequence or intervening instruction is accepted.
        // That form also proves both branch targets and requires an exact PDB data start. The table follows RET plus at most three
        // alignment bytes, belongs to that contribution, and every entry targets a later decoded instruction
        // between the dispatch and the table. A matching anonymous PDB data symbol can prove a table start
        // when multiple tables follow the function body. Unrecognized forms remain subject to the normal ISA gate.
        private static InlineTable ReadInlineTable(byte[] pe, List<Section> sections,
            List<Contribution> contributions, Dictionary<ulong, int> dataSymbols, ulong imageBase,
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
            var table = new InlineTable { Begin = begin, End = begin, Module = owner.Module,
                ContributionBegin = owner.Begin, ContributionEnd = owner.End };
            for (int entry = 0; entry < 4096 && table.End + 4 <= owner.End && raw + 4 <= pe.Length; ++entry, raw += 4)
            {
                if (entry != 0 && dataSymbols.ContainsKey(table.End)) break;
                ulong target = checked(imageBase + BitConverter.ToUInt32(pe, raw));
                if (target < afterJump || target >= begin) break;
                table.PendingTargets.Add(target);
                table.End += 4;
            }
            return table.End - table.Begin >= 8 ? table : null;
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
                    if (match.Success && modules.TryGetValue(module, out functionOwner) &&
                        Array.IndexOf(runtimeFunctions, Path.GetFileName(functionOwner.Object) + "!" + function) >= 0)
                    {
                        int index = Int32.Parse(match.Groups[1].Value);
                        ulong start = UInt64.Parse(match.Groups[2].Value);
                        ulong size = UInt64.Parse(match.Groups[3].Value);
                        functions.Add(new Function { Module = module, Name = function,
                            Begin = Address(index, start, imageBase, sections),
                            End = Address(index, checked(start + size), imageBase, sections) });
                    }
                    function = null;
                }
            }

            var result = new IsaScanResult();
            var inlineTables = new List<InlineTable>();
            int dispatchStage = 0;
            bool r8Base = false;
            bool raxBase = false;
            bool rdxBase = false;
            bool r9Base = false;
            int indexRegister = 0, targetRegister = 0;
            int preludeStep = 0;
            ulong firstPreludeTarget = 0, secondPreludeTarget = 0;
            ulong dispatchBegin = 0, precedingEnd = 0;
            uint tableRva = 0;
            bool previousSignedByteIndex = false, signedByteIndex = false;
            bool dlByteIndex = false;
            foreach (string line in File.ReadLines(disassembly))
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
                        dispatchStage = 2;
                    else if (r9Base && dispatchStage == 1 && encoded.Length == 9 && StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x84, 0x01))
                        dispatchStage = 2;
                    else if (r8Base && dispatchStage == 1 && preludeStep == 0 && encoded.Length == 9 &&
                        (StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x84, 0x00) || StartsWith(encoded, 0x41, 0x0f, 0xb6, 0x8c, 0x08)))
                    { indexRegister = encoded[3] == 0x84 ? 0 : 1; dispatchStage = 2; }
                    else if (raxBase && dispatchStage == 2 && encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x8c, 0x88))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 1; dispatchStage = 3; }
                    else if (rdxBase && dispatchStage == 1 && encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x8c, 0x82))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 1; dispatchStage = 3; }
                    else if (r9Base && dispatchStage == 2 && encoded.Length == 8 && StartsWith(encoded, 0x45, 0x8b, 0x84, 0x81))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = 8; dispatchStage = 3; }
                    else if (!r8Base && !raxBase && !rdxBase && !r9Base && (dispatchStage == 1 || dispatchStage == 2) && encoded.Length == 7 && StartsWith(encoded, 0x8b, 0x84, 0x81))
                    { tableRva = BitConverter.ToUInt32(encoded, 3); targetRegister = 0; dispatchStage = 3; }
                    else if (r8Base && (dispatchStage == 1 || dispatchStage == 2) &&
                        (preludeStep == 0 || preludeStep == 8) && encoded.Length == 8 &&
                        encoded[0] == 0x41 && encoded[1] == 0x8b &&
                        ((indexRegister == 0 && (encoded[2] == 0x84 || encoded[2] == 0x8c) && encoded[3] == 0x80) ||
                         (indexRegister == 1 && encoded[2] == 0x94 && encoded[3] == 0x88)))
                    { tableRva = BitConverter.ToUInt32(encoded, 4); targetRegister = (encoded[2] >> 3) & 7; dispatchStage = 3; }
                    else if (dispatchStage == 3 && encoded.Length == 3 &&
                        ((!r8Base && !raxBase && !rdxBase && !r9Base && StartsWith(encoded, 0x48, 0x03, 0xc1)) ||
                         (raxBase && StartsWith(encoded, 0x48, 0x03, 0xc8)) ||
                         (rdxBase && StartsWith(encoded, 0x48, 0x03, 0xca)) ||
                         (r9Base && StartsWith(encoded, 0x4d, 0x03, 0xc1)) ||
                         (r8Base && encoded[0] == 0x49 && encoded[1] == 0x03 && encoded[2] == 0xc0 + 8 * targetRegister)))
                        dispatchStage = 4;
                    else if (dispatchStage == 4 &&
                        ((r9Base && encoded.Length == 3 && StartsWith(encoded, 0x41, 0xff, 0xe0)) ||
                         (!r9Base && encoded.Length == 2 && encoded[0] == 0xff && encoded[1] == 0xe0 + targetRegister)))
                    {
                        InlineTable table = ReadInlineTable(pe, sections, contributions, dataSymbols, imageBase, dispatchBegin,
                            checked(address + (ulong)encoded.Length), tableRva, preludeStep != 0 || raxBase || rdxBase || r9Base || dlByteIndex, signedByteIndex);
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
                            }
                        }
                        if (table != null) inlineTables.Add(table);
                        dispatchStage = 0;
                    }
                    else dispatchStage = 0;
                    if (encoded.Length == 7 && (StartsWith(encoded, 0x48, 0x8d, 0x0d) || StartsWith(encoded, 0x4c, 0x8d, 0x05) || StartsWith(encoded, 0x4c, 0x8d, 0x0d) ||
                        StartsWith(encoded, 0x48, 0x8d, 0x05) || StartsWith(encoded, 0x48, 0x8d, 0x15)) &&
                        checked((long)address + 7 + BitConverter.ToInt32(encoded, 3)) == (long)imageBase)
                    { dispatchBegin = address; dispatchStage = 1; r8Base = encoded[0] == 0x4c && encoded[2] == 0x05;
                      raxBase = encoded[0] == 0x48 && encoded[2] == 0x05; indexRegister = 0;
                      rdxBase = encoded[0] == 0x48 && encoded[2] == 0x15;
                      r9Base = encoded[0] == 0x4c && encoded[2] == 0x0d;
                      signedByteIndex = !r8Base && !raxBase && !rdxBase && !r9Base && previousSignedByteIndex && address == precedingEnd;
                      dlByteIndex = false;
                      preludeStep = 0; firstPreludeTarget = 0; secondPreludeTarget = 0; }
                }
                previousSignedByteIndex = (bytes.Length == 5 || bytes.Length == 8) && bytes[0] == "48" &&
                                          bytes[1] == "0f" && bytes[2] == "be" &&
                                          ((bytes.Length == 5 && bytes[3] == "45") ||
                                           (bytes.Length == 8 && bytes[3] == "85"));
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
