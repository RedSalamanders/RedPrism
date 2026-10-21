# P1 pending display transport contention

Status: focused Debug Application qualified; full profiles, ordinary activation and G1 remain open

## Scenario and independent expectations

The actual hidden WARP Application opens Exposure on a literal 2×2 gray image. Its real recorded command executes,
then both completion-enqueue attempts are refused. The original source becomes dormant quarantine. Accepted Cancel
must acknowledge its own closure receipt while that unrelated GPU source stays retained. A committed opacity edit
then executes and enters a second dormant quarantine. The real Brush host request and pointer Down/Up must reach
the third original recorder attachment and report `HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)`, without publishing
pixels or history. Joined shutdown must retire both original quarantined sources and all held admissions.

The test observes fixed source slots without acknowledging, detaching or mutating them. Receipt waits dispatch
messages, completions and existing UI actions and block on the scheduler wake event. Their 15-second bounds detect
deadlock; they are not latency targets. No synthetic acknowledgement or global settle is used after quarantine.

## Preserved attempts

Times are UTC recorder bounds, not G1 or kernel elapsed-time evidence. Archives are under
`D:\RedPrismQualificationArchives\`; copied sources, products, logs and actual-child receipts were independently
rehashed by the main agent using `.build/P1Drafts/verify_checkpoint_archive.py`.

| Attempt | Actual child PID | UTC start / completion | Exit | Freeze SHA-256 | Archive |
| --- | ---: | --- | ---: | --- | --- |
| Debug build r443 | 39752 | 10:41:51.408809 / 10:46:53.388044 | 0 | `A199F29656AA6997D5C3F8357C86C80B36C18B35097081AED7D16293039560AB` | `P1-pending-transport-1015-Debug443-build-qualified-2026-10-08` |
| Application r444 | 63916 | 10:48:28.159166 / 10:49:25.111440 | 5 | `A199F29656AA6997D5C3F8357C86C80B36C18B35097081AED7D16293039560AB` | `P1-pending-transport-1015-Application444-failed-2026-10-08` |
| Diagnostic Debug build r445 | 106588 | 10:57:07.060334 / 11:01:25.285354 | 0 | `5DE1822771BB54DA5026BFA45AED01D3FBD455E0CCABA960A74DB5F45E962DAE` | `P1-pending-transport-1015-Debug445-diagnostic-build-2026-10-08` |
| Diagnostic Application r446 | 107280 | 11:01:58.777982 / 11:02:55.696498 | 5 | `5DE1822771BB54DA5026BFA45AED01D3FBD455E0CCABA960A74DB5F45E962DAE` | `P1-pending-transport-1015-Application446-diagnostic-failed-2026-10-08` |
| First prerequisite build r447 | 78784 | 11:03:52.210097 / 11:08:12.645038 | 0 | `361769CE3434DA73AE3512EB145F1E0FA0B62700082EFF7E5DA06E0AE1A4011B` | `P1-pending-transport-1015-Debug447-build-qualified-2026-10-08` |
| Application r448 | 39092 | 11:09:06.222297 / 11:10:03.133384 | 5 | `361769CE3434DA73AE3512EB145F1E0FA0B62700082EFF7E5DA06E0AE1A4011B` | `P1-pending-transport-1015-Application448-rows-order-failed-2026-10-08` |
| Corrected Debug build r449 | 108504 | 11:11:28.993769 / 11:15:52.752449 | 0 | `6C174F9563EA73FDA5F111B6F51FC5343C8242CD5E23E69CAC9B3D9ABCF55F48` | `P1-pending-transport-1015-Debug449-build-qualified-2026-10-08` |
| Corrected Application r450 | 75740 | 11:16:45.848525 / 11:18:48.725201 | 0 | `6C174F9563EA73FDA5F111B6F51FC5343C8242CD5E23E69CAC9B3D9ABCF55F48` | `P1-pending-transport-1015-Application450-qualified-2026-10-08` |

All eight preserved attempts freeze 1,015 inputs. r444/r446 reached stage 15 and preserved 12 successful transport
assertions before the third-attachment result assertion failed. r446 reports outcome Failed and error
`0x8000ffff` (`E_UNEXPECTED`), with both sources still attached and quarantined. Its executable hash remained
`7AC030622D59E5D57415E019772EF09F9F441F4E7AFC2383DDD29866480B0E18` throughout the actual process.

## Correction basis

`Application::StartStrokeProcessing` explicitly refuses without `_preparedLayerRowsEnabled`. The fixture enabled
recorded display and asynchronous stroke processing but omitted `EnablePreparedLayerRows()`. Existing actual stroke
sample fixtures enable that prerequisite. The correction adds it to this fixture's initialization; the expected
capacity HRESULT is unchanged. Diagnostic output prints the actual receipt and source counts only on mismatch.

The first correction in r447/r448 enabled rows before creating ChromeHost; `EnablePreparedLayerRows` refuses that
ordering. r448 therefore failed the initialization assertion. The final r449/r450 fixture initializes chrome first,
then enables rows and attaches the stroke processing owner source, matching existing stroke fixtures.

## Qualified Debug result

r450 exits zero and passes all 112 actual Application stages. Its 15 transport assertions pass in their actual stage,
including the exact original capacity refusal, unchanged raster identity/history revision/undo count, dormant source
counts, and complete joined shutdown. Main source-pinned verification preserves all 4,984 predecessor
PASS/INFO/SUMMARY records from the qualified r439 Application log in their original order and multiplicity; the
candidate contains those records plus 15 assertions and one summary (5,000 records total).

The executable hash remained `D95D89E881D1B3DB09E5E4E9932559C4F924061BF78D8AD0EB9471AF9EBE2AE4` throughout the process.
Actual stdout hash is `E5D2F6ACAE750E315E09E90758E32FF967165CACF810623A13563DC13FF345A5`.
The independently rehashed archive completion is
`DCD20559ED2189681D00DD9762CD8039F69FC2620A2DD00AEE05422F431B676F`; source and evidence manifests are
`542D8F00447BCA05855D9200AE11DD478D48CC68F140F21D1D6442F316118088` and
`A2BFFDD07741894A75FD4BEBD7AC98E6F82FAB0DD20E3312656ECCB8AD3D2D72`.
Companion readback and exact inventory reports are in `.build/P1Drafts/PendingDisplayTransport-r6/Main-Application450-*.json`.

This qualifies the focused Debug Application scenario. It does not replace the full Debug/Release/ASan gates for the
next combined candidate, or establish remaining pending-tool coverage, ordinary activation or G1 latency.
