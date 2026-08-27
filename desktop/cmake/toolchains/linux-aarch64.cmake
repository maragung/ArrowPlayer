set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
# On Ubuntu 22.04/24.04 the arm64 cross sysroot is /usr/aarch64-linux-gnu,
# but the actual libc and libm live under the multiarch subdirectory
# (lib/aarch64-linux-gnu/). Letting the compiler search its own sysroot
# via -rpath-link is more portable than pinning CMAKE_SYSROOT.
set(CMAKE_SYSROOT_COMPILE /usr/aarch64-linux-gnu CACHE PATH "sysroot at compile time")
set(CMAKE_SYSROOT_LINK /usr/aarch64-linux-gnu CACHE PATH "sysroot at link time")
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/aarch64-linux-gnu/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
