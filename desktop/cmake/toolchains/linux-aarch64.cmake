set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
# On Ubuntu 22.04/24.04 the arm64 cross sysroot is /usr/aarch64-linux-gnu,
# but the actual libc and libm live under the multiarch subdirectory
# (lib/aarch64-linux-gnu/). Pointing the sysroot at the multiarch
# directory is the cleanest fix: --sysroot and -L both pick up the
# right path, and the FIND_ROOT_PATH that CMake auto-derives from
# the sysroot matches the one the linker searches.
set(CMAKE_SYSROOT /usr/aarch64-linux-gnu/lib/aarch64-linux-gnu CACHE PATH "Cross compiler sysroot")
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu/lib/aarch64-linux-gnu /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
