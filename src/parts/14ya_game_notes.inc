// The notes card of the game overlay: a text box to write on the screen, kept in a file. The editor is the one of the Notes page of Tools
// (a Pango layout that is the truth of where every character is). A line that starts with "- " gets a button that copies it. Pinned, the
// card stays on the screen after the overlay closes, translucent and letting the clicks through. Included by 14y_game_overlay.inc.
using Clock = std::chrono::steady_clock;  // (what the editor of Tools expects)
PangoLayout* g_measure = nullptr;
void setFont(PangoLayout* l, double px, bool bold) {
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
}
#include "../tools/notes_editor.inc"

constexpr double kGnPadX = 16, kGnPadY = 12, kGnGutter = 30, kGnPx = 15, kGnMinW = 300, kGnMinH = 200, kGnGhostAlpha = 0.72, kGnEdge = 8;
constexpr int kGnSaveMs = 500;

struct GameNotes {
    notes_ui::TextEditor ed;
    bool loaded = false, focus = false, selDrag = false, dirty = false, ghostDraw = false, placed = false;
    textsel::Clicks clicks;
    double scroll = 0, textW = 300;
    Elastic band;  // the rubber band at the ends of the text, and the fading scroll mark
    Rect panel{};
    std::chrono::steady_clock::time_point dirtyAt{};
    std::vector<std::string> copies;
};
GameNotes g_gn;

void gnInitMeasure() {  // the layouts of the editors take their context from this one
    if (g_measure) return;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* c = cairo_create(s);
    g_measure = pango_cairo_create_layout(c);
    cairo_destroy(c);
    cairo_surface_destroy(s);
}

void drawGameCard(App& app, cairo_t* cr, PangoLayout* L, int w, std::vector<GameHit>& hits);
void drawGame(App& app);
void gameNotesGhostSync(App& app);

std::string gnFile(const char* name) {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/.local/state/end4pC/" + name;
}
void gnSaveText() {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(gnFile("x")).parent_path(), ec);
    std::ofstream(gnFile("game-notes.txt"), std::ios::trunc) << g_gn.ed.s;
}
void gnSaveState(const App& app) {
    const GameOverlay& G = app.game;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(gnFile("x")).parent_path(), ec);
    std::ofstream(gnFile("game-notes.state"), std::ios::trunc) << static_cast<int>(G.wx[GwNotes]) << " " << static_cast<int>(G.wy[GwNotes]) << " " << static_cast<int>(G.nw) << " " << static_cast<int>(G.nh) << " "
                                                               << (G.notesPinned ? 1 : 0) << " " << (G.open[GwNotes] ? 1 : 0) << "\n";
}
void gnLoad(App& app) {
    if (g_gn.loaded) return;
    g_gn.loaded = true;
    gnInitMeasure();
    g_gn.ed.px = kGnPx;
    std::ifstream in(gnFile("game-notes.txt"));
    std::stringstream ss;
    ss << in.rdbuf();
    g_gn.ed.set(ss.str());
    GameOverlay& G = app.game;
    std::ifstream st(gnFile("game-notes.state"));
    int x = 0, y = 0, w = 0, h = 0, pin = 0, open = 0;
    if (st >> x >> y >> w >> h >> pin >> open) {
        G.wx[GwNotes] = std::clamp(x, 0, 8000);
        G.wy[GwNotes] = std::clamp(y, 0, 8000);
        G.nw = std::clamp(w, static_cast<int>(kGnMinW), 2000);
        G.nh = std::clamp(h, static_cast<int>(kGnMinH), 2000);
        G.notesPinned = pin != 0;
        G.open[GwNotes] = open != 0;
        g_gn.placed = true;
    }
}

// The card goes where it was, or to the right of the screen the first time.
void gnPlace(App& app) {
    GameOverlay& G = app.game;
    gnLoad(app);
    if (!g_gn.placed) { G.wx[GwNotes] = std::max(0.0, G.W - G.nw - 60); G.wy[GwNotes] = 150; g_gn.placed = true; }
    G.wx[GwNotes] = std::clamp(G.wx[GwNotes], 0.0, std::max(0.0, G.W - G.nw));
    G.wy[GwNotes] = std::clamp(G.wy[GwNotes], 0.0, std::max(0.0, G.H - G.nh));
}

