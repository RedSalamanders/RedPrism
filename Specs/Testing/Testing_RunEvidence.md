# Generated test-run evidence

Explicit local SDK application execution uses the same create-only, owned and
byte/metadata-verified retention under `.build/evidence/local-sdk-test-runs`.
Its `local-sdk-runtime.json` has role `LocalUnpinnedApplicationRuntimeResult`, the
original build-record digest and source/engine/dependency/runtime identities,
`testsExecuted=true`, and false repository/production qualification flags. Only
actually completed selected Runtime/Captures scopes count. The original runtime
input must pass close verification before a successful receipt is published.
Ordinary `test-runs`, `normal-suites.json` and iteration evidence remain separate.

Status: normative current tooling contract
Last reviewed: 2026-11-09

After argument, desktop and build preflight, native test execution writes each invocation under `.build/evidence/test-runs/<UTC-pid-guid>/payload`.
It creates a new directory and exclusive owner lease; it never reuses an old run directory or sweeps older runs.
A flushed, create-only `started.json` records the owner process and start time plus the requested profile/scopes.
The `completed.json` record is staged, flushed, independently read back and renamed without overwrite only after
payload verification. A failed or interrupted invocation retains its output. Forced termination can leave only
`started.json` and partial data; absence of a valid completion record is never a successful result.

Verification includes hidden files, long Unicode paths, ordinary file contents and named data streams (SHA-256),
128-bit file identities, attributes, creation/write timestamps and owner/group/DACL metadata. Existing reparse
ancestors and payload links are refused without traversing or deleting their targets. Directory identity is freshly
checked; sharing flags alone do not prevent all ReFS directory renames. Files still open for writing fail verification.
Unique evidence stays in place; no archive payload copy, attribute normalization or read-only clearing occurs.
If native execution already failed or timed out, finalization failure is reported separately and preserves the
original exception and unsuccessful result. If execution succeeded, finalization failure remains fatal. An
active writer still prevents verified completion; preserving the primary diagnostic does not relax file pins,
metadata/stream checks, process ownership or any timeout. Failed and partial raw evidence remains retained.
The hidden rendering self-test emits ordered BEGIN/END markers around each fixture replacement, frame and
GPU readback, plus device-recovery stage markers. Its existing unbuffered stdout preserves the last entered
operation during a hosted timeout. These markers supply diagnosis only: assertions, selected work and failure
bounds remain unchanged, and an entered operation is never counted as completed coverage.
The application self-test's ordered stage markers additionally report monotonic elapsed milliseconds
from its entry. Final hidden-chrome/resource/close observations distinguish body completion from
shutdown, whose separate BEGIN/END includes elapsed time. These fields diagnose slow progress or
an unfinished exit; they do not raise failure bounds, skip assertions or establish performance acceptance.
Shared build/dependency/qualification files remain outside the writable run, and their existing read-only rules apply.

The record establishes retention and the outcome of the selected invocation only. It does not replace exact source,
binary/dependency/profile/scenario attestation, qualify omitted tests, or turn a capability skip, pending CI check,
cross-build, synthetic probe or failing comparison into native/product qualification. Sandbox markers, path containment,
process ownership, watchdog, desktop consent and ReFS capability checks remain required by their owning contracts.

Generated evidence stays ignored by Git. Normal builds do not sweep it. CI collects the new evidence path, including
hidden files, where the branch has an enabled native artifact workflow. Transport retention and stream support are
separate from lossless local evidence. Historical cleanup requires a separate inventory, liveness check and verified
same-volume move or full independent copy verification; a timestamp or dead PID alone never authorizes deletion.
RedPrism retains isolated crash dumps and pending markers, native suite logs, command-line/benchmark/ISA diagnostics
and harness screenshots in the run instead of deleting dumps or overwriting the previous run. Qualification archive
payload sharing is a separate workflow and is not modified by test-result retention.

Build logs also use fresh timestamp/GUID names under `.build/logs` so a later build cannot overwrite a failure diagnosis.

Inventory attributes and creation/write times come from pinned file handles, rather than cached directory entries.
Compare inventories in canonical relative-path and stream-name order without relaxing any identity, hash, attribute,
time or ACL check. Native moves retain the exact intent and observed post-move inventories before comparison; a
mismatch retains both snapshots and the payload and refuses completion. Independent regression controls keep
stale provider metadata and reverse enumeration while verifying the actual file and directory metadata exactly.
