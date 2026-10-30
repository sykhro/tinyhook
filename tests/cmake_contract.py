#!/usr/bin/env python3
"""Offline build-system tests. Dependency-provider fixtures are NOT real dependencies."""
import pathlib
import json
import shutil
import subprocess
import sys
import tempfile


def main():
    cmake, source, cxx, cc = sys.argv[1:]
    source = pathlib.Path(source).resolve()
    with tempfile.TemporaryDirectory(prefix="tinyhook-cmake-") as temp:
        root = pathlib.Path(temp)

        def run(*args, success=True):
            p = subprocess.run([str(a) for a in args], text=True, capture_output=True, timeout=60)
            if success and p.returncode:
                raise RuntimeError(p.stdout + p.stderr)
            if not success and not p.returncode:
                raise AssertionError("configuration should have been rejected")
            return p

        def configure(src, binary, *options, success=True):
            return run(cmake, "-S", src, "-B", binary, "-G", "Ninja",
                       f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_C_COMPILER={cc}",
                       "-DCMAKE_CXX_STANDARD=23", "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF", *options, success=success)

        disabled = ["-DTINYHOOK_BUILD_APPS=OFF", "-DTINYHOOK_BUILD_TESTS=OFF", "-DTINYHOOK_BUILD_EXAMPLES=OFF"]
        result = configure(source, root / "bad-dialect", *disabled,
                           "-DCMAKE_CXX_STANDARD=17", success=False)
        assert "requires a C++20-capable" in result.stdout + result.stderr

        configure(source, root / "producer", *disabled, "-DTINYHOOK_INSTALL=ON", "-DCMAKE_INSTALL_LIBDIR=lib")
        notices = (root / "producer" / "THIRD_PARTY.md").read_text()
        manifest = json.loads((source / "dependencies.json").read_text())
        assert notices.count("\n## ") == len(manifest)
        for package, dependency in manifest.items():
            assert f"## {package}\n" in notices
            assert f"License: {dependency['license']}." in notices
            assert f"Revision: `{dependency['git_tag']}`." in notices
            url = dependency['git_repository'].removesuffix('.git')
            assert f"{url}/blob/{dependency['git_tag']}/{dependency['license_file']}" in notices
        run(cmake, "--build", root / "producer")
        run(cmake, "--install", root / "producer", "--prefix", root / "stage-a")
        installed_notices = root / "stage-a" / "share" / "licenses" / "tinyhook" / "THIRD_PARTY.md"
        assert installed_notices.read_text() == notices
        (root / "stage-a").rename(root / "stage-b")
        shutil.rmtree(root / "producer")  # Installed package must not require the build tree.

        main_cpp = '''#include <th/protocol.hpp>
#ifndef TINYHOOK_EXTERNAL_POLICY
#error Consumer compiler policy was lost
#endif
int main() { th::Bytes b; th::u32(b,42); return th::Reader(b).get32()!=42; }
'''
        assertions = '''
foreach(prop INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_FEATURES INTERFACE_LINK_OPTIONS)
    get_target_property(value tinyhook::core "${prop}")
    if(value)
        message(FATAL_ERROR "Policy leaked through ${prop}: ${value}")
    endif()
endforeach()
if(NOT CMAKE_CXX_STANDARD STREQUAL "23" OR NOT BUILD_SHARED_LIBS OR CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    message(FATAL_ERROR "Consumer policy was modified")
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tinyhook::core)
'''
        consumer = root / "consumer"
        consumer.mkdir()
        (consumer / "main.cpp").write_text(main_cpp)
        (consumer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.30...4.0)
