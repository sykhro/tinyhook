# SiglusEngine4 x86

The `siglusengine4-x86` profile captures text from the wide-string object in EAX
immediately after a string-copy call. It has been used with Angel Beats! -1st
beat- under CrossOver. Other executable builds need a matching signature and
string layout.

## Run

Build the native host and x86 helpers, then start the host:

```sh
cmake --workflow --preset macos-small
cmake --workflow --preset windows-x86
./build/macos-small/bin/th-host --script-dir scripts/engines \
  --token-file "$PWD/session.token"
```

Connect the reader to the WebSocket URL printed by the host. Copy
`th-client.exe`, `th-hook.dll`, and `session.token` into `C:\TinyHook` inside the
game's Wine/CrossOver bottle. Start the game and find its Windows PID:

```bat
C:\TinyHook\th-client.exe list
C:\TinyHook\th-client.exe probe --pid 1234 --token-file C:\TinyHook\session.token
C:\TinyHook\th-client.exe attach --pid 1234 --dll C:\TinyHook\th-hook.dll --token-file C:\TinyHook\session.token
```

Replace `1234` with that PID. Run `attach` after `probe` succeeds, then advance
the dialogue. A matching signature confirms the hook location; testing capture
in the game is still necessary. Save before attaching.

## Hook and string layout

The profile uses this signature in the main executable:

```lua
pattern = "C7 47 14 07 00 00 00 C7 47 10 00 00 00 00 66 89 0F 8B CF 50 E8 ?? ?? ?? ??"
hook_offset = 25
point = "mid"
source = "eax"
layout = "msvc_wstring32"
encoding = "utf16"
```

The signature must occur once in an executable PE section. Offset 25 is the
instruction immediately after the call. The call displacement is wildcarded.

EAX points to a 24-byte x86 wide-string object:

| Offset | Contents |
| --- | --- |
| `+0x00` | Inline UTF-16 storage, or a 32-bit heap pointer |
| `+0x10` | 32-bit character count, excluding the terminator |
| `+0x14` | 32-bit capacity; less than 8 selects inline storage |

The reader checks lengths and memory access before copying. It reads the header
again to catch changes during capture; concurrent changes can still escape
that check.

The filter removes `NLI` and repeated text per process and thread. It drops
ASCII-only text by default. For an English translation, set `dropAscii = false`
in the profile. Speaker, UI, and dialogue events may share a stream.

## Troubleshooting

Use the x86 helpers, including on a 64-bit or Apple Silicon host. Select the
actual game process in the same bottle. A renamed executable can still match;
`SiglusEngine.exe` is a scan-priority hint.

If the signature is missing or ambiguous, record the probe output, executable
hash, and any translation patch. A different build may need a different
signature. Set `TINYHOOK_LOG=debug` for scan details.

## Experimental scenario hook

[`scripts/experimental/siglus-scenario-x86.luau`](../scripts/experimental/siglus-scenario-x86.luau)
captures the first stack argument at a scenario-rendering function entry. Its
signature was derived from the running Angel Beats executable. Ordinary
narration was captured; ruby encoding and complete-line boundaries remain
unresolved. It forwards raw text and is excluded from automatic detection.

To try it, start the host with `--script scripts/experimental/siglus-scenario-x86.luau`
and use the same probe/attach steps. Restart the game before switching profiles.

## References

- [Textractor's `InsertSiglus4Hook` and `SpecialHookSiglus4`](https://github.com/Chenx221/Textractor/blob/0ca60774c808202085b9ed1ab2d086d3248d919c/texthook/engine/engine.cc)
- [LunaHook string layout definitions](https://github.com/HIllya51/LunaTranslator/blob/363de6354ef9d085403f78f325cff5d69b97eda8/src/NativeImpl/LunaHook/LunaHook/util/textunion.h)
