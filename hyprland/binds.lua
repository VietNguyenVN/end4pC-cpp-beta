-- end4pC keybinds: registers every bind of the table below. The table is a plain text file (one bind per line, fields separated by a tab)
-- that Tools > Keybinds edits: ~/.config/hypr/keybinds.tsv is the user's copy, hyprland/binds.default.tsv the one that ships. Loaded by
-- hyprland/end4pC.lua; `hyprctl reload` applies a change.

local home = os.getenv("HOME")
local here = (debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$")) or (home .. "/.config/end4pC/hyprland")
local vars = {
    E    = home .. "/.config/end4pC/build/end4pC",        -- the shell binary (it is its own CLI)
    lock = home .. "/.config/end4pC/build/end4pC-lock",
}

local function split(line)
    local out, i = {}, 1
    while true do
        local t = line:find("\t", i, true)
        if not t then out[#out + 1] = line:sub(i); break end
        out[#out + 1] = line:sub(i, t - 1)
        i = t + 1
    end
    return out
end
local function unescape(s)
    return (s:gsub("\\(.)", function(c) return c == "t" and "\t" or c == "n" and "\n" or c end))
end
local function expand(s)  -- {name}: the shell binary, the lock screen, or a variable of hyprland/variables.lua (terminal, fileManager...)
    return (s:gsub("{(%w+)}", function(k) return vars[k] or _G[k] or ("{" .. k .. "}") end))
end

-- The number row and the keypad, by keycode: the same digit on every layout.
local numberKeys = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19}
local keypadKeys = {87, 88, 89, 83, 84, 85, 79, 80, 81, 90}

local function dispatcher(kind, arg)
    local E = vars.E
    if kind == "exec" then return hl.dsp.exec_cmd(expand(arg))
    elseif kind == "shell" then return hl.dsp.exec_cmd(E .. " '" .. arg .. "'")
    elseif kind == "close" then return hl.dsp.window.close()
    elseif kind == "float" then return hl.dsp.window.float({action = "toggle"})
    elseif kind == "fullscreen" then return hl.dsp.window.fullscreen({mode = arg ~= "" and arg or "fullscreen", action = "toggle"})
    elseif kind == "fullscreen_state" then
        local a, b = arg:match("(%d+)%s+(%d+)")
        return hl.dsp.window.fullscreen_state({internal = tonumber(a), client = tonumber(b)})
    elseif kind == "focus_dir" then return hl.dsp.focus({direction = arg})
    elseif kind == "move_dir" then return hl.dsp.window.move({direction = arg})
    elseif kind == "workspace" then return hl.dsp.focus({workspace = tonumber(arg) or arg})
    elseif kind == "movews" then return hl.dsp.window.move({workspace = tonumber(arg) or arg, follow = true})
    elseif kind == "movews_silent" then return hl.dsp.window.move({workspace = tonumber(arg) or arg, follow = false})
    elseif kind == "movespecial" then return hl.dsp.window.move({workspace = arg, follow = false})
    elseif kind == "movews_plain" then return hl.dsp.window.move({workspace = tonumber(arg) or arg})
    elseif kind == "special" then return hl.dsp.workspace.toggle_special(arg)
    elseif kind == "drag" then return hl.dsp.window.drag()
    elseif kind == "resize" then return hl.dsp.window.resize()
    elseif kind == "layout" then return hl.dsp.layout(arg)
    elseif kind == "lua" then
        local chunk = load("return " .. arg)
        return chunk and chunk()
    end
end

-- Dots from before the table still bind the same keys (Super+Q closes a window there) in hypr/hyprland/keybinds.lua (the shell's update never touches the dotfiles): then
-- every key is freed before it is bound here, so nothing runs twice. A key the user binds in hypr/custom/keybinds.lua stays theirs.
local function readAll(path)
    local f = io.open(path, "r")
    if not f then return "" end
    local s = f:read("*a")
    f:close()
    return s
end
local oldDots = readAll(home .. "/.config/hypr/hyprland/keybinds.lua"):find("hl.dsp.window.close()", 1, true) ~= nil  -- (the new file only has the virtual machine submap)
local customText = oldDots and readAll(home .. "/.config/hypr/custom/keybinds.lua"):lower() or ""
local function bindKey(keys, disp, opts)
    if oldDots then
        if customText:find('"' .. keys:lower() .. '"', 1, true) then return end  -- (theirs)
        pcall(hl.unbind, keys)
    end
    hl.bind(keys, disp, opts)
end

local function register(row)
    local cat, keys, kind, arg, desc, flags, rng = row[1], row[2], row[3], row[4] or "", row[5] or "", row[6] or "", row[7] or ""
    if not keys or keys == "" or not kind or kind == "" or flags:find("d", 1, true) then return end
    local opts = {}
    if flags:find("l", 1, true) then opts.locked = true end
    if flags:find("r", 1, true) then opts.repeating = true end
    if flags:find("e", 1, true) then opts.release = true end
    if flags:find("m", 1, true) then opts.mouse = true end
    if flags:find("n", 1, true) then opts.non_consuming = true end
    if not flags:find("h", 1, true) and desc ~= "" then opts.description = desc end  -- (a description puts the bind in the launcher's key bind list)
    if rng == "num" or rng == "pad" then
        for i = 1, 10 do
            local code = (rng == "num" and numberKeys or keypadKeys)[i]
            bindKey((keys:gsub("#", "code:" .. code)), dispatcher(kind, (arg:gsub("#", tostring(i)))), opts)
        end
    else
        bindKey(keys, dispatcher(kind, arg), opts)
    end
end

local function load_table()
    local user = home .. "/.config/hypr/keybinds.tsv"
    local f = io.open(user, "r") or io.open(here .. "/binds.default.tsv", "r")
    if not f then return end
    for line in f:lines() do
        if line ~= "" and line:sub(1, 1) ~= "#" then
            local row = split(line)
            for i = 1, #row do row[i] = unescape(row[i]) end
            local ok, err = pcall(register, row)
            if not ok then print("end4pC keybinds: " .. tostring(err) .. " (" .. line:sub(1, 60) .. ")") end
        end
    end
    f:close()
end
load_table()

-- While Tools > Keybinds listens for a new shortcut, the compositor must not act on the keys: this submap has no binds, so every key reaches the
-- window. Ctrl+Alt+Escape always leaves it (also if Tools stopped without doing it).
hl.define_submap("capture", function()
    hl.bind("CTRL + ALT + Escape", hl.dsp.submap("reset"))
end)
