#----------------------------------------------------------------------------
# KieeKey v3.3.1 — MinGW-w64 x86_64 cross toolchain file
#
# Produces a fully STATIC Windows x64 build from any Linux/macOS host (and
# from Windows hosts that only have a Linux-style MinGW toolchain installed).
# The resulting binaries depend on nothing beyond the Windows system DLLs.
#
# Debian/Ubuntu packages providing this toolchain:
#   g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix
#   binutils-mingw-w64-x86-64  mingw-w64-x86-64-dev
#
# Usage:
#   cmake -S . -B build-mingw -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   cmake --build build-mingw
#
# NOTE: the *posix* threading variant of MinGW-w64 is REQUIRED — this
# project uses std::thread/std::mutex extensively; the win32 variant of the
# runtime does not provide them.
#----------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Override on the command line when the toolchain lives elsewhere:
#   -DMINGW_TOOLCHAIN_PREFIX=/usr/bin/x86_64-w64-mingw32-
if(NOT DEFINED MINGW_TOOLCHAIN_PREFIX)
    set(MINGW_TOOLCHAIN_PREFIX "/usr/bin/x86_64-w64-mingw32-"
        CACHE STRING "Prefix of the x86_64 MinGW-w64 posix toolchain")
endif()

set(CMAKE_C_COMPILER   "${MINGW_TOOLCHAIN_PREFIX}gcc-posix")
set(CMAKE_CXX_COMPILER "${MINGW_TOOLCHAIN_PREFIX}g++-posix")
set(CMAKE_RC_COMPILER  "${MINGW_TOOLCHAIN_PREFIX}windres")

# Sanity: windres shells out to ${prefix}gcc for preprocessing the .rc.
# Debian's alternatives normally provide the unsuffixed name; a hand-extracted
# toolchain may only ship the -posix variants — create the symlink then:
#   ln -s x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc
if(NOT EXISTS "${MINGW_TOOLCHAIN_PREFIX}gcc")
    if(EXISTS "${MINGW_TOOLCHAIN_PREFIX}gcc-posix")
        message(WARNING "MinGW windres needs '${MINGW_TOOLCHAIN_PREFIX}gcc' "
                        "(unsuffixed) as its preprocessor. Symlink it to the "
                        "-posix variant if the .rc compile fails to find windows.h.")
    endif()
endif()
set(CMAKE_AR           "${MINGW_TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${MINGW_TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_STRIP        "${MINGW_TOOLCHAIN_PREFIX}strip")
set(CMAKE_OBJDUMP      "${MINGW_TOOLCHAIN_PREFIX}objdump")

# Search only inside the MinGW sysroot for headers/libs/packages; run host
# programs (ninja, etc.) from the plain PATH.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cache the static-runtime contract so it cannot drift per-config.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
