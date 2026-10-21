# ISA runtime-function object casing repair

Status: focused scanner qualification passed; P1 and the latest integrated native matrix remain open.

The three-file opt-in Size diagnostic links successfully against the qualified Release object cohort but
its original ISA scan refuses eight real vector instructions in `wmemchr`. The matching PDB owns that
complete procedure in lowercase `imagecodecs.obj`; the reviewed entry already names
`ImageCodecs.obj!wmemchr`. The old function lookup incorrectly compared the Windows object basename
case-sensitively. This is an observed scanner false refusal, not a pixel-kernel or DxUi change.

The repair compares the complete object basename with ordinal case-insensitive semantics and the entire
C/C++ procedure name with ordinal case-sensitive semantics. The existing PE/PDB GUID and age, exact
procedure extent, contribution ownership and decoded instruction checks remain in force. There is no
new allowlist entry or object-wide exemption.

The full existing ISA control entrypoint passes, including compiled scalar/AVX2/AVX512, misplaced vector,
wrong-library, wrong-object and mismatched/malformed PDB refusals. Seven added controls accept exact,
lowercase and uppercase object basenames, and refuse function-name case changes, object prefix/suffix
and function suffix. The fixture's vector bytes are parsed, never executed by these controls.

Rescanning the exact originally refused EXE/PDB and unchanged decoder input classifies all488 vectors
as already approved runtime instructions with zero violations. The earlier failed report remains intact.
EXE SHA256:04E7E685EE818F6E3F8EBB73EAC3EBFDFA3352A7AC23FFC1BD9F9184F126B26A.
PDB SHA256:0E4544D053D173A45307D137CB290570ED95A81D3023FDE40431D4BC53F512B2.
Allowlist SHA256:7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E.

Exact receipts and complete raw controls are under `.build/P1Evidence/p1-isa-runtime-object-case-r1/`.
The later diagnostic link also passes the corrected scanner. Neither diagnostic run qualifies frame
performance, ordinary activation, the latest full x64/ARM64 matrix or P1 Done. No native ARM64 host is
available. Those requirements remain on the parent checklist.
