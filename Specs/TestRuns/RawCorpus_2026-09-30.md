# RAW corpus expansion — 2026-09-30

## Result

Downloaded and independently verified 19 default RAW inputs under `C:\RedPrism.Perf\corpus`: 480,717,563 bytes
(458.45 MiB). Every planned v1 extension has an actual local file. No image binary is committed to Git.
The source images remain unchanged. Download identity/signature checks do not qualify RAW decoding or performance.

| Extension | Smaller correctness inputs / manifest IDs | Large performance input / ID |
| --- | --- | --- |
| ARW | Sony A7S compressed / 1582; A7 IV small YCC candidate / 6931 | Sony A1 lossless / 4466 |
| CR2 | Canon Rebel XT full RAW / 1230 | — |
| CR3 | Canon R6 normal RAW, 1.6× crop / 4661 | Canon R5 RAW / 4694 |
| DNG | Canon S2 IS CHDK CFA / 943; uncompressed Linear Raw converter output / 1031 | Pixel 8 Pro tiled DNG / 7751 |
| NEF | D2H compressed / 5227; D1H uncompressed / 5364; D7000 12-bit lossless / 959 | D850 14-bit lossless / 1840 |
| ORF | Olympus E-10 / 5424 | — |
| PEF | Pentax K10D compressed / 2239 | — |
| RAF | Fuji X-M1 uncompressed / 842; X-A1 uncompressed / 1214 | Fuji X-H2 lossless / 6002 |
| RW2 | Panasonic LX3 crop / 1395 | — |

The smallest correctness file is 3,236,017 bytes. The five existing large files retain their original Photoshop
measurement roles. Optional Leica id 7872 remains unselected and is unnecessary for extension coverage.

## License and acquisition evidence

Every selected original raw.pixls.us API row associates its image with CC0-1.0. The manifest retains each exact
license HTML, original URL, supplied SHA-256 and camera/mode row. The fresh API snapshot is
`C:\RedPrism.Perf\raw-pixls-index-2026-09-30.json`, SHA-256
`8dc5f5c74e20cc3a38f4548d53bba4bb516835e2a859e80bf86e7ac6283b3aa3`.

The exact [official CC0 legal text](https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt) was saved to
`Tests/Fixtures/Licenses/CC0-1.0.txt` and beside the images. Its 7,048 bytes have SHA-256
`a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499`.
The license's narrow `.gitattributes` rule disables line-ending conversion; a fresh index checkout with this
repository's `core.autocrlf=true` retained the exact 7,048 bytes and hash.
Local evidence also includes `raw-corpus-<SHA256-of-exact-manifest-bytes>.json`, with the complete per-file license
rows and source identities. `README-2026-09-30.md` links the current snapshot and lists every image.

Acquisition verification is `C:\RedPrism.Perf\corpus-verification-2026-09-30-v2.json`, with exact byte counts,
hashes and first-16-byte signatures for all 19 files. A preliminary local summary had an incorrect zero aggregate
because PowerShell did not enumerate ordered-map properties for `Measure-Object`; v2 corrects it using explicit
member evaluation, and a separate loop independently confirmed the total. The preliminary summary is preserved
and is not the manifest's current evidence. No image or original license evidence was replaced.

## Controls and implementation

- HEAD established exact lengths; bounded downloads validated length and supplied SHA-256 before pinned,
  no-replacement commits. Separate verification reread every file and checked its format-family signature.
- Default materialization includes `gating` and `correctness`, covering all nine extensions. Supplemental files
  remain opt-in. Preview validates but writes nothing; ordinary tests send no HTTP request.
- CC0 text and an exact manifest snapshot are saved before new image commits. A later transfer failure therefore
  leaves already saved images with local source/license evidence. An offline run with missing images refuses
  before evidence writes or HTTP setup. Existing differing files/evidence are preserved and cause failure.
- Offline controls cover the real extension matrix, allowlist, role selection, schema/traversal/unselected-license
  rejection, local legal-text hash, ownership, missing-file refusal and mismatched destination preservation.
  All 19 actual images and exact license/manifest bytes passed repeated offline verification.
- `test.ps1` now invokes these controls in every runtime configuration. Missing local corpus may explicitly SKIP
  the ordinary suite's byte checks; an existing mismatched selected file fails. P4/P5 required-file qualification
  must reject missing files and SKIP as specified by the parent plan.

## Remaining qualification

The integrated offline preparation gate passed 24 controls in each complete x64 Debug, Release and ASan Debug
`test.ps1` run, with all 19 files verified and no corpus SKIP. Skills/format checks and all ARM64 configurations
also built successfully. See [HostInputAdmission_2026-09-30.md](HostInputAdmission_2026-09-30.md) for matrix/log
provenance and the concurrent host implementation slice. ARM64 runtime was not executed.

The original linked EXIF texts for ids 1031 and 6931 are saved beside their images. Id 1031's raw SubImage1 reports
3960×2640, uncompressed Linear Raw, three 16-bit samples and strips; its thumbnail IFD is RGB and is not the raw
layout. Id 6931's linked text reports three-channel YCbCr small RAW. These are metadata-backed candidates, not
independently decoded samples. Lossy JPEG DNG id 1033 was rejected as supported coverage because v1 excludes it.

P4 still must run the pinned decoder/reference, independently confirm actual RAF CFA and linear/YCC layouts,
validate samples/develop tolerances, hostile/cancel/admission recovery and actual editor save/reopen/export round
trips for every `gating`/`correctness` file. P5 must rerun correctness on optimized paths. No Photoshop speed claim
or RAW decode success is made by this acquisition record.
