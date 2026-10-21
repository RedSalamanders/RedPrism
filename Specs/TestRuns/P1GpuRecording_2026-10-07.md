# P1 private GPU recording qualification

Date: 2026-10-07. Workspace: `C:\Users\eric\.codex\worktrees\p1-opacity-host-proof\RedPrism`.
Branch: `codex/p1-opacity-host-proof`, predecessor `077354851beb844e0afcd7ce290efdadf81b8fcc`.
DxUi remains `8e8994079e82f79e6765e6a885c03fc687957591` (API 3).

## Checklist and scope

- [x] Integrate the explicitly enabled single recorder lane, two-slot exact-lease command stream and stable owner pump.
- [x] Pass the complete canonical Debug r330 command and preserve its source, products and complete output.
- [x] Exercise real original WARP completion, failed signal/recovery and dormant quarantine followed by joined shutdown.
- [ ] Qualify the subsequent actual-device refusal, CPU/recorder/CPU same-record handoff and reservation-growth additions.
- [ ] Pass Release, real ASan, analysis and all three ARM64 cross-builds on that same final revised source.
- [ ] Activate complete admitted display/stroke graphs and qualify their application scenarios and G1 separately.

This mechanism establishes no complete image-graph admission, final display publication, first-display/G1 or RAW/Photoshop throughput claim.
No test expectation or legacy golden was weakened. ARM64 builds are compilation evidence only.

## Full Debug r330 — original 945-input candidate

The canonical launcher invoked the full `test.ps1 -Platform x64 -Configuration Debug` path, with no reduced suite
or skipped gates. Actual owned child PID 45396 ran from `2026-10-07T18:55:13.682176Z` to
`2026-10-07T19:06:15.396251Z`, exit 0. The source freeze has 945 inputs and SHA-256
`D2D6DC34BFCAC3B63F87B3A901BF0630892A26E6EFAB22A1D4998E3F3900AF28`.

The actual output contains 45 `recording-lane.*`, 87 `recorded-display.*` exact-transport and 69
`recording-pump.*` passing controls, with zero failures in those groups. All console suites, the complete hidden
WARP application self-test, ISA controls and build-process gates pass. The archive contains 102 application stages.
The `recorded-display.*` label group here tests transport and completion; it does not mean the application uses
the experimental recorded display graph.

| Evidence | SHA-256 |
| --- | --- |
| Actual stdout | `8A96E3F4735C646C7171099B4A1CAF144196844E73D95248C812048C199AF644` |
| Actual stderr | `DD0CFC685D290B5F489159DF1EFFD298DB5656AE7595F058EE0AF0358E0C1DBF` |
| Source archive manifest | `9539A8EA194A2A0B8B143E060AE76C0A16E8DA5757B8F802E355C255E6BEF198` |
| Evidence archive manifest | `F30D29086F5D2C53212F428691E99A1DC8030B46C3CBBD3B9181525B5CC5792D` |
| Archive completion | `9B07DDC76827D2DC37601991B941B0F4AE9796D0218D650AAED08AC76CBCBB6C` |

Archive: `D:\RedPrismQualificationArchives\P1-recording-945-Debug330-qualified-2026-10-07`.
All 1,242 copies verify at `2026-10-07T19:11:09.5400975Z`. The freeze, binaries, PDBs, dependency artifacts,
ISA output, complete test logs and original launcher/child evidence are retained there before source changes.

## Review-driven follow-up

The parent source's scheduler/ticket/numeric generation did not establish actual command-device identity.
The pump now calls `GetDevice` on the borrowed command list before Begin/Execute and compares it with the actual
queue-captured device. A second real WARP device with the same numeric generation must refuse with `E_INVALIDARG`,
zero executed acknowledgements, a retained original charge and joined worker retirement.

The next source adds same-ticket execution-lane handoffs and complete later-phase reservation growth. Their
state references observe actual stage acknowledgements, event gates, original tickets/charges, FIFO, worker
threads, fresh memory snapshots and final retained-memory transfer. These additions are not covered by r330.

## Full Debug r331 — preserved fixture failure

Actual child PID 104128 ran `2026-10-07T19:43:56.677858Z`–`19:51:07.511273Z`, exit 1, on 949 inputs,
freeze `90AC745411E86AAF4AF0EE5ABE1372A8E0CFCAD92D5A8AE699CC83DCF27C5509`.
`HostTests` failed only `execution-lane-stages.retained-original-charge-refunds-after-explicit-release`.
All reservation-growth controls and every other lane-stage control pass; later suites/application stages were
not reached. This attempt proves no complete-profile pass or GPU-pump result for its new source.

`ReleaseRetainedMemory` returns the bytes synchronously and calls `NotifyMemoryChanged`. Consequently
`HasUiWork` stays true until the real outer drain consumes that transition; immediate `IsIdle` was the wrong
fixture sequence. The repair adds one `DrainCompletions` after release and preserves the original zero-byte
and settled-idle expectation. No production code, outcome threshold or legacy golden changes.

