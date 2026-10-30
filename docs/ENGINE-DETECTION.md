# Engine detection

Start the host with a directory of engine profiles:

```sh
th-host --script-dir scripts/engines --token-file session.token
```

The host loads the Luau profiles. The Windows client receives their metadata and
native hook specifications when `probe` or `attach` requests a catalog.
`--script FILE` loads a single profile through the same path.

## Profile metadata

```lua
return {
    id = "siglusengine4-x86",
    engine = "SiglusEngine4",
    processes = { "SiglusEngine.exe" },
    hooks = { ... },
    on_text = function(text, event) ... end,
}
```

| Field | Meaning |
| --- | --- |
| `id` | Unique profile identifier; defaults to the filename stem. Up to 128 bytes. |
| `engine` | Display name; defaults to `id`. Up to 128 bytes. |
| `processes` | Up to 16 executable basenames, each up to 255 bytes. ASCII case-insensitive. |

Process names determine scan order. Hook signatures determine whether a profile
matches, so renaming an executable does not prevent detection.

## Selection

The host excludes profiles for a different architecture, then groups the rest:

1. Profiles with a matching process name.
2. Profiles without process-name hints.
3. Profiles whose process-name hints do not match.

The client probes one group at a time. Every hook in a profile must resolve.
With no matches, it tries the next group. With one match, it selects that profile.
Multiple matches in a group produce an ambiguity error.

To select a profile explicitly:

```bat
th-client.exe probe --pid 1234 --token-file session.token --engine siglusengine4-x86
```

`attach` accepts the same `--engine` option and repeats resolution before
injection. `probe` only queries and reads the target process. Set
`TINYHOOK_LOG=debug` to see rejected candidates.

Use separate profiles for engine variants with alternative signatures; all
hooks within a profile are required.

## Text routing

The client passes a tag derived from the profile ID and hook configuration to
the DLL. The DLL sends that tag with its capture connection so the host can
route events to the selected `on_text` callback. Connections authenticate with
the session token. Restart the target after changing profiles or DLLs.
