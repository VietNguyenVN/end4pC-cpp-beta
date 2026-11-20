// The on-screen keyboard: an overlay that never takes the keyboard focus, so what a key types goes to the window you were in. It can be dragged by
// its top bar. The keys are sent with wtype (virtual-keyboard protocol); Shift, Ctrl, Alt and Super stay down for the next key (Shift twice: locked).
struct OskKey { std::string lv[4], label, sym; double w = 1; int kind = 0; Rect r{}; bool cased = false; };  // lv: the text at base, Shift, AltGr, Shift + AltGr; kind: 0 character, 1 special, 2 modifier, 3 caps
constexpr double kOskUnit = 46, kOskGap = 5, kOskPad = 12, kOskHeader = 48, kOskBtn = 32, kOskBtnInset = 14;  // (the top bar: its two buttons are 32 px, 8 px from the top and the bottom of it)
enum { kOskShift = 1, kOskCtrl = 2, kOskAlt = 4, kOskSuper = 8, kOskAltGr = 16 };

std::vector<std::vector<OskKey>> g_oskRows;  // the keys of the layout in use, row by row
std::string g_oskLayout;                     // which layout g_oskRows was made for ("es" or "latam:deadtilde")

std::string oskKeysymText(xkb_keysym_t s) {
    if (s == XKB_KEY_NoSymbol) return "";
    static const std::map<xkb_keysym_t, const char*> dead = {{XKB_KEY_dead_acute, "\xC2\xB4"}, {XKB_KEY_dead_grave, "`"}, {XKB_KEY_dead_circumflex, "^"}, {XKB_KEY_dead_tilde, "~"},
        {XKB_KEY_dead_diaeresis, "\xC2\xA8"}, {XKB_KEY_dead_cedilla, "\xC2\xB8"}, {XKB_KEY_dead_abovering, "\xC2\xB0"}, {XKB_KEY_dead_macron, "\xC2\xAF"}};
    if (const auto d = dead.find(s); d != dead.end()) return d->second;
    char buf[8] = {};
    const int n = xkb_keysym_to_utf8(s, buf, sizeof buf);
    if (n <= 1 || static_cast<unsigned char>(buf[0]) < 0x20) return "";
    return buf;
}