Archive: `D:\RedPrismQualificationArchives\P1-recording-949-Debug331-failed-2026-10-07`.
All 1,246 copies verify at `2026-10-07T19:55:59.2312526Z`, with zero application stages.
Source manifest `116D3AD4C97DA7FF44968218A50126EA4E456FCECDC8B2DD46443CEDD24E53C7`;
evidence manifest `F576F20C76452B2E4AFE34824E90FDE778C3E5222B819E8940C73D808D67E478`;
completion `18B3E995BF4D44252D2117EB0121364786AC0D01A7E30289D8E8AAB32553B8CD`.
Original stdout `37447448DBB80A281CD0E47CEE11E6668457B22B62E93BC9131BF88DA817D6D8`;
stderr `A157B19E988824307C5FB4B67F8F1C7F2C50C767D44EC32F6E157EC93FA2D27C`.
Fresh qualification of the repaired source remains required.

## Full Debug r332 — preserved live-host prerequisite failure

Actual child PID 119336 ran `2026-10-07T19:56:51.688691Z`–`20:04:14.612581Z`, exit 1, on 949 inputs,
freeze `BD2CCF322A71B3E4638F2B0E4B8B89843018552346D0AC0406FD319DEA6D101F`.
All console suites pass, including the repaired lane fixture, growth controls and actual foreign-device pump
refusal. The hidden application stops in `RunStandaloneLiveShutdownSelfTest`: `Clone-after-preparation`
does not reach its lifecycle prerequisite gate within the existing ten-second deadlock bound. This is before
starting the live Clone draft, not a failed Clone pixel expectation. No root cause is yet established.

The failed attempt is archived in `D:\RedPrismQualificationArchives\P1-recording-949-Debug332-failed-2026-10-07`:
1,246 verified copies, 13 reached application stages, verification `2026-10-07T20:05:58.1260030Z`.
Source manifest `8B91CA094871334ACE29322B565AAE21AA92F31D8FCA333A6451187ADF01F144`;
evidence manifest `DC289A3F5060D0BB4B5AEA041E448CA7C1F7F9708F664B68C1777AC3F77C53F2`;
completion `66AFBDE7F481733C80139FC976D3954E0A56357452A4FC212E2CE6ACFBFE2A37`.
Stdout `A63B6069B917A2DEB648851E5497681752C076E760F928CF78AE90A233355EAE`;
stderr `D1372F4AEF62B2C7AC6DF010C0387FF4F21B5F21F0CAF443DEA1D4CE832B50DB`.

The next fixture revision prints reached gate/capture/admission/host/history/readiness/error state before that
same deadlock exit. It changes no expectation, timeout, production code or gate. A later unchanged pass alone
does not explain this failure. Qualification and diagnosis remain open.

## Full Debug r333 — diagnostic revision passes

The unchanged full canonical command passes on 949 frozen inputs, actual child PID 54500,
`2026-10-07T20:09:05.749150Z`–`20:18:28.663917Z`, exit 0. Freeze SHA-256:
`302725B23AE0BBBAC60D3498EF52CE506A6F80A6E3ABDE4BE45D10ADCCB5707A`.
All console suites and 102 application stages pass; the actual live-shutdown group has 432 checks and zero
failures. This proves the revised Debug profile, including the new phase/growth/device controls. It does not
establish the cause of r332's intermittent prerequisite timeout or the remaining profile/activation/G1 gates.

Archive: `D:\RedPrismQualificationArchives\P1-recording-949-Debug333-qualified-2026-10-07`;
1,248 copies verify at `2026-10-07T20:18:53.9171259Z`.

| Evidence | SHA-256 |
| --- | --- |
| Actual stdout | `945BD8F18927C47637B40D5727879E4CA078FC90F2513EE679F5496D183137E8` |
| Actual stderr | `858662DFF58527E9C98EA873AEF9CC18F3A02034851B24C20F60BC20D0619EDE` |
| Source archive manifest | `51D87B4B2629051CBAA0ABCA649F46C1226B0F93AD0401E8518A2F49C5A7CACE` |
| Evidence archive manifest | `5A247918AEB2B6DDC7381D56C15F8BF70918EC7D54BD58A62F707480A0C5766E` |
| Archive completion | `89DF56532CD6E64E4EF6887765D5C33CDB5D21463359E1250751D130C6A8068C` |

The separately compiled display parent remains an experiment. Its review repairs preserve ready owner
transitions after the original notification drains, avoid a worker read of owner-written failure state, select
the actual draft header before the production header and compile the missing brush shader from current source.
The inventory must include the selected SDK's actual headers/libraries and extensionless standard headers;
silently skipping an unresolved SDK root is not an exact-input proof. No application display activation is claimed.
