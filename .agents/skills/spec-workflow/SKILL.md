---
name: spec-workflow
description: Create, update, or reconcile RedPrism domain specs and manage the WIP-to-Done plan lifecycle. Use when durable product behavior, validation, specifications, or repository workflow change.
---

# RedPrism specification workflow

Read `Specs/README.md` completely before changing specification authority or plan state. Then read the owning domain
spec completely before changing its behavior.

## Authority

- `Specs/<Domain>/*.md` states current normative behavior.
- `AGENTS.md` and `Specs/README.md` state repository-wide policy.
- `Specs/Plans/WIP/` contains non-normative active work; index every direct plan in its `README.md`.
- `Specs/Plans/Done/` is historical and does not override a current domain spec.
- `docs/` is the user guide, not authority.
- Domain specs and independent numeric golden cases own behavior.

## Workflow

1. Identify the owning domain contract and its implementation and validation anchors.
2. Use a direct spec-plus-code change for small, settled work. Create one indexed WIP plan for multi-step work.
3. Keep requirements in the domain spec, not only in commentary, a WIP checklist, code, or tests.
4. Update all affected contracts and consumers in the same change when behavior changes.
5. Run the validation required by the owning spec and `.\validate-skills.ps1` for skill or policy edits.
6. Before closeout, merge lasting discoveries into the authoritative spec. Screenshots come from the application
   harness, never Computer Use or a desktop screenshot tool. Move a completed WIP plan to Done and remove its
   index row.

Use normative language only for observable requirements, ownership, compatibility, and required validation.
