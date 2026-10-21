# Polygonal keyboard/header latest ARM64 qualification — 2026-10-19

Status: canonical Release and real-ASan Debug cross-builds pass on adopted57d6631.
P1 remains WIP. Native ARM64 runtime is unavailable; these are build qualification only.

## Source and actual results

Source head and both C++ commits:57d6631796e96e000ae6523fae0df87edd52329c.
Application.cpp SHA256:7D792A83570584E63903619DF4E96BEE9B4E7DBFE619FE1660E7EDD4B7851CDB.
47-case /508-fact fixture SHA256:35C94BC79E5E2C554417302FFBA3EDBD17175565FD3811017DC15D546DE6341E.
Functional focused Release/real-ASan proof is in P1PolygonalKeyboard_2026-10-19.md and
P1PolygonalHeader_2026-10-19.md. This evidence adds no ARM64 behavioral assertion count.

| Canonical attempt | Actual PID and exit | UTC interval (2026-10-18) | Archived files | Completion SHA256 |
| --- | --- | --- | --- | --- |
| r1142 ARM64 Release |107816 /0 |22:35:05.901672–22:36:22.711378 |1881 |5DEE680604FE569967428DD949307A0668F4361EB9215FF9112F4ADFFA77376B |
| r1143 ARM64 ASan Debug |18772 /0 |22:37:50.353569–22:38:25.596187 |1885 |8FB98AB65FD507719F0EFE38C3B21CD6FC372D6F929508335442E30F2990B7FE |

Both real commands use build.ps1 -Platform ARM64 -MaxCpuCount1 and the selected configuration,
without Rebuild. Preparation pins and independently verifies the exact r1139/r1140 parent maps,
preserving both old objects before overwrite. Main verifies exactly two changed compiled sources
(Application.cpp and dispatcher fixture),992 byte-exact inherited objects, all351 Application objects
in the actual link, all994 actual compiler records from five tlogs, required warnings/SDL/conformance/
UTF-8/CRT/optimization/defines and real ASan instrumentation with the forced header where required.
Five AA64 executable/full-PDB pairs per profile pass independent CodeView GUID/age/hash checks.
Every archived source/evidence byte and inventory is independently checked before qualification.

## Frozen reference and readers

The new helper freezes all Specs (including plans/evidence) and the storage plan:1808 inputs,1803 Root
inputs plus four runner inputs and one .build storage input. Actual attempt freeze SHA256:
96851908CBBBF2691DDEA75246BB60FE0E01BEE46AA9E898458E26BAA8B5FCF7.
The source-only reference uses normalized SourceCopy paths; its exact hash is
CC0C4E25DEB565B016327E7863A85B47E86BB55267670890AFE29A4A2063745D.
Path/length/hash maps agree. Four positive and31 negative prelaunch reader controls pass.

Main corrects the cheaper prep's source-only map key: shared Common sources occur in several projects,
so unique compiler records are keyed by(project,source). The original preparation refusal is retained;
it precedes D-cohort creation and native launch. No scope/profile/count gate is relaxed.

Cohort:D:/RedPrismBuildCaches/.build/p1-polygonal-keyboard-arm-native-r1.
Main helper:.build/P1RunnerHost/PolygonalKeyboardArm-Main-r1.
All five completion steps pass for each profile: archive, independent artifacts, PE/PDB, mandatory C++
profile and object lineage. Current-object maps are independently rehashed and preserved before any
future overwrite: r1142-current-objects.json SHA256
FC5171397FCAFDD3D526640E168828587A7C94FF58EB9080DA3B87F93E34B3E4;
r1143-current-objects.json SHA256 C654491DDA7B83A5E2B18CD74D9D7A5913894FCC1740B627200B64BBE36430B8.
Each has994 exact profile-scoped records and the adopted source commit.

## Remaining

Latest full x64 Debug/Release/real-ASan/analysis, native ARM64 runtime, ordinary activation, remaining
header-kind/Ctrl-lift/consumer/lifetime/resource gates and measured G1 remain separate. No DxUi pin or
ISA allowlist change. The full x64 ASan r1144 helper's missing-import prelaunch refusal is retained
before any attempt root or native test exists; Main prepares a fresh reviewed cohort for that run.
