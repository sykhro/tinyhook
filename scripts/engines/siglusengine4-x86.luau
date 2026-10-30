-- SiglusEngine4 x86 profile. Validated with Angel Beats! -1st beat-.
-- Ported from Chenx221/Textractor's SiglusEngine4 hook. The upstream hook
-- identifies the site from the bytes before the call and hooks immediately
-- after it; it does not require the following instructions to match the old
-- trial disassembly. Run the READ-ONLY `th-client probe` first: the signature
-- must resolve uniquely in your executable.
-- Provenance/details: docs/SIGLUSENGINE4.md.
local dropAscii = true -- set false for English translation text
local lastByThread = {}
local seenThreads = 0
return {
    id = "siglusengine4-x86",
    engine = "SiglusEngine4",
    processes = { "SiglusEngine.exe" }, -- priority hint only; signature remains authoritative
    hooks = {
        {
            name = "siglus-dialogue",
            arch = "x86",
            module = "", -- actual main game module; filename need not be SiglusEngine.exe
            -- Instructions initialize a short wide-string, then call the string copy.
            pattern = "C7 47 14 07 00 00 00 C7 47 10 00 00 00 00 66 89 0F 8B CF 50 E8 ?? ?? ?? ??",
            hook_offset = 25, -- immediately AFTER that call; EAX is the string object
            point = "mid",
            source = "eax",
            layout = "msvc_wstring32", -- inline chars OR heap pointer, selected by capacity
            encoding = "utf16",
            max_bytes = 8192,
        },
    },
    on_text = function(text: string, event)
        -- Matches the upstream engine filter. Remove this line to inspect raw output.
        text = text:gsub("NLI", "")
        if text == "" or (dropAscii and not text:find("[\128-\255]")) then return nil end
        local key = tostring(event.pid) .. ":" .. tostring(event.tid)
        if lastByThread[key] == text then return nil end
        if lastByThread[key] == nil then
            seenThreads += 1
            if seenThreads > 256 then
                lastByThread = {}
                seenThreads = 1
            end
        end
        lastByThread[key] = text
        return text
    end,
}
