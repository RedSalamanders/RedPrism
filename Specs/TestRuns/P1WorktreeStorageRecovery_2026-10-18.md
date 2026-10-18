# P1 worktree preservation and storage recovery — 2026-10-18

Status: approved S5 cache relocation complete; four-worktree preservation/archiving in progress.
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
| `p1-sampler` | 459,643 expected | 10,497,802,726 expected | `P1ArchivedWorktrees-2026-10-18-r1` | preserve and independently read back before requesting |

The unique sampler-callers commit `3f49ea231fe538c69eaca0cbba3393438952f515` is in its verified recovery bundle. The sampler commit `8f5110dfe758a8e6b87a1cb26feea640e35a0de7` is listed in the sampler preservation inputs. The complete binary patches and untracked files are retained separately from these commit identities.

Each first three `main-preservation-qualified.json` receipts records the archive, manifest and bundle SHA-256 identities. Archive and bundle verification are read-only and leave original Git status unchanged. A queued app request is not reported as completed archival; this distinction remains in the opening checklist.
