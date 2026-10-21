# P1 unsigned wide-stack switch-table proof — 2026-10-12

The native ASan r967 build stopped at its ISA scan before runtime. Four decoded vector-looking rows
belong to fifteen inline DWORD targets in DxUi.Menu.obj. The reviewed correction proves the exact
21-byte unsigned stack guard and contiguous 19-byte image-relative dispatch, complete known body/RET,
PDB/contribution extent and target boundaries. Only guard-derived data bytes receive an exemption;
no runtime/tier/object/function/address allowlist changes. Prefix and LOOP-family direct transfers into
the table also refuse. Candidate row collection supplies proof inputs and grants no exemption itself.

The original failed image SHA-256 is `C162C580CB78CC88A583B49C6625ABD6A36316F2E89DA470425A611DB2BB3DBD`,
PDB `684525CCCC3FFDA151EDDCF1897530AE73D7BFB980C5CAF21F7D961148239A0C`. All seven original inputs are
bookended. The r2 strict rescan reports 11,690 runtime vectors, zero tier vectors, sixty inline-data byte
rows and no violations. The complete failed archive is byte-verified at
`Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LayerRowsAccounting-ASanDebug-r967-failed-complete-20261012`;
completion hash `5F649F7A680986A881ED6D8D6390ABA17E5ADC9E70530F62F21E3CC3B8D57C5C`.

Independent synthetic controls retain all predecessor controls and prove three accepted maxima (1,14,127)
and 27 malformed/real-vector refusals. Main corrected the cheaper fixture's PDB colons, decimal section
offset, overlapping-row address and vector-looking target bytes before adopting it. Earlier fixture
refusals are preserved. Main's added transfer-listing formatting error is also retained and corrected
without changing any bytes or expected result. PowerShell 7 is the successful control runner; the optional
Windows PowerShell 5.1 experiment cannot compile pre-existing scanner local functions and is not a PASS.

Native integration r970 is a failed build, not sanitizer evidence: actual PID59296 exits1 at
21:46:00.530045 UTC, before runtime, because Main misspelled the point test registration as
`RunBoundedLassoPointsTests`. The declared entrypoint is `RunBoundedLassoPointTests`; only that call is
corrected. Its complete failed archive has 1,688 byte-verified copies, completion hash
`9CED9E9460C2A8A8BF32229ED640B8EA12FD2AB4D6E4F4BA5A7F8CAF0D114975`.
Fresh native build/ISA, actual sanitizer suites and real detection probe remain required.
