# Release build-duration observations

| Before seconds | After seconds | Scope |
| ---: | ---: | --- |
| 537.7 | 587.42 | Captured build-wrapper wall time |

Descriptive single-build observations only. Build caches, restore costs, concurrent workloads and clean/incremental state were not controlled across this pair. No build-time regression, improvement or compiler-version effect is established.

Both builds use MSVC 14.52.37025. Exact receipt SHA256 values and source paths are retained in build-duration-observations.json in this owner''s external report folder. These times do not replace the frozen paired runtime fixture experiment.

Captured command outcomes: before=True; after=False.

The original candidate command linked its images but failed the post-link ISA guard; that failed duration remains explicit. The reviewed exact UCRT-wrapper guard and later six-profile qualification passed separately.
