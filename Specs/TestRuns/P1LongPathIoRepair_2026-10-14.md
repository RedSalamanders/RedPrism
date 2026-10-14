# P1 long-path I/O repair — 2026-10-14

Status: **private actual x64 real ASan scoped PASS**. The reviewed code/tests are selectively adopted;
canonical native analysis, full profiles and remaining F6/G1 delivery gates are pending.

## Implemented behavior and independent coverage

`Win32FilePath` selects file/directory API thresholds, preserves extended/device spellings, resolves ordinary
parsing before adding a prefix and preserves aliased input/output. Exploded containment compares matching DOS
or UNC namespace spellings and retains original attribute-query spelling, including drive roots.

Project and PNG/JPEG staging retains the destination leaf when the generated suffix fits 255 UTF-16 units.
Otherwise a short GUID-bearing sibling stays in that exact parent. Exclusive creation and exact owned cleanup
preserve unrelated siblings; the existing `.old` recovery protocol, `.prism` schema and pixel rules remain.

The 1,823 native predecessors retain order and predicates. The 130 long-path facts preserve the original 57,
including all nine previously failed predicates, and add 16 helper controls, one actual volume guard and
56 real save controls. ZIP/folder/PNG/JPEG each exercise 233/234-unit leaves, initial publication, overwrite,
literal round-trip, reached publication failure, preserved previous content and untouched sentinel ownership.
The volume guard queries the actual volume root and accepts only fixed local NTFS/ReFS reporting 255.

## Actual runs

| Profile | PID | Exit | Ordered assertions | UTC interval |
| --- | ---: | ---: | ---: | --- |
| C: ordinary TMP | 61928 | 0 | 1,953 | 2026-10-14T01:36:25.781100+00:00 to 2026-10-14T01:36:32.585856+00:00 |
| Explicit Z: TMP | 93412 | 0 | 1,953 | 2026-10-14T01:36:32.979099+00:00 to 2026-10-14T01:36:39.298438+00:00 |

The second process retains its ordinary 311-unit TMP/TEMP spelling. New fixtures report 312 units with the
separator; the independent `GetTempPathW` observation remains 14. **The 1,823 predecessor fixtures keep their
OS-selected C: fallback**; this evidence does not place those old cases on Z:.

All 84 I/O compile commands contain `/fsanitize=address`; the unchanged separate probe produces the actual
heap-use-after-free diagnostic. The linked ISA scan binds the executable/PDB and the unchanged runtime allowlist.
Main's strict reader verifies both exact streams, exit codes, single success footer, diagnostic roots, source,
dependency and product bookends. Fourteen rejection controls and nine original-failure mutations pass.

## Identity and preservation

- Candidate source tree: `206B9F2BE8670334CA6580203583D9C0FB3DE1939F7CC0F7BF525982CDE2EBC7` (1,361 files).
- Candidate lock: `A33F877A135558575AE8484123F20520886DD96F7C8B89194875F7D291AC616C`.
- Main qualifier: `CCCD66BBBAF11E0447E8BFA6E0336D2D4A705DFC01D9E045FAD0AB1A75774371`.
- Archive: `Z:\CodexArtifacts\RedPrism-P1\2026-10-12\P1-LongPath-R7-private-passed-20261014`; 3036 files verified at source and destination before/after copying.
- Archive completion: `279B970C3C08997F61651EAA31E6AE4FEB31730D4E1658CB4183424E05874EA2`.

Original R4 directory-creation refusal, R5 nine failed predicates and R6 two fixture C2664 errors remain retained.
Main's fresh fixture uses explicit wide filename stems without changing any label, pixel, boundary or fault value.
The original metadata refusals also remain: PowerShell culture sorting and Python ordinal sorting produced different
hashes over the same bytes; the roster is a JSON list. Both corrections are independently checked before runtime.

## Remaining qualification

Native affected/full profiles and ARM64 cross-builds must validate the integrated source. Broader filesystem,
slow-I/O/cancellation latency, power-loss survival, all clipboard/session-end cases, ordinary host activation and
P1 performance acceptance remain open. P1 stays WIP.
