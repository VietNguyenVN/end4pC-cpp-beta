// Force quit: a card with the open windows. Pick one and it is asked to close, or its process is killed when it does not answer.
constexpr double kFqW = 440, kFqPad = 20, kFqRow = 56, kFqGap = 4, kFqBtnH = 40, kFqIcon = 56;
constexpr int kFqMaxRows = 6;

struct FqGeom { Rect list, cancel, close, force; double h; int rows; };
FqGeom fqGeom(size_t count) {
    FqGeom g;
    g.rows = static_cast<int>(std::min<size_t>(std::max<size_t>(count, 1), kFqMaxRows));
    const double top = kFqPad + kFqIcon + 12 + 30 + 22 + 14;
    g.list = {kFqPad, top, kFqW - 2 * kFqPad, g.rows * kFqRow + (g.rows - 1) * kFqGap};
    const double by = g.list.y + g.list.h + 16;
    g.h = by + kFqBtnH + kFqPad;
    g.force = {kFqW - kFqPad - 124, by, 124, kFqBtnH};
    g.close = {g.force.x - 8 - 84, by, 84, kFqBtnH};
    g.cancel = {g.close.x - 8 - 84, by, 84, kFqBtnH};
    return g;
}

void fqLoad(FqState& S) {
    S.wins.clear();
    const std::string tsv = captureArgv({"bash", "-c",
        "hyprctl clients -j | jq -r '[.[]|select(.mapped and (.hidden|not))]|sort_by(.focusHistoryID)|.[]|[.address,.class,.title,(.pid|tostring),(.workspace.id|tostring)]|@tsv'"});
    std::istringstream in(tsv);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
        if (f.size() < 5 || f[0].empty()) continue;
        FqWin w;
        w.address = f[0];
        w.cls = f[1];
        w.title = f[2];
        w.pid = std::atoi(f[3].c_str());
        w.ws = std::atoi(f[4].c_str());
        if (w.pid <= 1 || w.pid == getpid()) continue;  // (never the shell itself)
        S.wins.push_back(w);
    }
}

void fqBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kFqBuffer = {fqBufferRelease};

void drawForceQuit(App& app) {
    FqState& S = app.fq;
    if (!S.surface || !S.configured) return;
    const FqGeom G = fqGeom(S.wins.size());
    const int W = static_cast<int>(kFqW), H = static_cast<int>(std::ceil(G.h)), stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-forcequit", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    roundedRect(cr, 0, 0, kFqW, G.h, 28);
    setSource(cr, app.color("surface_container_high"), 0.99);
    cairo_fill(cr);
    cairo_new_path(cr);
    cairo_arc(cr, kFqW / 2, kFqPad + kFqIcon / 2, kFqIcon / 2, 0, 2 * M_PI);
    setSource(cr, app.color("error_container"));
    cairo_fill(cr);
    drawIcon(cr, "close-circle", kFqW / 2 - 15, kFqPad + kFqIcon / 2 - 15, 30, app.color("on_error_container"));
    const double ty = kFqPad + kFqIcon + 12;
    clkText(cr, L, i18n::tr("Force quit"), 22, false, kFqPad, ty + 15, app.color("on_surface"), 1, kFqW - 2 * kFqPad, 1);
    clkText(cr, L, S.wins.empty() ? i18n::tr("No open windows") : i18n::tr("Pick the app that does not answer"), 14, false, kFqPad, ty + 30 + 11, app.color("on_surface_variant"), 1, kFqW - 2 * kFqPad, 1);
    cairo_save(cr);
    roundedRect(cr, G.list.x, G.list.y, G.list.w, G.list.h, 18);
    cairo_clip(cr);
    for (int r = 0; r < G.rows; r++) {
        const int i = S.scroll + r;
        if (i >= static_cast<int>(S.wins.size())) break;
        const FqWin& w = S.wins[i];
        const Rect row{G.list.x, G.list.y + r * (kFqRow + kFqGap), G.list.w, kFqRow};
        const bool sel = i == S.sel, hot = S.hover == i;
        roundedRect(cr, row.x, row.y, row.w, row.h, 18);
        setSource(cr, app.color(sel ? "secondary_container" : hot ? "surface_container_highest" : "surface_container"));
        cairo_fill(cr);
        if (cairo_surface_t* icon = launcherAppIcon(app, lowerCopy(w.cls))) drawWsAppIcon(cr, icon, row.x + 14 + 16, row.y + row.h / 2, 32);
        const double tx = row.x + 14 + 32 + 12, tw = row.w - (tx - row.x) - 14;
        std::string title = w.title.empty() ? w.cls : w.title;
        while (title.size() > 3 && clkTextWidth(L, title, 15, false) > tw) title.resize(title.size() - 1);
        const Color ink = app.color(sel ? "on_secondary_container" : "on_surface");
        clkText(cr, L, title, 15, true, tx, row.y + 20, ink, 1);
        clkText(cr, L, w.cls + "  \xC2\xB7  " + std::to_string(w.ws), 12, false, tx, row.y + 40, app.color(sel ? "on_secondary_container" : "on_surface_variant"), 0.8);
    }
    cairo_restore(cr);
    const bool have = S.sel >= 0 && S.sel < static_cast<int>(S.wins.size());
    const auto button = [&](const Rect& r, const char* label, int id, bool filled, bool danger) {
        const bool hot = S.hover == id && (id == 100 || have);
        const double alpha = id == 100 || have ? 1.0 : 0.38;
        if (filled || hot) {
            roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            if (filled) setSource(cr, app.color("error"), alpha * (hot ? 0.88 : 1.0));
            else setSource(cr, app.color("primary"), 0.12);
            cairo_fill(cr);
        }
        const Color ink = app.color(filled ? "on_error" : "primary");
        const int tw = clkTextWidth(L, label, 14, true);
        clkText(cr, L, label, 14, true, r.x + (r.w - tw) / 2.0, r.y + r.h / 2.0, danger ? ink : ink, alpha);
    };
    button(G.cancel, i18n::tr("Cancel"), 100, false, false);
    button(G.close, i18n::tr("Close"), 101, false, false);
    button(G.force, i18n::tr("Force quit"), 102, true, true);
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kFqBuffer, nullptr);
    wl_surface_attach(S.surface, buffer, 0, 0);
    wl_surface_damage_buffer(S.surface, 0, 0, W, H);
    wl_surface_commit(S.surface);
}

