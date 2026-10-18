# P1 worktree preservation and storage recovery — 2026-10-18

Status: approved S5 cache relocation, all four preservation/readback gates and all four worktree archival/removal checks pass. The current opacity checkout stays active.
Source checkout: `codex/p1-opacity-host-proof` at `f51ef753f8054e5733ffa0742e17ed41cbb15cbd`.
This evidence does not close P1 or qualify a native build.

## Approved cache relocation

The user explicitly approved relocation of `S5SelectionCursorHostRelease-r1-Main` to D: and removal of the verified C: duplicate. Main rechecked the unchanged paired manifest SHA-256 `B6E36450D26DA25BCDE713DD5E4F51E985DAB63251A76D4F0DC15E830D0039D1`: 7,391 files, 7,031,301,120 bytes.

The original C: path remains accessible through an NTFS junction to:
`D:/RedPrismBuildCaches/.build/p1-polygonal-terminal-native-r1/ColdEvidenceRelocation-r1/S5SelectionCursorHostRelease-r1-Main`.
All source/destination hashes passed before relocation. The backup, original-path junction and D: destination passed independent extended-path byte checks before removing the exact approved physical C: backup. Final checks passed at both retained paths. Main verified the absolute deletion target and absence of nested reparse points before removal.

The first post-rename reader stopped on a path made longer than normal Win32 handling by the backup suffix. No deletion occurred. A subsequent malformed extended-path reader failed before execution. Both original logs/scripts are retained; corrected `verify-approved-relocation-r3.py` passes without changing any data or expected hash. The final receipt is `main-approved-relocation-complete.json`, completed 2026-10-18T08:15:47Z. C: then had 13,521,006,592 free bytes, exceeding the unchanged 6 GiB build preflight guard; D: also exceeded its 18 GiB guard.

## Old P1 worktrees

The four approved candidates contain local changes. Sampler and sampler-callers also have unique commits absent from the current branch. Preservation copies include complete physical checkout contents and ignored evidence, a binary tracked patch, exact initial Git status, HEAD history bundle, per-file size/SHA-256 manifest and independent readback of every archive member. Generated preservation products remain private under:
`D:/RedPrismBuildCaches/.build/P1ArchivedWorktrees-2026-10-18-r1`.

Archiving proceeds only after each preservation receipt passes. The managed archive additionally stores recoverable Git snapshots of local and unpushed work. No claim that archived changes are integrated or tested follows from preservation.

### Verified preservation checkpoints

| Checkout | Files | Bytes | D: preservation cohort | Managed archive request |
| --- | ---: | ---: | --- | --- |
| `p1-io-durability` | 618 | 13,843,865 | `P1ArchivedWorktrees-2026-10-18-r1` | queued; completion not yet verified |
| `p1-sampler-callers` | 1,215 | 22,653,539 | `P1ArchivedWorktrees-2026-10-18-r2` | queued; completion not yet verified |
| `p1-wand-host-modes` | 1,239 | 23,323,247 | `P1ArchivedWorktrees-2026-10-18-r2` | queued; completion not yet verified |
| `p1-sampler` | 459,643 | 10,497,802,726 | `P1ArchivedWorktrees-2026-10-18-r1` | queued; completion not yet verified |

The unique sampler-callers commit `3f49ea231fe538c69eaca0cbba3393438952f515` is in its verified recovery bundle. The sampler commit `8f5110dfe758a8e6b87a1cb26feea640e35a0de7` is listed in the sampler preservation inputs. The complete binary patches and untracked files are retained separately from these commit identities.

Each first three `main-preservation-qualified.json` receipts records the archive, manifest and bundle SHA-256 identities. Archive and bundle verification are read-only and leave original Git status unchanged. A queued app request is not reported as completed archival; this distinction remains in the opening checklist.

### Completed sampler preservation and retained reader refusal

The completed 11,190,487,040-byte TAR includes all 459,643 physical files from the sampler checkout, representing 10,497,802,726 source bytes. The initial regular-file-only reader refuses the first NTFS hard link at archive member 1,586; original logs, manifest and TAR remain unchanged. No source file is removed and no copy/build/runtime is repeated.

The corrected independent reader resolves each backward hard link to an already verified archived payload, requires matching declared source size, and hashes the current source alias against that payload. It verifies all 340 hard links and every regular member, with exact counts, unchanged binary Git patch/status, and a verified HEAD-history bundle. Corrected hash metadata is a new file; the original manifest remains intact.

