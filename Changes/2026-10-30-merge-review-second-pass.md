- Extend the merge-review plan to PRs #19–#22 and re-anchor every row at `b6ec336`. PR #19 changed no product
  behavior. The pass adds:
  - **CI-8**: `package.ps1` still reads DxUi's license from the old 40-character source folder, so packaging
    fails.
  - **CI-9**: with `/WX` off, a second incremental build passes the zero-warning gate.
  - **CI-10**: `Core_Changes.md` describes push checks that `ci.yml` never runs.
  - **MR-24**: hand-edited generated BoundedClipper sources whose manifest no longer matches.
  - **MR-25**: the checkerboard fixture's unexplained Release failure matches MR-03's memory admission.
  - **AR-12**: replace 767 scoped implicitly-deleted suppressions (about 3,900 lines) with a policy entry.
  - **AR-13**: tooling consolidation.
  - **Q13–Q16**: questions on these items.

  It closes CI-2 and CI-3 and corrects four first-pass counts. Review only: nothing was built or run.
