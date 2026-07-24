# Cross-compiles the runtime for Windows with MinGW-w64 from a Linux host.
#
# This exists to compile the renderer for Windows without owning a Windows
# machine. It is not the shipping Windows toolchain: MSVC is (see the CI
# workflow's windows leg). What this catches is the portability class that
# does not need MSVC to find, which is most of it -- LLP64 assumptions,
# missing includes that glibc provided transitively, POSIX calls, and
# anything that assumes a case-sensitive filesystem or an ELF shared object.
#
# SDL3 is not packaged for MinGW by Fedora, so point SENCHA_MINGW_SDL3_ROOT at
# an extracted SDL3-devel-<version>-mingw archive:
#
#   cmake -B build-mingw -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#     -DSENCHA_MINGW_SDL3_ROOT=/path/to/SDL3-3.2.24
#
# Shaders are compiled by the host glslc: it is a build tool, not a target
# binary, so program lookup deliberately stays on the host paths.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(SENCHA_MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${SENCHA_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${SENCHA_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${SENCHA_MINGW_PREFIX}-windres)

set(SENCHA_MINGW_SYSROOT /usr/${SENCHA_MINGW_PREFIX}/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH ${SENCHA_MINGW_SYSROOT})

# GCC auto-vectorizes to 256-bit AVX at -O2/-O3 and spills the YMM registers
# to the stack with 32-byte-aligned stores (vmovdqa %ymm). The Windows x64 ABI
# guarantees only 16-byte stack alignment, and GCC's realignment for the
# 32-byte case does not hold on this target, so a vectorized frame-path
# function faults on its first spill. Confirmed by reproduction: the renderer
# crashed on the first frame with a 256-bit spill and ran clean once 256-bit
# stack temporaries were removed. Capping the vector width to 128 bits keeps
# vectorization within the guaranteed alignment. MSVC handles this correctly
# and needs no equivalent.
string(APPEND CMAKE_C_FLAGS_INIT " -mprefer-vector-width=128")
string(APPEND CMAKE_CXX_FLAGS_INIT " -mprefer-vector-width=128")

if(SENCHA_MINGW_SDL3_ROOT)
    list(APPEND CMAKE_FIND_ROOT_PATH
         ${SENCHA_MINGW_SDL3_ROOT}/${SENCHA_MINGW_PREFIX})
    set(SDL3_DIR
        ${SENCHA_MINGW_SDL3_ROOT}/${SENCHA_MINGW_PREFIX}/lib/cmake/SDL3
        CACHE PATH "SDL3 CMake package directory for the MinGW target")
endif()

# Host tools stay on the host; everything linked into the target comes from
# the cross sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
