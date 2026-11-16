// ------------------------------------------------------------- compositor backends
// The shell talks to the compositor only through the functions below. Hyprland keeps the commands the shell always used. niri is read with
// `niri msg --json` and jq, and turned into the same tab separated lines the callers already parse, so the callers do not know which
// compositor runs. Workspaces: Hyprland's ids; on niri the index of the workspace on the focused output (niri numbers them per output,
// from 1, and the bar shows the focused output's ones).
// Included by main.cpp inside its anonymous namespace, after 08_shapes_selector.inc (captureArgv) and 02_layout_data.inc (hyprRequest,
// runDetached, wmKind: the enum is there because the parts before this one check it too).

const char* const kNiriOutputIdx =
    "($ws | map(select(.is_focused)) | .[0].output) as $out"
    " | ($ws | map(select(.output == $out) | {(.id|tostring): .idx}) | add // {}) as $idx";

std::string niriWindowsJq(const std::string& program) {
    return "ws=$(niri msg --json workspaces 2>/dev/null); niri msg --json windows 2>/dev/null"
           " | jq -r --argjson ws \"${ws:-[]}\" '" + program + "'";
}

const char* const kNiriFocusTime = "def ts: ((.focus_timestamp.secs // 0) * 1000000000 + (.focus_timestamp.nanos // 0)); ";

// ---- sway helpers (sway and SwayFX). Same rules: the jq programs must not contain single quotes. `swaymsg -r -t get_tree` gives the
// tree root -> outputs -> workspaces -> containers; every container lists its children's ids in `focus`, most recently focused first,
// so walking the tree by those lists gives the windows in focus order (the focused one first, then the rest of its workspace, then the
// other workspaces by when they were used). Windows are the leaf containers that have a pid. The scratchpad lives on the "__i3" output.
const char* const kSwayJq =
    "def kids: ((.nodes // []) + (.floating_nodes // []));"
    " def byfocus: . as $n | (($n.focus // []) | map(. as $id | ($n | kids | map(select(.id == $id)) | .[0])) | map(select(. != null))) as $f"
    "   | ($f + (($n | kids) - $f));"
    " def leaves: if ((kids | length) == 0) then (if (.pid != null and (.type == \"con\" or .type == \"floating_con\")) then [.] else [] end)"
    "   else (byfocus | map(leaves) | add // []) end;"
    " def appid: (.app_id // .window_properties.class // \"\");"
    " def wins: ([byfocus[] | select(.name != \"__i3\") | byfocus[] | select(.type == \"workspace\") | . as $ws"
    "   | (leaves | to_entries | map({w: .value, num: ($ws.num // 0), first: (.key == 0)}))] | add // [])"
    "   | sort_by(if .w.focused then 0 else 1 end); ";

std::string swayJq(const char* type, const std::string& program) {
    return std::string("swaymsg -r -t ") + type + " 2>/dev/null | jq -r '" + kSwayJq + program + "'";
}

const std::map<std::string, std::string>& kbdRuleNames() {
    static const std::map<std::string, std::string> names = [] {
        std::map<std::string, std::string> out;
        std::ifstream f("/usr/share/X11/xkb/rules/base.lst");
        std::string line, section;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            if (line[0] == '!') { section = line.substr(line.find_first_not_of("! ")); continue; }
            std::istringstream in(line);
            std::string key;
            in >> key;
            std::string rest;
            std::getline(in, rest);
            const size_t b = rest.find_first_not_of(' ');
            if (key.empty() || b == std::string::npos) continue;
            rest = rest.substr(b);
            if (section == "layout") out[key] = rest;
            else if (section == "variant") {
                const size_t colon = rest.find(": ");
                if (colon != std::string::npos) out[rest.substr(0, colon) + "/" + key] = rest.substr(colon + 2);
            }
        }
        return out;
    }();
    return names;
}

// A layout's xkb code and variant, from its name as niri gives it.
struct XkbLayout { std::string code, variant; };

