# SiglusEngine4 x86 engine profile

## What is known, and what is not

Chenx221/Textractor's current x86 Siglus implementation includes the SiglusEngine4
hook used here. TinyHook names this profile `siglusengine4-x86`; Angel Beats! -1st beat-
is one validated consumer of that engine profile, not the identity of the script. It identifies a string-object initialization sequence followed by
a call and observes the **EAX string object immediately after that call**. That is
a mid-function hook, not a function-entry argument hook. The source comment also
retains the historical `AngleBeats trial` disassembly from 2015-05-23, but the
actual hook does **not** require the instructions after the call to equal that
old disassembly.

The object is the 24-byte x86 release-layout wide string used by that engine:

| Offset | Target representation |
|---|---|
| `+0x00` | Inline UTF-16 storage (8 code units including terminator), or a 32-bit heap pointer. |
| `+0x10` | 32-bit character count, excluding terminator. |
| `+0x14` | 32-bit capacity; `< 8` selects inline storage, otherwise heap storage. |

TinyHook reads the header explicitly in little-endian order and copies a bounded
number of UTF-16 bytes. It never casts the object to the host's `std::wstring`.
Invalid lengths, pointers, inaccessible memory and a changed header are rejected.
A second header read is only a best-effort race check, not atomicity.

The profile now follows Textractor's actual SiglusEngine4 matching rule: the
pre-call signature must occur **exactly once** in an executable PE section, then
the hook is placed immediately after the call. TinyHook does not use the old
trial's following `mov ebx,[absolute]` bytes as a second guard, because that was
stricter than Textractor and rejected compatible engine revisions. The host
script uses the actual main module, regardless of its filename; attach to the
game process, not an installer or launcher.

## Quick path on macOS + Wine/CrossOver

Use the x86 helpers even on an Intel x64 or Apple Silicon Mac. Keep the native
host in the Mac's native architecture. First build from the repository root:

```sh
cmake --workflow --preset macos-small
cmake --workflow --preset windows-x86
./build/macos-small/bin/th-host --script-dir scripts/engines \
  --token-file "$PWD/session.token"
```

For a smaller host, build `macos-vm`, compile the script with its `th-compile`,
and load the resulting bytecode instead. See the README.

Connect Texthooker UI to the exact authenticated WebSocket URL printed by the
host. Copy `build/windows-x86/bin/th-client.exe`, `th-hook.dll`, and the generated
`session.token` into `C:\TinyHook` in the **same bottle/prefix** as the game.
Keep the token private. Start the game, load a save, and reach a dialogue scene.
Use that bottle's Windows command launcher:

```bat
C:\TinyHook\th-client.exe list
C:\TinyHook\th-client.exe probe --pid 1234 --token-file C:\TinyHook\session.token
```

Replace `1234` with the game's Windows PID. `probe` first requests the engine
catalog. Because this profile declares `processes = { "SiglusEngine.exe" }`, it
is placed in the highest priority tier for the normal engine executable name;
the process hint does not bypass signature validation. `probe` then uses only
process-query and read rights: it does not inject a DLL, change a protection, or
patch memory. It checks the PE architecture and requires the complete Siglus
signature to resolve uniquely.

Only if it succeeds, and after saving your game:

```bat
C:\TinyHook\th-client.exe attach --pid 1234 --dll C:\TinyHook\th-hook.dll --token-file C:\TinyHook\session.token
```

Advance dialogue. `attach` repeats the checks, and the DLL checks again before
creating a hook. A successful `probe` does **not** prove that this is the correct
semantic hook for your executable. Wrong extraction rules can still crash a
process; test the included x86 fixture first when establishing a new bottle setup.


## Detection metadata

The profile begins with:

```lua
id = "siglusengine4-x86"
engine = "SiglusEngine4"
processes = { "SiglusEngine.exe" }
```

`processes` is only a prioritization hint. If the executable is renamed, TinyHook
will still try this profile after exact-name and generic candidates. The byte
signature and extraction rules decide whether the profile actually matches.

