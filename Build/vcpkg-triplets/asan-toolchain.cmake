# Chainloaded by x64-windows-asan.cmake: vcpkg's own Windows toolchain (from the managed checkout under
# .build/vcpkg-tool/), then /RTC1 dropped from the debug flags because it is incompatible with /fsanitize=address.
include("${CMAKE_CURRENT_LIST_DIR}/../../.build/vcpkg-tool/scripts/toolchains/windows.cmake")
foreach(lang C CXX)
    string(REPLACE "/RTC1" "" _redprism_debug_flags "${CMAKE_${lang}_FLAGS_DEBUG}")
    set(CMAKE_${lang}_FLAGS_DEBUG "${_redprism_debug_flags}" CACHE STRING "" FORCE)
endforeach()
unset(_redprism_debug_flags)