XkbLayout xkbForName(const std::string& name) {
    static const std::map<std::string, XkbLayout> byName = [] {
        std::map<std::string, XkbLayout> out;
        for (const auto& [key, value] : kbdRuleNames()) {
            const size_t slash = key.find('/');
            out.emplace(value, slash == std::string::npos ? XkbLayout{key, ""} : XkbLayout{key.substr(0, slash), key.substr(slash + 1)});
        }
        return out;
    }();
    if (const auto it = byName.find(name); it != byName.end()) return it->second;
    std::string code = name.substr(0, name.find(' '));
    for (char& ch : code) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return {code, ""};
}

std::string wmActiveWindowTsv() {
    if (wmKind() == Wm::Niri)
        return captureArgv({"bash", "-c", "niri msg --json focused-window 2>/dev/null | jq -r '[.app_id // \"\", .title // \"\"] | @tsv'"});
    if (wmKind() == Wm::Sway)
        return captureArgv({"bash", "-c", swayJq("get_tree", "[.. | objects | select(.focused == true and (.type == \"con\" or .type == \"floating_con\"))] | (.[0] // {}) | [appid, (.name // \"\")] | @tsv")});
    return captureArgv({"bash", "-c", "hyprctl -j activewindow 2>/dev/null | jq -r '[.class // \"\", .title // \"\"] | @tsv'"});
}

std::string wmWindowsTsv() {
    if (wmKind() == Wm::Niri) {
        return captureArgv({"bash", "-c", niriWindowsJq(std::string(kNiriFocusTime) +
            "($ws | map({(.id|tostring): .idx}) | add // {}) as $idx"
            " | sort_by(ts) | reverse | to_entries[] | .key as $r | .value as $w"
            " | [($w.id|tostring), ($w.app_id // \"\"), ($r|tostring), ($w.id|tostring),"
            "    (($idx[($w.workspace_id|tostring)] // 0)|tostring), ($w.title // \"\")] | @tsv")});
    }
    if (wmKind() == Wm::Sway)
        return captureArgv({"bash", "-c", swayJq("get_tree", "wins | to_entries[] | .key as $r | .value as $x"
            " | [($x.w.id|tostring), ($x.w|appid), ($r|tostring), ($x.w.id|tostring), ($x.num|tostring), ($x.w.name // \"\")] | @tsv")});
    return captureArgv({"bash", "-c",
        "hyprctl clients -j | jq -r '[.[]|select(.mapped and (.hidden|not))]|sort_by(.focusHistoryID)|.[]"
        "|[.address,.class,(.focusHistoryID|tostring),.stableId,(.workspace.id|tostring),.title]|@tsv'"});
}

std::string wmWorkspaceAppsTsv() {
    if (wmKind() == Wm::Niri) {
        return captureArgv({"bash", "-c", niriWindowsJq(std::string(kNiriFocusTime) + kNiriOutputIdx +
            " | [.[] | select($idx[(.workspace_id|tostring)] != null)] as $w"
            " | ($w | group_by(.workspace_id) | map(max_by(ts).id)) as $last"
            " | $w[] | [$idx[(.workspace_id|tostring)], (.app_id // \"\"),"
            "    (if (.id as $i | any($last[]; . == $i)) then 1e15 else ((.layout.window_size[0] // 0) * (.layout.window_size[1] // 0)) end)]"
            " | @tsv")});
    }
    if (wmKind() == Wm::Sway)
        return captureArgv({"bash", "-c", swayJq("get_tree", "wins | .[] | select(.num > 0)"
            " | [(.num|tostring), (.w|appid), (if .first then 1e15 else ((.w.rect.width // 0) * (.w.rect.height // 0)) end)] | @tsv")});
    return captureArgv({"bash", "-c", "ws=$(hyprctl -j workspaces 2>/dev/null); hyprctl -j clients 2>/dev/null | jq -r --argjson ws \"${ws:-[]}\" '. as $c | ($ws | map({(.id|tostring): .lastwindow}) | add // {}) as $last | $c[] | select(.mapped) | [.workspace.id, .class, (if $last[(.workspace.id|tostring)] == .address then 1e15 else (.size[0]*.size[1]) end)] | @tsv'"});
}

