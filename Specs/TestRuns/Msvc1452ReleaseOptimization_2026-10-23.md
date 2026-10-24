# MSVC 14.52 Release optimization qualification

Status: Active; measurements and matrix are pending.

## Reviewed CRT attribution

The original candidate scan rejected eight AVX2 instructions in IoTests.exe (SHA256 703AAFE2FBE4219ECF4527289D32872CD16787CF14CCCE45AA4A4FEFAEEB71D2), with matching PDB B157FC2D504822EB786999051C36AC66DE3C5AB49ABD93284C8F888D82200D5C. Original binary, PDB, report and build failure are preserved under Z:/src/RedPrism/.wt/lto/.build/msvc1452/failed-isa and .build/IsaScan/x64/Release/IoTests.

The matching PDB identifies IO.Tests.Runner.obj, S_GPROC32 std::_WChar_traits<wchar_t>::find at text offset 143760, length 380 (VA 0x140024190..0x14002430c), with S_INLINESITE wmemchr under that procedure. MSVC 14.52.37025 include/__msvc_string_view.hpp lines 416-429 implement that wrapper through UCRT wmemchr. Windows SDK 10.0.26100.0 ucrt/wchar.h lines 208-214 declare the _Avx2WmemEnabled flag with a zero-initialized weak fallback; lines 274-293 guard the vector loop with that flag and N >= 16.

The actual linked code compares address 0x140157368 against zero and branches at 0x1400241bf to the scalar path 0x14002420d. The PDB global _Avx2WmemEnabled is section 3 offset 4968; data VA 0x140156000 + 4968 = 0x140157368. The following count check also branches to the scalar path. All eight rejected vector instructions lie inside this exact CRT-containing procedure. LTO has changed its enclosing procedure name, not the required ISA of the application's baseline code.

Build/IsaRuntimeAllowlist.psd1 therefore adds only IO.Tests.Runner.obj!std::_WChar_traits<wchar_t>::find to the existing reviewed exact object/procedure allowance. This is a reviewed runtime exception, not a new automated proof of arbitrary CPU dispatch. Matching PE/PDB identity, exact procedure extent and containing contribution remain required; the scanner algorithm is unchanged. Other procedures and the rest of IO.Tests.Runner.obj remain gated. Subsequent compiler changes require renewed attribution review rather than broader allowances.