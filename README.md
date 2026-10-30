# TinyHook

Capture text from Windows games and send it to a WebSocket text reader.
TinyHook runs a native host on macOS or Linux, with an x86 or x64 helper inside
Windows, Wine, or CrossOver. Luau profiles select hooks and filter captured text.

```text
Game → th-hook.dll → th-host → WebSocket text reader
          ↑             ↑
       th-client    Luau profiles
```

The project is experimental. The SiglusEngine4 x86 profile has been probed
against Angel Beats! -1st beat- under CrossOver; support depends on the exact
engine build and hook signatures. Automatic detection matches the profiles in `scripts/engines`.

## Build

You need uv, Git, and a C++20 compiler and standard library. Install Python,
CMake, and Ninja with uv:

```sh
uv venv --python 3
uv pip install --upgrade -r requirements-dev.txt
```

Run build commands through `uv run --no-project` to use those tools.
The development presets fetch pinned dependencies from
[`dependencies.json`](dependencies.json).

On macOS, with Xcode Command Line Tools installed:

```sh
brew install uv llvm mingw-w64
CC="$(brew --prefix llvm)/bin/clang" CXX="$(brew --prefix llvm)/bin/clang++" \
  uv run --no-project cmake --workflow --preset macos-small
uv run --no-project cmake --workflow --preset windows-x86
```

The host is `build/macos-small/bin/th-host`. The cross build produces
`th-client.exe`, `th-hook.dll`, and the test fixture `th-demo.exe` in
`build/windows-x86/bin/`. Use `windows-x64` for a 64-bit game. The helper and DLL
must match the game's architecture; the host can use a different architecture.
The macOS cross builds use MinGW.

On Linux:

```sh
CC=clang CXX=clang++ uv run --no-project cmake --workflow --preset linux-small
```

On Windows, install uv, LLVM, and Visual Studio C++ build tools, then run the
uv setup commands above. In an x86 or x64 developer command prompt matching the
target:

```bat
uv run --no-project cmake --workflow --preset clang-x86
```

Use `clang-x64` in an x64 prompt for a 64-bit build. These presets use `clang-cl`
and LLD, with the Windows SDK and static MSVC runtime.

Each native workflow configures, builds, and runs the tests. CI installs the
latest stable upstream Clang and uses uv for Python, CMake, and Ninja on
Ubuntu 26.04, macOS 26, and Windows Server 2025 with VS 2026 (x86/x64).
It also runs Linux sanitizers and uploads runtime packages.

## Use

First check the browser connection without attaching to a game:

```sh
./build/macos-small/bin/th-host --stdin --token-file "$PWD/session.token"
```

The host creates a secret token file and prints an authenticated WebSocket URL.
Paste that URL into **Primary WebSocket** in
[Texthooker UI](https://aurorawright.github.io/texthooker-ui/), connect, and type
text in the terminal. The host sends UTF-8 text messages. It binds to loopback
only; both the browser and capture connections require the token.

For game capture, start the host with the engine profiles:

```sh
./build/macos-small/bin/th-host \
  --script-dir scripts/engines --token-file "$PWD/session.token"
```

Copy the matching Windows helpers and the token file into a directory accessible
to the game. For a native macOS window picker, run this inside the game's bottle:

```bat
th-client.exe listen --token-file session.token
```

Then, on the Mac:

```sh
./build/macos-small/bin/th-pick --token-file "$PWD/session.token"
```

Hover to highlight a window and click to attach. Escape or right-click cancels.
macOS may ask for Screen Recording permission so the picker can read window
titles. The client matches the title within its bottle, checks the engine profile,
and reports the attachment result. Keep one selection client connected at a time;
stop it before switching bottles. Duplicate titles across processes produce an
error. Use the client and DLL matching the game's architecture.

To choose a Windows PID manually instead, run in the game's bottle:

```bat
th-client.exe list
th-client.exe probe --pid 1234 --token-file session.token
th-client.exe attach --pid 1234 --dll th-hook.dll --token-file session.token
```

Replace `1234` with the game's **Windows PID**. `probe` checks the profile match
without injecting a DLL. After attaching, advance the game's text to receive it
in the connected reader. Stop capture with:

```bat
th-client.exe stop --pid 1234 --dll th-hook.dll
```

Restart the game before attaching a different DLL or configuration. Stopping
capture leaves the DLL mapped until process exit.

## Profiles and packaging

Profiles live in [`scripts/engines`](scripts/engines). Use `--script FILE` for a
single profile, or `--script-dir DIR` for detection across a catalog. Luau runs
in the host; the injected DLL contains native capture code and MinHook.
Set `TINYHOOK_LOG=debug` for diagnostics.

The `macos-vm` preset builds a smaller host that accepts bytecode and a separate
`th-compile` tool. The compiler and host must use the same pinned Luau revision.

Install a runtime package with:

```sh
uv run --no-project cmake --install build/macos-small --prefix dist --strip --component tinyhook-runtime
```

CMake generates `THIRD_PARTY.md` in the build directory from `dependencies.json`
and installs it with the license files. For package-manager builds, dependency
overrides, and the header-only `tinyhook::core` target, see the comments in
[`CMakePresets.json`](CMakePresets.json).

- [Writing Luau profiles](docs/SCRIPTING.md)
- [Engine detection](docs/ENGINE-DETECTION.md)
- [SiglusEngine4 profile and limitations](docs/SIGLUSENGINE4.md)

## License

Copyright (C) 2026 sykhro. [GNU GPLv3](LICENSE), GPL-3.0-only.
Third-party dependencies retain their own licenses and copyright notices.
