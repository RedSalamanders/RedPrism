# RedPrism `ASan Debug` (x64): the same CRT and linkage as the stock x64-windows triplet, with every library
# instrumented by AddressSanitizer like the application. A static library built without instrumentation (Clipper2)
# would otherwise refuse to link against sanitized objects (LNK2038, STL container annotations).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)
set(VCPKG_CXX_FLAGS "/fsanitize=address")
set(VCPKG_C_FLAGS "/fsanitize=address")
# vcpkg's Windows toolchain adds /RTC1 to debug builds, which cl.exe rejects together with /fsanitize=address.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/asan-toolchain.cmake")
# A chainloaded toolchain switches the Visual Studio environment off by default; the MSVC compiler still comes from it.
set(VCPKG_LOAD_VCVARS_ENV ON)
