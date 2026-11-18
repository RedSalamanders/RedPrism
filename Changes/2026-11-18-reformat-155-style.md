- Adopted the shared 155-line `.clang-format` of DxUi, RedSalamander, and RedMonitor (SHA-256 `768439d2…` as
  committed) and reformatted every C and C++ source under the `formatRoots` of `.ci/project.json` with the pinned
  clang-format 22.1.3: 989 files, each identical to `main` once whitespace is removed, and no include moved.
  `format.ps1` is now the shared script: it takes clang-format from `Install-CiFormatter` in the pinned
  `.ci-engine` instead of the Visual Studio, `PATH`, or standalone LLVM lookup, covers the eight extensions CI
  formats, and keeps `-Check`. `Find-RedPrismClangFormat` is removed; tooling evidence identifies the formatter by
  the engine's `Policy/clang-format.json`. `Build_Toolchain.md` and the BoundedClipper README describe it.
  `Common/BoundedClipper/clipper.*` is formatted like the other sources, not regenerated: its generator output
  no longer matches the committed files, which carry hand-made `/Wall` repairs (MergeReview MR-24, question Q14),
  so regenerating would drop them. Its manifests stay as stale as before (MR-21, MR-24).
  This is step CV-6-05 of the `RedHome` convergence plan (register D-01, D-02, D-05, D-06).
