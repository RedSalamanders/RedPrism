# RAW corpus provenance and downloader controls — 2026-09-27

## Corpus verification

The five `gating` entries in `Tests/Fixtures/raw-corpus.json` were present under
`C:\RedPrism.Perf\corpus`. Each local byte count and SHA256 matched both the manifest's
source-reported size/checksum and `C:\RedPrism.Perf\corpus-verification-2026-09-27.json`.
The original API snapshot at `C:\RedPrism.Perf\raw-pixls-index-2026-09-27.json` was
rehashed as `328eae30dfe3e826f524c9a724c3f0bfc2addc9bab26c9521bac2bc69494a2c3`, matching
the checked-in provenance. Its original rows identify all five gating files as CC0-1.0
([original API](https://raw.pixls.us/json/getrepository.php?set=all),
[CC0 terms](https://creativecommons.org/publicdomain/zero/1.0/)).

All five local files had their recorded binary signatures verified. The Pixel 8 Pro DNG's
raw IFD and complete tile arrays were inspected: 8160×6144 CFA16, JPEG compression,
256×256 tiles, 768 tiles, and every offset/count range within the file. These checks verify
downloaded bytes and container structure; they do not qualify RAW decoding or performance.

| ID | File | Bytes | SHA256 |
| ---: | --- | ---: | --- |
| 4466 | `4466-sony-ilce1-lossless.arw` | 81,575,280 | `4012536ee0f938d8c5c5e932bb7953a03134abbd9f3f4c664cb2526c1903d492` |
| 4694 | `4694-canon-r5-raw.cr3` | 40,632,406 | `21430c36387efe65bd09ac0fcd724bfd959d93dd391ec811a1b741c8663becb5` |
| 1840 | `1840-nikon-d850-lossless.nef` | 55,196,715 | `0c9438258ddae5260b9f5a1aace6658a723f1cb1acace3a6cb507116dc6da4f2` |
| 6002 | `6002-fuji-xh2-lossless.raf` | 40,083,440 | `109be38f59963c9b8ff289ebe1f5ec9168c52f609e5b601432bd194520b4605b` |
| 7751 | `7751-pixel8pro-tiled.dng` | 45,101,750 | `45420c595401547cca950ae58d552bc82b32d31ce1d58c082df33004016197f5` |

The Leica SL2 supplemental record (ID 7872) was not downloaded and remains pending.
All five gating entries are marked download-verified; decode qualification remains pending.

## Downloader controls

`get-raw-corpus.ps1` now accepts `-ManifestPath` for controlled manifest validation and
`-Offline` to verify existing files while refusing missing files before it creates an HTTP
client. The fixed `X:\RedPrism.Perf` path, ownership marker, corpus child, and reparse
protections remain in force.

`pwsh -NoProfile -File Tests/BuildProcessTests/RawCorpusTests.ps1` passed:

- Rejected unsupported manifest schema, mismatched original CC0 evidence, and an unsafe
  PerfRoot path.
- Rejected a manifest checksum mismatch for an existing verified destination and confirmed
  its bytes remained unchanged.
- Refused a missing file in offline mode before HTTP and confirmed no destination appeared.

`pwsh -NoProfile -File Build/bench/get-raw-corpus.ps1` passed its preview-only path and
selected the five gating files (262,589,591 server-reported bytes). The preview and offline
controls made no HTTP request. Acquisition and local container verification are recorded above;
RAW decoding and performance qualification remain future work.