// The keys of a keyboard layout, from its xkb keymap: the same physical keys as a PC keyboard (US shape, or ISO with the extra key beside Shift),
// with the symbols that layout gives them at the four levels.
void oskBuildRows(const std::string& layout, const std::string& variant) {
    static xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const std::string key = layout + ":" + variant;
    if (key == g_oskLayout && !g_oskRows.empty()) return;
    xkb_keymap* map = nullptr;
    if (ctx) {
        xkb_rule_names names = {};
        names.layout = layout.c_str();
        names.variant = variant.c_str();
        map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!map) { names.layout = "us"; names.variant = ""; map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS); }
    }
    g_oskLayout = key;
    g_oskRows.clear();
    if (!map) return;
    bool altGr = false;
    auto ch = [&](int evdev, double w = 1) {
        OskKey k;
        k.w = w;
        const xkb_keycode_t kc = static_cast<xkb_keycode_t>(evdev + 8);
        const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(map, kc, 0);
        for (xkb_level_index_t l = 0; l < 4 && l < levels; l++) {
            const xkb_keysym_t* syms = nullptr;
            if (xkb_keymap_key_get_syms_by_level(map, kc, 0, l, &syms) > 0) k.lv[l] = oskKeysymText(syms[0]);
            if (l == 0 && syms) {
                char name[64] = {};
                xkb_keysym_get_name(syms[0], name, sizeof name);
                k.sym = name;
                k.cased = xkb_keysym_to_upper(syms[0]) != xkb_keysym_to_lower(syms[0]);
            }
        }
        if (!k.lv[2].empty()) altGr = true;
        return k;
    };
    auto sp = [](const char* label, const char* sym, double w) { OskKey k; k.label = label; k.sym = sym; k.w = w; k.kind = 1; return k; };
    auto mod = [](const char* label, const char* sym, double w) { OskKey k; k.label = label; k.sym = sym; k.w = w; k.kind = 2; return k; };
    // ISO keyboards (everything but the US layout) have a key beside the left Shift and a taller Enter
    const bool iso = layout != "us" && layout != "us,us";
    std::vector<OskKey> r0 = {sp("Esc", "Escape", 1.3), ch(41)};
    for (int c = 2; c <= 13; c++) r0.push_back(ch(c));
    r0.push_back(sp("\xE2\x8C\xAB", "BackSpace", 1.7));
    std::vector<OskKey> r1 = {sp("Tab", "Tab", 1.6)};
    for (int c = 16; c <= 27; c++) r1.push_back(ch(c));
    if (!iso) r1.push_back(ch(43, 1.4));
    std::vector<OskKey> r2;
    { OskKey caps; caps.label = "Caps"; caps.sym = "Caps_Lock"; caps.w = 1.9; caps.kind = 3; r2.push_back(caps); }
    for (int c = 30; c <= 40; c++) r2.push_back(ch(c));
    if (iso) r2.push_back(ch(43));
    r2.push_back(sp("\xE2\x86\xB5", "Return", iso ? 1.3 : 2.1));
    std::vector<OskKey> r3 = {mod("Shift", "shift", iso ? 1.4 : 2.4)};
    if (iso) r3.push_back(ch(86));
    for (int c = 44; c <= 53; c++) r3.push_back(ch(c));
    r3.push_back(mod("Shift", "shift", 2.4));
    OskKey space; space.label = " "; space.sym = "space"; space.w = altGr ? 5.0 : 6.3; space.kind = 1; space.lv[0] = space.lv[1] = " ";
    std::vector<OskKey> r4 = {mod("Ctrl", "ctrl", 1.5), mod("Super", "super", 1.3), mod("Alt", "alt", 1.3), space};
    r4.push_back(altGr ? mod("AltGr", "altgr", 1.3) : mod("Alt", "alt", 1.3));
    for (const auto& a : {std::pair<const char*, const char*>{"\xE2\x86\x90", "Left"}, {"\xE2\x86\x91", "Up"}, {"\xE2\x86\x93", "Down"}, {"\xE2\x86\x92", "Right"}}) r4.push_back(sp(a.first, a.second, 1.1));
    g_oskRows = {r0, r1, r2, r3, r4};
    xkb_keymap_unref(map);
}

struct OskGeom { double w = 0, h = 0; std::vector<OskKey> keys; Rect header, closeBtn, resetBtn; };

OskGeom oskGeom() {
    OskGeom G;
    if (g_oskRows.empty()) oskBuildRows("us", "");
    const auto& rows = g_oskRows;
    double maxRow = 0;
    for (const auto& row : rows) {
        double u = 0;
        for (const OskKey& k : row) u += k.w;
        maxRow = std::max(maxRow, u * kOskUnit + (row.size() - 1) * kOskGap);
    }
    G.w = maxRow + 2 * kOskPad;
    double y = kOskHeader;
    for (const auto& row : rows) {
        double totalU = 0;
        for (const OskKey& k : row) totalU += k.w;
        const double free = (G.w - 2 * kOskPad) - (row.size() - 1) * kOskGap, unit = free / totalU;  // the row fills the width
        double x = kOskPad;
        for (OskKey k : row) {
            k.r = {x, y, k.w * unit, kOskUnit};
            x += k.w * unit + kOskGap;
            G.keys.push_back(k);
        }
        y += kOskUnit + kOskGap;
    }
    G.h = y - kOskGap + kOskPad;
    G.header = {0, 0, G.w, kOskHeader};
    G.closeBtn = {G.w - kOskBtnInset - kOskBtn, (kOskHeader - kOskBtn) / 2, kOskBtn, kOskBtn};
    G.resetBtn = {G.closeBtn.x - 8 - kOskBtn, G.closeBtn.y, kOskBtn, kOskBtn};
    return G;
}

