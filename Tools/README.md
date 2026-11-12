# Developer commands

Use the root build, test, validation and package entrypoints for ordinary qualification.

`Build-LocalSdkCandidate.ps1` explicitly compiles the Release application against a frozen local
DxUi SDK. Restore the selected app dependencies with `vcpkg-install.ps1` first. It reads those
dependencies and writes a fresh `.build/local-unpinned` operation with a live SDK/output lease,
diagnostic log and actual archive link proof. It never changes ordinary outputs or managed pins,
runs tests or ISA scans, packages, installs or publishes. The build record grants no qualification.

`test.ps1 -Configuration Release -SkipBuild -Suites Runtime,Captures
-LocalSdkBuildReceipt <original-record> -LocalSdkBuildSha256 <independent-hash>`
executes the existing application CLI/WARP and harness captures using held local SDK
inputs. Choose either or both explicit scopes. Source/API/ABI/engine/dependencies and
all runtime bytes are checked at entry and close. Local evidence stays separate from
ordinary iteration and cannot qualify a repository, native platform or production.
Full, standalone, sanitizer and hosted requests retain their ordinary gates.

The changelog commands validate fragments, or explicitly fold them through a separate pull request.
See `Changes/README.md` for their authoritative workflow.
