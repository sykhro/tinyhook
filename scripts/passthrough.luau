-- For --stdin / protocol testing, with no game hooks.
return {
    id = "passthrough",
    engine = "Pass-through",
    hooks = {},
    on_text = function(text: string, event)
        return text
    end,
}