// What a key shows and types with the modifiers that are on: Shift (and Caps, for the letters) and AltGr pick the level.
std::string oskKeyText(const OskState& S, const OskKey& k) {
    if (k.kind != 0) return k.label;
    const bool shift = (S.mods & kOskShift) != 0, altgr = (S.mods & kOskAltGr) != 0;
    const int lvl = (altgr ? 2 : 0) + ((k.cased ? (shift != S.caps) : shift) ? 1 : 0);
    if (!k.lv[lvl].empty()) return k.lv[lvl];
    return !k.lv[altgr ? 2 : 0].empty() ? k.lv[altgr ? 2 : 0] : k.lv[0];
}

void drawOsk(App& app);

// The layout the user is typing in (the bar's keyboard module reads it from the compositor).
void oskSync(App& app) {
    std::string code = "us", variant;
    if (!app.kbd.list.empty()) {
        const size_t i = std::min<size_t>(static_cast<size_t>(std::max(0, app.kbd.active)), app.kbd.list.size() - 1);
        code = app.kbd.list[i].code;
        if (i < app.kbd.variants.size()) variant = app.kbd.variants[i];
    }
    const std::string before = g_oskLayout;
    oskBuildRows(code, variant);
    if (app.osk.surface && app.osk.layer && g_oskLayout != before) {  // the layout changed under an open keyboard: its keys, and maybe its width
        const OskGeom G = oskGeom();
        zwlr_layer_surface_v1_set_size(app.osk.layer, static_cast<uint32_t>(std::ceil(G.w)), static_cast<uint32_t>(std::ceil(G.h)));
        wl_surface_commit(app.osk.surface);
        drawOsk(app);
    }
}

