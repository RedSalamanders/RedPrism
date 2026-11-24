@{
    # MSVC STL's CPUID-dispatched algorithms. Both the object AND importing library must match.
    Objects = @('vector_algorithms.obj')
    Libraries = @('msvcprt.lib', 'msvcprtd.lib')
    # MSVC /Oi can emit this runtime-dispatched CRT intrinsic inside a consumer object. Only its exact
    # S_[GL]PROC32 range in the matching PDB is exempt, never the surrounding ImageCodecs/other object.
    # Each owning module was observed in the matching Debug/Release/ASan PDBs. A compiler change that moves
    # the COMDAT to another module requires a new reviewed entry, not a wildcard.
    Functions = @('ImageCodecs.obj!wmemchr', 'Host.Tests.Runner.obj!wmemchr', 'IO.Tests.Runner.obj!wmemchr', 'SettingsStore.obj!wmemchr',
        # MSVC 14.52 /GL inlines UCRT wchar.h's flag-guarded wmemchr into this exact STL wrapper.
        # Matching PE/PDB and _Avx2WmemEnabled branch reviewed for the IO and Host test images.
        # See the CPU ISA boundary in Specs/Build/Build_Toolchain.md.
        'IO.Tests.Runner.obj!std::_WChar_traits<wchar_t>::find',
        'Host.Tests.Runner.obj!std::_WChar_traits<wchar_t>::find')
}
