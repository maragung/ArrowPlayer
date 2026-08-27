set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
# On Ubuntu 22.04/24.04 the arm64 cross sysroot is /usr/aarch64-linux-gnu,
# but the actual libc and libm live under the multiarch subdirectory
# (lib/aarch64-linux-gnu/). We pass --sysroot to the compiler and add
# the multiarch path to FIND_ROOT_PATH so the cross libraries resolve
# without an explicit rpath.
set(CMAKE_SYSROOT /usr/aarch64-linux-gnu CACHE PATH "Cross compiler sysroot")
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/aarch64-linux-gnu/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Append the multiarch library path to the link search list so the
# cross linker resolves libm/libc/libpthread against the real path
# on Ubuntu 22.04/24.04 rather than the top-level /lib/ that the
# sysroot points at.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L/usr/aarch64-linux-gnu/lib/aarch64-linux-gnu")
