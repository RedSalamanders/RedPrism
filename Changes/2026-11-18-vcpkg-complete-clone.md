- Restores vcpkg from complete clones again. vcpkg 2026-07-27 extracts each versioned port with `git read-tree` and
  `git checkout-index` in an environment that holds only `GIT_INDEX_FILE`; without `SystemRoot` that Git resolves no
  host, so a blob a `--filter=blob:none` clone left on the server failed the restore with "Could not resolve host:
  github.com". It started on 2026-11-18, when microsoft/vcpkg `master` (de36db0455) stopped carrying the
  `vcpkg-cmake` tree the baseline selects, and it stopped every fresh restore, ours and the pinned DxUi's.
  - `vcpkg-install.ps1` clones completely, fetches the pinned commit only when the default branch lacks it and never
    with `--depth`, and completes a partial checkout from an earlier version in place (unshallow, then
    `fetch --refetch --no-filter`; the filter setting goes only afterwards).
  - The pinned DxUi `f2e328a0` still makes the partial clone, and moving the pin is out of scope (D20), so
    `restore-dxui.ps1` first prepares `<output>\vcpkg-tool` with `Build/VcpkgToolCheckout.psm1`: a complete, clean
    checkout at the commit the pin's `vcpkg-tool.json` names, which that installer then reuses. The module is the same
    file in the other DxUi consumers; DxUi's own fix is DxUi pull request 267.
  - `Tests/BuildProcessTests/VcpkgToolCheckoutTests.ps1` (offline `file://` fixture) covers a fresh clone, a rerun,
    completing a partial shallow clone, extracting an older tree with no network, and refusing another repository
    or a directory that is not a checkout. `VcpkgToolsetTests.ps1` pins the installer's clone, fetch and completion
    order. The native restore and build are proven by the post-merge qualification.