// Sends one key: a character as text (Shift and Caps already applied), anything else as key names with the held modifiers.
void oskSend(const App& app, const OskKey& k) {
    const OskState& S = app.osk;
    const bool shift = (S.mods & kOskShift) != 0;
    std::vector<std::string> args = {"wtype"};
    const bool plain = k.kind == 0 && !(S.mods & (kOskCtrl | kOskAlt | kOskSuper)) && !oskKeyText(S, k).empty();
    if (plain) {  // typed as the text the key shows: the layout is already in it
        args.push_back("--");
        args.push_back(oskKeyText(S, k));
    } else {
        std::vector<const char*> held;
        if (S.mods & kOskCtrl) held.push_back("ctrl");
        if (S.mods & kOskAlt) held.push_back("alt");
        if (S.mods & kOskSuper) held.push_back("logo");
        if (shift) held.push_back("shift");
        for (const char* m : held) { args.push_back("-M"); args.push_back(m); }
        args.push_back("-k");
        args.push_back(k.sym);
        for (auto it = held.rbegin(); it != held.rend(); ++it) { args.push_back("-m"); args.push_back(*it); }
    }
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    if (fork() == 0) {
        childSignalReset();
        setsid();
        if (const char* home = std::getenv("HOME"); home && *home && chdir(home) != 0) {}
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
}

int oskModBit(const std::string& sym) {
    return sym == "shift" ? kOskShift : sym == "ctrl" ? kOskCtrl : sym == "alt" ? kOskAlt : sym == "altgr" ? kOskAltGr : sym == "super" ? kOskSuper : 0;
}

void drawOsk(App& app);

void oskBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kOskBuffer = {oskBufferRelease};

void drawOsk(App& app) {
    OskState& S = app.osk;
    if (!S.surface || !S.configured) return;
    const OskGeom G = oskGeom();
    const int W = static_cast<int>(std::ceil(G.w)), H = static_cast<int>(std::ceil(G.h));
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, S.scale.factor(), "end4pC-osk")) return;
    cairo_surface_t* surf = hf.surface;
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    roundedRect(cr, 0, 0, G.w, G.h, 26);
    setSource(cr, app.color("surface_container"), 0.97);
    cairo_fill(cr);
    // the top bar: a handle to drag by, a button that puts the keyboard back at the bottom centre, and the close button
    roundedRect(cr, G.w / 2 - 22, kOskHeader / 2 - 2.5, 44, 5, 2.5);
    setSource(cr, app.color("on_surface_variant"), S.dragging ? 0.9 : 0.45);
    cairo_fill(cr);
    const auto topButton = [&](const Rect& r, const char* icon, bool hot, bool danger) {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        const Color high = app.color("surface_container_highest"), e = app.color("error");
        setSource(cr, hot && danger ? Color{e.r * 0.3 + high.r * 0.7, e.g * 0.3 + high.g * 0.7, e.b * 0.3 + high.b * 0.7} : high, hot && !danger ? 1.0 : 0.85);
        cairo_fill(cr);
        drawIcon(cr, icon, r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, hot && danger ? e : app.color("on_surface_variant"));
    };
    topButton(G.resetBtn, "align-bottom", S.hover == -3, false);
    topButton(G.closeBtn, "close-circle", S.hover == -2, true);
    for (size_t i = 0; i < G.keys.size(); i++) {
        const OskKey& k = G.keys[i];
        const int bit = k.kind == 2 ? oskModBit(k.sym) : 0;
        const bool lit = (bit && (S.mods & bit)) || (k.kind == 3 && S.caps);
        const bool hot = S.hover == static_cast<int>(i), down = S.pressed == static_cast<int>(i);
        roundedRect(cr, k.r.x, k.r.y, k.r.w, k.r.h, 12);
        if (lit) setSource(cr, app.color(S.shiftLock && bit == kOskShift ? "tertiary" : "primary"));
        else if (k.kind == 1 || k.kind == 2 || k.kind == 3) setSource(cr, app.color(down ? "surface_container_highest" : "secondary_container"), down ? 1 : hot ? 1 : 0.85);
        else setSource(cr, app.color("surface_container_highest"), down ? 1 : hot ? 0.95 : 0.6);
        cairo_fill(cr);
        if (down || (hot && !lit)) { roundedRect(cr, k.r.x, k.r.y, k.r.w, k.r.h, 12); setSource(cr, app.color("on_surface"), down ? 0.16 : 0.07); cairo_fill(cr); }
        const std::string shown = oskKeyText(S, k);
        if (shown.empty()) continue;
        const Color ink = app.color(lit ? "on_primary" : (k.kind == 0 ? "on_surface" : "on_secondary_container"));
        const double px = k.kind == 0 ? 17 : (static_cast<unsigned char>(shown[0]) >= 0x80 ? 18 : 14);  // (the arrows, return and backspace are symbols)
        const int tw = clkTextWidth(L, shown, px, k.kind != 0);
        clkText(cr, L, shown, px, k.kind != 0, k.r.x + (k.r.w - tw) / 2.0, k.r.y + k.r.h / 2.0, ink, 1);
    }
    g_object_unref(L);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kOskBuffer, nullptr);
    hidpi::present(S.surface, buffer, hf, S.scale);
}

// The width and height of the monitor the keyboard is on, as its margins count them (a layer surface cannot leave its monitor).
double oskScreenW(const App& app, const Bar* bar) {
    return bar ? bar->width + 2.0 * (app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0)) : 1920;
}
Bar* oskBar(App& app) {  // the bar of the monitor in focus (the one a keybind was pressed on)
    const std::string info = wmFocusedOutputInfo();
    const std::string name = info.substr(0, info.find_first_of(" \n"));
    for (auto& b : app.bars) if (b->surface && b->connector == name) return b.get();
    return mainBar(app);
}
double oskScreenH(const App& app) { return app.screenH > 0 ? app.screenH : 1080; }

// A move of the drag: the keyboard stays inside its monitor (a layer surface cannot leave it, and past the edge it would be out of reach).
void oskMove(App& app, double dx, double dy) {
    OskState& S = app.osk;
    const OskGeom G = oskGeom();
    const double maxLeft = std::max(0.0, oskScreenW(app, S.bar) - G.w), maxBottom = std::max(0.0, oskScreenH(app) - G.h);
    S.marginLeft = std::clamp(S.marginLeft + dx, 0.0, maxLeft);
    S.marginBottom = std::clamp(S.marginBottom - dy, 0.0, maxBottom);
}

