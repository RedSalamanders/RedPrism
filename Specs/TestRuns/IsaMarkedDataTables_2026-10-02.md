# PDB-marked switch tables in the ISA gate, whatever the link layout — 2026-10-02

## Finding

On 2 October a relink of x64 Release with only a local display-size patch was rejected by the ISA gate: six
`vcvtsi2ss xmm8, xmm6, dword ptr [rax]` rows four bytes apart, `0x1402a4ae8` to `0x1402a4b04`, in `DxUi.Menu.obj`
([DxUiRevision3_2026-10-01.md](DxUiRevision3_2026-10-01.md), last section). They are the switch table of DxUi's
`MenuPointerKindFromMessage`, a 156-byte procedure at `0x1402a4a70` whose last 60 bytes are 15 image-relative DWORDs:

```
1402a4a70  81 c2 00 fe ff ff        add    edx, 0xfffffe00
1402a4a76  83 fa 0e                 cmp    edx, 0xe
1402a4a79  77 4a                    ja     0x1402a4ac5            ; the default
1402a4a7b  4c 8d 05 7e b5 d5 ff     lea    r8, [rip - 0x2a4a82]   ; 0x140000000
1402a4a82  41 8b 94 90 d0 4a 2a 00  mov    edx, dword ptr [r8 + 4*rdx + 0x2a4ad0]
1402a4a8a  49 03 d0                 add    rdx, r8
1402a4a8d  ff e2                    jmp    rdx
```

The default's RVA `0x2a4ac5` ends in `C5`, so each of its eight entries reads `c5 4a 2a 00`, and the linear decoder reads six of
them as VEX instructions. No dispatch recognizer covers this form (an RDX index through an R8 image base), so the scan never
proved the table. Unpatched, it lies elsewhere and its entries decode as no vector instruction. In the unpatched image of this
change's tree it starts at `0x1402ce210`, where it reads as `iretd`, `loope` and `add` rows. The revision 3 record wrongly
called it verified there; that record now carries a correction.

That is the general case, not an exception. The PDB marks each switch table that MSVC places in `.text` with an anonymous untyped
16-byte `S_LDATA32`. Every table the dispatch recognizers prove starts at such a record, but in Release they prove about a third of
the records:

| `RedPrism.exe` | Marked tables | Proved before | Proved after |
| --- | --- | --- | --- |
| Release with the display patch (the rejected image) | 220 | 76 | 220 |
| Release, this change's tree, unpatched | 222 | 77 | 222 |
| Debug, unpatched | 195 | 195 | 195 |
| ASan Debug, unpatched | 196 | 135 | 196 |

The unproved tables passed only because, where the linker placed them, their bytes did not decode as vector instructions. Any relink
can move one onto an address where they do, and every pin move or unrelated change relinks. This is the third such failure in two
days: the `std::variant` destruction table on 1 October
([IsaSignedBiasedByteTable_2026-10-01.md](IsaSignedBiasedByteTable_2026-10-01.md)), the ASan `Application::EditUnderWay` table on 2
October ([IsaTerminalStackGuard_2026-10-02.md](IsaTerminalStackGuard_2026-10-02.md)), and this one. Each previously took a new
recognizer.

## Change

`Build/IsaScan.cs` (`ReadPdbProvenTables`) proves every marked range from its record and its content, whatever dispatch reads it.
This is the general rule the 1 October record suggested; the contract is in [Build_Toolchain.md](../Build/Build_Toolchain.md).

- The range runs from the record to the next record of its module, or to the end of the one PDB procedure of that module and
  contribution that contains it.
- A DWORD table is DWORD-aligned, at least two entries, and each entry is a decoded instruction boundary of that procedure before
  its first marked range.
- A byte index table directly follows a proved DWORD table of the same procedure. Every byte is an index below that table's entry
  count, except a trailing `90`, `66 90` or `0F 1F 00` that ends the range on a DWORD boundary.

