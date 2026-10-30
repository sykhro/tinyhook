-- TEMPLATE ONLY: replace the module and RVA with a verified function ENTRY.
-- Wrong hook locations or pointer descriptions can destabilize the target.
-- The correct location, register and encoding depend on the exact game build.
local lastByHook: {[string]: string} = {}
return {
    id = "replace-engine-x64",
    engine = "REPLACE_WITH_ENGINE_NAME",
    processes = { "REPLACE_WITH_GAME.exe" }, -- priority hint, not a compatibility claim
    hooks = {
        {
            name = "dialogue",
            module = "REPLACE_WITH_GAME.exe",
            rva = 0x123456, -- EXAMPLE, not a known hook; ASLR-relative function entry
            -- Instead of rva, use symbol="export_name" or pattern="48 89 ...".
            source = "rdx", -- second integer/pointer argument in the standard x64 ABI
            deref = {}, -- e.g. {0x10, 0x20}: p = *(p+0x10); p = *(p+0x20)
            add = 0,   -- final byte offset, with no dereference
            encoding = "utf16", -- "utf8", "utf16", "cp932"
            max_bytes = 4096,
        },
    },
    on_text = function(text: string, event)
        -- Luau patterns are not JavaScript regular expressions.
        text = text:gsub("<[^>]*>", ""):gsub("\r\n", "\n")
        if text == "" or lastByHook[event.hook] == text then
            return nil
        end
        lastByHook[event.hook] = text
        return text
    end,
}
