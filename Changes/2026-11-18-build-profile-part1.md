- Moved the build profile out of the five project files into `Directory.Build.props`: the toolset (`v145`), the
  Windows SDK (`10.0.26100.0`), the Unicode character set, the debug-library choice, the runtime library (`/MDd`,
  `/MD`) and the optimization level (`/Od`, `/O2`), so a project file holds only what is its own (`RedHome`
  convergence CV-6-04 part 1, register C-11). `Build/NativeBuildPolicy.targets` stays equal to the copy in the
  pinned DxUi commit, which compiles the DxUi archive and is hashed into its build identity; it takes DxUi's newer
  text with the next DxUi pin.
  No compiler or linker input changes: an MSBuild evaluation of the five projects in the six profiles before and
  after shows the same properties read by the build targets and the same metadata on every compile, link,
  library, resource, manifest and build-event item.