All three alignment forms occur in the rejected image: `0F 1F 00` ends seven index tables, `66 90` two and `90` one. Without the
alignment rule, 11 of its marked tables are not proved.

The gate (`Scan` with seven arguments) always applies this proof. An eight-argument overload can disable it, so the recognizer
suites can still show what each dispatch form proves alone. Nothing is exempted by address, object or procedure name, and
`Build/IsaRuntimeAllowlist.psd1` is unchanged. Every byte outside a proved range stays gated.

The scan also loses a pass that did nothing. For each decoded row, the scan removed that address from the pending targets of every
proved table. A table enters that list only after all of its targets are proved and its pending set is cleared, so every set was
already empty. With three times as many tables, that pass made a first paired run about half again as slow (Release 1.9 to
3.0 s, ASan Debug 9.4 to 14.2 s). Without it, the scan is faster than before (see Scan time below).

## Evidence

The rejected image is `RedPrism.exe` SHA256 `40F02D5A326363D95CFB0BE760DF9A97D4B982A3B7F2361CDCB3B93F7021B465`, PDB
`A54106B40E92EC57C859801DDE9A80153064ADA70AC97BC0576EBB91A50855FF`. With this change it scans clean: 0 violations, the six rows
counted as verified table data, and 10 verified table-data rows in all (1 before).

This change's tree is `origin/main` `249820d` plus the change, unpatched. Its x64 builds pass the gate on all five linked images:

| Configuration | `RedPrism.exe` decoded rows | VEX/EVEX rows (all runtime) | Undecoded prefixes | Verified table data | Violations |
| --- | --- | --- | --- | --- | --- |
| Release | 804,621 | 459 | 2 (7 before) | 8 (3 before) | 0 |
| Debug | 5,005,439 | 11,690 | 0 | 31 (31 before) | 0 |
| ASan Debug | 4,382,084 | 11,690 | 0 (4 before) | 15 (11 before) | 0 |

Rows that used to count as undecoded prefixes now lie in proved tables and count as table data.

With the display patch applied to this tree (never committed), x64 Release also passes the gate on all five images. Its
`RedPrism.exe` has 459 vector rows, 3 undecoded prefixes (8 before) and 9 verified table-data rows (4 before); SHA256
`9D97D091BABA9D9B590E66F02D198FBF75C6C86930153E1674F15F7B3F38259F`, PDB
`2E000409BE7E79BC95920D86462DAF5E0A4B69F26795822C815F5DC9930E05A3`. In that image the Menu switch lies at `0x1402ce1c0`, where
none of its entries decodes as a vector instruction, so this tree does not reproduce the rejection. The rejected image above
remains the reproduction.

| Configuration | `RedPrism.exe` SHA256 | PDB SHA256 |
| --- | --- | --- |
| Release | `8F5B5F28D456C4377453992070F2D3881F56D7D1FB062AD5EE2511B525237FED` | `A39CCEBB7A7BA39397AA7A0526BCB1E9439A09D261CCFC6BBAC9B8DC3B9C7DC4` |
| Debug | `7902906FCB20B342528396812F53BB42A9B4190660FC9BC49F25FB3F5E943143` | `BEDE93C23ED0D900A80B5A3BDE84C655BE498245482224A3137DA857BDA52C8D` |
| ASan Debug | `B2C2344AB78FB09A6079772E618834DD05CDEFD6E5EA47A9EB3039D787A41C3F` | `406429803025CDAA6135C45C204152CD484673D4185F3F1669EADEE5C0EB743C` |

## Scan time

The time is that of the gate's `Scan` call on the same inputs, before and after alternately, in three rounds on this machine:

| `RedPrism.exe` | Before | After |
| --- | --- | --- |
| Release, unpatched | 2.0, 2.6, 2.3 s | 2.4, 1.5, 2.2 s |
| Release, the rejected image | 2.5, 2.2, 2.3 s | 2.2, 2.0, 2.2 s |
| ASan Debug | 15.9, 13.8, 13.4 s | 11.6, 9.9, 9.7 s |
| Debug | 14.8, 14.5, 14.6 s | 9.6, 9.4, 9.5 s |

