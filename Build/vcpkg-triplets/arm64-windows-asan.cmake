# RedPrism `ASan Debug` (ARM64): the same CRT and linkage as the stock arm64-windows triplet, with every library
# instrumented by AddressSanitizer like the application, mirroring x64-windows-asan.cmake. A static library built
# without instrumentation (Clipper2) would otherwise refuse to link against sanitized objects (LNK2038, STL
# container annotations). Decision D8 (Specs/Plans/WIP/ReviewFixes_2026-09-23.md).
# vcpkg-install.ps1 does not use this file in place: it copies it, followed by the Visual Studio installation and
# MSVC toolset pins of the build, to .build/vcpkg-triplets and passes that directory to vcpkg (Specs/Build/Build_Toolchain.md).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)
set(VCPKG_CXX_FLAGS "/fsanitize=address")
set(VCPKG_C_FLAGS "/fsanitize=address")
# vcpkg's Windows toolchain adds /RTC1 to debug builds, which cl.exe rejects together with /fsanitize=address.
# asan-toolchain.cmake is architecture-agnostic (it chainloads vcpkg's own windows.cmake, which reads
# VCPKG_TARGET_ARCHITECTURE above), so x64-windows-asan.cmake and this triplet share it unchanged.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/asan-toolchain.cmake")
# A chainloaded toolchain switches the Visual Studio environment off by default; the MSVC compiler still comes from it.
set(VCPKG_LOAD_VCVARS_ENV ON)