## What the profile tests

```lua
pattern = "C7 47 14 07 00 00 00 C7 47 10 00 00 00 00 66 89 0F 8B CF 50 E8 ?? ?? ?? ??"
hook_offset = 25
point = "mid"
source = "eax"
layout = "msvc_wstring32"
encoding = "utf16"
```

The five-byte `call` ends at offset 25, which is exactly where Textractor places
its SiglusEngine4 hook. The profile contains **no hardcoded virtual address or
guessed retail RVA**. The call displacement is wildcarded, while the 20 concrete
bytes before it identify the engine operation. TinyHook additionally requires
that signature to be unique before it will inject.

The text filter removes `NLI` and suppresses consecutive duplicates per process
and thread. It discards ASCII-only text by default to match the upstream filter;
that can hide text from an English translation. Set `dropAscii = false` near the
top of the script for translated text. The profile does not port upstream's
context-based stream splitting, so speaker/UI/dialogue separation is not guaranteed.

## When a probe fails

- **Architecture mismatch:** use the x86 client and DLL; do not use the x64 pair.
- **Module/PID error:** select the actual Windows game process in the same bottle.
- **No pattern or multiple matches:** stop. Do not substitute the trial's
  historical RVA. A different executable build needs its own verified pattern
  and extraction rule.

For diagnosis, retain the complete probe output and identify the executable
edition and any translation patch. Set `TINYHOOK_LOG=debug` before launching the
host/client when you want verbose spdlog diagnostics. On macOS, its file identity can be recorded
without running it:

```sh
shasum -a 256 "/actual/path/to/the/game/SiglusEngine.exe"
```

Use the actual executable filename. The hash identifies a build; it does not
prove compatibility. The next verification step needs the actual executable or
a disassembly around the candidate hook, not just the game's title.

## Primary-source provenance

- SiglusEngine4 implementation used for this port (`InsertSiglus4Hook` /
  `SpecialHookSiglus4`) in Chenx221/Textractor:
  <https://github.com/Chenx221/Textractor/blob/0ca60774c808202085b9ed1ab2d086d3248d919c/texthook/engine/engine.cc>
- Target string layout reference:
  <https://github.com/HIllya51/LunaTranslator/blob/363de6354ef9d085403f78f325cff5d69b97eda8/src/NativeImpl/LunaHook/LunaHook/util/textunion.h>

The byte signature and data-layout facts are reference material. No LunaHook
implementation, game binary, translation patch, or assets are included.

## Scenario/ruby investigation (2026-10-22)

`scripts/experimental/siglus-scenario-x86.luau` is an opt-in diagnostic
profile outside the automatic engine catalog. Its unique entry signature was
derived from the running Angel Beats image and masks relocated addresses.
The read-only probe resolved it at main-module RVA `0x1cbaa0`; disassembly
shows argument 1 being read as an MSVC wide string. This is the newer scenario
function family described by LunaHook's `ScenarioHook`, located upstream by
its `cmp esi, 0x300c` instruction and enclosing function entry.

The diagnostic captures the argument before rendering. Ordinary narration was
observed successfully. Ruby encoding and complete-line boundaries are **not yet
verified**; the profile deliberately forwards raw text without transformation.
Do not replace the default profile or infer ruby relationships from timing alone.

For the current investigation, a separate host listens on agent port 9012 and
WebSocket port 9011, with private trace output in
`/tmp/tinyhook-scenario-trace.log`. A separately named `C:\bin\th-hook-scenario.dll`
was attached to Windows PID 320 alongside the original hook, using the same
private token file. The diagnostic DLL includes the queue-contention fix.
The original host/viewer connection remains on its existing ports.

To stop only the diagnostic capture in that still-running process:

```bat
C:\bin\th-client.exe stop --pid 320 --dll C:\bin\th-hook-scenario.dll
```

PID 320 is session-specific. Stopping leaves the DLL loaded until game exit.
The game executable and compatibility plugin were not modified.