project(consumer LANGUAGES CXX)
find_package(tinyhook 0.3 CONFIG REQUIRED)
''' + assertions)
        policy = ["-DBUILD_SHARED_LIBS=ON", "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF", "-DCMAKE_CXX_FLAGS=-DTINYHOOK_EXTERNAL_POLICY=731"]
        configure(consumer, root / "consumer-build", f"-DCMAKE_PREFIX_PATH={root / 'stage-b'}", *policy)
        run(cmake, "--build", root / "consumer-build")
        run(root / "consumer-build" / "consumer")

        parent = root / "parent"
        parent.mkdir()
        (parent / "main.cpp").write_text(main_cpp)
        (parent / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.30...4.0)
project(parent LANGUAGES C CXX)
set(old_flags "${CMAKE_CXX_FLAGS}")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
add_subdirectory("''' + source.as_posix() + '''" child)
if(TARGET tinyhook.host OR TARGET tinyhook.tests.core OR TARGET tinyhook.demo)
    message(FATAL_ERROR "Subproject enabled applications/tests/examples by default")
endif()
if(NOT CMAKE_CXX_FLAGS STREQUAL old_flags OR NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
    message(FATAL_ERROR "Subproject changed flags or the CRT selection")
endif()
''' + assertions)
        configure(parent, root / "parent-build", *policy)
        run(cmake, "--build", root / "parent-build")
        run(root / "parent-build" / "consumer")

        # Test the provider's mechanics without downloading/pretending to compile
        # Luau, MinHook, or spdlog. Local tiny fixtures expose the relevant CMake contracts.
        luau, mh, spd, provider = (root / name for name in ("mock-luau", "mock-minhook", "mock-spdlog", "provider"))
        for d in (luau, mh, spd, provider):
            d.mkdir()
        (luau / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.10)
project(mock_luau LANGUAGES CXX)
option(LUAU_BUILD_TESTS "fixture" ON)
if(LUAU_BUILD_TESTS)
    message(FATAL_ERROR "Provider failed to scope dependency options")
endif()
add_library(Luau.VM INTERFACE)
add_library(Luau.Compiler INTERFACE)
''')
        (mh / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.0...3.5)
project(mock_minhook LANGUAGES C)
option(BUILD_SHARED_LIBS "fixture" ON)
if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "Provider failed to scope dependency options")
endif()
add_library(minhook INTERFACE)
''')
        (spd / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.10)
project(mock_spdlog LANGUAGES CXX)
option(SPDLOG_BUILD_TESTS "fixture" ON)
if(BUILD_SHARED_LIBS OR SPDLOG_BUILD_TESTS)
    message(FATAL_ERROR "Provider failed to scope spdlog/static options")
endif()
add_library(spdlog INTERFACE)
add_library(spdlog::spdlog ALIAS spdlog)
''')
        (provider / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.30...4.0)
project(provider_fixture LANGUAGES C CXX)
find_package(Luau CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Luau CONFIG REQUIRED)
if(NOT TARGET Luau::VM OR NOT TARGET Luau::Compiler OR NOT TARGET minhook::minhook OR NOT TARGET spdlog::spdlog)
    message(FATAL_ERROR "Missing dependency aliases/targets")
endif()
if(NOT BUILD_SHARED_LIBS OR NOT LUAU_BUILD_TESTS OR NOT SPDLOG_BUILD_TESTS)
    message(FATAL_ERROR "Dependency options escaped their function scope")
endif()
''')
        configure(provider, root / "provider-build",
                  f"-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={source / 'infra/cmake/use-fetch-content.cmake'}",
                  f"-DFETCHCONTENT_SOURCE_DIR_LUAU={luau}", f"-DFETCHCONTENT_SOURCE_DIR_MINHOOK={mh}",
                  f"-DFETCHCONTENT_SOURCE_DIR_SPDLOG={spd}",
                  "-DBUILD_SHARED_LIBS=ON", "-DLUAU_BUILD_TESTS=ON", "-DSPDLOG_BUILD_TESTS=ON")
        print("PASS: dialect diagnostic; install/relocate/find_package/build/run;")
        print("      passive imported target and add_subdirectory consumer; opt-in provider scoping (mock Luau/MinHook/spdlog packages)")


if __name__ == "__main__":
    main()
