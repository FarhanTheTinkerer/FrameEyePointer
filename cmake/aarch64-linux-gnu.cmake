# Cross-compile the Steam Frame build (ARM64 Linux) from an x86-64 Ubuntu 24.04 host:
#   sudo apt install g++-aarch64-linux-gnu
#   cmake -B build/frame -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake -DEYEPOINTER_BUILD_APP=OFF
# Ubuntu 24.04's cross toolchain has glibc 2.39, the same as SteamOS on the Frame.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
