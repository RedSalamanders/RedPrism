- Moved the vcpkg baseline (`vcpkg.json` `builtin-baseline`) and tool pin (`vcpkg-tool.json` `commit`), one value in
  this repository's toolchain contract, from `9eec002d` to `aeb162b7` (microsoft/vcpkg, 2026-10-22), RedSalamander's
  baseline and pin, so every repository uses one vcpkg commit (RedHome CV-4-07, register C-06 and C-07). Between the
  two commits only `7zip` and `sqlite3` change, and this repository uses neither, so no dependency version changes;
  the bootstrap stays on vcpkg-tool 2026-09-26. The managed checkout's scripts and triplets come from the new
  commit, so the first restore may rebuild the dependencies. `Specs/Build/Build_Toolchain.md` names the new commit.
  Compiler-free checks pass; native qualification runs on `main` after merge.
