# P1 All Layers Wand readback device rebind qualification

Status: qualified for this readback recovery and scanner slice. P1 remains ACTIVE. Native activation, complete Add/Subtract boolean
memory admission, Clone All Layers and measured G1 remain open.

## Independent behavior reference

Two new actual-host modes exercise device retirement before copied-byte acknowledgement, without sleeps
or polling. The host posts an owner message after real utility-device preparation and before the real
readback request; the shared settlement loop services messages before the next GPU pump. The message
requires a pending GPU request with no mapped-row lease, then calls actual renderer recovery and composition
device rebinding. The test gate is absent in normal application execution.

- One device change restarts under the original job ticket, history stamp and aggregate reservation.
  The 2×2 literal left-region outline is published as one Magic Wand history entry. Undo/Redo recover the
  exact prior selection and recovered outline. Images, masks and authority remain unchanged while held.
- Three device changes exceed the existing two-restart limit and fail with `E_ABORT`. The exact prior
  selection/document/generation/revision/epoch and redo entry survive; no partial selection/history is
  published and no CPU selection gate is reached.
- Both paths release workers, maps, utility ownership, queue requests, original reservation and receipt
  ownership. All eighteen previous modes remain present. Seventeen new expectations bring the mode
  inventory to twenty and its named checks to 128.

These are state-machine expectations. They do not assert latency or qualify the unsupported Add/Subtract
host route. That route still needs a complete Clipper workspace/output bound before admission and activation.

## First 858-input matrix and preserved failure

All attempts below froze the same 858 inputs, manifest
`CED93AFBE05F101C7EFCE18C18702A07700AAB50AA73497D03590BF23D437379`.
The x64 timestamps in these recorder receipts delimit launch recording and completion recording;
the ARM64 helper separately records `GetProcessTimes` for the actual owned child.

| Attempt | Result | Evidence |
| --- | --- | --- |
| Full Debug r243, child 68288 | PASS | 10,048 application controls / 89 stages; exact predecessor and 17-case inventory proof |
| Full Release r246, child 79592 | PASS | Same complete registry and predecessor proof |
| Release analysis r244, child 29192 | PASS | Five linked images, ISA controls, compiler analysis and provenance |
| ARM64 Debug/Release/ASan Debug r34 | PASS cross-builds | Children 38516 / 95320 / 107100; identical frozen inputs; no native runtime claim |
| Full ASan Debug r245, child 106492 | FAILED before runtime | ISA scan interpreted inline DWORD-table bytes as one EVEX instruction; no app/sanitizer runtime result |

The seven attempt archives under `D:\RedPrismQualificationArchives\P1-wand-rebind-858-*` preserve
sources, binaries/PDBs, tracking, logs and independently read-back archive receipts. The failed ASan
archive completion hash is `BE4964E1916AFE5B652B8522E6A3E0A98ACE31415B73CA7425A25DD4BAB23DA3`.
An earlier ASan application log beside the failed build is not r245 runtime evidence.

## Compact imm8 switch-table repair

The exact failed ASan `RedPrism.exe` hash is
`EC966BAB4404592271EE807E54C9CC32FC54C051DDB80D562A867D6A25331B5A`, its PDB hash
`D0650CCD6E6EFE448419D0447719E0F4793AEEA932DF90ACC7D66758C2D59176`.
The sole reported instruction at `0x14162c3fa` has bytes `62 01 21 c2 62 01`, spanning two
adjacent DWORD entries in `DxUi::ColorPicker::OnKeyDown`. No DxUi source/object change caused this
step; the new host test altered linked layout and exposed an unsupported compiler table encoding.

The PDB procedure starts at `0x14162c040` and ends at `0x14162c42c`. Anonymous same-procedure
records identify nine DWORD entries at `0x14162c3ec` and a 28-byte remap at `0x14162c410`.
The exact 42-byte guard/dispatch at `0x14162c13d` compares a DWORD at stack displacement 40
against sign-extended imm8 27, branches above the bound to the decoded default, zero-extends EAX,
then uses the contiguous RCX image-base remap/DWORD dispatch. RET and `66 90` precede the tables.

The independent PE proof checks literal dispatch bytes, all nine literal DWORD RVAs, all 28 literal
remap bytes, real decoded target boundaries and exact PDB/PE procedure/table extents. It binds the
unchanged failed image/PDB, original allowlist, full scanner inputs and observed replay. Proof hash:
`157E8EE0122BD8D0D63539738AF6E5FA6DABCBE493A51834A919AFC537EC8ACD`.
The private repaired scanner source hash is
`EF0D310C5DA7F3884DB48C121027548AE0EB2C4A8CD1E979939910B13BDE9FED`.

