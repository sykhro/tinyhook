# Building and packaging

The [README](../README.md#build) covers the standard builds. Presets choose the
compiler flags, build type, runtime linkage, and dependency provider. Each uses
its own directory under `build/`.

## Presets

| Preset | Target |
| --- | --- |
| `macos-small` | macOS host with Luau source support |
| `macos-vm` | macOS bytecode-only host and separate `th-compile` tool |
| `linux-small` | Linux host with Luau source support |
| `linux-relay` | Linux host without Luau |
| `linux-sanitize` | Linux relay with Clang, ASan, and UBSan |
| `clang-x86`, `clang-x64` | Native Windows helpers with clang-cl and LLD |
| `msvc-x86`, `msvc-x64` | Native Windows helpers with Visual Studio 2022 |
| `windows-x86`, `windows-x64` | Windows helpers cross-compiled with MinGW |
| `system` | Build using installed dependency packages |

Run a native preset with `cmake --workflow --preset NAME` to configure, build,
and test. MinGW cross workflows build without running the Windows executables.
For clang-cl, use an MSVC developer environment matching the target architecture.

## Dependencies

The development presets select `infra/cmake/use-fetch-content.cmake` as a CMake
dependency provider. It fetches the revisions in `dependencies.json` into the
build tree. The manifest also records license names and license filenames;
CMake uses it to generate `THIRD_PARTY.md` in the build directory.

For an offline build, point the provider at existing source trees:

```sh
cmake --preset macos-small \
  -DFETCHCONTENT_SOURCE_DIR_LUAU=/path/to/luau \
  -DFETCHCONTENT_SOURCE_DIR_SPDLOG=/path/to/spdlog
cmake --build --preset macos-small
ctest --preset macos-small
```

Windows builds also accept `FETCHCONTENT_SOURCE_DIR_MINHOOK`.

To use a package manager instead of fetching dependencies:

```sh
cmake --preset system -DCMAKE_PREFIX_PATH=/path/to/packages
cmake --build --preset system
ctest --preset system
```

Packages must expose `spdlog::spdlog`, `minhook::minhook` for Windows, and
`Luau::VM` plus `Luau::Compiler` for the relevant host features. Luau's upstream
targets are named `Luau.VM` and `Luau.Compiler`; the fetching provider supplies
aliases, and a packaged build needs equivalent targets.

## Options

| Option | Default | Purpose |
| --- | --- | --- |
| `TINYHOOK_BUILD_APPS` | Top-level builds | Build the host or Windows helpers |
| `TINYHOOK_BUILD_TESTS` | Top-level builds | Build and register tests |
| `TINYHOOK_BUILD_EXAMPLES` | Top-level builds | Build the Windows capture fixture |
| `TINYHOOK_INSTALL` | Top-level builds | Add installation rules |
| `TINYHOOK_WITH_LUAU` | `ON` | Embed Luau in the host |
| `TINYHOOK_WITH_COMPILER` | `ON` | Accept Luau source as well as bytecode |
| `TINYHOOK_BUILD_COMPILER_TOOL` | `OFF` | Build the separate `th-compile` tool |

For example, add `-DTINYHOOK_WITH_LUAU=OFF` when configuring a relay-only host.
The application, test, example, and install options default to `OFF` when
TinyHook is included with `add_subdirectory`.

## Installation

```sh
cmake --install build/macos-small --prefix dist --strip --component tinyhook-runtime
```

| Component | Contents |
| --- | --- |
| `tinyhook-runtime` | Host or Windows helpers, profiles, and license notices |
| `tinyhook-tools` | Separate bytecode compiler, when enabled |
| `tinyhook-examples` | Windows fixture, when enabled |
| `tinyhook-development` | Headers and CMake package |

The fetching provider installs the complete license files for dependencies it
supplies. Package-manager builds must arrange those files through their package
manager. The generated manifest lists all declared dependencies.

Consume the header library with:

```cmake
find_package(tinyhook CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE tinyhook::core)
```

The target exports header paths. Set C++20 or newer, warnings, optimization, and
runtime linkage in the consuming project.

## MinGW troubleshooting

The cross toolchains select target binutils, including `gcc-ar` and `gcc-ranlib`.
If a cached build selects macOS archive tools, reconfigure in a fresh build
directory. Inspect `CMAKE_AR` and `CMAKE_RANLIB` in `CMakeCache.txt`; their paths
should begin with the target triplet, such as `i686-w64-mingw32-`.

The x86 preset disables `-fdeclone-ctor-dtor` to work around GCC PR125359, which
can cause unresolved `std::string` constructor symbols under `-Os`. It keeps
size optimization enabled. Both MinGW presets disable IPO.
