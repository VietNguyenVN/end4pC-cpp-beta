require("hyprland.variables")
require("custom.variables")

local hyprScripts = "$HOME/.config/hypr/hyprland/scripts"

--##! Shell
-- The shell (end4pC) binds its own keys in ~/.config/end4pC/hyprland/end4pC.lua: launcher, wallpaper selector, media, settings, tools,
-- power menu, lock, screenshots, recording, brightness and the window switcher.
--###! Clipboard & Emoji

--##! Utilities
--###! Screenshot
--hl.bind("SUPER + SHIFT + S", hl.dsp.exec_cmd("hyprshot --freeze --clipboard-only --mode region --silent") )
hl.bind("Print", hl.dsp.exec_cmd("grim -o \"$(hyprctl activeworkspace -j | jq -r '.monitor')\" - | wl-copy"), {locked = true, description = "Screenshot"} )
hl.bind("CTRL + Print", hl.dsp.exec_cmd("mkdir -p $(xdg-user-dir PICTURES)/Screenshots && grim -o \"$(hyprctl activeworkspace -j | jq -r '.monitor')\" $(xdg-user-dir PICTURES)/Screenshots/Screenshot_\"$(date '+%Y-%m-%d_%H.%M.%S')\".png"), {locked = true, non_consuming = true, description = "Screenshot (save to file)"} )
hl.bind("CTRL + Print", hl.dsp.exec_cmd("grim -o \"$(hyprctl activeworkspace -j | jq -r '.monitor')\" - | wl-copy"), {locked = true, non_consuming = true} ) -- # [hidden]

--###! OCR & Search
hl.bind("SUPER + SHIFT + X", hl.dsp.exec_cmd(
    "pidof slurp || grim -g \"$(slurp $SLURP_ARGS)\" \"/tmp/ocr_image.png\" && tesseract \"/tmp/ocr_image.png\" stdout -l $(tesseract --list-langs | awk 'NR>1{print $1}' | tr '\\\\n' '+' | sed 's/\\\\+$/\\\\n/') | wl-copy && rm \"/tmp/ocr_image.png\""
), {description = "Character recognition (OCR)"} )
hl.bind("SUPER + SHIFT + A", hl.dsp.exec_cmd("pidof slurp || "..hyprScripts.."/snip_to_search.sh"), {description = "Google Lens"} )

--###! Color & AI
hl.bind("SUPER + SHIFT + C", hl.dsp.exec_cmd("hyprpicker -a"), {description = "Pick color"} )
hl.bind("SUPER + SHIFT + ALT + mouse:273", hl.dsp.exec_cmd(hyprScripts.."/ai/primary-buffer-query.sh"), {description = "Generate AI summary for selected text"} )

