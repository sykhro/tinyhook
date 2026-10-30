# Engine profile detection

TinyHook keeps Luau and profile discovery in the native host. The Windows client
never parses or executes Luau. This preserves the host/client split while still
letting `probe` and `attach` identify an engine automatically.

## Start a profile catalog

A compiler-enabled host can read source profiles directly:

```sh
th-host --script-dir scripts/engines --token-file session.token
```

A VM-only host reads `.luauc` files. Compile profiles into a separate directory
and point `--script-dir` there. When both `.luau` and `.luauc` with the same stem
are present in a compiler-enabled directory, source is preferred.

`--script FILE` is still supported. It behaves as a catalog containing one
profile, so the client path is the same.

## Profile metadata

The top-level table may include:

```lua
return {
    id = "siglusengine4-x86",
    engine = "SiglusEngine4",
    processes = { "SiglusEngine.exe" },
    hooks = { ... },
    on_text = function(text, event) ... end,
}
```

- `id` is the stable machine-facing profile name. It defaults to the script file
  stem and is limited to 128 bytes.
- `engine` is the human-facing name. It defaults to `id` and is limited to 128
  bytes.
- `processes` contains up to 16 executable basenames. Matching is ASCII
  case-insensitive. These names are **priority hints, not compatibility rules**.
- `hooks` and `on_text` retain their existing meaning.

IDs must be unique inside one `--script-dir` catalog.

## Detection algorithm

The client obtains the target executable basename from the Windows process list
and sends `{ architecture, process name }` to the native host. The host excludes
profiles whose hooks target a different architecture and orders the remaining
profiles into three tiers:

1. exact process-name hint;
2. no process-name hint (generic);
3. explicit process-name hints that do not match.

The client then fetches and read-only probes profiles in one tier at a time. A
profile matches only when **all hooks in that profile** resolve successfully.

- zero matches: continue to the next tier;
- one match: detection succeeds and lower tiers are not tried;
- two or more matches in the same tier: fail as ambiguous.

This means a renamed executable can still match its engine signature, while a
known process name avoids needless scans first.

Use an explicit override when two profiles intentionally overlap:

```bat
th-client.exe probe --pid 1234 --token-file C:\bin\session.token --engine siglusengine4-x86
```

`attach` accepts the same option.

## Why process names do not select an engine

Executable names are cheap metadata but weak evidence. Many games use generic
engine names, launchers can rename binaries, and different engine revisions may
share a filename. TinyHook therefore never treats `processes` as an allowlist or
as proof that a profile is safe. The normal hook selector (`pattern`, `symbol`,
or `rva`) remains the compatibility test.

## Host-side callback routing

After a profile is selected, the client passes a profile identity tag into the
injected DLL. Capture connections send that tag back to the host, which routes
each event to the selected profile's `on_text` callback.

The identity includes both the profile ID and serialized hook configuration.
This prevents two profiles with identical hook locations but different filters
from being confused. Authentication still uses the independent 256-bit session
token; the profile tag is not a security credential.

## Failure behavior

Detection is deliberately fail-closed. `probe` never injects or patches target
memory. `attach` runs the same resolution path before injection. Set
`TINYHOOK_LOG=debug` to see why individual candidates were rejected.

The current model treats every hook in one profile as required. If an engine
family needs alternative/optional hooks, make separate version profiles for now
rather than combining mutually exclusive signatures in one profile.
