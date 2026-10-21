# P1 exclusive stroke sample foundation

Status: historical full x64 Debug/Release/ASan pass; analysis repair, fresh matrix and application migration pending

## Changes and proof scope

The primary added exclusive owner-to-worker sample capture, scalar convex-hull admission planning,
original retained-lease growth, unchanged Catmull-Rom/coverage/tile composition, exact structured coverage
acknowledgement, cancellation refusal and owner-only density return. The application Append/Up consumers
are not activated by these APIs. Captured source/history and all private CPU/GPU/map/command owners require
the original parent's complete charge and actual joins before publication/refund.

The cheaper agent drafts were reviewed before integration. Primary review corrected an invalid changed/total
bound comparison, a missing cancellation token, stale/pointer checks that had only reached the incomplete
coverage guard, composition accidentally run on the owner, and a source-identity comparison against the new
committed image. Primary implementation review also corrected new detached tiles' density/coverage allocation.
No golden/tolerance was changed. Fatal allocation preparation and actual host/G1 remain separate scenarios.

## Independent references

- `Document.Tests.StrokeSample`: unchanged direct scalar Append; exact per-sample density/coverage/tile bytes,
  placement and final sparse snapshots for hard/soft curved/crossing paths, opacity/selection, masks, erase,
  non-square grids and duplicate input. Synthetic uint delivery independently checks key/count/sequence/pointer
  refusals, clamp, complete-set publication, owner/engine identity and cancellation before/after preparation.
- `Document.Tests.StrokeSampleSession`: immutable state-machine expectations, real joined CPU worker,
  source/history/target authority, exact scalar opening retention, one apply, literal BGRA/Gray8 source
  preservation and final reference equality. Public Undo cancels this pending edit before reaching private
  Restore; direct private Restore injection is not claimed.
- `Host.Tests.RetainedGrowth`: exact HRESULT/charge/sample-count/slot-generation expectations, independent
  physical and commit bottlenecks, reentrant sampler invalidation, foreign worker final reset and Close lifetime.
- Existing `recorded-brush` actual WARP parent: actual post-readback barrier before density return; the same
  GPU buffers/charge are restored without reset, then transferred to another preallocated epoch for MTA retirement.

## Remaining P1 obligations

Actual accepted Down/Move/Up/Lost/FIFO/quota/cancel/device/shutdown application paths, clone opening,
preview/display/rows preparation on the original parent, complete peak admission, normal activation,
performance measurement, the full current-source validation matrix and final workflow closeout remain open.

## Preserved r343 build failure

Full Debug attempt r343 stopped at compilation; no suite/application test pass is claimed.
Actual child PID 26524 ran 2026-10-07T22:02:31.237982Z–22:03:57.857496Z, exit 1.
977 inputs freeze `00F521C6549F86F8FC7AEAE4A3F6E65023BE8DB174CFFC7E5B387310BC99A234`.
The primary epoch-return code used `.get()` on its borrowed raw device pointer; a copied delegated
session fixture still had a draft-relative include. Both are corrected without changing expectations.

Archive `D:\RedPrismQualificationArchives\P1-sample-977-Debug343-build-failed-2026-10-08`
contains 1,288 copies and zero application stages, read-back verified 2026-10-07T22:04:38.4173021Z.
Source manifest `EA7FA195E2CCFA3388862D4F8CF5E6EDF990F13E75C798659AFE4F8C095F4F9E`;
evidence `B4E28F701EE2DDEEBE2DEBE8AD24E01B3EFE7C5C8544AAF0C83365F01AF16EB4`;
completion `AA3C1BB845C5176F3EF622661327CA6B6794A8D5C7F3F6EEDA10D54924EC3BC6`.
Stdout `2F188214F778A78D3085E698413C7303499A2EBCF4C3861ADFC0B08C17DBD681`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.

## Preserved analysis r350 failure and isolated diagnostic repair

Actual child PID 105916 ran 2026-10-07T23:04:12.558988Z–23:07:44.251033Z, exit 1.
977 inputs freeze `55BB59C01486FDF2A16D95FBF623EA3A813B698C31DA66EE0D6F6FCDFA1AFDDD`.
Moving the protected read resolves C26130, but concurrency analysis now reports scoped-lock bookkeeping
at return paths, and another exact HRESULT comparison needs explicit conversion.