void closeForceQuit(App& app) {
    FqState& S = app.fq;
    if (!S.surface) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    wl_surface_destroy(S.surface);
    S.surface = nullptr;
    S.layer = nullptr;
    S.configured = false;
    S.hover = -1;
    S.wins.clear();
    app.onFq = false;
}

void onFqConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->fq.configured = true;
    drawForceQuit(*app);
}
void onFqClosed(void* d, zwlr_layer_surface_v1*) { closeForceQuit(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kFqListener = {onFqConfigure, onFqClosed};

void openForceQuit(App& app) {
    FqState& S = app.fq;
    if (S.surface) { closeForceQuit(app); return; }
    if (wmKind() != Wm::Hyprland || !app.compositor || !app.layerShell || !app.shm) return;
    fqLoad(S);
    S.sel = -1;
    S.scroll = 0;
    S.hover = -1;
    const FqGeom G = fqGeom(S.wins.size());
    const Bar* bar = mainBar(app);
    S.surface = wl_compositor_create_surface(app.compositor);
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-forcequit");
    zwlr_layer_surface_v1_set_anchor(S.layer, 0);
    zwlr_layer_surface_v1_set_size(S.layer, static_cast<uint32_t>(kFqW), static_cast<uint32_t>(std::ceil(G.h)));
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(S.layer, &kFqListener, &app);
    wl_surface_commit(S.surface);
}

int fqHit(const App& app, double x, double y) {
    const FqState& S = app.fq;
    const FqGeom G = fqGeom(S.wins.size());
    if (G.cancel.contains(x, y)) return 100;
    if (G.close.contains(x, y)) return 101;
    if (G.force.contains(x, y)) return 102;
    if (!G.list.contains(x, y)) return -1;
    const int r = static_cast<int>((y - G.list.y) / (kFqRow + kFqGap)), i = S.scroll + r;
    return i >= 0 && i < static_cast<int>(S.wins.size()) ? i : -1;
}

void fqMotion(App& app) {
    const int over = fqHit(app, app.pointerX, app.pointerY);
    if (over != app.fq.hover) { app.fq.hover = over; drawForceQuit(app); }
}

void fqSelect(App& app, int index) {
    FqState& S = app.fq;
    if (S.wins.empty()) return;
    S.sel = std::clamp(index, 0, static_cast<int>(S.wins.size()) - 1);
    if (S.sel < S.scroll) S.scroll = S.sel;
    if (S.sel >= S.scroll + kFqMaxRows) S.scroll = S.sel - kFqMaxRows + 1;
    drawForceQuit(app);
}

// Asks the window to close, or ends its process (every window of it goes from the list).
void fqAct(App& app, bool force) {
    FqState& S = app.fq;
    if (S.sel < 0 || S.sel >= static_cast<int>(S.wins.size())) return;
    const FqWin w = S.wins[S.sel];
    if (force) {
        kill(w.pid, SIGKILL);
        const std::string label = w.cls.empty() ? w.title : w.cls;
        runDetached({"notify-send", "-a", "end4pC", i18n::tr("Force quit"), label.c_str()});
        S.wins.erase(std::remove_if(S.wins.begin(), S.wins.end(), [&](const FqWin& o) { return o.pid == w.pid; }), S.wins.end());
    } else {
        wmCloseWindow(w.address);
        S.wins.erase(S.wins.begin() + S.sel);
    }
    S.sel = -1;
    S.scroll = std::clamp(S.scroll, 0, std::max(0, static_cast<int>(S.wins.size()) - kFqMaxRows));
    if (S.wins.empty()) { closeForceQuit(app); return; }
    drawForceQuit(app);
}

void fqButton(App& app, uint32_t button, bool pressed) {
    if (button != 0x110 || !pressed) return;
    const int hit = fqHit(app, app.pointerX, app.pointerY);
    if (hit < 0) return;
    if (hit == 100) closeForceQuit(app);
    else if (hit == 101) fqAct(app, false);
    else if (hit == 102) fqAct(app, true);
    else fqSelect(app, hit);
}

void fqScroll(App& app, int dir) {
    FqState& S = app.fq;
    const int most = std::max(0, static_cast<int>(S.wins.size()) - kFqMaxRows);
    S.scroll = std::clamp(S.scroll + dir, 0, most);
    drawForceQuit(app);
}