Rect gnPanel(const Rect& r) {
    const double top = r.y + kGameCardPad + kGameBadge + 12;
    return {r.x + kGameCardPad, top, r.w - 2 * kGameCardPad, r.y + r.h - kGameCardPad - top};
}
double gnTextW(const Rect& p) { return std::max(60.0, p.w - 2 * kGnPadX - kGnGutter); }

void gnClampScroll() {
    const double content = g_gn.ed.contentHeight() + 2 * kGnPadY;
    g_gn.scroll = std::clamp(g_gn.scroll, 0.0, std::max(0.0, content - g_gn.panel.h));
}
void gnEnsureCaret() {
    notes_ui::TextEditor& ed = g_gn.ed;
    ed.layout(g_gn.textW);
    double x = 0, y = 0, h = 0;
    ed.caret(ed.cur, x, y, h);
    if (y < g_gn.scroll) g_gn.scroll = y;
    else if (y + h + 2 * kGnPadY > g_gn.scroll + g_gn.panel.h) g_gn.scroll = y + h + 2 * kGnPadY - g_gn.panel.h;
    gnClampScroll();
}
void gnMarkDirty() {
    g_gn.dirty = true;
    g_gn.dirtyAt = std::chrono::steady_clock::now();
}

// The lines that start with "- ": what each copy button puts on the clipboard.
struct GnBullet { std::string text; int start = 0, end = 0; };
std::vector<GnBullet> gnBullets(const std::string& s) {
    std::vector<GnBullet> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(pos, nl - pos);
        const size_t dash = line.find_first_not_of(" \t");
        if (dash != std::string::npos && line[dash] == '-' && dash + 1 < line.size() && (line[dash + 1] == ' ' || line[dash + 1] == '\t')) {
            const size_t from = line.find_first_not_of(" \t", dash + 1), to = line.find_last_not_of(" \t\r");
            if (from != std::string::npos && to != std::string::npos && to >= from) out.push_back({line.substr(from, to - from + 1), static_cast<int>(pos), static_cast<int>(nl)});
        }
        if (nl == s.size()) break;
        pos = nl + 1;
    }
    return out;
}

