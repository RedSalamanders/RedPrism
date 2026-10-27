# Header-only RedMonitor adoption — 2026-10-23

All six x64/ARM64 Debug, Release and ASan Debug builds passed through the normal wrappers. All three x64 Full
profiles passed Captures, Composition, Document, Host, IO, Performance, Runtime and Tooling; ASan also passed
Stress and the real AddressSanitizer detection probe. Existing Pipeline tracing remains independent.

The actual application ETW and absent-helper probes passed in all three profiles. Final executable hashes
match those observations. The portable ZIP passed clean extraction, help, hidden WARP and version checks,
contains the exact vendored MIT notice, and has no viewer executable or satellites. Normal builds verify only
the pinned local header/license and never build RedMonitor.

See header-client-validation.json for source, dependency, artifact and run identities. Logs remain under .build
as header-{platform}-{configuration}-build.log and header-x64-{configuration}-test.log (configuration spaces
are hyphens). Runtime evidence is retained by the canonical run archive. Original failures and cancelled
checkpoints are not promoted to this qualification.

ARM64 results here are cross-builds. Native execution, physical input/IME/AT, paired Release performance and
publication remain WIP gates. The short Performance smoke checks establish harness behavior, not non-regression.
