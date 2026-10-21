# Exact joint unsigned switch-tail data proof

Date: 2026-10-18
Scope: build-time ISA scanner; P1 remains WIP.
Base: `8d6b2f3777477680533eaa4f9f2b178dc654eb60`.

## Original rejection and correction

The intermediate focused crop ASan image passes its native 40-case / 407-fact runtime but the scanner rejects four apparent EVEX instructions at 0x141a96271 / 27d / 289 / 295 in unchanged pinned DxUi.WindowHost.obj. The exact matching PDB identifies a 468-byte procedure at 0x141a960d0 ending at 0x141a962a4. Two guarded unsigned stack dispatches precede RET at 0x141a96212 and NOP at 213; the tail is 32 DWORD-table bytes, 28 remap bytes and 84 final-table bytes. The apparent vector prefixes belong to encoded case RVAs.

Main implements a joint proof of both exact guards/dispatches, all three anonymous same-owner spans, PE/decoded boundaries, complete known body, direct/indirect flow, both protected guard intervals, all case/default targets and exact terminal procedure extent. Candidate body collection is bounded and proves nothing alone. Every reported data byte is checked against the PE; real vector code remains gated. No DxUi source/pin, instruction/tier/runtime/object/function allowlist, legacy golden or product behavior changes.

A first conservative draft refuses because the preceding unsigned remap is also unsupported; Main retains that refusal and instead proves the complete pair. An initial local-check placement C# compile error is preserved before correction. The cheaper synthetic draft initially omits the post-table INT3/later-symbol shape; Main requires faithful whole-body/tail layout and retains the failed fixture before correction. No native tests are replayed to repair a reader or scanner result.

## Validation

- Complete existing Release ISA scanner suite and compiled controls: actual success, all 13,795 predecessor PASS records preserved exactly against canonical r1136 raw logs. These include scalar/AVX2/AVX512 positives, misplaced instructions, wrong runtime owner/library/function and missing/mismatched/identity-first PDB negatives.
- Adopted portable synthetic suite: all 94 unique cases pass. Independent literal bytes prove 29 RVA rows; hostile guard/row/remap/target/ownership/body/flow/RET/NOP controls retain their exact expected violations. Real body VEX and adjacent EVEX remain rejected. The old root scanner rejects the valid synthetic case with 29 vector violations and zero table rows.
- The same original 7,136,958-instruction decoder input now has 11,690 allowed runtime vector instructions, zero EVEX instructions/violations and 45 verified table-data rows. Every original executable, full PDB, decoder/PDB text and rejection report hash is unchanged before/after.
- Main independently validates the original PE/full-PDB GUID 25BE54D7-E04D-4B7B-9270-D399010AD43C / age1 and unchanged allowlist. The adopted scanner bytes exactly match the corrected full-image scan source.
- The initial predecessor reader refuses the known mixed-encoding build banner. Main preserves the refusal and compares exact ASCII ISA assertion records from raw bytes; no predicate is dropped and no build/test replay occurs.

## Exact identity and retained records

| Input | SHA-256 |
| --- | --- |
| Original executable | 7DF59ABA387C0CAA49E6711ECB68CDBCC770178866E3AA90E7517178F56D2D42 |
| Matching full PDB | DE322F5CD03886BB828BA9BDB9B8B10ABF7B47F1F1E982B18C9092CFFE1F9D09 |
| Unchanged allowlist | 7870AF49195841A9B9B88DFD15378D550B127754683666C83AED83753FD80E1E |
| Corrected adopted scanner | 9AE63782E3B4ECD5E5D49E346843E07AC66B909A6343926D66643A5CDECB913B |
| Portable 94-control source | 430FCE86CD1B6D5BEA6E56A0E71B2CCCBEDECC9BBEF0A58C1AFCE5D8871FC945 |
| Complete predecessor regression log | 88C9A369C379CF84248BA2714635436416B7D77B121776A536BFD909E81C45E4 |
| Portable control log | BF8D4563848F3C72539F47A3029337F15DAFFF3DDFC1C093C683BF31B69AE439 |

Original failure lives under D:\RedPrismBuildCaches\.build\p1-polygonal-held-crop-focus-native-r2\ASanDebug\IsaScan.
Correction evidence lives under D:\RedPrismBuildCaches\.build\p1-isa-split-unsigned-tail-r1, including main-original-image-qualified.json and main-scanner-qualified.json. Agent and Main draft sources, original failed drafts and source-only logs remain under .build/P1Drafts/IsaSplitTailUnsigned-*.

This step closes the observed scanner gap. It does not qualify latest full Debug/Release/ASan/analysis/ARM integration, ordinary activation, visual layout, G1 or P1 Done.