std::set<int> wmOccupiedWorkspaces() {
    std::set<int> occ;
    if (wmKind() == Wm::Niri) {
        const std::string out = captureArgv({"bash", "-c", niriWindowsJq(std::string(kNiriOutputIdx) + " | .[] | ($idx[(.workspace_id|tostring)] // empty)")});
        std::istringstream in(out);
        for (int id; in >> id;) occ.insert(id);
        return occ;
    }
    if (wmKind() == Wm::Sway) {
        const std::string out = captureArgv({"bash", "-c", swayJq("get_tree", "[wins[] | .num | select(. > 0)] | unique[]")});
        std::istringstream in(out);
        for (int id; in >> id;) occ.insert(id);
        return occ;
    }
    const std::string j = hyprRequest("j/workspaces");
    size_t pos = 0;
    while ((pos = j.find("\"id\":", pos)) != std::string::npos) {
        const int id = std::atoi(j.c_str() + pos + 5);
        size_t w = j.find("\"windows\":", pos);
        size_t nextId = j.find("\"id\":", pos + 5);
        if (w != std::string::npos && (nextId == std::string::npos || w < nextId) && std::atoi(j.c_str() + w + 10) > 0)
            occ.insert(id);
        pos += 5;
    }
    return occ;
}

int wmActiveWorkspace() {
    if (wmKind() == Wm::Niri)
        return std::atoi(captureArgv({"bash", "-c", "niri msg --json workspaces 2>/dev/null | jq -r '[.[] | select(.is_focused)][0].idx // 0'"}).c_str());
    if (wmKind() == Wm::Sway)
        return std::atoi(captureArgv({"bash", "-c", "swaymsg -r -t get_workspaces 2>/dev/null | jq -r '[.[] | select(.focused)][0].num // 0 | if . < 0 then 0 else . end'"}).c_str());
    const std::string j = hyprRequest("j/activeworkspace");
    const size_t pos = j.find("\"id\":");
    return pos == std::string::npos ? 0 : std::atoi(j.c_str() + pos + 5);
}

void wmFocusWindow(const std::string& addr) {
    if (wmKind() == Wm::Niri) { runDetached({"niri", "msg", "action", "focus-window", "--id", addr.c_str()}); return; }
    if (wmKind() == Wm::Sway) { const std::string c = "[con_id=" + addr + "] focus"; runDetached({"swaymsg", c.c_str()}); return; }
    const std::string r = hyprRequest("dispatch hl.dsp.focus({ window = \"address:" + addr + "\" })");
    if (r.rfind("ok", 0) != 0) hyprRequest("dispatch focuswindow address:" + addr);
}

void wmCloseWindow(const std::string& addr) {
    if (wmKind() == Wm::Niri) { runDetached({"niri", "msg", "action", "close-window", "--id", addr.c_str()}); return; }
    if (wmKind() == Wm::Sway) { const std::string c = "[con_id=" + addr + "] kill"; runDetached({"swaymsg", c.c_str()}); return; }
    const std::string r = hyprRequest("dispatch hl.dsp.window.close({ window = \"address:" + addr + "\" })");
    if (r.rfind("ok", 0) != 0) hyprRequest("dispatch closewindow address:" + addr);
}

std::string wmKillWindowsCommand() {
    if (wmKind() == Wm::Niri) return "niri msg --json windows 2>/dev/null | jq -r '.[].pid' | xargs -r kill; sleep 0.4; ";
    if (wmKind() == Wm::Sway) return swayJq("get_tree", "[wins[] | .w.pid] | unique[]") + " | xargs -r kill; sleep 0.4; ";
    return "hyprctl clients -j | jq -r '.[].pid' | xargs -r kill; sleep 0.4; ";
}

std::string wmQuitCommand() {
    if (wmKind() == Wm::Niri) return "niri msg action quit --skip-confirmation";
    if (wmKind() == Wm::Sway) return "swaymsg exit";
    return "pkill -i Hyprland";
}

// Workspaces: the bar's numbers. Hyprland's ids; on niri the focused output's index.
void focusWorkspace(int id) {
    if (wmKind() == Wm::Niri) { runDetached({"niri", "msg", "action", "focus-workspace", std::to_string(id).c_str()}); return; }
    if (wmKind() == Wm::Sway) {
        if (wmActiveWorkspace() != id) runDetached({"swaymsg", "workspace", "number", std::to_string(id).c_str()});
        return;
    }
    const std::string r = hyprRequest("dispatch hl.dsp.focus({ workspace = " + std::to_string(id) + " })");
    if (r.rfind("ok", 0) != 0) hyprRequest("dispatch workspace " + std::to_string(id));
}

