-- end4pC: Hyprland rules and possible keybinds for this shell.
-- Loaded from ~/.config/hypr/hyprland.lua (see the "end4pC" block there). Edit here, then `hyprctl reload`.

local E = os.getenv("HOME") .. "/.config/end4pC/build/end4pC"   -- the shell binary (also acts as its own CLI)

------------------------------------------------------------------------------------------------------------------
-- Layer rules. Namespaces: end4pC-bar, end4pC-frame (the black screen frame), end4pC-wallpaper.
------------------------------------------------------------------------------------------------------------------

-- Blur behind the bar. ignore_alpha must stay above 0: the float and island styles leave parts of the bar surface
-- transparent, and those must not be blurred. 0.35 also skips the faint anti-aliased fringe of the curved edges (hug
-- pieces, rounded ends): blurred, that fringe showed as light dots over white windows. The price: a bar more see-through
-- than barOpacity 35 is not blurred at all.
-- xray = false lets the blur show the windows under the bar, not just the wallpaper.
hl.layer_rule({match = {namespace = "end4pC-bar"}, blur = true, xray = false, ignore_alpha = 0.35})

-- Wallpaper selector (an overlay layer, not a window). Same blur as the bar so it follows `barOpacity`;
-- It grows in from its centre by itself (the shell animates it, no_anim below), the same on every compositor. ignore_alpha 0.35 for the same reason as the bar: with 0 the
-- whole rectangle of the surface was blurred, corners outside the rounded panel included, and the faint fringe of the curve
-- showed as light dots over bright wallpapers. (Like the bar: a barOpacity under 35 turns its blur off.)
hl.layer_rule({match = {namespace = "end4pC-selector"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-selector"}, no_anim = true})

-- Screenshot tool (a full screen overlay holding the frozen screen): it must appear and vanish at once, or the picture would seem to jump.
hl.layer_rule({match = {namespace = "end4pC-capture"}, no_anim = true})

-- Launcher (overlay). Its surface is bigger than the pill (room for the drop shadow), so ignore_alpha has to sit
-- between the shadow (at most ~0.22 near the pill) and the pill itself (barOpacity, 0.5 by default): only the
-- painted pill gets blurred, not the shadow or the empty margin. If you set barOpacity below 25 the blur turns off.
hl.layer_rule({match = {namespace = "end4pC-launcher"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-launcher"}, no_anim = true})

-- Notification popups (cards at the top right). Same blur as the bar so they follow barOpacity; the surface is much
-- taller than the cards, so ignore_alpha keeps the blur off the transparent rest.
hl.layer_rule({match = {namespace = "end4pC-notifications"}, blur = true, xray = false, ignore_alpha = 0.05})
hl.layer_rule({match = {namespace = "end4pC-notifications"}, no_anim = true})

-- Notification panel (history card that opens from the bell). Its surface is bigger than the card (room for the drop
-- shadow), so ignore_alpha sits above the faint shadow: only the card is blurred.
hl.layer_rule({match = {namespace = "end4pC-notif-panel"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-notif-panel"}, no_anim = true})

-- Player card (opens from the media widget). It animates itself like the clock popup. The card is opaque (its own blurred cover), so no compositor blur.
hl.layer_rule({match = {namespace = "end4pC-media"}, no_anim = true})

-- Clock popup: it animates itself (grows out of the clock, shrinks back into it), so Hyprland must not add its own popin/fade on top.
hl.layer_rule({match = {namespace = "end4pC-clock"}, no_anim = true})
-- Blur behind the card, so it follows barOpacity like the other panels (ignore_alpha keeps it off the faint shadow).
hl.layer_rule({match = {namespace = "end4pC-clock"}, blur = true, xray = false, ignore_alpha = 0.35})

-- Dock (the bottom pill): it slides itself when it auto-hides, so no Hyprland animation; the same blur as the bar's cards.
hl.layer_rule({match = {namespace = "end4pC-dock"}, no_anim = true})
hl.layer_rule({match = {namespace = "end4pC-dock"}, blur = true, xray = false, ignore_alpha = 0.35})

-- On-screen display (volume / brightness bar at the right edge). Opaque, so no blur; it just slides in from the edge.
hl.layer_rule({match = {namespace = "end4pC-osd"}, no_anim = true})

-- The frame is solid black: nothing to blur. The wallpaper is a plain background layer.
-- hl.layer_rule({match = {namespace = "end4pC-frame"}, blur = false})

------------------------------------------------------------------------------------------------------------------
-- Possible keybinds. All commented out so nothing clashes with the ones you already have: uncomment what you want.
-- Every action is `end4pC <command>`; `end4pC config` prints the current settings.
------------------------------------------------------------------------------------------------------------------

-- Active binds -------------------------------------------------------------------------------------------------
-- The descriptions show in the launcher's key bind list (type `<` and a name): keep them short and in Title Case.

-- Wallpaper selector (overlay): open / close
hl.bind("SUPER + W", hl.dsp.exec_cmd(E .. " selector toggle"), {description = "Wallpaper Selector"})

-- Screenshot: drag a region (Esc cancels); saved in Pictures/Screenshots and copied to the clipboard
hl.bind("SUPER + SHIFT + S", hl.dsp.exec_cmd(E .. " screenshot region"), {description = "Screenshot (Region)"})

-- Same, but the picture opens in swappy (draw, crop, text) before it is saved
hl.bind("SUPER + ALT + S", hl.dsp.exec_cmd(E .. " screenshot edit"), {description = "Screenshot (Region, Edit in Swappy)"})

-- Player card (the media panel): toggle
hl.bind("SUPER + M", hl.dsp.exec_cmd(E .. " media toggle"), {description = "Media Player"})

-- Settings app (separate window)
hl.bind("SUPER + Escape", hl.dsp.exec_cmd(E .. " settings"), {description = "Settings"})

-- Tools app (translator, notes, image converter, video editor): toggles the window.
hl.bind("SUPER + slash", hl.dsp.exec_cmd(E .. " tools"), {description = "Tools"})

-- Launcher: a lone Super press (released without another key).
hl.bind("SUPER + Super_L", hl.dsp.exec_cmd(E .. " launcher toggle"), {release = true, description = "Launcher"})

-- Clipboard history: the launcher, opened straight in its clipboard mode (press again to close).
hl.bind("SUPER + V", hl.dsp.exec_cmd(E .. " 'launcher clipboard'"), {description = "Clipboard history"})

-- Screen magnifier: SUPER + = / SUPER + - (and the keypad's + / -) zoom the screen between 1x and 3x
-- On-screen keyboard: SUPER + K shows or hides it
hl.bind("SUPER + K", hl.dsp.exec_cmd(E .. " 'osk toggle'"), {description = "On-screen keyboard"})

hl.bind("SUPER + Equal", hl.dsp.exec_cmd(E .. " 'magnifier in'"), {repeating = true, description = "Zoom in"})
hl.bind("SUPER + Minus", hl.dsp.exec_cmd(E .. " 'magnifier out'"), {repeating = true, description = "Zoom out"})
hl.bind("SUPER + code:86", hl.dsp.exec_cmd(E .. " 'magnifier in'"), {repeating = true})
hl.bind("SUPER + code:82", hl.dsp.exec_cmd(E .. " 'magnifier out'"), {repeating = true})

-- Brightness keys: change the monitor brightness (DDC) and show the on-screen bar.
hl.bind("XF86MonBrightnessUp",   hl.dsp.exec_cmd(E .. " brightness up"),   {locked = true, repeating = true, description = "Brightness Up"})
hl.bind("XF86MonBrightnessDown", hl.dsp.exec_cmd(E .. " brightness down"), {locked = true, repeating = true, description = "Brightness Down"})

-- Possible binds, off by default ---------------------------------------------------------------------------------

-- Screen recording on/off (wf-recorder, saved in ~/Videos)
hl.bind("SUPER + SHIFT + R", hl.dsp.exec_cmd(E .. " 'quick 1'"), {locked = true, description = "Record Screen"})

-- Colour picker (hyprpicker, copies the colour to the clipboard)
-- hl.bind("SUPER + SHIFT + C", hl.dsp.exec_cmd(E .. " quick 0"))

-- Move the bar to the top / bottom of the screen
-- hl.bind("SUPER + ALT + Up",   hl.dsp.exec_cmd(E .. " set barPosition top"))
-- hl.bind("SUPER + ALT + Down", hl.dsp.exec_cmd(E .. " set barPosition bottom"))

-- Workspace style: numbered boxes (default) or dots (gnome)
-- hl.bind("SUPER + ALT + N", hl.dsp.exec_cmd(E .. " set workspaceStyle default"))
-- hl.bind("SUPER + ALT + D", hl.dsp.exec_cmd(E .. " set workspaceStyle gnome"))

-- Show the workspaces on the left next to the media player / hide them again
-- hl.bind("SUPER + ALT + W", hl.dsp.exec_cmd(E .. " set left workspaces,media"))
-- hl.bind("SUPER + ALT + E", hl.dsp.exec_cmd(E .. " set left media"))

-- Restart the shell (the control center has a button for it too). The script works from outside, so it also fixes a shell that is stuck.
hl.bind("SUPER + R", hl.dsp.exec_cmd("bash " .. os.getenv("HOME") .. "/.config/end4pC/scripts/restart.sh"), {description = "Restart the Shell"})

-- Settings app (end4pC-settings): a normal window, always floating, centred and with a fixed size.
hl.window_rule({
    name  = "end4pC-settings",
    match = { class = "^end4pC-settings$" },

    float  = true,
    center = true,
    size   = "1000 720",
    -- The app draws its own big rounded corners (more than a rule allows), so the compositor adds none.
    rounding    = 0,
    border_size = 0,
    no_shadow   = true,
    no_blur     = true,   -- else the compositor blurs the whole rectangle and the transparent corners show it
    no_dim      = true,
    decorate    = false,
})

-- Tools app (end4pC-tools): a normal window, always floating, centred and with a fixed size.
hl.window_rule({
    name  = "end4pC-tools",
    match = { class = "^end4pC-tools$" },

    float  = true,
    center = true,
    size   = "1240 740",
    -- The app draws its own big rounded corners (more than a rule allows), so the compositor adds none.
    rounding    = 0,
    border_size = 0,
    no_shadow   = true,
    no_blur     = true,   -- else the compositor blurs the whole rectangle and the transparent corners show it
    no_dim      = true,
    decorate    = false,
})

-- Start the shell with the session.
hl.on("hyprland.start", function()
    hl.exec_cmd(os.getenv("HOME") .. "/.config/end4pC/build/end4pC")
end)

-- Lock screen: its own binary (ext-session-lock). It takes over SUPER+L from hypr/hyprland/keybinds.lua (loginctl lock-session);
-- unbinding here leaves that file untouched. SUPER+CTRL+L stays as a second way in.
local lockCmd = os.getenv("HOME") .. "/.config/end4pC/build/end4pC-lock"
pcall(hl.unbind, "SUPER + L")
hl.bind("SUPER + L", hl.dsp.exec_cmd(lockCmd), {description = "Lock Screen"})
hl.bind("SUPER + CTRL + L", hl.dsp.exec_cmd(lockCmd), {description = "Lock Screen"})

-- Power menu (overlay with lock, logout, suspend, shutdown, hibernate, reboot). The unbind keeps working if your own
-- hypr/hyprland/keybinds.lua binds SUPER+P to something else.
pcall(hl.unbind, "SUPER + P")
hl.bind("SUPER + P", hl.dsp.exec_cmd(E .. " power toggle"), {description = "Power Menu"})
hl.layer_rule({match = {namespace = "end4pC-power"}, blur = true, xray = false, ignore_alpha = 0})
hl.layer_rule({match = {namespace = "end4pC-power"}, no_anim = true})

-- Window switcher (Alt+Tab): the bind opens it and moves to the next window; letting go of Alt picks it. Shift goes backwards.
-- If Alt+Tab clashes with something, the same can be done on SUPER + Tab.
hl.bind("ALT + Tab", hl.dsp.exec_cmd(E .. " 'switcher next'"), {repeating = true, description = "Window Switcher"})
hl.bind("ALT + SHIFT + Tab", hl.dsp.exec_cmd(E .. " 'switcher prev'"), {repeating = true, description = "Window Switcher (Back)"})
hl.layer_rule({match = {namespace = "end4pC-switcher"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-switcher"}, no_anim = true})

-- Password dialog (polkit): a scrim over the screen with the dialog in the middle (solid), so only the scrim is blurred.
hl.layer_rule({match = {namespace = "end4pC-polkit"}, blur = true, xray = false, ignore_alpha = 0})
hl.layer_rule({match = {namespace = "end4pC-polkit"}, no_anim = true})

-- Control center (opens from the avatar). Its surface is bigger than the card (room for the shadow), so ignore_alpha
-- sits above the faint shadow: only the card is blurred.
hl.layer_rule({match = {namespace = "end4pC-control"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-control"}, no_anim = true})

-- Menu of a tray icon (drawn by the shell from the app's own menu). Same as the control center: blur only under the card, not
-- under its faint shadow.
hl.layer_rule({match = {namespace = "end4pC-tray-menu"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-tray-menu"}, animation = "popin 85%"})

-- Volume popup (a left click on the volume icon: the output and input devices). The same card as the tray menu: blur under
-- the card only, and the same pop-in.
hl.layer_rule({match = {namespace = "end4pC-audio-menu"}, blur = true, xray = false, ignore_alpha = 0.35})
-- It animates itself (it grows out of the volume icon and is swallowed back into it), so no Hyprland pop-in on top.
hl.layer_rule({match = {namespace = "end4pC-audio-menu"}, no_anim = true})

-- Bluetooth popup (the bar's Bluetooth icon): the same card, blur under it only, and it animates itself like the volume popup.
hl.layer_rule({match = {namespace = "end4pC-bt-menu"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-bt-menu"}, no_anim = true})

-- The dynamic island's expansion: the island's own body grows into a card (an overlay that takes the island's place), so it blurs like the bar
-- and animates itself.
hl.layer_rule({match = {namespace = "end4pC-island-card"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-island-card"}, no_anim = true})

-- Desktop menu (right click on the wallpaper): blur under its cards only, and the same pop-in as the control center.
hl.layer_rule({match = {namespace = "end4pC-desktop-menu"}, blur = true, xray = false, ignore_alpha = 0.35})
hl.layer_rule({match = {namespace = "end4pC-desktop-menu"}, no_anim = true})

-- To-Do desktop widget: blur under its card. The card is 90% opaque; its soft shadow (50% at most, a little more just under the card
-- because it is drawn 3 px lower) must not be blurred, or a dark band shows below the card: ignore_alpha sits between the two.
hl.layer_rule({match = {namespace = "end4pC-widget-todo"}, blur = true, xray = false, ignore_alpha = 0.6})

-- Calendar desktop widget: a card of 94% opacity, blurred under it like the To-Do.
hl.layer_rule({match = {namespace = "end4pC-widget-calendar"}, blur = true, xray = false, ignore_alpha = 0.6})

-- Resources desktop widget: four translucent tiles (the empty part of each is 34% opaque, so ignore_alpha has to sit under 0.34), blurred
-- under the tiles. The widget's own shadow reaches at most ~0.25 outside the tiles, so 0.3 keeps the blur off it.
hl.layer_rule({match = {namespace = "end4pC-widget-resources"}, blur = true, xray = false, ignore_alpha = 0.3})

-- Desktop widgets (every layer called end4pC-widget-<name>, so a new widget needs no new rule): no open / close / resize
-- animation. A drag makes the surface the whole screen and back to a card; Hyprland would animate that as a big zoom (the
-- "boing"). The widgets draw their own movement.
hl.layer_rule({match = {namespace = "end4pC-widget-.*"}, no_anim = true})

-- Settings that only make sense on one machine go in hyprland/local.lua (not in git): this file is the same for everybody.
do
    local path = os.getenv("HOME") .. "/.config/end4pC/hyprland/local.lua"
    local f = io.open(path, "r")
    if f then
        f:close()
        local ok, err = pcall(dofile, path)
        if not ok then print("end4pC: " .. tostring(err)) end
    end
end

-- Options saved from the Hyprland page of the settings (the same files end4-pC writes). hyprland.lua of end4-pC already
-- requires them; if yours does not, they are loaded here.
do
    local home = os.getenv("HOME")
    local f = io.open(home .. "/.config/hypr/hyprland.lua", "r")
    local main = f and f:read("*a") or ""
    if f then f:close() end
    if not main:find("shellOverrides", 1, true) then
        for _, name in ipairs({"main", "animations"}) do
            local path = home .. "/.config/hypr/hyprland/shellOverrides/" .. name .. ".lua"
            local o = io.open(path, "r")
            if o then
                o:close()
                local ok, err = pcall(dofile, path)
                if not ok then print("end4pC: " .. tostring(err)) end
            end
        end
    end
end
