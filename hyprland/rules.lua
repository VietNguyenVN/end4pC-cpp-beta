-- end4pC window rules: the ones made in Settings > Hyprland > Rules. A plain text table, one rule per line, fields separated by a tab
-- (~/.config/hypr/windowrules.tsv, written by Settings). Loaded by hyprland/end4pC.lua; `hyprctl reload` applies a change.
-- Fields: id, class, title, workspace, silent, monitor, mode (0 as it comes, 1 floating, 2 tiled), center, size (WxH), opacity (10-100), pin.
-- Hyprland matches the whole text: a plain class is exact and case blind, a plain title is a part of the title; anything with regex
-- characters is used as the regular expression it is.

local path = os.getenv("HOME") .. "/.config/hypr/windowrules.tsv"
local f = io.open(path, "r")
if not f then return end

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
local function plain(s) return not s:find("[%^%$%.%*%+%?%[%]%(%)%|\\]") end

for line in f:lines() do
    local p = split(line)
    if #p >= 11 then
        local cls, title = unescape(p[2]), unescape(p[3])
        local match = {}
        if cls ~= "" then match.class = plain(cls) and ("(?i:" .. cls .. ")") or cls end
        if title ~= "" then match.title = plain(title) and (".*(?i:" .. title .. ").*") or title end
        if next(match) then
            local rule = {name = "end4pC-user-" .. unescape(p[1]), match = match}
            local ws = tonumber(p[4]) or 0
            if ws > 0 then rule.workspace = tostring(ws) .. (p[5] == "1" and " silent" or "") end
            local mon = unescape(p[6])
            if mon ~= "" then rule.monitor = mon end
            if p[7] == "1" then rule.float = true elseif p[7] == "2" then rule.tile = true end
            if p[8] == "1" then rule.center = true end
            local w, h = unescape(p[9]):match("^(%d+)x(%d+)$")
            if w then rule.size = w .. " " .. h end
            local op = tonumber(p[10]) or 100
            if op < 100 then rule.opacity = string.format("%.2f", op / 100) end
            if p[11] == "1" then rule.pin = true end
            local ok, err = pcall(hl.window_rule, rule)
            if not ok then print("end4pC rules: " .. tostring(err)) end
        end
    end
end
f:close()
