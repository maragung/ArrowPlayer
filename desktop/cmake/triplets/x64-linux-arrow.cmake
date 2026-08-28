# Arrow Player triplet: x64-linux — spec §3.1 target matrix.
#
# Linkage is decided per port by arrow-linkage.cmake, which encodes the
# Linkage column of the §4.2 dependency register.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CRT_LINKAGE dynamic)

include("${CMAKE_CURRENT_LIST_DIR}/arrow-linkage.cmake")
