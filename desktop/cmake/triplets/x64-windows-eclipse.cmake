# Eclipse Player triplet: x64-windows — spec §3.1 target matrix.
#
# Linkage is decided per port by eclipse-linkage.cmake, which encodes the
# Linkage column of the §4.2 dependency register.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)  # must match Qt's dynamic CRT (§4.3)

include("${CMAKE_CURRENT_LIST_DIR}/eclipse-linkage.cmake")