The repaired exact-image replay reports zero violations, 5,470,486 decoded instructions, 11,690
runtime vector instructions, zero EVEX, ten proved inline-table vector data rows and two separately
counted undecoded prefixes. The original report had one violation, one EVEX, seven proved table rows
and four undecoded prefixes. No allowlist entry, object exclusion or instruction-skipping rule was added.

The old 115 imm32 controls pass unchanged. A separate 116-control compact fixture passes the bound/
displacement matrix, signed invalid bounds, all eight row-evidence failure classes, operands/PDB/
table/range/target/fallback faults and real body/adjacent VEX/EVEX rejection. Prototype r1 failed to
retain the short comparison row; diagnostic r2 confirmed that gap; clean r3 adds that row at offset −15
from the image-base LEA and passes both fixtures. Preserve all versions. The first replay helper
incorrectly required zero undecoded prefixes and failed after a zero-violation scan; fresh replay r2
records the actual counts above and succeeds. Executed helper/receipt files were not edited.

## Registered repaired qualification

The repaired registered source freezes 859 inputs with manifest
`4BF043FD0FD64F7B2B3614269A75D8552CB7CD7EB097E5567192975E4449E637`.
ASan attempt r248 stopped at DxUi vcpkg restore because concurrent x64 builds competed for the
same dependency-install lock. Its child 112344 exited 1 before compilation/runtime, recorded
13:10:29.319008–13:10:37.905762 UTC; no assertion, lock check or build rule was relaxed.
The preserved independently verified archive completion hash is
`680F5554086CD5B6519783E0286D4BC3502EE25810C65EA1664D2F87290C67F3`.
Fresh ASan r250 starts after the other restore stages finish, with the same source.

ARM64 r35 completes all three cross-builds. These intervals come from `GetProcessTimes` on
the actual owned children, and all three archive receipts independently read back their copies:

| Configuration | Child / exit | Actual child interval, UTC | Archive completion hash |
| --- | --- | --- | --- |
| Debug | 108892 / 0 | 13:14:52.191983–13:15:17.727096 | `0F8F39327C3B44E10E8CF904D68554CEA223D6066B242C77938A8588DAD64AAB` |
| Release | 69096 / 0 | 13:15:23.397492–13:15:43.740288 | `94620986A30A16D7A3F072BFD36424D88F7A23E781462CF23454B7BC83AF79CA` |
| ASan Debug | 30540 / 0 | 13:15:48.719580–13:16:06.483227 | `71AEAD9E2B133BC4DAFEA5929BC333ED5FFBF5AA10294A3DAD1353F1E42B749D` |

The repaired step completes the full x64 Debug/Release/real ASan matrix, Release analysis and
ARM64 cross-builds on identical source. Complete predecessor/control registry proof and every
archive readback pass. Native ARM64 runtime and overall P1/G1 remain unqualified.

### Current qualification failures and diagnosis

Full Debug r247's child 73764 exited 1 after the hidden application child exited 5. Its recorded
interval is 13:10:29.321636–13:21:24.605045 UTC. The existing sampler-host graph passed opening,
three-point FIFO and real continuation settlement, then failed the requirement that the sealed
first/middle/transparent stream publish literal opaque middle color `0xFF804020`. The application
stopped before the new Wand modes; it is not a passing Wand/application registry result. The exact
failed executable hash `8E21937DE2BF0BDB17B5FACBB7ED942FDC7A19F6352FBE7ECABFC76B979BCF8A`
is byte-identical to the archived passing Debug r243 executable. No assertion or expected color was
changed. Archive completion: `F78D180ADEC6CF6577A8584C88EBA08B5E7FD73A407578831BD7D2536F98F0AD`.
Fresh unchanged-binary runtime r252 investigates reproduction; a later passing attempt cannot
reclassify this failed run. Root cause is not established.

Isolated Debug runtime r252 exits 0 with unchanged executable hash and exact byte-identical full
application counters/stages against qualified r243: 10,048 named controls / 89 stages. Child 98104
is recorded 13:23:59.716908–13:26:26.511295 UTC. The sampler failure is not reproduced; the isolated
runtime does not qualify a full script gate. Its archive completion is
`69A6DB3185FD5E5B4A5516F12805D36FC87C2D1D1DE8F9BFC6B9315B40067E85`.

ASan r250's child 16928 exits 1 after the application clipboard mirror check fails: the clipboard
sequence advances from 49,641 to 49,658 after the app offered sequence 49,655. The write itself
succeeded, but the asserted system mirror did not match. This app interval overlaps Debug runtime
r252, which also exercises the real system clipboard. The attempt is failed, not qualified; no
clipboard expectation was relaxed and interference is not claimed as a proven product root cause.
Recorded script interval: 13:13:10.087787–13:26:08.455543 UTC. Archive completion:
`46F39341355071983138D13152832048CAEE301A57BD85AAFC4DC854208DA8AA`.
Subsequent full runtime gates run sequentially to avoid overlapping shared desktop/clipboard use.

