if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows backend needs an x86 or x64 target toolchain")
endif()
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
    #if !defined(_M_IX86) && !defined(__i386__) && !defined(_M_X64) && !defined(__x86_64__)
    #error Select a Windows x86 or x64 target, not native Windows ARM.
    #endif
    int main() { return 0; }
]=] TINYHOOK_WINDOWS_X86_FAMILY)
if(NOT TINYHOOK_WINDOWS_X86_FAMILY)
    message(FATAL_ERROR "Select an x86 or x64 toolchain matching the game's PE architecture")
endif()

find_package(minhook CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(tinyhook.hook SHARED)
set_target_properties(tinyhook.hook PROPERTIES OUTPUT_NAME th-hook PREFIX "")
target_sources(
    tinyhook.hook
    PRIVATE hook.cpp
    PRIVATE FILE_SET HEADERS FILES win.hpp scan.hpp
)
target_link_libraries(tinyhook.hook PRIVATE tinyhook::core minhook::minhook spdlog::spdlog ws2_32)
target_include_directories(tinyhook.hook PRIVATE "${PROJECT_SOURCE_DIR}/src")

add_executable(tinyhook.client)
set_target_properties(tinyhook.client PROPERTIES OUTPUT_NAME th-client)
target_sources(
    tinyhook.client
    PRIVATE client.cpp
    PRIVATE FILE_SET HEADERS FILES win.hpp scan.hpp
)
target_link_libraries(tinyhook.client PRIVATE tinyhook::core spdlog::spdlog ws2_32)
target_include_directories(tinyhook.client PRIVATE "${PROJECT_SOURCE_DIR}/src")

# Entry point/export spelling is a binary interface requirement, not build policy.
if(MINGW)
    target_link_options(tinyhook.client PRIVATE -municode)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(tinyhook.hook PRIVATE LINKER:--kill-at)
    endif()
elseif(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_sources(tinyhook.hook PRIVATE hook-x86-msvc.def)
endif()

if(TINYHOOK_BUILD_EXAMPLES)
    add_executable(tinyhook.demo)
    set_target_properties(tinyhook.demo PROPERTIES OUTPUT_NAME th-demo)
    target_sources(tinyhook.demo PRIVATE demo.cpp PRIVATE FILE_SET HEADERS FILES win.hpp)
    target_link_libraries(tinyhook.demo PRIVATE tinyhook::core spdlog::spdlog)
    target_include_directories(tinyhook.demo PRIVATE "${PROJECT_SOURCE_DIR}/src")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(tinyhook.demo PRIVATE demo-x86-msvc.def)
    endif()
    if(TINYHOOK_INSTALL)
        install(TARGETS tinyhook.demo RUNTIME COMPONENT tinyhook-examples)
    endif()
endif()

if(TINYHOOK_INSTALL)
    install(TARGETS tinyhook.hook tinyhook.client RUNTIME COMPONENT tinyhook-runtime)
endif()
