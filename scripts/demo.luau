-- Windows x64 fixture: th-demo.exe. Launch it in the same bottle as th-client.
local previous: string? = nil
return {
    id = "tinyhook-demo-x64",
    engine = "TinyHook fixture x64",
    processes = { "th-demo.exe" },
    hooks = {
        {
            name = "dialogue",
            module = "th-demo.exe",
            symbol = "th_demo_text",
            source = "rcx", -- Windows x64 first integer/pointer argument
            encoding = "utf16",
            max_bytes = 4096,
        },
    },
    on_text = function(text: string, event)
        local cleaned = text:gsub("\r\n", "\n")
        if cleaned == previous then
            return nil
        end
        previous = cleaned
        return cleaned
    end,
}
