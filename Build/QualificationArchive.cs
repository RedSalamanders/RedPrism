// Windows-only archive I/O. New destinations only: no replacement, deletion, or mutation of shared objects.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;

namespace RedPrism.Archives
{
    public sealed class Store : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)] struct Time { public uint Low, High; public long Value => ((long)High << 32) | Low; }
        [StructLayout(LayoutKind.Sequential)] struct Info
        {
            public uint Attr; public Time Created, Accessed, Written;
            public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
            public long Size => ((long)SizeHigh << 32) | SizeLow;
            public string Id => Volume.ToString("X8") + ":" + IndexHigh.ToString("X8") + IndexLow.ToString("X8");
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct StreamInfo
        {
            public long Size; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)] public string Name;
        }
        [StructLayout(LayoutKind.Sequential)] struct FileId { public ulong Volume, Low, High; }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern SafeFileHandle CreateFileW(string p, uint a, uint s, IntPtr sec, uint c, uint f, IntPtr t);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetFileInformationByHandle(SafeFileHandle h, out Info i);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetFileInformationByHandleEx(SafeFileHandle h, int type, out FileId i, uint length);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateHardLinkW(string p, string existing, IntPtr reserved);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr FindFirstStreamW(string p, int level, out StreamInfo data, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool FindNextStreamW(IntPtr h, out StreamInfo data);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool FindClose(IntPtr h);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
        [DllImport("advapi32.dll", SetLastError = true)] static extern uint GetSecurityInfo(IntPtr h, int type, uint requested, out IntPtr owner, out IntPtr group, out IntPtr dacl, out IntPtr sacl, out IntPtr descriptor);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool ConvertSecurityDescriptorToStringSecurityDescriptorW(IntPtr descriptor, uint revision, uint info, out IntPtr text, out uint length);
        [DllImport("advapi32.dll")] static extern uint GetSecurityDescriptorLength(IntPtr descriptor);
        [DllImport("advapi32.dll", SetLastError = true)] static extern bool GetSecurityDescriptorDacl(IntPtr descriptor, out bool present, out IntPtr dacl, out bool defaulted);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode)] static extern uint SetNamedSecurityInfoW(string path, int type, uint info, IntPtr owner, IntPtr group, IntPtr dacl, IntPtr sacl);

        readonly string root;
        readonly byte[] buffer = new byte[1024 * 1024];
        readonly Dictionary<string, SafeFileHandle> directories = new Dictionary<string, SafeFileHandle>(StringComparer.OrdinalIgnoreCase);
        FileStream gate;
        bool disposed;
        static string Long(string p) => @"\\?\" + p;
        static Exception Win(string op)
        {
            int error = Marshal.GetLastWin32Error();
            return new Win32Exception(error, op + ": " + new Win32Exception(error).Message);
        }
        static Info ReadInfo(SafeFileHandle h) { if (!GetFileInformationByHandle(h, out var i)) throw Win("Read file identity"); return i; }
        static string Identity(SafeFileHandle h)
        {
            if (!GetFileInformationByHandleEx(h, 18, out var id, 24)) throw Win("Read full file identity");
            return id.Volume.ToString("X16") + ":" + id.High.ToString("X16") + id.Low.ToString("X16");
        }
        static string Full(string path)
        {
            if (!System.Text.RegularExpressions.Regex.IsMatch(path, @"^[A-Za-z]:[\\/]")) throw new IOException("Require an absolute local drive path");
            var p = Path.GetFullPath(path);
            if (p.Substring(2).Contains(':') || p.EndsWith(' ') || p.EndsWith('.')) throw new IOException("Ambiguous or stream path");
            return p;
        }
        string Inside(string path)
        {
            path = Full(path);
            if (!path.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Outside archive root");
            return path;
        }
        // Pin each ancestor without delete sharing, then inspect its handle. This closes junction/rename races.
        void Pin(string directory)
        {
            if (directories.ContainsKey(directory)) return;
            var parent = Path.GetDirectoryName(directory);
            if (parent != null) Pin(parent);
            var h = CreateFileW(Long(directory), 0x80, 3, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
            if (h.IsInvalid) { h.Dispose(); throw Win("Pin archive/source directory: " + directory); }
            try { var i = ReadInfo(h); if ((i.Attr & 0x410) != 0x10) throw new IOException("Directory reparse point refused: " + directory); directories.Add(directory, h); }
            catch { h.Dispose(); throw; }
        }
        public void CreateDirectory(string path)
        {
            path = Inside(path);
            var parent = Path.GetDirectoryName(path);
            if (!Directory.Exists(parent)) CreateDirectory(parent);
            Pin(parent);
            Directory.CreateDirectory(path);
            Pin(path);
        }
        sealed class Lease : IDisposable
        {
            public readonly SafeFileHandle Handle;
            public readonly FileStream Stream;
            public Lease(string p)
            {
                Handle = CreateFileW(Long(p), 0x80000000, 1, IntPtr.Zero, 3, 0x08200000, IntPtr.Zero);
                if (Handle.IsInvalid) { Handle.Dispose(); throw Win("Open read/no-write/no-delete lease: " + p); }
                try { var i = ReadInfo(Handle); if ((i.Attr & 0x410) != 0) throw new IOException("Regular files only: " + p); Stream = new FileStream(Handle, FileAccess.Read); }
                catch { Handle.Dispose(); throw; }
            }
            public void Dispose() { Stream?.Dispose(); Handle.Dispose(); }
        }
        string Hash(FileStream stream)
        {
            stream.Position = 0;
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            int n; while ((n = stream.Read(buffer, 0, buffer.Length)) != 0) hash.AppendData(buffer, 0, n);
            return Convert.ToHexString(hash.GetHashAndReset());
        }
        static string Security(Lease lease, out byte[] binary)
        {
            IntPtr descriptor = IntPtr.Zero, text = IntPtr.Zero;
            try
            {
                uint error = GetSecurityInfo(lease.Handle.DangerousGetHandle(), 1, 7, out _, out _, out _, out _, out descriptor);
                if (error != 0) throw new Win32Exception((int)error);
                binary = new byte[GetSecurityDescriptorLength(descriptor)]; Marshal.Copy(descriptor, binary, 0, binary.Length);
                if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, 1, 7, out text, out _)) throw Win("Read owner/group/DACL");
                return Marshal.PtrToStringUni(text);
            }
            finally { if (text != IntPtr.Zero) LocalFree(text); if (descriptor != IntPtr.Zero) LocalFree(descriptor); }
        }
        static void CheckStreams(string path)
        {
            var h = FindFirstStreamW(Long(path), 0, out var data, 0);
            if (h == new IntPtr(-1)) { if (Marshal.GetLastWin32Error() == 38) return; throw Win("Enumerate streams"); }
            try
            {
                do { if (data.Name != "::$DATA") throw new IOException("Alternate streams refused: " + path); } while (FindNextStreamW(h, out data));
                if (Marshal.GetLastWin32Error() != 38) throw Win("Enumerate remaining streams");
            }
            finally { FindClose(h); }
        }
        static uint Comparable(uint attributes) => attributes & ~0x81U;
        static bool Link(string destination, string existing, uint expectedAttributes)
        {
            if (CreateHardLinkW(Long(destination), Long(existing), IntPtr.Zero))
            {
                // NTFS can set the backup Archive bit on link creation. Restore that bit only; never clear RO.
                uint actual = (uint)File.GetAttributes(existing);
                if ((actual & 1) == 0 || (actual & ~0x20U) != (expectedAttributes & ~0x20U))
                    throw new IOException("Unexpected metadata change during link creation");
                if (actual != expectedAttributes) File.SetAttributes(existing, (FileAttributes)expectedAttributes);
                return true;
            }
            // Some readable source DACLs disallow link creation. Preserve them with an independent copy.
            if (Marshal.GetLastWin32Error() == 5) return false;
            throw Win("Create immutable archive alias");
        }
        public static uint LinkCount(string path)
        {
            using var file = new Lease(Full(path));
            return ReadInfo(file.Handle).Links;
        }
        public Store(string archiveRoot)
        {
            root = Full(archiveRoot).TrimEnd('\\');
            if (root.Length <= 3 || !Directory.Exists(root)) throw new IOException("Archive root must already exist beneath an authorized workspace");
            try
            {
                Pin(root);
                var path = Inside(Path.Combine(root, ".archive.lock"));
                if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) throw new IOException("Reparse lock refused");
                var h = CreateFileW(Long(path), 0xC0000000, 0, IntPtr.Zero, 4, 0x00200000, IntPtr.Zero);
                if (h.IsInvalid) { h.Dispose(); throw Win("Archive store is busy or lock cannot be acquired"); }
                if ((ReadInfo(h).Attr & 0x410) != 0) { h.Dispose(); throw new IOException("Invalid archive lock"); }
                gate = new FileStream(h, FileAccess.ReadWrite);
            }
            catch { Dispose(); throw; }
        }
        void Verify(Lease file, string path, string hash, long length)
        {
            CheckStreams(path);
            if (ReadInfo(file.Handle).Size != length || Hash(file.Stream) != hash) throw new IOException("Archive/source hash or length differs: " + path);
        }
        void Copy(Lease source, Info original, byte[] security, string expectedSecurity, string destination)
        {
            CreateDirectory(Path.GetDirectoryName(destination));
            using (var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, buffer.Length, FileOptions.WriteThrough))
            { source.Stream.Position = 0; source.Stream.CopyTo(output, buffer.Length); output.Flush(true); }
            File.SetCreationTimeUtc(destination, DateTime.FromFileTimeUtc(original.Created.Value));
            File.SetLastWriteTimeUtc(destination, DateTime.FromFileTimeUtc(original.Written.Value));
            // Apply immutability before a restrictive source DACL can remove write-attributes access.
            File.SetAttributes(destination, (FileAttributes)((original.Attr & ~0x80U) | 1U));
            string copiedSecurity;
            using (var copied = new Lease(destination)) copiedSecurity = Security(copied, out _);
            // Ordinary archive creation needs no privilege to reassign ownership. Existing equal security is kept.
            // A different owner/group fails final verification rather than requesting privilege or weakening it.
            uint inheritance = expectedSecurity.Contains("D:P", StringComparison.Ordinal) ? 0x80000000U : 0x20000000U;
            if (copiedSecurity != expectedSecurity)
            {
                var pinned = GCHandle.Alloc(security, GCHandleType.Pinned);
                try
                {
                    if (!GetSecurityDescriptorDacl(pinned.AddrOfPinnedObject(), out bool present, out var dacl, out _) || !present)
                        throw new IOException("Source DACL cannot be preserved");
                    uint error = SetNamedSecurityInfoW(Long(destination), 1, 4U | inheritance, IntPtr.Zero, IntPtr.Zero, dacl, IntPtr.Zero);
                    if (error != 0) throw new Win32Exception((int)error, "Preserve DACL on new archive object");
                }
                finally { pinned.Free(); }
            }
        }
        public sealed class Entry
        {
            public string Path { get; set; } public string ArchivedPath { get; set; } public string Hash { get; set; }
            public long Length { get; set; } public long CreatedFileTime { get; set; } public long AccessedFileTime { get; set; }
            public long WrittenFileTime { get; set; } public uint OriginalAttributes { get; set; }
            public string OriginalId { get; set; } public string ArchivedId { get; set; } public string SecuritySddl { get; set; }
            public string Storage { get; set; } public string ObjectPath { get; set; }
        }
        public Entry Archive(string sourcePath, string destinationPath, string hash, long length, bool share)
        {
            if (disposed) throw new ObjectDisposedException(nameof(Store));
            var source = Full(sourcePath); var destination = Inside(destinationPath);
            if (!System.Text.RegularExpressions.Regex.IsMatch(hash, "^[A-Fa-f0-9]{64}$") || length < 0) throw new IOException("Require expected SHA-256 and length");
            hash = hash.ToUpperInvariant();
            if (source.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Archive store cannot be its own input");
            Pin(Path.GetDirectoryName(source)); CreateDirectory(Path.GetDirectoryName(destination));
            if (File.Exists(destination) || Directory.Exists(destination)) throw new IOException("Refuse archive path reuse");
            using var input = new Lease(source);
            var original = ReadInfo(input.Handle);
            // Reject storage semantics an ordinary byte copy cannot faithfully preserve.
            if ((original.Attr & ~0x20A7U) != 0) throw new IOException("Unsupported storage attributes; original retained");
            Verify(input, source, hash, length);
            var security = Security(input, out var descriptor);
            share = share && original.Links == 1 && new DriveInfo(Path.GetPathRoot(root)).DriveFormat == "NTFS";
            var method = "Independent"; string objectPath = null; bool independent = false;
            if (share)
            {
                var metadata = original.Written.Value + ":" + Comparable(original.Attr) + ":" + security;
                var key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(metadata)));
                var bucket = Path.Combine(root, ".payloads", "v1", hash.Substring(0, 2), hash, key);
                CreateDirectory(bucket);
                foreach (var candidate in Directory.GetFiles(bucket, "*.blob"))
                {
                    using var stored = new Lease(candidate);
                    var info = ReadInfo(stored.Handle);
                    Verify(stored, candidate, hash, length); // Never trust the object name or prior receipt.
                    if ((info.Attr & 1) == 0 || info.Written.Value != original.Written.Value || Comparable(info.Attr) != Comparable(original.Attr) || Security(stored, out _) != security)
                        throw new IOException("Shared object metadata/read-only state differs");
                    if (info.Links >= 900) continue; // Leave headroom below NTFS's link limit; begin a new shard.
                    if (!Link(destination, candidate, (original.Attr & ~0x80U) | 1U))
                    {
                        Copy(input, original, descriptor, security, destination); independent = true; break;
                    }
                    using var alias = new Lease(destination);
                    if (Identity(alias.Handle) != Identity(stored.Handle)) throw new IOException("Alias identity differs");
                    Verify(alias, destination, hash, length);
                    objectPath = candidate; method = "Reused"; break;
                }
                if (objectPath == null && !independent)
                {
                    objectPath = Path.Combine(bucket, Guid.NewGuid().ToString("N") + ".blob");
                    Copy(input, original, descriptor, security, objectPath);
                    using var stored = new Lease(objectPath);
                    Verify(stored, objectPath, hash, length);
                    if (Security(stored, out _) != security) throw new IOException("Copied security differs");
                    if (Link(destination, objectPath, (original.Attr & ~0x80U) | 1U)) method = "NewObject";
                    else { Copy(input, original, descriptor, security, destination); objectPath = null; }
                }
            }
            else Copy(input, original, descriptor, security, destination);
            using var readback = new Lease(destination);
            Verify(readback, destination, hash, length);
            var after = ReadInfo(readback.Handle); var current = ReadInfo(input.Handle);
            if (current.Id != original.Id || current.Written.Value != original.Written.Value || current.Attr != original.Attr || current.Size != original.Size)
                throw new IOException("Input changed under archive lease");
            CheckStreams(source);
            if (Security(input, out _) != security) throw new IOException("Input security changed under archive lease");
            if ((after.Attr & 1) == 0 || Comparable(after.Attr) != Comparable(original.Attr) || after.Written.Value != original.Written.Value || Security(readback, out _) != security)
                throw new IOException("Final archive metadata/security differs");
            if (objectPath != null)
            {
                using var retained = new Lease(objectPath);
                if (Identity(retained.Handle) != Identity(readback.Handle)) throw new IOException("Final shared identity differs");
            }
            return new Entry { Path = source, ArchivedPath = destination, Hash = hash, Length = length, OriginalId = Identity(input.Handle),
                ArchivedId = Identity(readback.Handle), CreatedFileTime = original.Created.Value, AccessedFileTime = original.Accessed.Value,
                WrittenFileTime = original.Written.Value, OriginalAttributes = original.Attr, SecuritySddl = security,
                Storage = method, ObjectPath = objectPath };
        }
        public void Dispose()
        {
            if (disposed) return; disposed = true;
            gate?.Dispose(); foreach (var handle in directories.Values) handle.Dispose(); directories.Clear();
        }
    }
}
