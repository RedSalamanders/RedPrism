# Inline switch table proof — 2026-09-30

Writer qualification exposed two compiler-emitted data forms absent from the instruction scanner: DxUi
typography's `MOVZX EAX,DL` after an RCX image-base load, and WIC orientation's RDX-base/RCX-target dispatch.
The parser recognizes only the exact contiguous forms and requires an anonymous same-owner untyped
16-byte PDB data record, aligned PE table, same contribution and decoded in-body targets. It does not exempt
an object or procedure, and the runtime allowlist is unchanged.

The rejected WIC row at `0x140091138` decodes linearly as `vpunpckhdq`, but is the first two of seven DWORD RVAs.
The dispatch at `0x14009011c` loads that table, adds the image base and jumps. Matching PDB data begins at
section 1 offset 590136. After the correction the same Release executable passes: 414 runtime vector
instructions, 10 verified table-data rows, zero violations. The report is
`.build/p1-project-save-rdx-scan/report.json`; executable SHA256
`8E313B778FF1BFB3803DEC245FC55C216B400862AAB7B6756322AAE06B4DA3A2`.
Allowlist SHA256 `6F55A40AB0CE23E98BB10839EC68765531129EF375B00DD29B298C3DC97EE560`.

All 157 standalone parser controls pass, including 29 RDX-form cases. They retain changed register/opcode,
noncontiguous/extra load, invalid PDB record/owner/start, contribution/target bounds and real-vector negatives.
Log: `.build/p1-save-rdx-parser-controls.log`. An initial positive fixture used the wrong PDB record size;
it was corrected to the observed 16-byte symbol rather than broadening the parser.

Full product/configuration validation is recorded with the subsequent writer qualification. These controls
prove the bounded scanner forms; they do not qualify third-party DLL execution on baseline hardware.
