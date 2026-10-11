# Bounded selection engine

This maintained source fork isolates Clipper2 2.0.1 in `RedPrismBoundedClipper`.
The ordinary vcpkg `Clipper2Lib` remains the independent reference and serves other
selection callers. Copyright headers and the complete Boost license are retained.

`BoundedSelectionBoolean` runs only after admission on a worker. Its fixed arena
covers engine containers, nodes, sort scratch and Debug iterator proxies. Allocation
or cancellation refusal unwinds into the scheduler's existing `bad_alloc` boundary.
The arena owns cleanup independently of incomplete engine links. No arena container,
iterator or node escapes. Final ordinary vectors have a separate admitted capacity
and immutable geometry/history lifetime. Explicit checkpoints count work; they do
not promise an allocator, OS, destructor or presentation deadline.

## Reproducing the engine sources

`Build/GenerateBoundedClipper.py` reproduces the four `clipper.*` files from the exact
upstream `Clipper2_2.0.1` sources. It verifies the input hashes, including the copied
containment helper, before transforming them. Output goes to a new directory so
existing sources cannot be replaced accidentally. Use the repository clang-format
and `.clang-format`, then compare the four resulting files and their manifest:

```powershell
python Build/GenerateBoundedClipper.py `
  --source <upstream-C++-directory> `
  --output .build/BoundedClipperReplay `
  --clang-format <VS-clang-format.exe> `
  --format-style .clang-format
```

The tool changes namespace/header guards, substitutes arena allocation and stable
sorting, inserts cancellation checkpoints, and makes failed construction cleanup
arena-owned. It preserves numerical conversion, winding, topology and equal-key
ordering. The precision-2 reference uses ClipperD's binary scale 128. `Workspace.h`
and the adapter are maintained separately. `generation-manifest.json` is source
provenance; compilation, numerical parity, admission and lifetime require tests.

Native `Document.Tests.WandBoolean*` tests retain independent archived behavioral reference literals,
compare all three operations with the original pinned engine, walk every reached
allocation/cancellation prefix, sweep 1,021 arena capacities and exercise large
collinear/disjoint inputs. Wand host tests separately cover FIFO, selection pixels,
Undo/Redo, original-ticket charges, copied/weak ownership and real Close.

Ordinary live-gesture migration, large supported-image acceptance and measured G1
remain tracked in the P1 plan. This engine is not a claim that those gates are closed.
