# TinyHook 0.3.0 — C++20 / Luau / Wine / WebSocket

Copyright (C) 2026 sykhro. Licensed under [GNU GPLv3](LICENSE) (GPL-3.0-only).

CLI text capture with a native macOS host and **Windows x86 or x64** helpers.
Luau stays outside the game; the injected DLL contains native capture code and
MinHook, not a scripting runtime or browser engine.

**Status:** experimental. The local Linux protocol/gateway/packaging tests pass.
The macOS host and Windows x86 client/DLL have also been built under the target
CrossOver workflow, and the SiglusEngine4 profile has passed a read-only probe
against the retail Angel Beats! -1st beat- process. These are prior local observations; CI verifies the automated build and test suite.

Engine profiles live under `scripts/engines/`. Run the host with `--script-dir`
to expose the profile catalog; `th-client probe`/`attach` then detect the target
engine automatically. Process-name metadata only changes the order in which
profiles are tried: byte signatures/selectors remain authoritative.

For the currently validated SiglusEngine4 profile, see
[docs/SIGLUSENGINE4.md](docs/SIGLUSENGINE4.md).

## Architecture

```text
macOS native host (arm64 or x86_64)
  th-host + Luau VM
    --script FILE       -> one profile
    --script-dir DIR    -> engine-profile catalog
    immutable extraction specifications
    on_text(copied_text, metadata) -> string or nil
    authenticated TCP: 127.0.0.1:9002
    authenticated WebSocket: 127.0.0.1:9001 -> Texthooker UI
                    ^
                    | loopback TCP, fixed-width versioned wire format
                    |
same Wine/CrossOver bottle as game
  th-client.exe list / probe / attach / stop
    probe/attach request the catalog, prioritize by process name, then
    read-only probe each candidate until one priority tier has a unique match
  game.exe (x86 OR x64)
    th-hook.dll (same Windows architecture as game)
      native gateway -> bounded immediate copy -> bounded queue -> sender
```

The host does not dereference a Wine pointer from macOS. It receives copied text.
Callbacks do not call Luau, allocate dynamically, or write to sockets. A full or
contended queue drops the snapshot rather than blocking a game thread on the
network. Bounded memory reads, hooks, and thread suspension still have overhead.
This is not a real-time guarantee or a Frida-compatible replacement.

## Build

GitHub Actions installs the latest stable upstream LLVM/Clang and CMake on
Linux, macOS, and Windows. Windows CI tests both x86 and x64 with clang-cl;
Linux also runs ASan/UBSan, and macOS checks VM-only compiled profiles.
Runtime packages and their license notices are uploaded as CI artifacts.

Requires a C++20-capable compiler and standard library, **CMake 3.30+**, Ninja,
and Git for the opt-in fetching provider. On macOS, install Xcode Command Line
Tools and the build tools, for example:

```sh
brew install cmake ninja mingw-w64
```

The presets, not the product CMake files, choose C++ mode, warnings, IPO, size
optimization, CRT, dependency provider, output folders, and toolchains.

### macOS host and Windows x86 helpers

From the extracted source directory:

```sh
cmake --workflow --preset macos-small
cmake --workflow --preset windows-x86
```

Expected artifacts:

```text
build/macos-small/bin/th-host
build/windows-x86/bin/th-client.exe
build/windows-x86/bin/th-hook.dll
build/windows-x86/bin/th-demo.exe
```

`windows-x86` selects the complete `i686-w64-mingw32` toolchain, including
target `gcc-ar`/`gcc-ranlib` rather than the macOS archive tools. For a 64-bit Windows game,
use `windows-x64` instead. **Do not build the macOS host as i386.** The host and
helper architectures need not match; the helper, DLL and target game must match.
Use the game's existing bottle/prefix. Do not change a working game's Wine prefix
architecture merely to build or run these helpers.

The `windows-x86` preset also carries `-fno-declone-ctor-dtor` for GCC PR125359.
Current GCC 16/17 MinGW builds can otherwise fail under CMake's `MinSizeRel`
`-Os` with unresolved `std::__cxx11::basic_string(...&&)` C4 constructor symbols.
This retains the rest of `-Os` instead of switching the whole x86 build to `-O2`.

The Windows cross-build workflows compile but do **not** try to execute Windows
binaries on macOS. The native `msvc-x86` and `msvc-x64` workflows, using Visual
Studio 2022 on Windows, also run the fixture injection test. These workflows
remain available for local use. CI uses the `clang-x86` and `clang-x64`
workflows with clang-cl and LLD in a matching MSVC developer environment.

### Smaller VM-only host

```sh
cmake --workflow --preset macos-vm
mkdir -p compiled-profiles
./build/macos-vm/bin/th-compile scripts/engines/siglusengine4-x86.luau compiled-profiles/siglusengine4-x86.luauc
./build/macos-vm/bin/th-host --script-dir compiled-profiles --token-file "$PWD/session.token"
```

Only `th-host` is needed at runtime. `th-compile` is a separate build-time tool.
Compiler and VM must come from the same pinned Luau revision. Load only scripts
and bytecode you trust. No Luau analyzer or native-code JIT is linked.

