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
        private static readonly Regex ModuleLine = new Regex(@"^\s*Mod (\d+) \| `(.+)`:", RegexOptions.Compiled);
        private static readonly Regex ObjectLine = new Regex(@"^\s*Obj: `(.+)`:", RegexOptions.Compiled);
        private static readonly Regex ContributionLine = new Regex(@"^\s*SC\[.+?\]\s*\| mod = (\d+), (\d+):(\d+), size = (\d+),", RegexOptions.Compiled);
        private static readonly Regex FunctionLine = new Regex(@"\| S_[GL]PROC32(?:_ID)? \[size = \d+\] `(.+)`", RegexOptions.Compiled);
        private static readonly Regex FunctionAddress = new Regex(@"addr = (\d+):(\d+), code size = (\d+)", RegexOptions.Compiled);
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
            string function = null;
            module = -1;
            foreach (string line in File.ReadLines(symbols))
            {
                Match match = ModuleLine.Match(line);
                if (match.Success) { module = Int32.Parse(match.Groups[1].Value); function = null; }
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
            foreach (string line in File.ReadLines(disassembly))
            {
                Match match = InstructionLine.Match(line);
                if (!match.Success) continue;
                ++result.Instructions;
                string[] bytes = match.Groups[2].Value.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                int prefix = 0;
                byte opcode = Byte.Parse(bytes[prefix], NumberStyles.HexNumber);
                while (IsLegacyPrefix(opcode) && ++prefix < bytes.Length)
                    opcode = Byte.Parse(bytes[prefix], NumberStyles.HexNumber);
                if (opcode != 0xc4 && opcode != 0xc5 && opcode != 0x62) continue;
                // Linear disassembly can encounter inline jump tables. An undecoded data byte is not an
                // instruction. Report it separately instead of exempting any decoded instruction.
                if (match.Groups[3].Value.StartsWith("<unknown>", StringComparison.Ordinal))
                { ++result.UndecodedPrefixes; continue; }
                ++result.VectorInstructions;
                if (opcode == 0x62) ++result.EvexInstructions;
                ulong address = UInt64.Parse(match.Groups[1].Value, NumberStyles.HexNumber);
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
