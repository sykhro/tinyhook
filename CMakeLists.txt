cmake_minimum_required(VERSION 3.30...4.0)

project(
    tinyhook
    VERSION 0.3.0
    DESCRIPTION "Small text-capture client and native WebSocket host"
    LANGUAGES C CXX
)

option(TINYHOOK_BUILD_APPS "Build the applications for this target platform" ${PROJECT_IS_TOP_LEVEL})
option(TINYHOOK_BUILD_TESTS "Build TinyHook tests" ${PROJECT_IS_TOP_LEVEL})
option(TINYHOOK_BUILD_EXAMPLES "Build the Windows capture fixture" ${PROJECT_IS_TOP_LEVEL})
option(TINYHOOK_INSTALL "Generate TinyHook install rules" ${PROJECT_IS_TOP_LEVEL})
option(TINYHOOK_WITH_LUAU "Embed Luau in the native host" ON)
option(TINYHOOK_WITH_COMPILER "Accept Luau source as well as bytecode" ON)
option(TINYHOOK_BUILD_COMPILER_TOOL "Build the separate Luau bytecode compiler" OFF)

include(infra/cmake/require-cxx20.cmake)
include(GNUInstallDirs)

add_subdirectory(include/th)

if(TINYHOOK_BUILD_APPS)
    if(WIN32)
        add_subdirectory(src/windows)
    else()
        add_subdirectory(src/host)
    endif()
endif()

if(TINYHOOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TINYHOOK_INSTALL)
    include(infra/cmake/install.cmake)
endif()
