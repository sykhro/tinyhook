# 0.3.0

- Add automatic engine detection without moving Luau into the Windows client. `th-host --script-dir DIR` loads a catalog of engine profiles; `th-client probe` and `attach` request that catalog and read-only probe candidates by priority.
- Extend the top-level Luau DSL with `id`, `engine`, and `processes`. Process names are case-insensitive basename hints used only for ordering: exact-name profiles are tried first, generic profiles second, and explicit non-matches last. Hook selectors/signatures remain authoritative.
- Fail closed on ambiguous detection. A priority tier must contain exactly one fully matching profile; otherwise use `--engine PROFILE_ID` to override explicitly.
- Route capture events to the Luau profile selected by the client. Profile identity includes both the profile ID and hook configuration so profiles may share hook specs but use different `on_text` filters.
- Keep the existing `--script FILE` workflow as a one-profile catalog and retain the legacy direct-config role for compatibility diagnostics.
- Rename the Angel Beats-specific script to the engine-oriented `scripts/engines/siglusengine4-x86.luau`; Angel Beats! -1st beat- is documented as a validated SiglusEngine4 consumer rather than the profile name.

# 0.2.4

- Make spdlog 1.17.0 a required runtime dependency for host/client/DLL/tools and route diagnostics through spdlog instead of ad-hoc stderr/debug strings. Set `TINYHOOK_LOG=debug` (or another spdlog level) for more detail.
- Log successful config/capture/WebSocket authentication on the native host so a probe is visible even when no hook DLL is injected.
- Port the Angel Beats!/SiglusEngine4 matching behavior from Chenx221/Textractor more faithfully: match the unique pre-call signature and hook immediately after the call. The old trial-specific post-call `expected` bytes are no longer required.
- Keep mid-function hooks fail-closed: omitting `expected` is accepted only for x86 pattern-based hooks with at least 12 concrete signature bytes; ambiguous signatures are still rejected.

# 0.2.3

- Work around GCC PR125359 for 32-bit MinGW size builds. GCC 16/17 can emit references to a non-exported unified C4 constructor when `-Os` enables constructor/destructor decloning; this manifests as unresolved `std::__cxx11::basic_string(...&&)` symbols. The `windows-x86` developer preset keeps `-Os` and adds `-fno-declone-ctor-dtor`.
- Correct the 0.2.2 diagnosis: disabling IPO/LTO removed `*.ltrans.o` from the failure, but the underlying unresolved constructor was independent of LTO.
- Leave x64 MinGW unchanged because the published PR125359 reproducer and failure are specific to the 32-bit MinGW path.

# 0.2.2

- Disable CMake IPO/LTO in the MinGW x86 and x64 cross-build presets. Homebrew's i686 MinGW toolchain can emit unresolved libstdc++ C++11-ABI string symbols from GCC LTO partitions when linking fully static binaries.
- Keep IPO as an explicit developer-preset policy for native macOS/Linux and MSVC builds instead of inheriting it from the common preset.
- Preserve static `libgcc`/`libstdc++` linkage for the Windows helpers.

# TinyHook 0.2.1

- Fix macOS -> MinGW cross-builds by selecting target `gcc-ar`, `gcc-ranlib`, and related binutils in both MinGW toolchains. This also makes static archives valid when the developer presets enable IPO/LTO.
- Avoid GCC `-Wcast-function-type` for the optional `IsWow64Process2` lookup while preserving the Win32 `GetProcAddress` calling convention.

# TinyHook 0.2.0

## Windows x86 and game profile

- Add an x86 observer gateway with general-register/flags and x87/SSE preservation.
- Add x86 registers, target-width stack/pointer reads, signed hook offsets,
  guarded mid-function locations, and explicit-length MSVC x86 wide-string layout.
- Add a read-only `probe` and reject helper/game architecture mismatches.
- Add an x86 fixture script and an Angel Beats! -1st beat- **candidate** profile
  based on the upstream trial SiglusEngine4 disassembly.
- Version the incompatible architecture-aware wire format as THK2/config 2.

## Build system

- Replace global forced settings with developer presets, passive product targets,
  C++20 feature checks, and optional packager-controlled dependencies.
- Use `find_package`; an explicitly enabled provider fetches full pinned source
  revisions with function-local options and supplies upstream target adapters.
- Use header file sets and install/export a relocatable `tinyhook::core` package.
- Add native macOS, MinGW x86/x64, MSVC x86/x64, relay, and sanitizer workflows.
- Test source composition, installed consumption, relocation, and provider scoping.

## Verification

Linux release and ASan/UBSan suites pass 6/6 tests each. Windows/macOS builds,
actual Luau/MinHook compilation, Wine injection, CMake 4.0, and the retail game
remain unverified. See VERIFICATION.md and its retained transcripts.
