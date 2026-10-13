# P1 selection-outline clipping — 2026-10-12

Status: **private composition clipping qualification PASS; application integration and fractional-pixel host acceptance remain due.** Qualified receipt: `.build/P1Drafts/SelectionOutlineClip-r1-Main/actual-release-r1/qualified.json`, SHA-256 `EE21C9567B29D358406832D26BB73C68FE0E0B91DDBDB9148D771B2FF55A987B`.

## Evidence

The x64 Release `CompositionTests` build used 1,298 source inputs. MSBuild child 68176 exited 0 at 2026-10-12 18:06:24.208867–18:10:20.848741 UTC; stdout SHA-256 `2A22E58544C4E59AB8065995DC424CDDCCF3B171776C1BCED064D3EE75D1FF3B`, stderr empty (`E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`). The qualified output book reports all source, dependency and product bookends passed.

ISA scan child 7172 exited 0 at 18:10:22.242668–18:10:27.567624 UTC; stdout SHA-256 `B42FC8095BB5FBE722B847EED7DC6C8E2F85E134B921844E11159F74E0DE4789`, stderr empty. The report accepts 113 runtime vector instructions, with zero EVEX, zero undecoded prefixes, zero inline table data and zero violations. Scanned image SHA-256: `75C21FB8CE8B65CA834DBA8D26D91867C4DEE5495ADBFA613C993F4AA5F7E556`; ISA report SHA-256: `13457C45BFEB1A9D4D8B574B8807A00BADA17FE760EA52E84DD53B4FD8FFD736`.

Actual `CompositionTests.exe` child 65488 exited 0 at 18:10:27.708631–18:10:38.518303 UTC; stdout SHA-256 `97C6E9B664B1C541D46970B0F4DC75D9EABFDD833536FDD5F6FD25C9656ED27E`, stderr empty. The qualifier reports 7,287 prior records plus 41 new clipping records, 7,328 total, preserving every original assertion occurrence. It validates only the two specified recovery and two brush-interleaving normalization regions; 10 reader negative controls passed. Protocol SHA-256 `D0C6F8D5FAF3BCA6651413ECA13A5B885DE5249D530A3A8510E228658C3A5082`; reader SHA-256 `A8BB612DEFABF155D8F15272D3EF057671173E7F42F7728237CA4CA9F4B94360`.

## Clipping result and boundary

The test exercises raw nine-plane worker output followed by one analytic fractional view clip after solid/dash composition. New records include nine quarter-pixel composite cases (three clip positions by three base colors) with one-UNORM tolerance and a direct WIC white-then-black clip-once literal-area control. The complete composition stdout reports the clipping assertions passed and the composition suite passed.

This is composition-level evidence. The application adapter source was authored but **not compiled in this qualification**. The receipt does not establish production host wiring, actual fractional host equality, ordinary activation, G1, cancellation, or native integration. The prior host-r3 47-assertion result remains scoped to its original assertions; it did not cover fractional-pixel equality. Parent S5 and P1 gates remain open.