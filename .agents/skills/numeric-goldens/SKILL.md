---
name: numeric-goldens
description: Maintain independent numeric expectations for RedPrism document, project I/O, rendering, tool and history behavior. Use when adding or reviewing golden tests; never derive expected values from the implementation under test.
---

# Independent numeric goldens

Read [specification authority](../../../Specs/README.md), the owning domain contract, and the existing
numeric cases under `Tests/` before changing behavior. Domain specs own the required semantics and tolerances.

## Reference owners

| Behavior | Contract |
| --- | --- |
| Manifest fields, validation, limits and serialized shapes | [Project format](../../../Specs/IO/IO_ProjectFormat.md) |
| Layers, masks, transforms and raster identity | [Document model](../../../Specs/Document/Document_Model.md) |
| History budgets, no-op rules and undo/redo | [Document history](../../../Specs/Document/Document_History.md) |
| Selection, brush, retouch, adjustments and command eligibility | [Document tools](../../../Specs/Document/Document_Tools.md) |
| GPU layer passes, clipping and coverage | [Canvas composition](../../../Specs/Rendering/Rendering_Composition.md) |
| Brush density, sampling, sparse tiles and stroke preview | [Brush rendering](../../../Specs/Rendering/Rendering_Brush.md) |
| Execution, WARP readback and failure reporting | [Validation](../../../Specs/Testing/Testing_Validation.md) |

## Golden rules

- Preserve all existing numeric expectations and their documented tolerances. Never rebaseline a golden to make
  an implementation pass, and never use screenshot similarity as a numeric oracle.
- State the independent formula, literal fixture, serialized schema or event-gated state transition that owns
  each new expectation. Keep sizes, alphas, colors, operation order and channel order explicit.
- Document space is top-left origin with +Y down. Rasters use premultiplied BGRA; alpha expectations remain
  independent of RGB channel ordering.
- Hold a `Raster` while using its storage identity as a cache key; released storage addresses can be reused.
- Preserve documented JSON required/optional keys, array forms, enum values and dictionary representation.
- Use the current `DocumentSession` eligibility and pending-edit rules, including color, transform, brush and
  warp state. Preserve each existing edit's commit, cancellation and lifetime behavior.
- Give each `Expect` a stable `suite.case` identifier, report actual values on failure, and add its expected
  numbers and tolerance to the owning domain spec.