Archive `D:\RedPrismQualificationArchives\P1-sample-977-analysis350-failed-2026-10-08`
has 1,255 copies and zero application stages, verified 2026-10-07T23:08:10.3201434Z.
Source manifest `59CE5077D5EA58BDEB34708ACEC1F8A5C0585A8AFA32C764575B886F93607692`;
evidence `90529E301C7707434D1FEBB0C024CF220265604F311BF527DCE375E2E33B2EAE`;
completion `6FEB6B9745B1D7C2DF5ABD2E776BCF83AA86B192B064BE2F577AECD1C52E44CD`.
Stdout `B043ADE669554A5025028743FC7539E31F6FFED5383520F736BF6E7A34A7B349`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.

An isolated two-translation-unit diagnostic compile checks the real concurrency plugin before another
canonical analysis attempt. Named Impl validation methods explicitly require the scheduler lock;
distinct scoped validation/accounting guards retain both checks around the unlocked memory query.
The probe exposes the corresponding pre-existing IncreaseReservation lambda requirement too, so it receives
the same explicit method contract without changing refusal, revalidation, sampling or accounting behavior.
Every macro-derived HRESULT comparison in that fixture now converts the API's int32_t result explicitly.
The isolated compile passes; it is a diagnostic check, not full qualification. Fresh canonical gates remain due.
Fresh qualification is required for the repaired source.

## Preserved r344 linkage failure

The repaired 977-input candidate compiles its new code, but the full Debug build stops linking IoTests.
The new brush reservation-growth definition referenced JobScheduler from BrushStroke.cpp; the deliberately
small I/O project compiles brush primitives and retained endpoint lifetime without the scheduler implementation.
Move that definition into StrokeSampleWork.cpp, compiled by the actual application/document worker modules.
No extra scheduler dependency is added to IoTests; no runtime or numeric assertion is changed.

Actual child PID 87720, 2026-10-07T22:05:48.832133Z–22:06:07.357470Z, exit 1, zero application stages.
Freeze `A4271E8696EDDDFF2A82B73C2ECB723204E59B2EED9582561A4A2867C15FC044`;
stdout `70F74AB675DE4CC89B5DE388DDF0474F52D6BBF837F6C23561EF8DCCA6F52951`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
Archive `D:\RedPrismQualificationArchives\P1-sample-977-Debug344-link-failed-2026-10-08`:
1,297 copies, verified 2026-10-07T22:07:00.8971365Z. Source manifest
`EC9AF6D66184E19D1BBBE9DB4D708138FFBD1961772998FA800D45E2E0459A0B`;
evidence `452171392900FD8A31E52BB88BF0FAA1525120FFC4EEF1655516F635A9311CCA`;
completion `0A2F939C1985DDC589DEBC870C0E10F1CF6E7A72C084501DB89768D16F861C9E`.
Fresh qualification remains required.

## Full Debug r345

The actual canonical test child PID 54216 ran 2026-10-07T22:08:00.061978Z–22:18:56.717763Z,
exit 0. All core/host/composition/I/O suites, crash probes, WARP application self-test,
19-format RAW corpus/downloader checks and harness screenshot gates pass. 103 application stages are reached.
977 inputs freeze `DA826B05F02DF75DADEE63598A65D28695E5FD006951CC963694F76230A7B8DF`;
stdout `5D627675AD15353C44049AD905F8E32C4FB6D4318BF9FA89ED842E8C98679033`;
stderr `0E142843548327FFDC706277F52C8FD244242BFE3FE3BB4386704D47AF2B5093`.

Archive `D:\RedPrismQualificationArchives\P1-sample-977-Debug345-qualified-2026-10-08`
contains 1,280 copies and is read-back verified 2026-10-07T22:19:44.9105104Z.
Source manifest `A4E372643E4BF5580EFC7A8424F13BA604D48778F794E01B07AC87A96D006C12`;
evidence `DFA5CB54D57D98D174E8B5BA7855AB975161071DC001F6EDEBD233DF8A9BE9A0`;
completion `92D22DE19B7021A101989F1E4DEC331D2DB1E035F29345E62C1DF4ED1561BF28`.

This qualifies the stated foundation scope in Debug. It does not qualify actual asynchronous Append/Up,
normal display activation, measured G1, Release/ASan/analysis or ARM64. The remaining matrix starts on
the identical source; independent inventory and mutation checks remain due.

## Full Release r346 and real ASan r347

Both full canonical profiles pass on the identical r345 977-input freeze
`DA826B05F02DF75DADEE63598A65D28695E5FD006951CC963694F76230A7B8DF`.
Each reaches 103 application stages. ASan includes the actual detection probe.