- TAR SHA-256: `A8F5F1F16F8915525787A05CD8A13E2752E2F2F273EC3A7B33396E5826AB7606`.
- Corrected manifest SHA-256: `8B9A42E4E92F54EE225A80B7FBE0AABF1C97D1DE063BCFDC8B0910DC498D7240`.
- Bundle SHA-256: `3C697B2D971A2538212D67C965C280C0005D64EAEB9FDD6796273717602FD408`.
- Qualified receipt: `p1-sampler/main-preservation-qualified-r2.json`, 2026-10-18T08:46:16Z.

All four preservation gates now pass: **462,715 files / 10,557,623,377 source bytes**. The source changes are preserved, not automatically adopted into the current P1 implementation.

### Managed archive tool limitation

Main submits the four exact attached identities to `archive_worktree` only after their corresponding preservation gates pass. The tool returns `queued`; live `list_artifacts` and `git worktree list` still show the four active attachments/checkouts. Main therefore leaves managed archiving unchecked. Read-only inspection of the installed app shows that its wrapper returns `queued` for every non-error response, which does not establish successful removal. The current app log confirms tool delivery; unrelated historic cleanup errors cannot establish the cause for these four requests. No app state/database is edited and no physical deletion is used to simulate a completed managed archive.

A concise user request asks for the result/error of the app's own Archive action on the fully preserved I/O checkout, while native qualification continues. The current opacity checkout, its branch, source and test evidence remain active.

### User UI failure and exact empty-directory cleanup

The user reports that the app's I/O removal action shows an error. The installed app log records `settings-delete-targeted` at 2026-10-18T09:15:07Z. Recovery snapshot creation succeeds before removal: commit `2d37d804995205bcff55b1e952f050a06ab0de75`, ref `refs/codex/snapshots/ffd7b72e3a00a487fb1529d909a50eacd9baca8e`. The following Git removal exits 255 and the fallback reports `EBUSY: resource busy or locked, rmdir 'C:\Users\eric\.codex\worktrees\p1-io-durability\RedPrism'`. The displayed error is “Worktree removal failed after saving its recovery snapshot.” The log alone does not identify the process responsible for the lock.

Main independently verifies the saved commit, observes that the checkout is absent from Git's worktree roster and that only its empty directory remains, checks its exact absolute path and absence of a reparse point, then removes that empty directory from `Z:/src/RedPrism`. This succeeds. No recursive deletion, process termination, current-checkout modification or application-state/database edit occurs. The independent D: TAR, binary patch, manifest and bundle remain preserved.

A subsequent managed archive call refuses with “This worktree is owned by another task.” Its read-only owner lookup now fails because the checkout no longer exists; this is not evidence of an actual ownership transfer. The app still lists the I/O attachment, so managed archive completion remains unchecked. Main requests a retry from the card's Archive action, records the distinction between physical removal and sidebar recovery metadata, and continues the existing ARM64 cohort. The other three checkouts retain their original current-chat ownership metadata and are preserved; another Wand archive request still returns `queued`.
### Completed managed archival

Read-only attachment ownership checks identify duplicate references on this chat's closed child agents: 35 stale attachment records across the four approved old roots. The normal reassociation tool twice returns “Server overloaded”; the installed app-server control proxy also refuses its socket connection. These failures are retained.

Main generates the installed app's experimental protocol schema, backs up every affected child attachment, verifies that each child belongs to this parent and has `closed` spawn status, then removes only those 35 duplicate references through supported `thread/attachment/remove` requests on a temporary local stdio app-server. All four parent attachments and the active opacity checkout are preserved. Owner lookup then confirms exactly this parent remains for each approved root. No SQLite or application-state file is edited directly, and no chat or agent is restarted.

The normal managed archive tool then archives Wand, sampler-callers and sampler successfully. The large sampler physical deletion completes separately. For I/O, the app's already-saved snapshot and absent checkout are independently reverified; Main backs up its stale active payload, uses supported attachment add/remove requests to retain that exact payload as archived recovery metadata, and removes the obsolete active attachment. This completes the app's interrupted metadata transition without recreating or deleting another checkout.

Live `list_artifacts` now reports all four as `archived_worktree`; all four physical directories and Git worktree registrations are absent. The user independently confirms the I/O card disappeared. `p1-opacity-host-proof` is the sole active attached worktree and remains clean. All D: preservation cohorts, unique-commit bundles and ignored test evidence remain retained. The earlier queued/EBUSY failures above describe the preserved failure path, not the final state.

Supported-API receipts and exact request/response journals are retained under `P1ArchivedWorktrees-2026-10-18-r2`: `closed-child-api-cleanup-r1.complete.json`, its attachment backup/journal, and `io-parent-api-recovery-r1.complete.json` with its original payload backup/journal. This closes only the requested cleanup; P1's implementation and validation checklist remains WIP.