void drawGameNotes(App& app, cairo_t* cr, PangoLayout* L, const Rect& r, std::vector<GameHit>& hits) {
    GameOverlay& G = app.game;
    gnLoad(app);
    notes_ui::TextEditor& ed = g_gn.ed;
    const Rect p = gnPanel(r);
    g_gn.panel = p;
    g_gn.textW = gnTextW(p);
    ed.layout(g_gn.textW);
    gnClampScroll();
    roundedRect(cr, p.x, p.y, p.w, p.h, 20);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);
    const bool live = !g_gn.ghostDraw;
    if (live) hits.push_back({p, GhNoteText, 0});
    const Color ink = app.color("on_surface"), dim = app.color("on_surface_variant");

    cairo_save(cr);
    roundedRect(cr, p.x, p.y, p.w, p.h, 20);
    cairo_clip(cr);
    cairo_translate(cr, p.x + kGnPadX, p.y + kGnPadY - (g_gn.scroll + g_gn.band.over));
    if (ed.s.empty()) {
        PangoLayout* ph = pango_cairo_create_layout(cr);
        setFont(ph, kGnPx, false);
        pango_layout_set_width(ph, static_cast<int>(g_gn.textW * PANGO_SCALE));
        pango_layout_set_wrap(ph, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(ph, i18n::tr("Write something here...\nStart a line with - to get a button that copies it."), -1);
        setSource(cr, dim, 0.7);
        cairo_move_to(cr, 0, 0);
        pango_cairo_show_layout(cr, ph);
        g_object_unref(ph);
    }
    for (const Rect& s : ed.selectionRects()) {
        setSource(cr, app.color("primary"), 0.3);
        cairo_rectangle(cr, s.x, s.y, s.w, s.h);
        cairo_fill(cr);
    }
    PangoLayout* lay = ed.layout(g_gn.textW);
    pango_cairo_update_layout(cr, lay);
    setSource(cr, ink);
    cairo_move_to(cr, 0, 0);
    pango_cairo_show_layout(cr, lay);
    const bool focused = live && g_gn.focus && !G.closing;
    if (focused) {
        double cx = 0, cy = 0, ch = 0;
        ed.caret(ed.cur, cx, cy, ch);
        roundedRect(cr, cx, cy, 2, ch, 1);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
    cairo_restore(cr);

    g_gn.copies.clear();
    if (live) {
        constexpr double kBtn = 24;
        for (const GnBullet& b : gnBullets(ed.s)) {
            if (focused && ed.hi() > b.start && ed.lo() <= b.end) continue;  // (not while the caret or the selection is on that line)
            if (focused && ed.cur >= b.start && ed.cur <= b.end) continue;
            double x = 0, y = 0, h = 0;
            ed.caret(b.start, x, y, h);
            const double by = p.y + kGnPadY - (g_gn.scroll + g_gn.band.over) + y + (h - kBtn) / 2;
            if (by < p.y + 2 || by + kBtn > p.y + p.h - 2) continue;
            const Rect btn{p.x + p.w - 12 - kBtn, by, kBtn, kBtn};
            const int idx = static_cast<int>(g_gn.copies.size());
            g_gn.copies.push_back(b.text);
            hits.push_back({btn, GhNoteCopy, idx});
            cairo_arc(cr, btn.x + kBtn / 2, btn.y + kBtn / 2, kBtn / 2, 0, 2 * M_PI);
            setSource(cr, app.color("surface_container_highest"), G.hover == GhNoteCopy * 10 + idx ? 1 : 0.75);
            cairo_fill(cr);
            drawIcon(cr, "copy", btn.x + (kBtn - 14) / 2, btn.y + (kBtn - 14) / 2, 14, ink);
        }
    }
    const double content = ed.contentHeight() + 2 * kGnPadY;
    if (content > p.h && g_gn.band.thumbAlpha() > 0) {  // a thin scroll mark, shown while the wheel moves
        const double th = std::max(24.0, (p.h - 16) * p.h / content), ty = p.y + 8 + (p.h - 16 - th) * g_gn.scroll / std::max(1.0, content - p.h);
        roundedRect(cr, p.x + p.w - 6, ty, 3, th, 1.5);
        setSource(cr, dim, 0.4 * g_gn.band.thumbAlpha());
        cairo_fill(cr);
    }
    if (!live) return;
    // the edges that resize the card: the right one, the bottom one and the corner (a grip is drawn there)
    const double gx = r.x + r.w - 20, gy = r.y + r.h - 20;
    setSource(cr, dim, G.hover == GhResize * 10 + 3 ? 0.9 : 0.4);
    cairo_set_line_width(cr, 2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int k = 0; k < 2; k++) {
        cairo_move_to(cr, gx + 12, gy + 4 + k * 5 + 0.0);
        cairo_line_to(cr, gx + 4 + k * 5 + 0.0, gy + 12);
        cairo_stroke(cr);
    }
    hits.push_back({{r.x + r.w - kGnEdge, r.y + 70, kGnEdge + 6, r.h - 70 - 14}, GhResize, 1});
    hits.push_back({{r.x + 24, r.y + r.h - kGnEdge, r.w - 48, kGnEdge + 6}, GhResize, 2});
    hits.push_back({{r.x + r.w - 26, r.y + r.h - 26, 32, 32}, GhResize, 3});
    (void)L;
}

// ---- input ------------------------------------------------------------------------------------------------------------------------
bool gameNotesFocused(const App& app) { return app.game.surface && !app.game.closing && app.game.open[GwNotes] && g_gn.focus; }

size_t gnIndexAtPointer(const App& app) {
    return static_cast<size_t>(g_gn.ed.indexAt(app.pointerX - (g_gn.panel.x + kGnPadX), app.pointerY - (g_gn.panel.y + kGnPadY) + g_gn.scroll));
}

void gnClick(App& app, bool shift) {
    notes_ui::TextEditor& ed = g_gn.ed;
    g_gn.focus = true;
    const int idx = static_cast<int>(gnIndexAtPointer(app));
    const int n = g_gn.clicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), app.pointerX, app.pointerY);
    if (n >= 3) {  // the whole line
        const size_t ls = idx > 0 ? ed.s.rfind('\n', static_cast<size_t>(idx - 1)) : std::string::npos;
        const int a = ls == std::string::npos ? 0 : static_cast<int>(ls) + 1;
        const size_t e = ed.s.find('\n', static_cast<size_t>(idx));
        ed.anc = a;
        ed.cur = e == std::string::npos ? static_cast<int>(ed.s.size()) : static_cast<int>(e);
    } else if (n == 2) ed.selectWordAt(idx);
    else ed.moveTo(idx, shift);
    g_gn.selDrag = n == 1;
}

