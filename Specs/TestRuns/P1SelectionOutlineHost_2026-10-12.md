# P1 selection-outline host route — 2026-10-12

Status: **private actual-owner r7 x64 Release/WARP candidate PASS, scoped to both startup orders and joined Close.** Native integration, default/ordinary activation, complete parent admission, filesystem case/open follow-up, and P1 acceptance remain due.

## Qualified result

`SelectionOutlineHost-r3-Main/actual-release-r1/qualified.json` reports PASS (SHA-256 `94F6CB56D8655AA7F6484C187F90592E50FECE43F71E256607DE3C2110BB74BB`), source count 1,295, 47 new assertions, and 8 negative controls. Its reader passed with the complete original stdout/stderr rosters retained (7,543 stdout and 7,613 stderr records); the qualified output book records 7,592 stdout and 7,613 stderr records. Original-roster manifest SHA-256: `949CA9D1AFAE65053C6B05D3AE9BEE55BA4963DAED3440FC8FFAB75BB6806031`; new host roster SHA-256: `02C210D24F32612FFE7AE4C0C25703257FEF80DC07EC008BAF7247482F7EAD26` (47 assertions plus summary record). The source/dependency/product bookends passed. Protocol SHA-256 `282E553DEC474B567F5F3BDD5DE4F401922221AD619C3BAF114ACFAE543452A2`; reader SHA-256 `20C6CD0CD5A4C1DA6C422C08DCB07C9C19CAEC5806D769B0B108A6D8563EE5E8`.

The Release build was PID 97752, exit 0, 17:31:11.769311–17:39:16.102424 UTC; stdout SHA-256 `548AC4C1A4FD5C29F515094B76354CADC50C4DFEB86F17CE033FB6FB3CB34E36`, stderr empty. ISA scan PID 102344 exited 0, 17:39:20.286265–17:39:31.119611 UTC; stdout SHA-256 `9891E9421F54FD8C974CB50CAF046AD97A0C3FB8094DA86CA89608EFEBB9601E`, stderr empty. The saved ISA report records 488 runtime vector instructions, zero EVEX, and zero violations.

The actual `--self-test --warp` run was PID 22172, exit 0, 17:39:31.252283–17:40:55.742916 UTC; stdout SHA-256 `48ABF85DD732E284B89BEFF5896A8C123A718C20110D5727052612454577DC04`, stderr SHA-256 `C3E5C05E39F4843CA486CCE8CE8511E79442E0D3EBD12043623F535B16964587`.

## Cold-acquisition correction and scope

The earlier host-r1 46/47 result and host-r2 manifest-tooling build failure remain historical failures. In r3, the formerly failing `refused-empty-route-can-acquire-after-pressure-retires` assertion passes: selection acquisition is bound to its own actual queue and checks the strengthened capacity precondition. The accompanying actual close assertion verifies charge refund after that acquisition.

The 47 new controls cover the opt-in host route’s prepared canvas and worker-created outline, actual renderer phases, stale source/view/device exclusion, refusal, replacement, retirement, and joined Close. They do not establish fractional-pixel equality: r3 clips the solid and dash planes independently, while legacy `CanvasOverlay::Paint` places one axis-aligned clip around `PaintSelection`’s white-and-black pair. Direct2D documents clip blending at `PopAxisAlignedClip`, so separate plane clips can change edge colors. Main is preparing raw worker planes with one final composition clip, scalar quarter-pixel goldens, and a direct WIC clip-once control; those checks are pending and no equality claim is made. The 47-assertion PASS remains valid within its tested scope. Lasso drafts, opaque allocation/cancellation, G1, and parent/default activation are also outside this evidence. Fully responsive current routing peaks at 10 owner-work slots; a concurrent selection source requires capacity 11. Current-view/frame activation, complete host admission, native integration, and ordinary use remain open.


## Actual-frame parity follow-up

The clip-once component is separately qualified in [the clipping receipt](P1SelectionOutlineClipping_2026-10-12.md). Host-r4 adds actual Application/Renderer parity at fractional pane edges and DPI 96/144/192, eight phases each, with the original Direct2D pair as reference and one-byte tolerance. It retains 47 host controls and all 133 original application stages.

Preparatory direct `ClCompile` refuses four missing generated HLSL headers; that command bypasses shader generation. Subsequent ordinary full Build supplies those headers and passes (actual PID 97336, exit 0, 19:43:30–19:45:29 UTC, stdout `B48333412F965E4F45D6628A0718A108AF7981412DB8F8A300BECC60DBDC6C04`), followed by successful ISA. Actual application PID 105436 exits 5 at 19:45:47–19:45:50 UTC, stdout `A2B0275E1EF0D5AC7543A8864E7C73FD22FB973D7BACB008E38B924081DD0834`: all 47 predecessor host assertions pass, then background capture fails.