Release child PID 70308 ran 2026-10-07T22:20:46.643570Z–22:30:19.428919Z, exit 0.
Archive `D:\RedPrismQualificationArchives\P1-sample-977-Release346-qualified-2026-10-08`
has 1,272 copies, verified 2026-10-07T22:30:43.5775162Z. Source manifest
`B9FCF74A3B7B9945C416433A9ED0FBAF7CD15463576BE7C7AC7162B5FE08D064`;
evidence `CABF6577C00C17B06EC5793128C77CD36E3F9B8000C07694A8B9EF660C8CE25B`;
completion `7E538B09F34590966AD1E5F9EE4D8DE3037123DE6BBBA1301110C82AC66D781B`.
Stdout `6B7C8A999EC66F1EC50E1C4176226D02FE8FB2DB7B4CFFB0C4CE075EBCAE8DF0`;
stderr `F826A19A012AD39D929191CAA65DDCC36A5F6082027690E35617A5DD0820F15C`.

ASan child PID 116972 ran 2026-10-07T22:31:22.126380Z–22:46:50.595192Z, exit 0.
Archive `D:\RedPrismQualificationArchives\P1-sample-977-ASan347-qualified-2026-10-08`
has 1,275 copies, verified 2026-10-07T22:47:50.3772310Z. Source manifest
`9F6752B27C5AFBEB6AB30348A2A637FBD62FBF1F989F64F06AC2DD21E293F9B2`;
evidence `4C3B722D792548F90FCA678512DDAC83279E606C94C407AF65E65FCA8461809F`;
completion `ED2AD6E911390C39909565933E83996AD4BDABD6C46DBAB40AB38A0B9E84BD62`.
Stdout `FBF1EF7D8CA1B6BCBB29D9492A5A516DFCDE4D8111CDFAA4BAB29C059409FCC1`;
stderr `D24743B28828611E840393A4AA266511DFF39E625803C92896A1163DC982F755`.

## Preserved Release analysis r348 failure

Actual child PID 68532 ran 2026-10-07T22:48:13.134711Z–22:51:54.343193Z, exit 1,
on the same 977-input freeze. Analysis reports C26130: IncreaseRetainedReservation validates
the lock-protected closing flag outside the scheduler lock. Its initial and post-sampler validation
now hold that lock; the reentrant OS/test memory query remains outside it. The final validation
and charge update share one lock interval.

The three C6221 reservation-growth test expressions now compare against a typed HRESULT local.
The display parent's declared 128 MiB charge equality moves to static_assert; its runtime control
checks the scheduler's actual complete charge. No expected HRESULT, charge, golden or tolerance changes.

Archive `D:\RedPrismQualificationArchives\P1-sample-977-analysis348-failed-2026-10-08`
has 1,255 copies and zero application stages, verified 2026-10-07T22:52:39.2332626Z.
Source manifest `1F665284ADD8EF37FED5CE86033C2D95FD0A751E11DD74B2B00C8341D858FEC8`;
evidence `F1B7D166ADB7BDE1134D8CB94326CBE6E79EB96DA10EAA3583CE87203C34C25D`;
completion `B527502A0228E41A447817EFB269CBF7E61DE070110FBE86DEA2D7E4D030F101`.
Stdout `F1E4A41FD0B48D3253E7FF1F9B7BBD128D3D07763AA675D68FFA0A5C107F82B7`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.

r345–r347 prove the historical pre-repair source. Fresh qualification is required for the repaired candidate.

## Preserved analysis r349 failure

Actual child PID 78980 ran 2026-10-07T22:58:13.570269Z–23:02:31.750766Z, exit 1.
977 inputs freeze `F0F3265AA983BBC8DA2DEAB95828D19349332B34CFBDD64C734165FD7471CC27`.
The explicit call-site locks are correct, but the analyzer separately requires the lambda's
lock-protected read to have a declared requirement. Move the shutdown-state checks out of that lambda
into both locked validation intervals. Test API results also need an explicit HRESULT conversion before
exact HRESULT equality; a typed comparison constant alone did not resolve the semantic integer warning.
The expected errors and accounting assertions remain unchanged; no warning suppression is added.

Archive `D:\RedPrismQualificationArchives\P1-sample-977-analysis349-failed-2026-10-08`
has 1,255 copies and zero application stages, verified 2026-10-07T23:03:05.6743742Z.
Source manifest `36F93FCA9B9BDB5FD68C0023FCA8F03E5FF398DDA942BF50B018FB0872DE72ED`;
evidence `7C6B1FFD0357FC72F33ACE496897ABAF31077FDF841E0673F003505114450C1F`;
completion `53DCE94D2DAD3B0BA722DC68593C6BB11F67C62CF4601DD46B9DDCA2A82F959D`.
Stdout `98C4908662910AF392ACF38CBB60836C68BBDFA4E94F148766C047B6090DF52D`;
stderr `A8A4252C62957836AF4D50437F1F3B8C24BB1945D8E3E2F5B7FD1DA6346E00C7`.
