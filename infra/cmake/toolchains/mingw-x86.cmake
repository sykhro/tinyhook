set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(_tinyhook_mingw_triplet i686-w64-mingw32)

find_program(CMAKE_C_COMPILER NAMES "${_tinyhook_mingw_triplet}-gcc" REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES "${_tinyhook_mingw_triplet}-g++" REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES "${_tinyhook_mingw_triplet}-windres" REQUIRED)

# CMake cannot use the host macOS archive tools for PE/COFF objects.  Use the
# compiler-aware MinGW wrappers so archives also work when a developer preset
# enables IPO/LTO.
find_program(CMAKE_AR NAMES "${_tinyhook_mingw_triplet}-gcc-ar" REQUIRED)
find_program(CMAKE_RANLIB NAMES "${_tinyhook_mingw_triplet}-gcc-ranlib" REQUIRED)
find_program(CMAKE_NM NAMES "${_tinyhook_mingw_triplet}-gcc-nm" REQUIRED)
find_program(CMAKE_STRIP NAMES "${_tinyhook_mingw_triplet}-strip" REQUIRED)
find_program(CMAKE_OBJCOPY NAMES "${_tinyhook_mingw_triplet}-objcopy" REQUIRED)
find_program(CMAKE_OBJDUMP NAMES "${_tinyhook_mingw_triplet}-objdump" REQUIRED)
find_program(CMAKE_DLLTOOL NAMES "${_tinyhook_mingw_triplet}-dlltool" REQUIRED)

# These are the tools CMake's GNU IPO rules use for static libraries.
set(CMAKE_C_COMPILER_AR "${CMAKE_AR}")
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_RANLIB}")
set(CMAKE_CXX_COMPILER_AR "${CMAKE_AR}")
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_RANLIB}")

unset(_tinyhook_mingw_triplet)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