void gnDragSelect(App& app) {
    const Rect& p = g_gn.panel;
    if (app.pointerY < p.y) g_gn.scroll -= 14;
    else if (app.pointerY > p.y + p.h) g_gn.scroll += 14;
    gnClampScroll();
    g_gn.ed.moveTo(static_cast<int>(gnIndexAtPointer(app)), true);
}

void gameNotesScroll(App& app, int dir) {
    const double content = g_gn.ed.contentHeight() + 2 * kGnPadY;
    elasticWheel(g_gn.band, g_gn.scroll, std::max(0.0, content - g_gn.panel.h), dir * 48.0);
    drawGame(app);
}

std::string gnClipboard() {
    std::string clip;
    if (!clipboard::paste(clip)) return "";
    if (clip.size() > 20000) {
        clip.resize(20000);
        while (!clip.empty() && textsel::isContinuation(clip.back())) clip.pop_back();
    }
    return clip;
}

// The keys of a multi-line editor: arrows, Home/End, Backspace/Delete, Enter, Tab, undo, the clipboard and typing. False: not a key it uses.
bool editorKey(App& app, notes_ui::TextEditor& ed, uint32_t key, double lines) {
    const xkb_keycode_t code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code), low = xkb_keysym_to_lower(sym);
    const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    const bool shift = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    const int n = static_cast<int>(ed.s.size());
    if (ctrl) {
        if (low == XKB_KEY_a) { ed.anc = 0; ed.cur = n; }
        else if (low == XKB_KEY_c) { if (ed.hasSel()) clipboard::copy(ed.selected()); }
        else if (low == XKB_KEY_x) { if (ed.hasSel()) { clipboard::copy(ed.selected()); ed.insert(""); } }
        else if (low == XKB_KEY_v) { const std::string clip = gnClipboard(); if (!clip.empty()) ed.insert(clip); }
        else if (low == XKB_KEY_z && !shift) ed.restoreFrom(ed.undo, ed.redo);
        else if (low == XKB_KEY_y || low == XKB_KEY_z) ed.restoreFrom(ed.redo, ed.undo);
        else if (sym == XKB_KEY_BackSpace) ed.backspace(true);
        else if (sym == XKB_KEY_Delete) ed.del(true);
        else if (sym == XKB_KEY_Home) ed.moveTo(0, shift);
        else if (sym == XKB_KEY_End) ed.moveTo(n, shift);
        else if (sym == XKB_KEY_Left) ed.moveTo(ed.wordLeft(ed.cur), shift);
        else if (sym == XKB_KEY_Right) ed.moveTo(ed.wordRight(ed.cur), shift);
        else return false;
        return true;
    }
    if (sym == XKB_KEY_BackSpace) ed.backspace(false);
    else if (sym == XKB_KEY_Delete) ed.del(false);
    else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) ed.insert("\n");
    else if (sym == XKB_KEY_Tab) ed.insert("  ");
    else if (sym == XKB_KEY_Left) { if (ed.hasSel() && !shift) ed.moveTo(ed.lo(), false); else ed.moveTo(ed.prev(ed.cur), shift); }
    else if (sym == XKB_KEY_Right) { if (ed.hasSel() && !shift) ed.moveTo(ed.hi(), false); else ed.moveTo(ed.next(ed.cur), shift); }
    else if (sym == XKB_KEY_Up) ed.vertical(-1, shift);
    else if (sym == XKB_KEY_Down) ed.vertical(1, shift);
    else if (sym == XKB_KEY_Page_Up) ed.vertical(-lines, shift);
    else if (sym == XKB_KEY_Page_Down) ed.vertical(lines, shift);
    else if (sym == XKB_KEY_Home) ed.moveTo(ed.lineEdge(false), shift);
    else if (sym == XKB_KEY_End) ed.moveTo(ed.lineEdge(true), shift);
    else {
        char buf[16];
        const int len = xkb_state_key_get_utf8(app.xkbState, code, buf, sizeof buf);
        if (len > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f) ed.insert(std::string(buf, static_cast<size_t>(len)));
        else return false;
    }
    return true;
}

bool gameNotesKey(App& app, uint32_t key) {
    if (!gameNotesFocused(app) || !app.xkbState) return false;
    notes_ui::TextEditor& ed = g_gn.ed;
    if (xkb_state_key_get_one_sym(app.xkbState, key + 8) == XKB_KEY_Escape) return false;  // (it closes the overlay)
    ed.layout(g_gn.textW);
    const std::string before = ed.s;
    if (!editorKey(app, ed, key, std::max(1.0, std::floor(g_gn.panel.h / (kGnPx * 1.5))))) return false;
    if (ed.s != before) gnMarkDirty();
    gnEnsureCaret();
    drawGame(app);
    return true;
}

