# Build-system design and migration

## Reference

The restructuring follows the source/build/install-tree separation and header
file sets in Vito Gamberini's C++Now 2025 presentation, and the opt-in dependency
provider/passive-project approach illustrated by `beman.exemplar`.

- Presentation and slides: <https://schedule.cppnow.org/session/2025/post-modern-cmake/>
- Exemplar revision consulted: `32b69bd5846676013a204ce3c3fcbd6c074ea701`
  (<https://github.com/bemanproject/exemplar/tree/32b69bd5846676013a204ce3c3fcbd6c074ea701>)
- Beman CMake guidance: <https://bemanproject.org/docs/beman_standard/#cmake>

This application is not a Beman library and does not claim Beman conformance or
affiliation. No Beman implementation files were copied. The build code here is
an application-specific implementation of those patterns.

## Where things belong

```text
CMakeLists.txt                         project, public options, composition
CMakePresets.json                      developer/CI policy and workflows
dependencies.json                      full dependency revision pins
include/th/CMakeLists.txt              tinyhook::core header FILE_SET
src/host/CMakeLists.txt                host targets, private dependencies
src/windows/CMakeLists.txt             helper/DLL targets, required ABI settings
tests/CMakeLists.txt                   test targets and test-only dependencies
infra/cmake/use-fetch-content.cmake    optional dependency provider
infra/cmake/toolchains/                cross-target compiler selection
infra/cmake/install.cmake              components, exported package, file sets
infra/cmake/require-cxx20.cmake         diagnose unsupported environment, do not change it
```

There are no global compiler-flag assignments, `CACHE ... FORCE`, global IPO/CRT
choices, dependency downloads, or umbrella warning targets in the product
CMake files. The namespaced header target exports no compile policy.
`find_package` can be fulfilled by a package manager or the developer-selected
provider. Dependency options are scoped within its function, with CMP0077 NEW
for upstream projects that otherwise reset that policy. No third-party checkout
is written into the source tree.

The two private MinGW link options retained in the Windows target definition
select `wmain` and stable exported names. They are ABI/entry-point requirements,
not optimization or warning policy.

The CMake minimum is `3.30...4.0`, not an arbitrary demand for a 4.x executable.
The current local tool is CMake 3.31.6. CMake 4.0 CI is specified but not run in
this delivery; the version range is not evidence of a tested 4.0 build.

## Consumer and install behavior

Applications, tests, examples and installation default to `PROJECT_IS_TOP_LEVEL`.
Consumers can request them explicitly. GNUInstallDirs is respected; headers are
installed via `FILE_SET HEADERS`. `tinyhook::core` is available both in the build
tree and from the relocatable `tinyhook` config package. The package's version
compatibility is same-minor and its header-only export is architecture-independent.

The CMake contract test verifies a real install, moves the install prefix,
deletes its producer build directory, and builds/runs a fresh consumer through
`find_package`. Another consumer embeds TinyHook with `add_subdirectory` while
retaining its C++23/flags/CRT/IPO/shared-library settings. A C++17 configuration
must fail with a useful diagnostic, not silently select C++20. Local mock Luau, MinHook,
and spdlog projects test provider scoping without claiming to build them.

## Migration from 0.1

Use a fresh build directory. The old `TH_*` CMake options have been replaced by
`TINYHOOK_*` options. `TH_LTO` is removed: the appropriate CMake IPO variable
belongs to the preset, command line, or toolchain. The MinGW toolchains moved
from `cmake/` to `infra/cmake/toolchains/`. Presets place runtime outputs in
`build/<preset>/bin`; MSVC multi-config generators add the configuration folder.

Options:

- `TINYHOOK_BUILD_APPS`, `TINYHOOK_BUILD_TESTS`, `TINYHOOK_BUILD_EXAMPLES`, `TINYHOOK_INSTALL`
- `TINYHOOK_WITH_LUAU`, `TINYHOOK_WITH_COMPILER`, `TINYHOOK_BUILD_COMPILER_TOOL`

A packager-controlled relay-only build, without a provider or network:

```sh
cmake -S . -B build/custom -G Ninja \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DTINYHOOK_WITH_LUAU=OFF
cmake --build build/custom
ctest --test-dir build/custom --output-on-failure
```

All runtime application builds require a config package exposing
`spdlog::spdlog`. For a Luau build without the provider, supply `Luau::VM` and,
when needed, `Luau::Compiler`. The Windows build additionally needs
`minhook::minhook`. These are explicit package contracts, not a claim that every
upstream checkout ships a compatible installable config package.

## macOS MinGW archive sanity

The MinGW toolchain files deliberately select target binutils (`*-gcc-ar`,
`*-gcc-ranlib`, `*-nm`, and friends). A PE/COFF static archive must not be
indexed by `/usr/bin/ranlib` from macOS. The compiler-aware wrappers are also
required when the developer preset enables IPO/LTO.

If an older build directory was configured before 0.2.1, remove it before
reconfiguring because CMake caches the archive tools:

```sh
rm -rf build/windows-x86
cmake --workflow --preset windows-x86
```

During configuration, this is a useful one-line check:

```sh
grep -E 'CMAKE_(AR|RANLIB):FILEPATH' build/windows-x86/CMakeCache.txt
```

Both paths should name `i686-w64-mingw32-*`, never
`/Library/Developer/CommandLineTools/usr/bin/ranlib`.

## Homebrew i686 MinGW, `-Os`, and GCC PR125359

The MinGW cross presets intentionally disable CMake IPO/LTO, but that is not the
root cause of the 32-bit `std::__cxx11::basic_string(...&&)` link failure. GCC
PR125359 is a GCC 16/17 regression in the 32-bit MinGW path: `-Os` enables
`-fdeclone-ctor-dtor`, which can make a constructor clone call the unified C4
constructor symbol even though libstdc++ does not export that symbol.

TinyHook keeps `MinSizeRel`/`-Os` for the x86 Windows build and disables only the
faulty transformation with `-fno-declone-ctor-dtor`. This preserves the other
size optimizations. The x64 preset is unchanged.

If upgrading from 0.2.1 or 0.2.2, remove or reconfigure the x86 build directory
and do a clean rebuild:

```sh
rm -rf build/windows-x86
cmake --workflow --preset windows-x86
```

A verbose x86 C++ compile command should now contain both `-Os` and
`-fno-declone-ctor-dtor`, and should not contain `-flto`.