// niri's workspaces are dynamic: the ones that exist on the focused output (always one empty at the end). 0 on Hyprland (its count is
// the configured one).
int wmWorkspaceCount() {
    if (wmKind() != Wm::Niri) return 0;
    return std::atoi(captureArgv({"bash", "-c", "niri msg --json workspaces 2>/dev/null | jq -r '(map(select(.is_focused)) | .[0].output) as $o | [.[] | select(.output == $o)] | length'"}).c_str());
}

void refreshOccupied(App& app) {
    app.occupied = wmOccupiedWorkspaces();
    app.wsCount = wmWorkspaceCount();
}

void refreshActiveWorkspace(App& app) {
    if (const int id = wmActiveWorkspace(); id > 0) app.workspace = id;
}

std::string wmKeyboardsTsv() {
    if (wmKind() == Wm::Niri) {
        const std::string out = captureArgv({"bash", "-c",
            "niri msg --json keyboard-layouts 2>/dev/null | jq -r '[(.names | join(\"|\")), (.current_idx | tostring)] | @tsv'"});
        const std::string line = out.substr(0, out.find('\n'));
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) return "";
        std::string codes, variants;
        std::stringstream names(line.substr(0, tab));
        for (std::string name; std::getline(names, name, '|');) {
            const XkbLayout l = xkbForName(name);
            codes += (codes.empty() ? "" : ",") + l.code;
            variants += (variants.empty() ? "" : ",") + l.variant;
        }
        if (codes.empty()) return "";
        return "niri\t" + codes + "\t" + variants + "\t" + line.substr(tab + 1) + "\t";
    }
    if (wmKind() == Wm::Sway) {
        const std::string out = captureArgv({"bash", "-c",
            "swaymsg -r -t get_inputs 2>/dev/null | jq -r '[.[] | select(.type == \"keyboard\" and ((.xkb_layout_names // []) | length) > 0)] | .[0] // {}"
            " | [((.xkb_layout_names // []) | join(\"|\")), ((.xkb_active_layout_index // 0) | tostring)] | @tsv'"});
        const std::string line = out.substr(0, out.find('\n'));
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) return "";
        std::string codes, variants;
        std::stringstream names(line.substr(0, tab));
        for (std::string name; std::getline(names, name, '|');) {
            const XkbLayout l = xkbForName(name);
            codes += (codes.empty() ? "" : ",") + l.code;
            variants += (variants.empty() ? "" : ",") + l.variant;
        }
        if (codes.empty()) return "";
        return "sway\t" + codes + "\t" + variants + "\t" + line.substr(tab + 1) + "\t";
    }
    return captureArgv({"bash", "-c",
        "hyprctl -j devices 2>/dev/null | jq -r '.keyboards[]|select(.main)|[.name,.layout,.variant,(.active_layout_index|tostring),.active_keymap]|@tsv' | head -1"});
}

void wmSwitchKeyboard(const std::string& device, int idx) {
    if (wmKind() == Wm::Niri) { runDetached({"niri", "msg", "action", "switch-layout", std::to_string(idx).c_str()}); return; }
    if (wmKind() == Wm::Sway) { runDetached({"swaymsg", "input", "type:keyboard", "xkb_switch_layout", std::to_string(idx).c_str()}); return; }
    runDetached({"hyprctl", "switchxkblayout", device.c_str(), std::to_string(idx).c_str()});
}

std::string wmFocusedOutputInfo() {
    if (wmKind() == Wm::Niri)
        return captureArgv({"bash", "-c", "niri msg --json focused-output 2>/dev/null | jq -r '[.name, (.logical.x|tostring), (.logical.y|tostring)] | join(\" \")'"});
    if (wmKind() == Wm::Sway)
        return captureArgv({"bash", "-c", "swaymsg -r -t get_outputs 2>/dev/null | jq -r '.[] | select(.focused) | [.name, (.rect.x|tostring), (.rect.y|tostring)] | join(\" \")'"});
    return captureArgv({"bash", "-c", "hyprctl monitors -j | jq -r '.[]|select(.focused)|\"\\(.name) \\(.x) \\(.y)\"'"});
}

