# Luau scripting surface and limitations

TinyHook embeds Luau in the native host only. The Windows client and injected DLL
do not contain Luau. Scripts describe immutable hook specifications and may
transform text after the DLL has copied it out of the target process.

## Top-level profile table

```lua
return {
    id = "engine-version-x86",
    engine = "Engine version",
    processes = { "Game.exe" },
    hooks = { ... },
    on_text = function(text, event) ... end,
}
```

`id`, `engine`, and `processes` are detection metadata. They do not grant access
to the process or change extraction behavior. `processes` is a priority hint;
profile hooks still have to resolve in the target.

## What scripts can do

- declare up to 16 native capture hooks;
- locate hooks by one symbol, RVA, or executable-section byte pattern;
- choose supported x86/x64 registers or a target-width stack slot as the text
  source;
- follow up to eight bounded pointer dereferences and one final signed offset;
- decode UTF-8, UTF-16, or CP932 captures;
- use the specific x86 `msvc_wstring32` string-object reader;
- filter/transform copied UTF-8 text in `on_text` and return a string or `nil`;
- inspect `event.pid`, `event.tid`, `event.hook`, and `event.hook_id`.

## What scripts cannot do

- read or write arbitrary target memory from Luau;
- call Windows APIs, native functions, or arbitrary addresses from Luau;
- install/remove hooks dynamically from `on_text`;
- implement Frida-style `onEnter`/`onLeave` JavaScript callbacks;
- inspect live CPU registers from Luau; extraction rules run natively first;
- create new network connections, files, subprocesses, or OS resources through
  the sandboxed standard library;
- bypass the 50 ms callback budget or the host's output-size limits;
- express optional/alternative hooks inside one profile. Every hook in a profile
  is required for automatic detection. Use separate profiles for engine variants;
- make a process-name hint authoritative. A matching executable name only raises
  the profile's detection priority.

## Hard limits

- script file: 1 MiB maximum; source compilation path: 256 KiB maximum;
- profile ID/engine: 128 bytes each;
- process hints: at most 16, each at most 255 bytes;
- hooks: at most 16 per profile;
- byte pattern: at most 256 bytes;
- pointer dereference chain: at most 8 steps;
- capture: 2–8192 bytes per hook;
- stack offset: 0–4096 and aligned to target pointer width;
- `on_text`: approximately 50 ms execution budget per event;
- returned text/event protocol frame: 32 KiB maximum.

## Architecture restrictions

x86 supports function-entry and guarded mid-function hooks. Mid-function hooks
must use an x86 profile and either provide `expected` bytes at the final patch
site or use a pattern selector with at least 12 concrete bytes.

x64 currently supports normal function-entry hooks only. Arbitrary mid-function
x64 observation, live AVX/AVX-512 preservation, and unwind semantics are outside
the supported model.

## Script directory behavior

`th-host --script-dir DIR` loads engine profiles from one directory. A
compiler-enabled host accepts `.luau` and `.luauc` and prefers source when both
share a stem. A VM-only host sees `.luauc` only. Invalid profiles or duplicate
IDs fail host startup instead of being silently skipped.

The client never receives Luau bytecode. It receives only profile metadata and
the serialized native hook specification selected for probing/attachment.
