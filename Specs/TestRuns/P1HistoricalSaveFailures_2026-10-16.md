# Historical save failures: recovered evidence — 2026-10-16

## Scope

Main recovers the two original September 30 failed logs through their recorded tool-command pointers and
independently verifies byte-identical private readbacks. This closes the missing-log-location investigation;
it does not establish either failure's cause, qualify changed code, or close F6/P1. Neither original log has
a usable captured native-error diagnostic or an independently bound complete failed-source snapshot.

| Original log | Actual failed assertions | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `p1-save-pre-observer-Debug.log` | `save-durability.folder-first-save` | 233,458 | `30D570260161C9448B70A09736EE045DBDCEA583E05025F46B29F58026214747` |
| `p1-save-observer-io-2.log` | `project-cancel.publish-exploded.published-stage-visited`, `.returns-ok-after-stop`, `.publishes-new-literal-content` | 23,017 | `8670E976D473622EDB514E3AD5B4CD01BB320C1A722EC0D86DCB6E067C7739F2` |

The first log contains one failed assertion and its one-failure/zero-CRT-report footer. Its subsequent
folder rotation and round trip pass; that does not identify the initial failure. The second contains the
three listed failures and its three-failure/zero-CRT-report footer; fixture creation and unrelated-sentinel
preservation pass. It is the second standalone I/O attempt, before the later publication diagnostics.
Private originals and hash-verified copies remain available; raw machine paths/logs are not published here.

## Distinctions and remaining work

The separately retained `p1-cancel-first-Debug-save-outline-failure` is a corrected save-outline fixture
expectation. The `p1-cancel-second-Debug-io-failure` concerns `v7.save-exploded`. Neither replaces the two
logs above. Native error 5 from a separately instrumented publication attempt cannot be assigned to these
original failures. Later passing repetitions and injected fault coverage do not establish their causes.

Keep the F6 cause investigation open. Current publication code captures immediate rename errors before
reporting/cleanup; reached future failures must preserve those diagnostics and final destination, backup,
and staging outcomes under their actual source/product identity. No new acceptance gate is introduced.