--##! Hardware
--###! Volume
hl.bind("XF86AudioRaiseVolume", hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 2%+ -l 1.5"), {locked = true, repeating = true, description = "Volume up"} )
hl.bind("XF86AudioLowerVolume", hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 2%-"),        {locked = true, repeating = true, description = "Volume down"} )
hl.bind("XF86AudioMute",        hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_SINK@ toggle"),             {locked = true, description = "Toggle mute"} )
hl.bind("SUPER + SHIFT + M",    hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_SINK@ toggle"),             {locked = true, description = "Toggle mute"} )
hl.bind("ALT + XF86AudioMute",  hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_SOURCE@ toggle"),           {locked = true} ) -- # [hidden]
hl.bind("XF86AudioMicMute",     hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_SOURCE@ toggle"),           {locked = true} ) -- # [hidden]
hl.bind("SUPER + ALT + M",      hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_SOURCE@ toggle"),           {locked = true, description = "Toggle mic"} )

--###! Media
local mediaNextCommand = "playerctl next || playerctl position `bc <<< \"100 * $(playerctl metadata mpris:length) / 1000000 / 100\"`"
hl.bind("SUPER + SHIFT + N",             hl.dsp.exec_cmd(mediaNextCommand),          {locked = true, description = "Next track"} )
hl.bind("SUPER + SHIFT + B",             hl.dsp.exec_cmd("playerctl previous"),       {locked = true, description = "Previous track"} )
hl.bind("SUPER + SHIFT + P",             hl.dsp.exec_cmd("playerctl play-pause"),     {locked = true, description = "Play/pause media"} )
hl.bind("XF86AudioNext",                 hl.dsp.exec_cmd(mediaNextCommand),          {locked = true} ) -- # [hidden]
hl.bind("XF86AudioPrev",                 hl.dsp.exec_cmd("playerctl previous"),       {locked = true} ) -- # [hidden]
hl.bind("XF86AudioPlay",                 hl.dsp.exec_cmd("playerctl play-pause"),     {locked = true} ) -- # [hidden]
hl.bind("XF86AudioPause",                hl.dsp.exec_cmd("playerctl play-pause"),     {locked = true} ) -- # [hidden]
hl.bind("SUPER + SHIFT + ALT + mouse:275", hl.dsp.exec_cmd("playerctl previous") )                      -- # [hidden]
hl.bind("SUPER + SHIFT + ALT + mouse:276", hl.dsp.exec_cmd(mediaNextCommand) )                           -- # [hidden]

--##! Window
--###! Focus & Move
hl.bind("SUPER + mouse:272", hl.dsp.window.drag(),   {mouse = true, description = "Move window"} )
hl.bind("SUPER + mouse:273", hl.dsp.window.resize(), {mouse = true, description = "Resize window"} )
hl.bind("SUPER + mouse:274", hl.dsp.window.drag(),   {mouse = true} ) -- # [hidden]
--#/# SUPER + ←/↑/→/↓ -- Focus in direction
for i = 1, 6 do
    local arrowkey = {"Left","Right","Up","Down","BracketLeft","BracketRight"}
    local focusdir = {"l","r","u","d","l","r"}
    hl.bind("SUPER + "..arrowkey[i], hl.dsp.focus({direction = focusdir[i]}) )
end
--#/# SUPER + SHIFT + ←/↑/→/↓ -- Move window in direction
for i = 1, 4 do
    local arrowkey = {"Left","Right","Up","Down"}
    local focusdir = {"l","r","u","d"}
    hl.bind("SUPER + SHIFT + "..arrowkey[i], hl.dsp.window.move({direction = focusdir[i]}) )
end

--###! Close & Float
hl.bind("SUPER + Q",           hl.dsp.window.close(),                                          {description = "Close"} )
hl.bind("SUPER + SHIFT + ALT + Q", hl.dsp.exec_cmd("hyprctl kill"),                            {description = "Forcefully zap a window"} )
hl.bind("ALT + F4", function() hl.exec_cmd("notify-send \"Wrong close keybind\" \"Use Super+Q\" -a Hyprland") end, {non_consuming = true} ) -- # [hidden]
hl.bind("SUPER + T",     hl.dsp.window.float({action = "toggle"}),                             {description = "Float/Tile"} )
hl.bind("SUPER + F", hl.dsp.window.fullscreen({mode = "fullscreen", action = "toggle"}), {description = "Fullscreen"} )
hl.bind("SUPER + ALT + F", hl.dsp.window.fullscreen_state({internal = 0, client = 3}), {description = "Fullscreen spoof"} )

--###! Split ratio
--#/# SUPER, ;/' -- Adjust split ratio
hl.bind("SUPER + Semicolon",  hl.dsp.layout("splitratio -0.1"), {repeating = true, description = "Decrease split ratio"} )
hl.bind("SUPER + Apostrophe", hl.dsp.layout("splitratio +0.1"), {repeating = true, description = "Increase split ratio"} )

--###! Send to workspace
--#/# SUPER + SHIFT + 1-10 -- Send to workspace (follow)
for i = 1, 10 do
    local numberkey = {10,11,12,13,14,15,16,17,18,19}
    hl.bind("SUPER + SHIFT + code:"..numberkey[i], hl.dsp.window.move({ workspace = i, follow = true}) )
end
--#/# SUPER + ALT + 1-10 -- Send to workspace (silent)
for i = 1, 10 do
    local numpadkey = {87,88,89,83,84,85,79,80,81,90}
    hl.bind("SUPER + ALT + code:"..numpadkey[i], hl.dsp.window.move({ workspace = i, follow = false}) )
end
--#/# SUPER + Scroll ↑/↓ -- Volume up/down
hl.bind("SUPER + mouse_up",   hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 2%+ -l 1.5"), {description = "Volume up"} )
hl.bind("SUPER + mouse_down", hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 2%-"),        {description = "Volume down"} )

for i = 1, 6 do -- # [hidden]
    local key = {"SUPER + ALT + Page_", "SUPER + SHIFT + Page_", "CTRL + SUPER + SHIFT + "}
    local keycombos = {key[1].."down", key[1].."up", key[2].."down", key[2].."up", key[3].."Right", key[3].."Left"}
    local prefix = {"+","-","r+","r-","r+","r-"}
    hl.bind(keycombos[i], hl.dsp.window.move({workspace = prefix[i].."1"}) )
end
hl.bind("SUPER + ALT + S", hl.dsp.window.move({workspace = "special:special", follow = false}), {description = "Send to scratchpad"} )

--##! Workspace
--###! Switch
--#/# SUPER + 1-10 -- Focus workspace
for i = 1, 10 do
    local numberkey = {10,11,12,13,14,15,16,17,18,19}
    hl.bind("SUPER + code:"..numberkey[i], hl.dsp.focus({ workspace = i}) )
end
for i = 1, 10 do -- # [hidden]
    local numpadkey = {87,88,89,83,84,85,79,80,81,90}
    hl.bind("SUPER + code:"..numpadkey[i], hl.dsp.focus({ workspace = i}) )
end
--#/# CTRL + SUPER + ←/→ -- Focus left/right
for i = 1, 4 do
    local key = {"CTRL + SUPER + ", "CTRL + SUPER + ALT + "}
    local keycombos = {key[1].."Right", key[1].."Left", key[2].."Right", key[2].."Left"}
    local prefix = {"r+","r-","m+","m-"}
    hl.bind(keycombos[i], hl.dsp.focus({workspace = prefix[i].."1"}) )
end
--#/# SUPER + Page ↑/↓ -- Focus left/right
for i = 1, 4 do
    local key = {"SUPER + Page_Down", "SUPER + Page_Up"}
    local keycombos = {key[1], key[2], "CTRL + "..key[1], "CTRL + "..key[2]}
    local prefix = {"r+","r-","r+","r-"}
    hl.bind(keycombos[i], hl.dsp.focus({workspace = prefix[i].."1"}) )
end

for i = 1, 4 do -- # [hidden]
    local key = {"BracketLeft","BracketRight","Up","Down"}
    local prefix = {"-1","+1","r-5","r+5"}
    hl.bind("CTRL + SUPER + "..key[i], hl.dsp.focus({workspace = prefix[i]}) )
end

--###! Scratchpad
hl.bind("SUPER + S",          hl.dsp.workspace.toggle_special("special"), {description = "Toggle scratchpad"} )
hl.bind("CTRL + SUPER + S",   hl.dsp.workspace.toggle_special("special") ) -- # [hidden]
hl.bind("SUPER + mouse:275",  hl.dsp.workspace.toggle_special("special") ) -- # [hidden]

--##! Screen
--##! Apps
hl.bind("SUPER + Return",             hl.dsp.exec_cmd(terminal),      {description = "Terminal"} )
hl.bind("CTRL + ALT + T",             hl.dsp.exec_cmd(terminal) )                                  -- # [hidden]
hl.bind("SUPER + E",                  hl.dsp.exec_cmd(fileManager),   {description = "File manager"} )
hl.bind("SUPER + C",                  hl.dsp.exec_cmd(codeEditor),    {description = "Code editor"} )
hl.bind("SUPER + X",                  hl.dsp.exec_cmd(textEditor),    {description = "Text editor"} )
hl.bind("CTRL + SUPER + V",           hl.dsp.exec_cmd(volumeMixer),   {description = "Volume mixer"} )
hl.bind("SUPER + I",                  hl.dsp.exec_cmd(settingsApp),   {description = "Settings app"} )
hl.bind("CTRL + SHIFT + Escape",      hl.dsp.exec_cmd(taskManager),   {description = "Task manager"} )
hl.bind("CTRL + SUPER + SHIFT + ALT + W", hl.dsp.exec_cmd(officeSoftware), {description = "Office software"} )

--##! Session
hl.bind("SUPER + L",                       hl.dsp.exec_cmd("loginctl lock-session"),                          {description = "Lock Session"} )
--hl.bind("SUPER + L",                       hl.dsp.exec_cmd("hyprlock"), {description = "Lock Session"} )
hl.bind("SUPER + SHIFT + L",               hl.dsp.exec_cmd("systemctl suspend || loginctl suspend"),          {locked = true, description = "Suspend system"} )
hl.bind("CTRL + SHIFT + ALT + SUPER + Delete", hl.dsp.exec_cmd("systemctl poweroff || loginctl poweroff"),    {description = "Shutdown"} ) -- # [hidden]

--##! Virtual machines
hl.define_submap("virtual-machine", function()
    hl.bind("SUPER + ALT + F1", function()
        local currentsubmap = hl.get_current_submap()
        if currentsubmap == "virtual-machine" then
            hl.dispatch(hl.dsp.exec_cmd("notify-send 'Exited Virtual Machine submap' 'Keybinds re-enabled' -a 'Hyprland'") )
            hl.dispatch(hl.dsp.submap("reset") )
        elseif currentsubmap == "" then
            hl.dispatch(hl.dsp.exec_cmd("notify-send 'Entered Virtual Machine submap' 'Keybinds disabled. hit SUPER+ALT+F1 to escape' -a 'Hyprland'") )
            hl.dispatch(hl.dsp.submap("virtual-machine") )
        end
    end, {submap_universal = true})
end)

--##! Testing
hl.bind("SUPER + ALT + F11", hl.dsp.exec_cmd("bash -c 'RANDOM_IMAGE=$(find ~/Pictures -type f | shuf -n 1); ACTION=$(notify-send \"Test notification with body image\" \"This notification should contain your user account <b>image</b> and <a href=\\\"https://discord.com/app\\\">Discord</a> <b>icon</b>. Oh and here is a random image in your Pictures folder: <img src=\\\"$RANDOM_IMAGE\\\" alt=\\\"Testing image\\\"/>\" -a \"Hyprland\" -p -h \"string:image-path:/var/lib/AccountsService/icons/$USER\" -t 6000 -i \"discord\" -A \"openImage=Profile image\" -A \"action2=Open the random image\" -A \"action3=Useless button\"); [[ $ACTION == *openImage ]] && xdg-open \"/var/lib/AccountsService/icons/$USER\"; [[ $ACTION == *action2 ]] && xdg-open \"$RANDOM_IMAGE\"'") ) -- # [hidden]
hl.bind("SUPER + ALT + F12", hl.dsp.exec_cmd("bash -c 'RANDOM_IMAGE=$(find ~/Pictures -type f | shuf -n 1); ACTION=$(notify-send \"Test notification\" \"This notification should contain a random image in your <b>Pictures</b> folder and <a href=\\\"https://discord.com/app\\\">Discord</a> <b>icon</b>.\n<i>Flick right to dismiss!</i>\" -a \"Discord (fake)\" -p -h \"string:image-path:$RANDOM_IMAGE\" -t 6000 -i \"discord\" -A \"openImage=Profile image\" -A \"action2=Useless button\"); [[ $ACTION == *openImage ]] && xdg-open \"/var/lib/AccountsService/icons/$USER\"'") ) -- # [hidden]
hl.bind("SUPER + ALT + Equal", hl.dsp.exec_cmd("notify-send 'Urgent notification' 'Ah hell no' -u critical -a 'Hyprland keybind'") ) -- # [hidden]

--# Cursed stuff
hl.bind("CTRL + SUPER + Backslash", hl.dsp.window.resize({x = 640, y = 480, "exact"}) ) -- # [hidden]
