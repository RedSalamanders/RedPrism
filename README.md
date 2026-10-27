
## Recording changes

Follow the [Changes workflow](Changes/README.md): validate while editing, then fold before PrePush/final handoff and immediately after integration, even for one fragment.

## Testing

Use `./Test-Changes.ps1 -Explain` to inspect affected scopes, then `./Test-Changes.ps1` to run them.
The default advances from its last successful run; unchanged input selects no tests. Explicit partial
selections and failures do not advance that baseline. Exact successful evidence can be reused.

Use `./Test-Changes.ps1 -Mode PrePush` to account for local and matching PR obligations without
repeating identical CI tests; pending CI is not a pass. `-Mode Full` remains the explicit full-profile run.
See the [test guide](Tests/README.md) and [validation contract](Specs/Testing/Testing_Validation.md).

Builds require the default MSVC **14.52.x** tools (v145). Release uses /Gy /GL and final /OPT:REF /LTCG; archive and final-link tools must match exactly. The build contract documents the preserved API pin, explicit archive forwarding and policy-specific output identity.
