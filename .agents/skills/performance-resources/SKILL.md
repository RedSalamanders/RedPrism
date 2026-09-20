---
name: performance-resources
description: Design, implement, or review RedPrism CPU, memory, allocation, batching, wake-up, idle, and resource-lifetime behavior. Use for every material architecture or hot-path change.
---

# RedPrism performance and resources

Read `Specs/Core/Core_PerformanceAndResources.md` before material architecture or rendering changes.

- Preserve correctness, security, visual quality, and required behavior first. Among correct designs choose the
  lowest steady-state resource cost.
- Make steady-state hot paths allocation-free with bounded reusable storage.
- Cache derived state until an input changes. Batch compatible copies, uploads, state changes, and draws.
- Block on messages, events, or presentation pacing when work is unavailable. Never add a busy loop.
- Hidden, minimized, suspended, occluded, and display-off execution must not Present or keep a canvas/UI surface.
- Keep histories, queues, caches, and device resources bounded. Fail locally on capacity exhaustion.
- Create expensive resources lazily and release them when inactive unless a normative lifetime rule says otherwise.
- Measure before accepting a regression. Record justified regressions in the owning spec or active plan.

Rendering changes keep the WARP `--self-test` path green.