struct WmEvents {
    bool occupied = false;
    bool window = false;
    bool focus = false;
    bool keyboard = false;
    bool covered = false;   // a window may have gone fullscreen or left it
    bool reread = false;    // the workspace in use must be asked again (niri's events do not name it)
    int workspace = 0;
};

void wmClassifyEvent(const std::string& line, WmEvents& e) {
    if (wmKind() == Wm::Niri) {
        const auto is = [&line](const char* name) { return line.rfind(std::string("{\"") + name + "\":", 0) == 0; };
        if (is("WorkspacesChanged")) e.occupied = e.window = e.focus = e.reread = true;
        else if (is("WorkspaceActivated")) e.occupied = e.window = e.focus = e.reread = true;
        else if (is("WindowsChanged") || is("WindowOpenedOrChanged") || is("WindowClosed")) e.occupied = e.window = e.focus = true;
        else if (is("WindowFocusChanged") || is("WorkspaceActiveWindowChanged")) e.window = e.focus = true;
        else if (is("KeyboardLayoutsChanged") || is("KeyboardLayoutSwitched") || is("ConfigLoaded")) e.keyboard = true;
        return;
    }
    if (wmKind() == Wm::Sway) {
        const size_t c = line.find("\"change\": \"");
        if (c == std::string::npos) return;
        const size_t cEnd = line.find('"', c + 11);
        const std::string change = cEnd == std::string::npos ? "" : line.substr(c + 11, cEnd - c - 11);
        const size_t k = line.find('"', cEnd == std::string::npos ? line.size() : cEnd + 1);
        const std::string key = k == std::string::npos ? "" : line.substr(k + 1, line.find('"', k + 1) - k - 1);
        if (key == "container") {
            e.window = true;
            if (change != "title") e.focus = true;
            if (change == "new" || change == "close" || change == "move") e.occupied = true;
        } else if (key == "old" || key == "current") {
            e.occupied = e.window = e.focus = e.reread = true;
        } else if (key == "input") {
            if (change.rfind("xkb_", 0) == 0 || change == "added" || change == "removed") e.keyboard = true;
        }
        return;
    }
    const auto starts = [&line](const char* p) { return line.rfind(p, 0) == 0; };
    if (starts("createworkspace>>") || starts("destroyworkspace>>") || starts("openwindow>>") || starts("closewindow>>") || starts("movewindow>>"))
        e.occupied = true;
    if (starts("activewindow>>") || starts("windowtitle>>") || starts("closewindow>>") || starts("openwindow>>") || starts("workspace>>"))
        e.window = true;
    if (starts("activewindow>>")) e.focus = true;
    if (starts("fullscreen>>") || starts("workspace>>") || starts("activewindow>>") || starts("closewindow>>") || starts("openwindow>>") || starts("movewindow>>")) e.covered = true;
    if (starts("activelayout>>") || starts("configreloaded>>")) e.keyboard = true;
    if (starts("workspace>>")) {
        const int id = std::atoi(line.c_str() + 11);
        if (id > 0) e.workspace = id;
    }
}

// ---- A tray icon's window: a left click on the tray goes to the app's window when it has one
// Many tray apps (Discord, Telegram, ...) ignore the StatusNotifierItem Activate call under Wayland (no activation token, so the
// compositor refuses the focus), so the shell finds the app's window itself and focuses it.

std::string wmWindowsPidTsv() {
    if (wmKind() == Wm::Niri) {
        return captureArgv({"bash", "-c", niriWindowsJq(std::string(kNiriFocusTime) +
            "sort_by(ts) | reverse | to_entries[] | .key as $r | .value as $w"
            " | [($w.id|tostring), ($w.app_id // \"\"), (($w.pid // 0)|tostring), ($r|tostring), ($w.title // \"\"), ($w.app_id // \"\")] | @tsv")});
    }
    if (wmKind() == Wm::Sway)
        return captureArgv({"bash", "-c", swayJq("get_tree", "wins | to_entries[] | .key as $r | .value as $x"
            " | [($x.w.id|tostring), ($x.w|appid), (($x.w.pid // 0)|tostring), ($r|tostring), ($x.w.name // \"\"), ($x.w.window_properties.class // ($x.w|appid))] | @tsv")});
    return captureArgv({"bash", "-c",
        "hyprctl clients -j | jq -r '[.[]|select(.mapped and (.hidden|not))]|sort_by(.focusHistoryID)|.[]"
        "|[.address,.class,(.pid|tostring),(.focusHistoryID|tostring),.title,.initialClass]|@tsv'"});
}

