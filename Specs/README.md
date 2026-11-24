# RedPrism specification authority and workflow

Status: current normative repository policy
Last reviewed: 2026-11-19

## Purpose

This catalog defines where RedPrism requirements live and how specifications, plans, implementation, and
validation stay consistent. Location alone does not make a document authoritative.

## Authority classes

| Class | Location | Meaning |
| --- | --- | --- |
| Normative domain contract | `Specs/<Domain>/*.md` | Current intended behavior, ownership boundaries, compatibility constraints, and required validation. |
| Normative repository policy | `AGENTS.md` and this file | Cross-domain engineering and closeout workflow. |
| Machine contract | Schemas, manifests, project files, resources, and lock data | Exact data that the build, the application, or its tools consume. Link to it instead of copying it into prose. |
| Active plan | `Specs/Plans/WIP/*.md`, excluding its `README.md` | Non-normative proposal, decision, or execution record for unfinished work. |
| Active-plan index | `Specs/Plans/WIP/README.md` | Routing only; it does not define behavior. |
| Historical plan | `Specs/Plans/Done/*.md`, excluding its `README.md` | Completed sequencing, rationale, and evidence; never the authority for current behavior. |
| Run evidence | `Measurements/` | Reviewed evidence packets under the [run-evidence contract](Testing/Testing_RunEvidence.md). A green historical run is not proof for changed code. |
| End-user documentation | `docs/` | The user guide. It agrees with the contracts and is updated when an end-user scenario changes. It is not an authority for behavior and holds no measurement receipts, checkpoints, generated reports, or WIP notes. |
| Implementation and tests | Source folders, `Tests/`, and root scripts | Executable behavior and evidence that must agree with the owning contract. Tests are evidence, not permission to ignore a contract. |
| Archived run evidence | `Specs/TestRuns/` | Immutable run records written before 2026-11-19. They stay where they are; new records go to `Measurements/`. |

## Reconciliation rule

Neither prose nor implementation wins only because of its file type. When a spec, implementation, test, manifest,
or document disagrees:

1. Identify the owning normative domain contract and every affected consumer.
2. Inspect current runtime behavior, implementation anchors, tests, and relevant historical plans.
3. Correct the stale side when intent is mechanically clear.
4. Preserve the safe current behavior and create a `DECISION` WIP plan when product intent is unresolved.
5. Update the contract, implementation, tests, and `docs/` together when behavior changes.

## Change workflow

1. Read this catalog and the owning domain spec before changing durable behavior.
2. For a small, settled change, update the contract, implementation, and validation directly in one change.
3. For multi-step, risky, or undecided work, create one plan under `Specs/Plans/WIP/` that names the domain specs
   it expects to change, and add it to the [WIP index](Plans/WIP/README.md). When the index names a single current
   plan, add new in-scope work there instead.
