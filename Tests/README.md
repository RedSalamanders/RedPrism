
## Affected testing

Use `.\Test-Changes.ps1 -Explain` from the repository root to see selected scopes, paths and reasons, then `.\Test-Changes.ps1` to execute. `-Scopes A,B` is an explicit partial selection; `-Mode Full` and `-Mode PrePush` retain full applicable obligations. `-Force` bypasses exact result reuse. The next ordinary edit compares against the last successful default affected run, not every older change on the branch. Read [the validation contract](../Specs/Testing/Testing_Validation.md) for mutation barriers, CI_PENDING and qualification limits.

Hidden WARP WAND startup failures retain a named job-settlement category on redirected stderr. The timeout and failing exit status are unchanged; preserve the original run evidence when rerunning a failure.

RedMonitor controls verify the two vendored files entirely offline: exact bytes, missing/tampered inputs, invalid locks and reparse paths. No source checkout or viewer build is required. Actual application --redmonitor-probe tests cover cross-process Unicode/severity/PID/TID; installer closure tests belong to RedMonitor.
