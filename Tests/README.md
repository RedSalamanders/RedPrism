
## Affected testing

Use `./Test-Changes.ps1 -Explain` from the repository root to see selected scopes, paths and reasons, then
`./Test-Changes.ps1` to execute. `-Scopes A,B` selects specific scopes. Full covers one profile, and PrePush
covers three x64 runtime profiles and three ARM64 build-only profiles. `-Force` bypasses exact result reuse.
The affected baseline lasts up to 24 hours. The shared runner lists IO and Runtime separately because both
use the session clipboard. Read the [validation contract](../Specs/Testing/Testing_Validation.md).

Hidden WARP WAND startup failures retain a named job-settlement category on redirected stderr. The timeout and failing exit status are unchanged; preserve the original run evidence when rerunning a failure.

RedMonitor controls verify the two vendored files entirely offline: exact bytes, missing/tampered inputs, invalid locks and reparse paths. No source checkout or viewer build is required. Actual application --redmonitor-probe tests cover cross-process Unicode/severity/PID/TID; installer closure tests belong to RedMonitor.
