# Luau profiles

Profiles describe where to capture text and how to filter it. The host runs
Luau after the DLL copies text from the game. Hook callbacks in the game execute
native extraction rules.

## Profile table

```lua
return {
    id = "my-engine-x86",
    engine = "My engine",
    processes = { "Game.exe" },
    hooks = {
        {
            name = "dialogue",
            arch = "x86",
            module = "",
            symbol = "DrawDialogue",
            source = "stack",
            stack_offset = 4,
            encoding = "utf16",
            max_bytes = 8192,
        },
    },
    on_text = function(text, event)
        return text
    end,
}
```

Replace the example locator and extraction rules with ones for the target game.
See [engine detection](ENGINE-DETECTION.md) for `id`, `engine`, and `processes`.

## Hook fields

| Field | Meaning |
| --- | --- |
| `name` | Unique hook label. |
| `arch` | `"x86"` or `"x64"`; defaults to `"x64"`. |
| `module` | Loaded module filename; `""` selects the main executable. |
| `symbol`, `rva`, `pattern` | Set exactly one locator. Patterns use hexadecimal bytes and `??` wildcards, and must match once in executable PE sections. |
| `hook_offset` | Signed byte offset from the located address; defaults to 0. |
| `point` | `"entry"` or `"mid"`; mid-function hooks require x86. |
| `expected` | Byte pattern checked at the final hook address. Required for x86 mid-function hooks unless the locator pattern contains at least 12 concrete bytes. |
| `source` | An x86/x64 general register, or `"stack"`. |
| `stack_offset` | For `"stack"`, read a target-width pointer at the original stack pointer plus this offset. Defaults to 4 on x86 and 40 on x64. |
| `deref` | Up to eight signed pointer-chain offsets. |
| `add` | Final signed byte offset after dereferences. |
| `layout` | `"cstring"` or `"msvc_wstring32"`, the x86 wide-string object layout used by the Siglus profile. |
| `encoding` | `"utf16"`, `"utf8"`, or `"cp932"`. |
| `max_bytes` | Capture limit, from 2 to 8192; must be even for UTF-16. Oversized explicit-length strings are dropped. |

The x86 register names are `eax ecx edx ebx ebp esi edi esp`. The x64 names are
`rax rcx rdx rbx rbp rsi rdi rsp r8 r9 r10 r11 r12 r13 r14 r15`.

## Filtering

`on_text(text, event)` receives copied UTF-8 text. Return a string to send it to
the reader, or `nil` to drop it. `event` contains `pid`, `tid`, `hook` (the name),
and `hook_id` (a zero-based index). Script errors go to the host log.

Scripts can retain state between events. They cannot access live process memory,
call native functions, change hooks, or use files, sockets, and subprocesses.
Every hook in a profile is required during detection; use separate profiles for
alternative hook sets.

## Limits

| Resource | Limit |
| --- | --- |
| Script file | 1 MiB; source compilation limited to 256 KiB |
| Hooks per profile | 16 |
| Byte pattern | 256 bytes |
| Pointer chain | 8 steps |
| Capture per hook | 8192 bytes |
| Stack offset | 0–4096, aligned to target pointer width |
| `on_text` execution | Approximately 50 ms per event |
| Returned text or protocol frame | 32 KiB |

The x86 gateway preserves general registers, EFLAGS, x87/MMX, XMM0–7, and MXCSR;
it requires FXSAVE/SSE. AVX/AVX-512 state and exceptional unwinding through the
observer are unsupported. x64 hooks support normal Win64 function entries.

## Loading profiles

A compiler-enabled host accepts `.luau` and `.luauc` files. Source takes
precedence when both have the same filename stem. A VM-only host accepts
bytecode. Compile it with `th-compile` from the same Luau revision as the host.

`--script-dir DIR` reads one directory without recursion. Invalid profiles and
duplicate IDs stop startup. Load only trusted scripts and bytecode.