### Installed packages / package managers

The runtime targets require spdlog and call `find_package(spdlog CONFIG REQUIRED)`;
Windows builds also require `minhook`, while Luau-enabled native hosts require
Luau. The product consumes `spdlog::spdlog`, `minhook::minhook`, `Luau::VM`,
and `Luau::Compiler`. The developer provider, selected by
`CMAKE_PROJECT_TOP_LEVEL_INCLUDES`, supplies adapters for the upstream target
names. It reads full source revisions from `dependencies.json`, downloads into
the build tree, and scopes dependency options without forced cache writes.

For a packager-controlled build, omit that provider entirely:

```sh
cmake --preset system -DCMAKE_PREFIX_PATH=/path/to/dependency/prefix
cmake --build --preset system
```

The supplied packages must provide the targets above. Luau upstream target names
are `Luau.VM` and `Luau.Compiler`; the opt-in provider aliases them. A separately
packaged Luau may need an equivalent package-config adapter. This project does
not pretend every upstream dependency already installs a compatible package.

Offline developer builds can supply already populated source trees with
`FETCHCONTENT_SOURCE_DIR_LUAU`, `FETCHCONTENT_SOURCE_DIR_MINHOOK`, and
`FETCHCONTENT_SOURCE_DIR_SPDLOG`. A network connection is needed otherwise.
No third-party source is vendored in this ZIP.

### Installation and library consumption

```sh
cmake --install build/macos-small --prefix stage/mac --strip --component tinyhook-runtime
cmake --install build/windows-x86 --prefix stage/windows-x86 --strip --component tinyhook-runtime
```

The optional `tinyhook-tools` component installs the separate compiler; the
`tinyhook-examples` component installs the Windows fixture. `tinyhook-development`
installs header file sets and a relocatable `tinyhook` config package:

```cmake
find_package(tinyhook 0.3 CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE tinyhook::core)
```

The exported target supplies header paths, not warnings, C++ standard flags,
compile definitions, CRT choices, IPO settings, or link flags. The consumer
selects a C++20-or-newer environment. Applications/tests/examples/install rules
default off as a subproject. See [docs/BUILD.md](docs/BUILD.md).

## Logging

spdlog is a required runtime dependency, not an optional feature. `th-host`,
`th-client`, `th-hook.dll`, `th-compile`, and the fixture use it for diagnostics.
Normal command results remain on stdout where appropriate. The default level is
`info`; set `TINYHOOK_LOG=debug` (or `trace`, `warn`, `error`) before launch to
change it. The injected DLL uses spdlog's Windows debugger sink and also sends
important runtime/status diagnostics over the authenticated capture channel so
`th-host` can show them.

## Test the browser connection first

```sh
./build/macos-small/bin/th-host --stdin --token-file "$PWD/session.token"
```

The host creates a 256-bit secret with mode `600` and prints:

```text
ws://127.0.0.1:9001/?token=YOUR_64_CHARACTER_HEX_TOKEN
```

Set **Primary WebSocket** in <https://aurorawright.github.io/texthooker-ui/> to
that exact URL. Connect before typing a Japanese sentence into the host terminal.
Text is sent as plain UTF-8 WebSocket text messages, not diagnostic JSON. There
is no history replay. Ctrl+C exits; stdin EOF does not stop the server.

The allowed origins default to `https://aurorawright.github.io` and
`https://renji-xd.github.io`. If local-network or mixed-content policy blocks the
hosted page, use the UI's offline version and explicitly allow its origin rather
than disabling browser security globally. For a file-based page:

```sh
./build/macos-small/bin/th-host --stdin --token-file "$PWD/session.token" --allow-origin null
```

The token is still required. For a local HTTP copy, allow its exact origin instead.
The server binds to loopback only; this is not a LAN service or a remote debugger.

## Automatic engine detection

For normal use, point the host at the engine-profile directory:

```sh
TINYHOOK_LOG=debug ./build/macos-small/bin/th-host \
  --script-dir scripts/engines \
  --token-file "$PWD/session.token"
```

Each profile can declare stable metadata at the top level:

```lua
return {
    id = "siglusengine4-x86",
    engine = "SiglusEngine4",
    processes = { "SiglusEngine.exe" }, -- exact basename hints; priority only
    hooks = { ... },
    on_text = function(text, event) ... end,
}
```

`id` defaults to the script filename stem, `engine` defaults to `id`, and
`processes` is optional. An exact case-insensitive process-name match is tried
before generic profiles; generic profiles are tried before profiles whose explicit
process hints do not match. Within a priority tier, detection succeeds only if
exactly one complete profile resolves. Ambiguity is a hard failure. Use
`--engine PROFILE_ID` on `probe` or `attach` to select a profile explicitly.

The process hint is deliberately **not** an allowlist. A renamed executable can
still be detected from its hook signatures, just later in the scan. See
[docs/ENGINE-DETECTION.md](docs/ENGINE-DETECTION.md) for the complete ordering,
ambiguity, and callback-routing rules.

`--script FILE` remains available and exposes a one-profile catalog, so the old
single-profile workflow and the script/callback structure are preserved.