// One move per frame: the pointer's position is relative to the surface, so a second move before the compositor has shown the first
// would be measured from the old place and applied twice (that was the flicker). The next one waits for the frame callback.
void oskPlace(App& app);
void oskFrameDone(void* d, wl_callback* cb, uint32_t) {
    auto* app = static_cast<App*>(d);
    wl_callback_destroy(cb);
    app->osk.movePending = false;
    OskState& S = app->osk;
    if (S.dragging && S.onOther && (S.accDx != 0 || S.accDy != 0)) {  // moves that came in while the last one was still on its way
        const double dx = S.accDx, dy = S.accDy;
        S.accDx = S.accDy = 0;
        oskMove(*app, dx, dy);
        oskPlace(*app);
    }
}
const wl_callback_listener kOskFrame = {oskFrameDone};

void oskPlace(App& app) {
    OskState& S = app.osk;
    if (!S.layer) return;
    zwlr_layer_surface_v1_set_margin(S.layer, 0, 0, static_cast<int>(std::lround(S.marginBottom)), static_cast<int>(std::lround(S.marginLeft)));
    if (S.dragging) {
        S.movePending = true;
        wl_callback* cb = wl_surface_frame(S.surface);
        wl_callback_add_listener(cb, &kOskFrame, &app);
    }
    wl_surface_commit(S.surface);
}

void closeOsk(App& app) {
    OskState& S = app.osk;
    if (!S.surface) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    hidpi::detach(S.scale);
    wl_surface_destroy(S.surface);
    S.surface = nullptr;
    S.layer = nullptr;
    S.configured = false;
    S.mods = 0;
    S.hover = S.pressed = -1;
    S.dragging = false;
    S.movePending = false;
    app.onOsk = false;
}

void onOskConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->osk.configured = true;
    drawOsk(*app);
}
void onOskClosed(void* d, zwlr_layer_surface_v1*) { closeOsk(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kOskListener = {onOskConfigure, onOskClosed};

// Where it first opens: centred on the screen, 40 px above the bottom edge. The reset button brings it back here.
void oskHome(App& app) {
    OskState& S = app.osk;
    const OskGeom G = oskGeom();
    const Bar* bar = S.bar ? S.bar : oskBar(app);
    const double screenW = bar ? bar->width + 2.0 * (app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0)) : 1920;
    S.marginLeft = std::max(0.0, (screenW - G.w) / 2);
    S.marginBottom = 40;
    S.placed = true;
    S.dragging = false;
    oskPlace(app);
}

void openOsk(App& app) {
    OskState& S = app.osk;
    if (S.surface || !app.compositor || !app.layerShell || !app.shm) return;
    kbdRead(app);  // (the layout in use now)
    oskSync(app);
    const OskGeom G = oskGeom();
    Bar* bar = oskBar(app);
    S.bar = bar;
    const double screenW = bar ? bar->width + 2.0 * (app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0)) : 1920;
    if (!S.placed) { S.marginLeft = std::max(0.0, (screenW - G.w) / 2); S.marginBottom = 40; S.placed = true; }
    S.marginLeft = std::clamp(S.marginLeft, 0.0, std::max(0.0, screenW - G.w));  // (this monitor may be smaller than the one it was left on)
    S.marginBottom = std::clamp(S.marginBottom, 0.0, std::max(0.0, oskScreenH(app) - G.h));
    S.mods = 0;
    S.caps = S.shiftLock = false;
    S.hover = S.pressed = -1;
    S.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, S.scale, S.surface, [&app] { drawOsk(app); });
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-osk");
    zwlr_layer_surface_v1_set_anchor(S.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(S.layer, static_cast<uint32_t>(std::ceil(G.w)), static_cast<uint32_t>(std::ceil(G.h)));
    zwlr_layer_surface_v1_set_margin(S.layer, 0, 0, static_cast<int>(S.marginBottom), static_cast<int>(S.marginLeft));
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);  // the focus stays in the window being typed into
    zwlr_layer_surface_v1_add_listener(S.layer, &kOskListener, &app);
    wl_surface_commit(S.surface);
}

