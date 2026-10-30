-- Diagnostic candidate for the installed Angel Beats engine; not auto-selected.
-- Entry signature derived from the live image, with relocated addresses masked.
-- Argument 1 is the MSVC wide string consumed by the scenario renderer.
-- Preserve raw markup until ruby semantics have been observed.
return {
    id = "siglus-scenario-x86",
    engine = "Siglus scenario diagnostic",
    processes = { "SiglusEngine.exe" },
    hooks = {{
        name = "scenario-input",
        arch = "x86",
        module = "",
        pattern = "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 81 EC 1C 04 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 F0 53 56 57 50 8D 45 F4 64 A3 00 00 00 00 8B D9 8B 7D 08 83 7F 10 00",
        point = "entry",
        source = "stack",
        stack_offset = 4,
        layout = "msvc_wstring32",
        encoding = "utf16",
        max_bytes = 8192,
    }},
    on_text = function(text: string, event)
        return text
    end,
}