## Controls

`Tests/BuildProcessTests/IsaMarkedTableTests.ps1` has 39 controls, run by `IsaScanTests.ps1` in every x64 build. The groups are
listed in [Testing_Validation.md](../Testing/Testing_Validation.md). Each builds a small image from the observed bytes and
decodes it with the gate's own `llvm-objdump`, so the table reads as it would at that address. The observed image is accepted with
its six vector rows as table data. With the marked-range proof disabled, the same image is rejected; that is the failure this
change closes. Moved to `0x2a5b70`, `0x2a4270` and `0x2a4a80`, the switch is still accepted, with seven, seven and no vector rows.

The inline (575), AL-guard (91) and RDX (39) suites now evaluate every control twice. Under the full gate, an accepted control
stays accepted and a control that plants vector code stays refused. With the marked-range proof disabled, their original
assertions apply unchanged.

To check that the controls bear on the rules, each rule was removed in turn from a scratch copy of the scanner and the new suite
run against it. Each rule that can refuse a range is killed by the control written for it:

- the DWORD alignment by `TableNotDwordAligned`, the whole-DWORD length by `ProcedureEndsOneByteEarly`, and the two-entry minimum
  by `SingleEntryRange`;
- an entry before the procedure by `EntryBeforeProcedure`, an entry at or after the first marked range by `EntryIntoEarlierTable`
  (the weaker bound "before this range"), and dropping that upper bound altogether by `EntryInNextProcedure`;
- the decoded boundary by `EntryMidInstruction`, and the single holder by `TwoProcedures`;
- index adjacency by `MarkedGapBeforeIndices`, the same procedure by `IndicesStartNextProcedure`, and the index bound by
  `IndexAtEntryCount`;
- padding off a DWORD boundary by `NopNotAtDwordEnd`, and padding that is any byte by `IndexAtEntryCount`.

Each rule that proves a range fails an accepted control when removed: the next-record bound and the first-record scope by
`TablesInTwoProcedures`; the `0F 1F 00`, `66 90` and `90` forms by `AlignedByThreeByteNop`, `AlignedByTwoByteNop` and
`AlignedByNop`; and disabling the proof in the gate, or in its seven-argument entry, by `Observed`.

Seven mutants change no control:

- Five checks restate what the parser or the procedure bound already guarantees: a record's contribution belongs to its module, a
  holder's module and contribution, the module of the records that bound a range or find the first one, and a proved DWORD table's
  module and contribution when it lies in the same procedure. They stay in the method so that it does not rely on them.
- The 64 KiB limit bounds work, not proof.
- An index range that is only alignment holds no vector byte, so accepting it changes nothing.

## Validation

- `format.ps1 -Check` passes (505 files). `validate-skills.ps1` did not run: this machine's Python lacks the `yaml` module. No
  skill changed.
- The three ARM64 configurations build. The x64 gate does not run there.
- `test.ps1` passes in x64 Debug (10,771 passing checks), Release (10,771) and ASan Debug (13,918), with the hidden WARP self-test
  and the ISA controls. As in the revision 3 record, three checks are skipped on this machine: `path.junction-load`, the absent
  gradient snapshot and the RAW corpus byte controls.
- The `test.ps1` runs used two local patches, never committed and reverted afterwards:
  - the display-size patch of the revision 3 record;
  - a no-clipboard patch, so that the runs could not replace the user's clipboard. Neither clipboard scope
    (`SystemClipboard.cpp`, `ClipboardTransfer.cpp`) opens it. The self-test leaves out its five clipboard stages and the
    system-mirror checks of its pixel-edit block. IoTests leaves out `RunClipboardTransferTests` and
    `RunClipboardAvailabilityTests`.

  This change touches no product code.
