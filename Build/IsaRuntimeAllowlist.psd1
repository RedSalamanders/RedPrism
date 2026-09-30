@{
    # MSVC STL's CPUID-dispatched algorithms. Both the object AND importing library must match.
    Objects = @('vector_algorithms.obj')
    Libraries = @('msvcprt.lib', 'msvcprtd.lib')
    # MSVC /Oi can emit this runtime-dispatched CRT intrinsic inside a consumer object. Only its exact
    # S_[GL]PROC32 range in the matching PDB is exempt, never the surrounding ImageCodecs/other object.
    # Each owning module was observed in the matching Debug/Release/ASan PDBs. A compiler change that moves
    # the COMDAT to another module requires a new reviewed entry, not a wildcard.
    Functions = @('ImageCodecs.obj!wmemchr', 'HostTests.obj!wmemchr', 'IoTests.obj!wmemchr', 'SettingsStore.obj!wmemchr')
}