## Wine fixture

Run the host with `--script scripts/demo-x86.luau` for the x86 Windows fixture,
or `--script scripts/demo.luau` for the x64 fixture. Keep the browser connected. Copy the
matching client, DLL, fixture and token file to `C:\TinyHook` in the game's bottle.
Start `th-demo.exe` there. From that same bottle's command launcher:

```bat
C:\TinyHook\th-client.exe list
C:\TinyHook\th-client.exe probe --pid 1234 --token-file C:\TinyHook\session.token
C:\TinyHook\th-client.exe attach --pid 1234 --dll C:\TinyHook\th-hook.dll --token-file C:\TinyHook\session.token
```

Replace `1234` with the fixture's **Windows PID**, not a macOS PID. In CrossOver,
use Run Command in the selected bottle. With plain Wine, use the same Wine
installation and `WINEPREFIX` as the game. Do not mix unrelated Wine distributions.

Stop capture with:

```bat
C:\TinyHook\th-client.exe stop --pid 1234 --dll C:\TinyHook\th-hook.dll
```

The DLL and generated code remain mapped until process exit, so an in-flight
callback cannot return into freed code. **Restart the target before attaching a
different configuration or DLL version.** No hot reload or safe DLL unload is
promised. The capture handshake remains `THK2` / configuration version 2. Version 0.3 adds
catalog/config-request roles and a profile identity tag in the bootstrap. Replace
the host, client and DLL together when moving from 0.2 to 0.3.

## Script API

A script returns a table with optional profile metadata plus the existing
`hooks` and `on_text` fields. The extraction/callback structure is unchanged:

```lua
return {
    id = "engine-version-x86",       -- optional; filename stem if omitted
    engine = "Human-readable engine", -- optional; defaults to id
    processes = { "Game.exe" },       -- optional detection-priority hints
    hooks = { ... },
    on_text = function(text, event) ... end,
}
```

`on_text` returns a UTF-8 string, or `nil` to drop an event. Its metadata has
`pid`, `tid`, `hook` (the hook name), and `hook_id` (zero-based). Errors are diagnostics, not page text.

| Field | Meaning |
|---|---|
| `name` | Unique hook label. |
| `arch` | `"x86"` or `"x64"`; default `"x64"`. |
| `module` | Loaded module filename; `""` selects the main executable. |
| `symbol` / `rva` / `pattern` | Exactly one locator. Pattern must have a unique match in executable PE sections; byte wildcards are `??`. |
| `hook_offset` | Signed byte offset from the located address, default 0. |
| `point` | `"entry"` or `"mid"`; mid-function support is x86 only. |
| `expected` | Optional byte pattern checked at the final hook address. An x86 mid-function hook may omit it only when selected by a pattern containing at least 12 concrete bytes. This is not an instruction decoder. |
| `source` | x86: `eax ecx edx ebx ebp esi edi esp`; x64: `rax rcx rdx rbx rbp rsi rdi rsp r8`–`r15`; or `"stack"`. |
| `stack_offset` | For `source="stack"`, read a **target-width pointer** at original ESP/RSP plus this offset. Defaults: x86 4, x64 40. |
| `deref` | Up to eight signed pointer-chain offsets, each read using target pointer width. |
| `add` | Final signed byte offset after pointer-chain reads. |
| `layout` | `"cstring"` or `"msvc_wstring32"`; the latter is a specific x86 UTF-16 layout, not any arbitrary `std::wstring`. |
| `encoding` | `"utf16"`, `"utf8"`, or `"cp932"`. |
| `max_bytes` | 2–8192 bytes; even for UTF-16. Explicit-length strings that exceed the limit are dropped, not truncated. |

For x86, the gateway preserves general registers, EFLAGS, x87/MMX, XMM0–7 and
MXCSR, and tail-jumps to the trampoline without changing the target's argument
stack or its cdecl/stdcall/fastcall cleanup convention. It requires FXSAVE/SSE.
**Live AVX/AVX-512 state, exceptional stack unwinding through the x86 observer,
and arbitrary malicious code are outside the support contract.**

For x64, only normal Win64 function entries are supported. Mid-function hooks
with arbitrary live SIMD/extended state are not supported there. The complete
host-side scripting boundary is documented in
[docs/SCRIPTING.md](docs/SCRIPTING.md).

## Tests and limits

```sh
cmake --workflow --preset linux-relay
cmake --workflow --preset linux-sanitize
```

Both are offline once the compiler/tooling is present and do not require Luau.
The x86 Linux test executes generated i386 code in CPU compatibility mode; it
is not a Wine test or a Windows executable. Dependency-provider tests use local
mock CMake packages and do not validate upstream Luau/MinHook compilation.

Automatic detection is profile-based; it does not infer unknown engines or synthesize
hooks. There is no existing Agent JS compatibility, H-code parser, emulator
guest-address mapping, arbitrary remote-memory Luau API,
`onLeave`, stealth injection, or anti-cheat bypass. Use with software you are
authorized to instrument. An incorrect hook can crash the game: save first,
probe, and validate the fixture before trying a new game build.