The fixture requires BGRA8 but `Renderer::kTargetFormat` is RGBA8. Its callback returns `E_INVALIDARG` before incrementing capture count, so no parity was evaluated. Fresh host-r5 corrects only staging allocation and format guard to RGBA8; its 75 assertions, one-byte tolerance and all predecessors remain unchanged. Build/ISA/runtime are active. This does not yet qualify real fractional frames or ordinary activation.

## Actual-frame qualified result

Fresh host-r5 qualifies full private Release Build, linked ISA, and the actual hidden WARP application suite. Report SHA-256 `49AC99E5E5C19892064EF0546CA4BA192163E1066A9A8805BC08FBF4F466E187`; source count 1,296; 122 additions (47 host + 75 parity); exact 7,669 stdout / 7,613 stderr records and eight reader negative controls. All 133 original application stages remain in order. Protocol `10171EC2EC5B982301DE04F8C6E0EC43B6D12478D22FE1CC2D7FE8B1B62AFA27`; source/dependency/product bookends pass.

Actual application PID 49576 exits 0, 20:28:46.534556–20:30:18.378331 UTC. Stdout `534C4B8BE2702EFD22E5E882D6FC5062B895E8F883EFB7F23EAED02FE9D45816`; stderr `C3E5C05E39F4843CA486CCE8CE8511E79442E0D3EBD12043623F535B16964587`. Actual ISA PID 94544 exits 0; its successful interval is 20:28:31.992986–20:28:46.324578 UTC, stdout `966E6B9D36400449642DF0C26B13779C24D407EBE325329A72A3F4393CD88C6E`.

The fixture now captures the actual RGBA8 target. Each of eight prepared/reference phase pairs at each forced DPI 96/144/192 matches the original Direct2D white/black paint pair within one byte at the fractional pane edge. Independent nonempty-region assertions exclude two blank frames. Joined Close refunds original source and GPU charges. This is actual frame numeric evidence for the opt-in route; visible presentation latency, complete service admission, ordinary activation, lasso, opaque allocation/cancellation and native integration remain open.

The next actual cold-owner candidate retains those 122 controls and adds 40 named facts across both startup orders, exact membership of all eleven services, twelfth refusal and joined shutdown. Main review catches and corrects the cheaper overlay's retained ten-slot constant before any build. The corrected overlay remains unqualified.

## Actual owner-capacity r7

The exact qualified report is `.build/P1Drafts/SelectionOutlineActualOwners-r3-Main/actual-release-r7/qualified.json`, SHA-256 `31A37CC59649EBE7C5B1622BD712E76176164925A4A2A1B3B86C13E778800C22`. It records source count 1,299; 40 actual-owner facts; the 47 host and 75 parity assertions; all 133 predecessor stages; and 7,711 stdout / 7,613 stderr records. Protocol SHA-256 `A509F3AC6BECB7BA8054DB084DFC74C576C7409D060C9E7F44ECA6277C873B8B`; before-book SHA-256 `D67BABEB29FF1D7715E453F842D779BEA6CF5376004148BB9C993AFDC5B00853`; product-book SHA-256 `A838ADD91908966A377D33CC063E51B9FC7E133DAEB2D029018BBE24FCDFD08E`; reader SHA-256 `3C895A477459588B64FFF87FC12B9CE7030BD721FA9254BE763B613485658370`. All source/dependency/product bookends pass.

Build PID 59896 and ISA PID 66892 exited 0. The actual `--self-test --warp` application PID 16216 exited 0 at 21:24:06.761817–21:25:22.217910 UTC; stdout SHA-256 `091AF75926297CBB3E780C4B76F2419A879D558846A55F2F9B1515BEEB1DE43F`, stderr SHA-256 `C3E5C05E39F4843CA486CCE8CE8511E79442E0D3EBD12043623F535B16964587`. Both startup orders prove eleven exact actual members, twelfth refusal, and joined Close. Corrected literal capacity fixtures use 5/4/3/2 responsive slots and 8/7 processing slots; the original 254 + 593 predicates remain unchanged.

Historical r3–r6 failures remain failures and are not superseded as evidence: the earlier fixture omitted device binding; capacity fixtures were corrected to fill literal occupancy without changing predecessor predicates; the Z-drive runtime TEMP experiment retained 29 save-case failures. In r7 TEMP was redirected to Z only during compilation and restored to the inherited C TEMP/TMP before runtime. The F6 filesystem case/open follow-up is still open. This evidence is not ordinary activation, native integration, or G1 acceptance.