4. A WIP plan is an execution aid only; it cannot override a current contract.
5. Run the validation that the owning spec names and the commands under
   [Specification validation](#specification-validation).
6. Before closeout, merge every durable requirement discovered during the work into the owning domain spec.
7. When the finished change affects an end-user scenario, anything a person using the product sees, touches,
   configures, or is prompted about, update `docs/` in the same closeout.
8. Only when every claimed implementation, test, and documentation obligation is closed, move the plan to
   `Specs/Plans/Done/`, add it to the [Done index](Plans/Done/README.md), remove its WIP row, and update inbound
   links, all in the same change. A completed plan never stays under WIP.
9. Record the change as one fragment under `Changes/`, as the [Changes workflow](Core/Core_Changes.md) states;
   never edit `CHANGELOG.md`.

If a behavior has no owning domain spec yet, create that spec in the first change that makes the behavior durable.

## Naming

- Domain specs: `<Domain>/<Domain>_<Title>.md`, without spaces.
- Plans, decision records, and RFCs: `<Title>_<yyyy-mm-dd>.md`, dated the day the plan was created. A plan keeps
  its name when it moves to Done.
- Index rows: a bullet that links the plan's title to its file and ends with an em dash and its purpose, opening
  with the bold status and a colon (`- **ACTIVE**:`) in the WIP index and without a status in the Done index.

## Domain entry points

| Domain | Start with |
| --- | --- |
| Build | [Build/Build_Toolchain.md](Build/Build_Toolchain.md) |
| Qualification archive storage | [Build/Build_QualificationArchives.md](Build/Build_QualificationArchives.md) |
| Packaging (from C15) | [Build/Build_Packaging.md](Build/Build_Packaging.md) |
| Architecture | [Core/Core_Architecture.md](Core/Core_Architecture.md) |
| Performance | [Core/Core_PerformanceAndResources.md](Core/Core_PerformanceAndResources.md) |
| DxUi pin (from C2) | [Core/Core_DxUiIntegration.md](Core/Core_DxUiIntegration.md) |
| Settings (from C14) | [Core/Core_Settings.md](Core/Core_Settings.md) |
| Crash handling (from C14) | [Core/Core_CrashHandling.md](Core/Core_CrashHandling.md) |
| Document model (from C4) | [Document/Document_Model.md](Document/Document_Model.md) |
| Document tools (transform from C8, selection from C9, brush session from C10, retouch from C11, pixel edits from C9b, color adjustments and filters from C12, content-aware fill from C13) | [Document/Document_Tools.md](Document/Document_Tools.md) |
| Document history (from C4) | [Document/Document_History.md](Document/Document_History.md) |
| Application chrome | [UI/UI_ApplicationChrome.md](UI/UI_ApplicationChrome.md) |
| Windowing | [UI/UI_Windowing.md](UI/UI_Windowing.md) |
| Project I/O | [IO/IO_ProjectFormat.md](IO/IO_ProjectFormat.md) |
| Image codecs | [IO/IO_ImageCodecs.md](IO/IO_ImageCodecs.md) |
| Canvas composition | [Rendering/Rendering_Composition.md](Rendering/Rendering_Composition.md) |
| Brush rendering (from C10) | [Rendering/Rendering_Brush.md](Rendering/Rendering_Brush.md) |
| Testing | [Testing/Testing_Validation.md](Testing/Testing_Validation.md) |
| Benchmarks | [Testing/Testing_Benchmarks.md](Testing/Testing_Benchmarks.md) |
| Run receipts and the evidence archive | [Testing/Testing_RunEvidence.md](Testing/Testing_RunEvidence.md) |
| End-user guide | [../docs/README.md](../docs/README.md) |

## Shared contracts

These files have the same role in every RedSalamanders repository:

- The [Changes workflow](Core/Core_Changes.md) owns fragment format, validation, and the fold of merged fragments
  into `CHANGELOG.md` on the default branch.
- The [Unified CI contract](Build/Build_UnifiedCI.md) owns how this repository adopts the shared CI engine.
- The [shared native build policy](Build/Build_ConsoleAndPaths.md) owns zero-warning acceptance, terminal
  output, and short consumer paths.
- The [performance and resource contract](Core/Core_PerformanceAndResources.md) owns the performance mandate;
  measurement procedure stays under `Testing/`.
- The [run-evidence contract](Testing/Testing_RunEvidence.md) owns receipts and the `Measurements/` archive.
- [RedMonitor integration](Core/Core_RedMonitorIntegration.md) keeps this repository's adapter facts
  for the optional diagnostics client; RedMonitor's consumer contract owns the rules.

## Specification validation

Before closing a specification change, run:

```powershell
./validate.ps1
./validate-specs.ps1
./validate-skills.ps1
./Tools/Test-Changelog.ps1
```

Also run the build and runtime checks that each modified domain spec names. Generated reports and build products
belong under `.build/`, never under `Specs/` or `docs/`.

## Repository rules

- `Core/Core_DxUiIntegration.md` is the consumer pin contract.
- Domain specs and independent numeric golden cases own behavior; a screenshot comparison never does.
