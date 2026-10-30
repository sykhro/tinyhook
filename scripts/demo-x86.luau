-- Use with the Win32 build of th-demo.exe, not the x64 build.
return {
    id = "tinyhook-demo-x86",
    engine = "TinyHook fixture x86",
    processes = { "th-demo.exe" },
    hooks = {
        {
            name = "dialogue",
            arch = "x86",
            module = "th-demo.exe",
            symbol = "th_demo_text",
            source = "stack",
            stack_offset = 4, -- first cdecl argument at ORIGINAL ESP + 4
            encoding = "utf16",
            max_bytes = 4096,
        },
    },
    on_text = function(text: string) return text end,
}
