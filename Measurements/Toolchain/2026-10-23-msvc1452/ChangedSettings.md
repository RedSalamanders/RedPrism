# RedPrism before/after tool metadata changes

This table groups every changed captured field by tool. changed-tool-settings.csv maps each change to project/configuration/platform and full before/after values. The comparison uses each project/profile field value set; full-tool-metadata.csv.gz retains exact per-file assignments. Literal unset/default differences and dependency-output fingerprints are preserved, not replaced with assumed compiler behavior. No native source files changed.

| Tool / field | Changed project-profile rows | Configurations | Before values | After values |
| --- | ---: | --- | --- | --- |
| ClCompile / AdditionalIncludeDirectories | 12 | ASan Debug, Debug, Release | project/file-specific; see CSV | project/file-specific; see CSV |
| ClCompile / ForcedIncludeFiles | 2 | ASan Debug | Z:\src\RedPrism\.build\dependencies\DxUi\source\8e8994079e82f79e6765e6a885c03fc687957591\src/Support/RequireAddressSanitizer.h;&lt;REPO&gt;\Common\RequireAddressSanitizer.h; | &lt;REPO&gt;\.build\dependencies\DxUi\source\8e8994079e82f79e6765e6a885c03fc687957591\src/Support/RequireAddressSanitizer.h;&lt;REPO&gt;\Common\RequireAddressSanitizer.h; |
| ClCompile / WholeProgramOptimization | 8 | Release | false | true |
| Lib / LinkTimeCodeGeneration | 8 | Release | &lt;unset&gt; | true |
| Link / LinkTimeCodeGeneration | 9 | Release | &lt;unset&gt;; UseFastLinkTimeCodeGeneration | UseLinkTimeCodeGeneration |