std::string trayNorm(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

std::vector<std::string> trayWords(const TrayIcon& t) {
    static const std::set<std::string> common = {"status", "icon", "tray", "panel", "indicator", "symbolic", "systray", "notifier", "item", "client", "app", "active", "attention", "passive", "desktop", "linux"};
    std::vector<std::string> words;
    for (const std::string* src : {&t.id, &t.iconName, &t.title}) {
        std::string cur;
        auto flush = [&] {
            while (!cur.empty() && std::isdigit(static_cast<unsigned char>(cur.back()))) cur.pop_back();
            if (cur.size() >= 4 && !common.count(cur) && std::find(words.begin(), words.end(), cur) == words.end()) words.push_back(cur);
            cur.clear();
        };
        for (const unsigned char c : *src) {
            if (std::isalnum(c)) cur += static_cast<char>(std::tolower(c)); else flush();
        }
        flush();
    }
    return words;
}

int trayParentPid(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(f, line);
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return 0;
    int ppid = 0;
    std::istringstream rest(line.substr(close + 1));
    std::string state;
    rest >> state >> ppid;
    return ppid;
}

bool trayPidIsAbove(int a, int b) {
    for (int i = 0, p = b; i <= 3 && p > 1; i++, p = trayParentPid(p)) if (p == a) return true;
    return false;
}

int trayItemPid(App& app, const TrayIcon& t) {
    if (!app.bus) return 0;
    sd_bus_message* reply = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    uint32_t pid = 0;
    if (sd_bus_call_method(app.bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", &err, &reply, "s", t.bus.c_str()) >= 0)
        sd_bus_message_read(reply, "u", &pid);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return static_cast<int>(pid);
}

struct TrayWindow {
    std::string id, cls;
    int rank = -1;
    const char* how = "none";
};

TrayWindow trayFindWindow(App& app, const TrayIcon& t) {
    TrayWindow best;
    int bestScore = 0;
    const int pid = trayItemPid(app, t);
    const std::vector<std::string> words = trayWords(t);
    std::istringstream in(wmWindowsPidTsv());
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        size_t from = 0;
        for (;;) {
            const size_t tab = line.find('\t', from);
            f.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
            if (tab == std::string::npos) break;
            from = tab + 1;
        }
        if (f.size() < 5) continue;
        const int wpid = std::atoi(f[2].c_str()), rank = std::atoi(f[3].c_str());
        std::vector<std::string> keys = {trayNorm(f[1])};
        if (f.size() > 5) keys.push_back(trayNorm(f[5]));
        const size_t dot = f[1].rfind('.');
        if (dot != std::string::npos) keys.push_back(trayNorm(f[1].substr(dot + 1)));
        const std::string title = trayNorm(f[4]);
        int score = 0;
        const char* how = "none";
        if (pid > 1 && wpid > 1 && (wpid == pid || trayPidIsAbove(pid, wpid) || trayPidIsAbove(wpid, pid))) { score = 3; how = "pid"; }
        for (const std::string& w : words) {
            for (const std::string& k : keys) {
                if (k.empty()) continue;
                const bool same = k == w || (w.size() >= 5 && k.find(w) != std::string::npos) || (k.size() >= 5 && w.find(k) != std::string::npos);
                if (same && score < 2) { score = 2; how = "class"; }
            }
            if (score < 1 && w.size() >= 5 && title.find(w) != std::string::npos) { score = 1; how = "title"; }
        }
        if (score > bestScore || (score == bestScore && score > 0 && rank < best.rank)) {
            bestScore = score;
            best = {f[0], f[1], rank, how};
        }
    }
    return best;
}

bool trayFocusWindow(App& app, const TrayIcon& t) {
    const TrayWindow w = trayFindWindow(app, t);
    if (w.rank < 0) return false;
    if (w.rank != 0) wmFocusWindow(w.id);
    return true;
}
