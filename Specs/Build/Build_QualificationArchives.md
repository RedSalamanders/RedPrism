# Qualification archives

Status: current normative repository contract
Owner: `archive-qualification.ps1`, `Build/QualificationArchive.psm1`, `Build/QualificationArchive.cs`

## Collection and qualification

Future qualification collectors MUST use `archive-qualification.ps1` for persistent archive storage instead of
copying complete source/product trees per attempt. Historical frozen helpers and archives remain immutable.
An older worktree can invoke the maintained checkout's entrypoint explicitly and record its writer/module/backend
hashes; adopting this storage tool does not update application sources, dependencies or qualification results.

The collector MUST retain its existing exact source freeze, expected input hashes, attempt identity, profile,
output enumeration, freshness intervals, stage checks, PE/PDB/build provenance and independent runtime readers.
It MUST not infer output ownership from a process name or stop an independently launched application. Storage
success establishes preservation only. Failed and skipped results MUST retain their actual status; earlier
failures MUST remain available after a later pass. These requirements also apply to custom/private collectors.

## Explicit input contract

`-ArchiveRoot` MUST be an existing local directory beneath a workspace or an explicitly authorized `<Project>.Perf`
root. The tool never initializes a drive root. The caller MUST choose a new `-ArchiveName`; completed and incomplete
names cannot be reused. There is no automatic retention sweep, garbage collection or recursive cleanup.

`-InputManifest` is a UTF-8 JSON array with these fields:

| Field | Meaning |
| --- | --- |
| `Path` | Absolute physical input, usually an already frozen source copy or an enumerated product/evidence file. |
| `RelativePath` | Logical destination within the role. Traversal, devices, streams, collisions and ambiguous names are rejected. |
| `Role` | Exactly `Sources` or `Evidence`. |
| `Length`, `Hash` | Expected byte length and SHA-256, supplied by the collector. Hashing the copy alone is insufficient. |
| `ProvenancePath` (optional) | Original logical source name when `Path` names a frozen copy. Original physical file identity is also retained. |

`-RunRecord` is the collector's JSON attempt record. It is copied verbatim alongside the original input manifest
under `Run/`. A minimal invocation is:

```powershell
# $archiveRoot already exists beneath an exact authorized root or workspace.
$receipt = .\archive-qualification.ps1 -ArchiveRoot $archiveRoot -ArchiveName $attemptName `
    -InputManifest $frozenInputManifest -RunRecord $attemptResult
$receipt | ConvertTo-Json -Depth 12
# Continue the collector's independent source/product/PE/PDB/profile/stage/runtime verification.
```

Collectors MUST include all useful run logs, traces, captures, validation output and supplements in the explicit
evidence list. This tool deliberately does not guess which mutable `.build` paths belong to the attempt. Private
readers adopting this schema MUST retain their semantic predicates rather than treating a completion hash as a pass.

## Immutable shared storage

Source payloads and Evidence `.exe`, `.pdb`, `.dll`, `.lib` and `.obj` payloads MAY share verified objects under
`<ArchiveRoot>/.payloads/v1/`. Run logs, captures, run manifests, attempt records and other evidence MUST have independent
per-run file objects, even when their bytes happen to match. Git stores and recovery bundles are never shared.
Inputs already having multiple hard links are copied independently. Inputs are never linked directly into the
archive. ReFS and other non-NTFS archive destinations use independent verified read-only copies.
Readable DACLs that deny hard-link creation also use independent verified read-only copies, preserving permissions.

Sharing requires matching actual SHA-256, length, last-write time, storage attributes and owner/group/DACL.
Source creation/access timestamps, attributes, identity and security are preserved as per-run manifest metadata;
shared file creation/access timestamps describe the retained object. SACL auditing metadata is outside this
sharing contract. Named streams and unsupported compressed/sparse/encrypted/offline or other storage attributes
are refused without changing the input. Different supported metadata starts another independent object.
Ownership that cannot be preserved without additional privilege fails closed; the tool never requests elevation
or weakens permissions. DACL inheritance state is checked after copying, as well as the ACEs.

The store MUST serialize writers with an exclusive filesystem handle, pin ancestors against replacement, reject
reparse points, hold read/no-write/no-delete leases during hashing/copy/link verification, fully rehash an existing
object before reuse and verify the resulting alias identity and content. Content names and prior receipts alone
are insufficient. A shared object MUST already be read-only before creating any run alias. Objects at 900 links
start another shard. NTFS's link-creation change to the Archive backup bit is restored without clearing read-only;
other unexpected attribute changes fail verification. No shared object is cleared read-only, overwritten or removed
during archive creation.

All archived files, including independent per-run records, are read-only. To edit or restore into a writable
workspace, copy to a new independent destination first, then clear read-only on that copy. Clearing read-only on
an archive alias changes the shared object. The archive root and `.payloads` belong to one retention unit: do not
prune either by age or rewrite manifests. Future pruning requires its own verified reference/recovery workflow.

## Publication and failures

Each run retains `archive-started.json`, raw `Run/` records, source/evidence/run manifests, and original hashes.
The writer records its script/module/backend hashes. Every archived path receives a separate full SHA-256/length
readback before `archive-completed.json` is written and flushed as the last publication marker. Completion binds
all three manifest hashes and the unchanged attempt data. The returned receipt binds completion.

Any error fails the command and leaves partial evidence intact with no valid completion marker; where possible
`archive-failed.json` records the reason. A process termination may leave only `archive-started.json` and a partial
object. It MUST never be interpreted as completion. Do not retry into that name, automatically clear attributes,
delete partial objects or bypass a corrupt object. Preserve the failed record, investigate/quarantine independently
with a verified recovery map, then use a new run name. A fully written orphan object may be reused only after the
same fresh content/metadata/read-only checks. No successful receipt can be emitted after a failed publication.

## Validation

`Tests/BuildProcessTests/QualificationArchiveTests.ps1` runs directly and through `validate.ps1` and `test.ps1`'s
Tooling scope, using the existing independent tooling discovery. Its retained filesystem
fixtures prove repeated-source/binary reuse without another payload copy, distinct attempts, independent identical
logs, unchanged failure status, completion bindings, ordinary-write refusal, independent copy-out, stale hashes,
partial preservation, archive reuse refusal, path/role/collision confinement, exclusive writers, different content,
same-size different bytes, length/time/DACL mismatches, streams, locked inputs, reparse parents, Unicode long paths,
the actual 900-link cap, original read-only and backup-bit state, protected DACL fallback, prelinked inputs,
writable shared-object refusal and corrupt-store refusal. Non-NTFS destination
fallback is separately exercised on the authorized ReFS workspace. No application or desktop focus is required.
These storage tests do not replace any native/platform/performance qualification gate.