void toggleOsk(App& app) { if (app.osk.surface) closeOsk(app); else openOsk(app); }

int oskKeyAt(double x, double y) {
    const OskGeom G = oskGeom();
    for (size_t i = 0; i < G.keys.size(); i++) if (G.keys[i].r.contains(x, y)) return static_cast<int>(i);
    return G.closeBtn.contains(x, y) ? -2 : G.resetBtn.contains(x, y) ? -3 : -1;
}

// The same drag when the pointer is over another surface of the shell: that one does not move, so its moves are the pointer's own. They are
// added up and applied once per frame, like the moves of the keyboard's own surface.
void oskDragOther(App& app, double x, double y) {
    OskState& S = app.osk;
    S.accDx += x - S.otherX;
    S.accDy += y - S.otherY;
    S.otherX = x;
    S.otherY = y;
    if (S.movePending || (std::fabs(S.accDx) < 0.5 && std::fabs(S.accDy) < 0.5)) return;
    const double dx = S.accDx, dy = S.accDy;
    S.accDx = S.accDy = 0;
    oskMove(app, dx, dy);
    oskPlace(app);
}

void oskMotion(App& app) {
    OskState& S = app.osk;
    if (S.dragging) {  // the pointer is relative to the surface, which moves with it: the new place is the old one plus how far the pointer is from where it took hold
        if (S.movePending) return;
        const double dx = app.pointerX - S.grabX, dy = app.pointerY - S.grabY;
        if (std::fabs(dx) < 0.5 && std::fabs(dy) < 0.5) return;
        oskMove(app, dx, dy);
        oskPlace(app);
        return;
    }
    const int over = oskKeyAt(app.pointerX, app.pointerY);
    if (over != S.hover) { S.hover = over; drawOsk(app); }
}

void oskButton(App& app, uint32_t button, bool pressed) {
    OskState& S = app.osk;
    if (button != 0x110) return;
    if (!pressed) {
        const bool wasDrag = S.dragging;
        if (wasDrag && S.onOther && (S.accDx != 0 || S.accDy != 0)) {  // (what the last frames did not apply yet)
            const double dx = S.accDx, dy = S.accDy;
            S.accDx = S.accDy = 0;
            oskMove(app, dx, dy);
            oskPlace(app);
        }
        S.onOther = false;
        S.dragging = false;
        if (S.pressed >= 0) { S.pressed = -1; drawOsk(app); }
        else if (wasDrag) drawOsk(app);
        return;
    }
    const OskGeom G = oskGeom();
    const int k = oskKeyAt(app.pointerX, app.pointerY);
    if (k == -2) { closeOsk(app); return; }
    if (k == -3) { oskHome(app); return; }
    if (k < 0) {
        if (G.header.contains(app.pointerX, app.pointerY)) { S.dragging = true; S.onOther = false; S.accDx = S.accDy = 0; S.grabX = app.pointerX; S.grabY = app.pointerY; drawOsk(app); }
        return;
    }
    const OskKey& key = G.keys[k];
    S.pressed = k;
    if (key.kind == 2) {
        const int bit = oskModBit(key.sym);
        if (bit == kOskShift && (S.mods & kOskShift) && !S.shiftLock) { S.shiftLock = true; }  // twice: locked
        else if (S.mods & bit) { S.mods &= ~bit; if (bit == kOskShift) S.shiftLock = false; }
        else S.mods |= bit;
    } else if (key.kind == 3) {
        S.caps = !S.caps;
    } else {
        oskSend(app, key);
        S.mods &= S.shiftLock ? kOskShift : 0;  // the held modifiers last for one key (Shift also, when locked)
    }
    drawOsk(app);
}
