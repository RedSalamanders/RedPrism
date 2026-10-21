# P1 visible cancellation costs — 2026-10-10

## Current checklist

- [x] Run the six actual visible direct/prepared cancellation rosters on the qualified Release component.
- [x] Authenticate every original source, EXE/PDB/DLL, child interval, raw roster and immutable archive.
- [x] Rerun the strict reader from each archived native source; retain all warmup, refused and finish-race rows.
- [x] Check Apply across every recorded active trial, including finish-before-Escape trials.
- [x] Preserve the WARP Apply overrun against the unchanged 2ms limit.
- [ ] Fix and qualify the overrun and remaining owner/frame costs.
- [ ] Measure the complete ordinarily activated P1 operation roster and close G1.

## Scope

These are actual visible cancellation measurements on the 1,144-input DAB609 source committed in `0d2cb0e`, using the immutable qualified full Release r760 runtime mirror. Its exact app EXE SHA256 is `AD50E92FA53FDB0C941674047E1CBD994BC676FF841BB8469145374D80F72A11`; its PDB is `A8EFC1E20AA10FE5CE3295C3A9C623BCECE198C9E73705EECEA634EECC1B8A27`. All 13 mirror products, original r760 archive/profile and ARM90 join are read back before and after each child. No builds or other native test producers run in parallel. Observed hardware is Ryzen9 9950X3D, 16 physical/32 logical cores, 66,155,405,312 bytes physical RAM and RTX5080, driver32.0.16.1714, Windows build26300. WARP cases use the actual software device.

Each profile preserves ten operations, eleven trials per operation, ten warmups and 100 recorded trial positions. Large profiles explicitly refuse 22 extreme Gaussian/motion rows; these are not timed kernels. The remaining 80 recorded active rows all contribute to the Apply maximum. 4K profiles have 100 recorded active rows. Finish races remain excluded from cancellation intervals but remain included in the all-recorded Apply maximum. Real first source Present is proved by the original ordinal for every measured cancellation. This does not qualify all G1 frames, kernels, utilities or ordinary activation.

## Results

| Actual attempt | Route/profile/device | Child PID/exit | Measured cancellations | All-recorded Apply max | Owner action max | 2ms Apply result |
| --- | --- | --- | --- | --- | --- | --- |
| r769 | prepared/8192x5464/hardware | 31408/0 | 70 | 1078us | 3692us | PASS |
| r764 | prepared/10000x10000/hardware | 3476/0 | 70 | 1734us | 4238us | PASS |
| r765 | prepared/3840x2160/WARP | 54656/0 | 81 | 2227us | 201us | FAILED_UNCHANGED_2MS_LIMIT |
| r766 | direct/8192x5464/hardware | 30968/0 | 70 | 255us | 164us | PASS |
| r767 | direct/10000x10000/hardware | 62356/0 | 70 | 285us | 1568us | PASS |
| r768 | direct/3840x2160/WARP | 61068/0 | 85 | 985us | 409us | PASS |

Prepared WARP r765 Levels trial1, returned-after-stop, measures Apply2227us and exceeds2000us. The native child completes its valid roster with exit0; the unchanged latency gate fails. This failure is never converted into a passing result. Prepared hardware action maxima3692us/4238us and direct100MP frame intervals require their separate owner/frame assessment. G1 remains open. Previous r702 Apply2183us and display-off r735/r741 remain historical failures; the current direct45 roster measures all-recorded Apply255us without explaining or erasing the older overrun.

## Original preservation

Main reads every source/evidence manifest entry and product back from each immutable archive, compares the original child receipt/argv/process interval, and reruns the canonical saved-output parser copied with the actual source. Its independent audit includes every recorded Apply trial. Each archive has1,144 source copies and30 evidence copies. Archives are under `D:\RedPrismQualificationArchives`.

| Attempt | Actual interval UTC | Archive completion SHA256 | Main audit SHA256 |
| --- | --- | --- | --- |
| r769 | 2026-10-10T15:16:52.051424+00:00 → 2026-10-10T15:18:13.162408+00:00 | `34EA58395FD2C996BFB7C25E27453435680DB42434158D9CD0DAEFBBFF7DCBC6` | `BECD9D502899C6E58C417D711AD89F8A9ED6EB39DB8B7FD7CAE2CC95E64A7AA9` |
| r764 | 2026-10-10T15:18:37.253982+00:00 → 2026-10-10T15:20:33.617795+00:00 | `69FF6DA4DA1D0F8D5C03099085D28B52E5B8604468A6F67F212058CAACFF8422` | `7D6A2FC228B5B540C92C1C148D2C44C72AF55618CC0EC09E6736AB601F3A8D40` |
| r765 | 2026-10-10T15:20:56.254670+00:00 → 2026-10-10T15:22:05.030677+00:00 | `14F4962BA9D518CF385E3F444D97A889F4FE6A66337CB69FE50224ADEB60EC47` | `59352F08BDAB231F5610C461435BD697C7D6E6052A2D2F73D269D9A4AF0B6BA1` |
| r766 | 2026-10-10T15:22:27.710500+00:00 → 2026-10-10T15:23:17.716337+00:00 | `6FF26A2B7994B41645AE7ADBDBBFC4C6EF57510B44F31A86893CCF3CC3737459` | `C7E0D08BCBE0D4E60A626B8FA6741C85B4CC02CFF9FB45C6C8A0908569DCA41F` |
| r767 | 2026-10-10T15:23:40.071955+00:00 → 2026-10-10T15:25:11.333635+00:00 | `81F5DCA9257793812C3CC9C92BD1DF18E6DB1244A73C941C753C7617435CD983` | `024B0001CF4EFD86E8A48432AE115B7254F63B626F0AB6DDAF1B28F2795C1DC2` |
| r768 | 2026-10-10T15:25:35.062314+00:00 → 2026-10-10T15:26:02.435407+00:00 | `089208CBEA53170212615FF3F8D6A3F5005C3436D741A7BE42A77FAACA01B97D` | `C648464AAB6F6E1C3B88007AD31A2F3FDB297497038551A2360E901C574C7F60` |

Archive names are `P1-utility-1144-cancellation-<attempt>-2026-10-10`. The complete original reports retain measured distributions, all row values, power scheme before/after, source/qualification/control hashes and captured child identity. Active power scheme is unchanged after each child; the application restores its temporary execution-state lease through its scope guard, and its actual thread exits. No synthetic display/presentation or logged power-request restoration evidence is substituted.

Original r763 displays visible1/displayOn1 and reaches Presents, then fails ambient pointer input with exit5 before a complete roster. Its original copy-name reader additionally fails on slash separators. Its unchanged child/archive/reader failure remain in `P1-utility-1144-cancellation-r763-2026-10-10`, completion `8132058B58E1B3B5A99670BC283858E7B461140200F975278B341397224FA3C7`. The user then explicitly authorizes untouched visible runs. Main fixes only private reader defects: the actual `UI CANCEL REPORT` name/dimensions and safe copy-name slash normalization. Eight pure reader checks and actual gate/product preflight pass before fresh r769; these are reader checks, not native timing proof.

Ordinary activation=false; G1=false; P1 Done=false.