void gameNotesTick(App& app, std::chrono::steady_clock::time_point now, bool& redraw) {
    if (g_gn.band.step()) redraw = true;
    if (!g_gn.dirty || now - g_gn.dirtyAt < std::chrono::milliseconds(kGnSaveMs)) return;
    g_gn.dirty = false;
    gnSaveText();
    redraw = true;
    (void)app;
}
std::chrono::steady_clock::time_point gameNotesWake() {
    const auto save = g_gn.dirty ? g_gn.dirtyAt + std::chrono::milliseconds(kGnSaveMs) : std::chrono::steady_clock::time_point::max();
    return g_gn.band.busy() ? std::min(save, std::chrono::steady_clock::now() + std::chrono::milliseconds(16)) : save;
}

// ---- pinned: the card stays after the overlay closes, translucent, and the clicks go through it ------------------------------------------
void onNotesGhostConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t);
void closeNotesGhost(App& app) {
    GameOverlay& G = app.game;
    if (G.ghostLayer) zwlr_layer_surface_v1_destroy(G.ghostLayer);
    hidpi::detach(G.ghostScale);
    if (G.ghostSurface) wl_surface_destroy(G.ghostSurface);
    G.ghostLayer = nullptr;
    G.ghostSurface = nullptr;
    G.ghostConfigured = false;
}
void onNotesGhostClosed(void* d, zwlr_layer_surface_v1*) { closeNotesGhost(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kNotesGhostListener = {onNotesGhostConfigure, onNotesGhostClosed};

void drawNotesGhost(App& app) {
    GameOverlay& G = app.game;
    if (!G.ghostSurface || !G.ghostConfigured) return;
    const int R = kLaunShadow + 4, W = static_cast<int>(G.nw) + 2 * R, H = static_cast<int>(G.nh) + 2 * R;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, G.ghostScale.factor(), "end4pC-notes-ghost")) return;
    cairo_surface_t* surf = hf.surface;
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    std::vector<GameHit> ignored;
    g_gn.ghostDraw = true;
    const int hover = G.hover;
    G.hover = 0;
    cairo_push_group(cr);
    cairo_translate(cr, R - G.wx[GwNotes], R - G.wy[GwNotes]);
    drawGameCard(app, cr, L, GwNotes, ignored);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, kGnGhostAlpha);
    G.hover = hover;
    g_gn.ghostDraw = false;
    g_object_unref(L);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kGameBuffer, nullptr);
    hidpi::present(G.ghostSurface, buffer, hf, G.ghostScale);
}

void onNotesGhostConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->game.ghostConfigured = true;
    drawNotesGhost(*app);
}

void openNotesGhost(App& app) {
    GameOverlay& G = app.game;
    if (G.ghostSurface || !app.compositor || !app.layerShell || !app.shm) return;
    gnLoad(app);
    const Bar* bar = mainBar(app);
    const int R = kLaunShadow + 4;
    G.ghostSurface = wl_compositor_create_surface(app.compositor);
    attachScale(app, G.ghostScale, G.ghostSurface, [&app] { drawNotesGhost(app); });
    G.ghostLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, G.ghostSurface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-game-notes");
    zwlr_layer_surface_v1_set_anchor(G.ghostLayer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(G.ghostLayer, static_cast<int>(G.nw) + 2 * R, static_cast<int>(G.nh) + 2 * R);
    zwlr_layer_surface_v1_set_margin(G.ghostLayer, static_cast<int>(G.wy[GwNotes]) - R, 0, 0, static_cast<int>(G.wx[GwNotes]) - R);
    zwlr_layer_surface_v1_set_exclusive_zone(G.ghostLayer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(G.ghostLayer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(G.ghostSurface, none);
    wl_region_destroy(none);
    zwlr_layer_surface_v1_add_listener(G.ghostLayer, &kNotesGhostListener, &app);
    wl_surface_commit(G.ghostSurface);
}

// The ghost is there while the card is pinned and open and the overlay is not.
void gameNotesGhostSync(App& app) {
    GameOverlay& G = app.game;
    gnLoad(app);
    if (G.notesPinned && G.open[GwNotes] && !G.surface) { if (!G.ghostSurface) openNotesGhost(app); }
    else closeNotesGhost(app);
}