Full Release r251 passes after its app runtime no longer overlaps another self-test. Child 96964
exits 0, recorded 13:20:49.114752–13:28:33.082220 UTC. Independent registry r3 verifies every
previous CPU/I/O/WARP/application counter, all 17 new Wand expectations, all 116 new ISA controls,
every predecessor parser/control row and the five current binary/PDB/allowlist-bound scan reports.
Application inventory: 10,048 named controls / 89 stages. The complete Release analysis r249 also
exits 0 (child 111240), recorded 13:10:31.575467–13:19:34.444872 UTC; archive completion
`436AE2A0016004EA813E7795D5B212F804236440D3E7C5FADC39E158931CCAEB`.

The proof-bundle archive helper's first execution failed before directory creation on the
unsupported `Split-Path -LiteralPath -Parent` parameter combination. The helper and failure
record were preserved. Fresh helper r2 uses `Path.GetDirectoryName`, independently checks the
original failed-archive binding and every source/copy before and after copying, and preserves
32 private prototype/replay/proof/helper files in
`D:\RedPrismQualificationArchives\P1-wand-rebind-isa-proof-bundle-2026-10-06-r2`.
Its manifest is `7AD1203D5AAA806C6B86473E80DB37BE6B5960DF2510946E39B371F4CE51AA2C`;
completion is `1559CF89BC1E040DDA5F341FEED8B3465F8DB388A1193CA63888F1DB9F1F0F8B`.

## Final repaired matrix

| Full x64 gate | Child / exit | Recorded launch/completion interval, UTC | Archive completion hash |
| --- | --- | --- | --- |
| Debug r253 | 109536 / 0 | 13:29:31.564411–13:37:39.834964 | `F224FE196480C388A0362F6C606DA44E6520988AE6E3D1935CC3045EEFE5D1D7` |
| Release r251 | 96964 / 0 | 13:20:49.114752–13:28:33.082220 | `57EBEE4CD5D3A55F80660B10768FA619224DAC0FB5747E2C583B8B1DAD4200B1` |
| ASan Debug r254 | 24816 / 0 | 13:38:36.745032–13:49:07.601616 | `009A169F03F81E12E5920A0C4FF40B1A8DE0A494C56616FC9AD5C8931116473C` |

All three freeze the 859-input manifest above. Exact registry r3 verifies Debug and Release;
fresh r4 verifies ASan. Each preserves all 7,448 predecessor ISA parser/control pass rows and
adds exactly 116 controls plus their unique summary. Five current linked-image scan reports
bind their actual binary/PDB/allowlist hashes and contain no violations. Each actual application
preserves the full previous route inventory and all 89 stages, replacing only the eighteen-mode
Wand summary with the independently specified seventeen checks and twenty-mode summary:
10,048 named application controls, 128 Wand checks / 20 modes.

Normal HostTests preserve 7,657 controls; ASan preserves its previous 7,658. DocumentTests preserve
7,213, CompositionTests 1,694 and IoTests 1,736. ASan separately preserves the aggregate concurrent
raster-stress expectation and all 8,055 worker-stress controls; its isolated negative probe produces
the required real AddressSanitizer diagnostic. All harness and benchmark runner gates pass.

The first independent checker r3 failed after the successful product run because it expected a
document-suite end sentinel after raster stress. Raster stress instead ends at the unique following
worker-stress header. The protected checker and exact failure record remain preserved. Fresh r4
uses that actual boundary for both current and predecessor streams, compares every stress pass row
unchanged, and retains all source, log, stage, control and sanitizer assertions. No product test or
expected result was changed. Its source hash is
`E7C5D96FB3F4C225D5C7E02219C6EDDC971C4EC50AE367C03DDB51A99790281B`.
The six-file independently verified supplement in the closed ASan archive at
`Supplements/wand-registry-r4-r1` preserves the original checker/failure and final checker/proof/log;
manifest `EDEDB68C89AD268BEB7FC63368E532685417B80BFE6C97BEF8A77DECBA9C598A`.
The original archive completion remains unchanged.

Skills validation passes all 15 repository skills; formatting checks all 733 files. Current source
timestamps are preserved when activating the private scanner/fixture; no old C++ object was copied.
The successful subsequent gates do not establish the root cause of the original sampler failure.
That diagnosis, complete Add/Subtract admission, Clone All Layers, Native activation and measured
P1/H4/G1 remain explicit open work in the parent's beginning checklist.
