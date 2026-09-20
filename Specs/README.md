# RedPrism specification authority and workflow

Status: current normative repository policy
Last reviewed: 2026-09-20

## Purpose

This catalog defines where RedPrism requirements live and how specifications, plans, implementation, and validation
stay consistent. Location alone does not make a document authoritative.

## Authority classes

| Class | Location | Meaning |
| --- | --- | --- |
| Normative domain contract | `Specs/<Domain>/*.md` | Current intended product behavior, ownership, and required validation. |
| Normative repository policy | `AGENTS.md` and this file | Cross-domain engineering and closeout workflow. |
| Machine contract | Manifests, project files, schemas, lock files | Exact data consumed by the build or application. |
| Active plan | `Specs/Plans/WIP/*.md`, excluding `README.md` | Non-normative proposal or execution record. |
| Active-plan index | `Specs/Plans/WIP/README.md` | Routing only. |
| Historical plan | `Specs/Plans/Done/` | Completed sequencing; never current behavior authority. |
| Evidence | `Specs/TestRuns/` | Immutable run evidence. A green historical run is not proof for changed code. |
| End-user documentation | `docs/` | User guide. Must agree with contracts; does not override them. |
| Implementation and tests | `RedPrism/`, `Common/`, `Tests/`, root scripts | Executable behavior that must agree with the owning contract. |

## Reconciliation

When a spec, implementation, test, or document disagrees:

1. Identify the owning domain contract.
2. Inspect runtime behavior, tests, and the port plan.
3. Correct the stale side when intent is mechanically clear.
4. Preserve safe current behavior and create a `DECISION` WIP plan when product intent is unresolved.
5. Update the contract, implementation, tests, and (when an end-user scenario changed) `docs/` together.

## Plan lifecycle

1. Multi-step work uses one indexed file under `Specs/Plans/WIP/`.
2. WIP files cannot override current contracts.
3. Durable requirements move into domain specs before closeout.
4. Completed plans move to `Specs/Plans/Done/` and leave the index.

## Naming

- Domain specs: `Domain_Title.md`
- Plans: `Title_YYYY-MM-DD.md`
- RFCs while undecided: `RFC_Domain_Title.md` under WIP

## Domain entry points

| Domain | Start with |
| --- | --- |
| Build | [`Build/Build_Toolchain.md`](Build/Build_Toolchain.md) |
| Architecture | [`Core/Core_Architecture.md`](Core/Core_Architecture.md) |
| Performance | [`Core/Core_PerformanceAndResources.md`](Core/Core_PerformanceAndResources.md) |
| DxUi pin (from C2) | [`Core/Core_DxUiIntegration.md`](Core/Core_DxUiIntegration.md) |
| Windowing | [`UI/UI_Windowing.md`](UI/UI_Windowing.md) |
| Testing | [`Testing/Testing_Validation.md`](Testing/Testing_Validation.md) |

`Core/Core_DxUiIntegration.md` is created in C2. Until then the consumer-dxui skill and the port plan describe the
intended pin; they are not a substitute for that contract after C2 lands.
