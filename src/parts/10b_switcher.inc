
constexpr int kSwCardW = 296, kSwThumbH = 150, kSwHead = 34, kSwPad = 12, kSwGap = 12, kSwPanelPad = 18, kSwRadius = 36;

std::string swCacheDir() { return homeDir() + "/.cache/end4pC/switcher"; }

struct SwLayout {
    int cols = 1, rows = 1, visRows = 1, firstRow = 0;
    double s = 1;
    double px = 0, py = 0, pw = 0, ph = 0;
    double cardH = 0;
    bool visible(int i) const { const int r = i / cols; return r >= firstRow && r < firstRow + visRows; }
    Rect card(int i) const {
        if (!visible(i)) return {0, 0, 0, 0};
        return {px + s * (kSwPanelPad + (i % cols) * (kSwCardW + kSwGap)), py + s * (kSwPanelPad + (i / cols - firstRow) * (cardH + kSwGap)), kSwCardW * s, cardH * s};
    }
};
SwLayout swLayout(const Switcher& S) {
    SwLayout l;
    const int n = std::max<int>(1, S.wins.size());
    l.cardH = kSwPad + kSwHead + 6 + kSwThumbH + kSwPad;
    const double availW = S.w * 0.94, availH = S.h * 0.88, minScale = 0.55;
    double best = -1;
    for (int cols = 1; cols <= n; cols++) {
        const int rows = (n + cols - 1) / cols;
        const double pw = cols * kSwCardW + (cols - 1) * kSwGap + 2 * kSwPanelPad, ph = rows * l.cardH + (rows - 1) * kSwGap + 2 * kSwPanelPad;
        const double sc = std::min({1.0, availW / pw, availH / ph});
        if (sc > best + 1e-9) { best = sc; l.cols = cols; }
    }
    l.s = std::max(best, minScale);
    l.rows = (n + l.cols - 1) / l.cols;
    if (best < minScale) {
        l.cols = std::clamp(static_cast<int>((availW / l.s - 2 * kSwPanelPad + kSwGap) / (kSwCardW + kSwGap)), 1, n);
        l.rows = (n + l.cols - 1) / l.cols;
    }
    l.visRows = std::clamp(static_cast<int>((availH / l.s - 2 * kSwPanelPad + kSwGap) / (l.cardH + kSwGap)), 1, l.rows);
    const int selRow = std::clamp(S.sel, 0, n - 1) / l.cols;
    l.firstRow = std::clamp(selRow - l.visRows / 2, 0, l.rows - l.visRows);
    l.pw = l.s * (l.cols * kSwCardW + (l.cols - 1) * kSwGap + 2 * kSwPanelPad);
    l.ph = l.s * (l.visRows * l.cardH + (l.visRows - 1) * kSwGap + 2 * kSwPanelPad);
    l.px = (S.w - l.pw) / 2;
    l.py = (S.h - l.ph) / 2;
    return l;
}

constexpr double kSwScale0 = 0.97;

void drawSwitcher(App& app) {
    Switcher& S = app.sw;
    if (!S.open() || !S.configured || S.w <= 0 || S.h <= 0) return;
    const int W = S.w, H = S.h, stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-switcher", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const double rev = std::clamp(S.anim.reveal, 0.0, 1.0);
    setSource(cr, app.color("scrim"), 0.28 * rev);
    cairo_paint(cr);

    const SwLayout l = swLayout(S);
    popGroupBegin(cr);
    roundedRect(cr, l.px, l.py, l.pw, l.ph, kSwRadius);
    setSource(cr, app.color("surface_container"), 0.84);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* titleFont = pango_font_description_from_string("Sans Bold 10");
    PangoFontDescription* subFont = pango_font_description_from_string("Sans 8");
    for (int i = 0; i < static_cast<int>(S.wins.size()); i++) {
        SwitcherWin& w = S.wins[i];
        if (!l.visible(i)) continue;
        const Rect screenR = l.card(i);
        cairo_save(cr);
        cairo_translate(cr, screenR.x, screenR.y);
        cairo_scale(cr, l.s, l.s);
        const Rect r = {0, 0, double(kSwCardW), l.cardH};
        const bool on = i == S.sel, hot = i == S.hover && !on;
        roundedRect(cr, r.x, r.y, r.w, r.h, 26);
        if (on) setSource(cr, app.color("primary_container"));
        else setSource(cr, app.color("surface_container_high"), hot ? 0.95 : 0.55);
        cairo_fill(cr);
        if (on) {
            roundedRect(cr, r.x + 1.5, r.y + 1.5, r.w - 3, r.h - 3, 25);
            setSource(cr, app.color("primary"));
            cairo_set_line_width(cr, 3);
            cairo_stroke(cr);
        }
        const Color ink = app.color(on ? "on_primary_container" : "on_surface");

        const double hx = r.x + kSwPad, hy = r.y + kSwPad;
        cairo_surface_t* icon = launcherAppIcon(app, lowerCopy(w.cls));
        if (icon) {
            cairo_save(cr);
            const double k = 28.0 / cairo_image_surface_get_width(icon);
            cairo_translate(cr, hx, hy + 3);
            cairo_scale(cr, k, k);
            cairo_set_source_surface(cr, icon, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            cairo_new_sub_path(cr);
            cairo_arc(cr, hx + 14, hy + 17, 14, 0, 2 * M_PI);
            setSource(cr, app.color("secondary_container"));
            cairo_fill(cr);
            pango_layout_set_font_description(layout, titleFont);
            const std::string initial = w.cls.empty() ? "?" : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(w.cls[0]))));
            pango_layout_set_text(layout, initial.c_str(), -1);
            int iw = 0, ih = 0;
            pango_layout_get_pixel_size(layout, &iw, &ih);
            setSource(cr, app.color("on_secondary_container"));
            cairo_move_to(cr, hx + 14 - iw / 2.0, hy + 17 - ih / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
        const double tx = hx + 28 + 10, tw = r.w - 2 * kSwPad - 38;
        pango_layout_set_width(layout, static_cast<int>(tw * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_font_description(layout, titleFont);
        pango_layout_set_text(layout, w.title.empty() ? w.cls.c_str() : w.title.c_str(), -1);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        setSource(cr, ink);
        cairo_move_to(cr, tx, hy + 2);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_font_description(layout, subFont);
        pango_layout_set_text(layout, w.cls.c_str(), -1);
        setSource(cr, ink, 0.65);
        cairo_move_to(cr, tx, hy + 2 + lh);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

        const double bx = r.x + kSwPad, by = r.y + kSwPad + kSwHead + 6, bw = r.w - 2 * kSwPad, bh = kSwThumbH;
        roundedRect(cr, bx, by, bw, bh, 16);
        setSource(cr, app.color(on ? "primary" : "surface_container_highest"), on ? 0.16 : 0.9);
        cairo_fill(cr);
        if (w.thumb) {
            const double iw = cairo_image_surface_get_width(w.thumb), ih = cairo_image_surface_get_height(w.thumb);
            const double sc = std::min(bw / iw, bh / ih), dw = iw * sc, dh = ih * sc;
            cairo_save(cr);
            roundedRect(cr, bx + (bw - dw) / 2, by + (bh - dh) / 2, dw, dh, 12);
            cairo_clip(cr);
            cairo_translate(cr, bx + (bw - dw) / 2, by + (bh - dh) / 2);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, w.thumb, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_restore(cr);
        } else if (icon) {
            cairo_save(cr);
            const double k = 56.0 / cairo_image_surface_get_width(icon);
            cairo_translate(cr, bx + (bw - 56) / 2, by + (bh - 56) / 2);
            cairo_scale(cr, k, k);
            cairo_set_source_surface(cr, icon, 0, 0);
            cairo_paint_with_alpha(cr, 0.55);
            cairo_restore(cr);
        }
        cairo_restore(cr);
    }
    pango_font_description_free(titleFont);
    pango_font_description_free(subFont);
    g_object_unref(layout);
    const double cx = l.px + l.pw / 2, cy = l.py + l.ph / 2, grow = kSwScale0 + (1 - kSwScale0) * rev;
    if (rev > 0.001) popFadeEnd(cr, rev, cx, cy, kSwScale0);
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, S.surface, S.blur, W, H, [&](cairo_t* m) {  // the card only: the faint dim around it stays sharp (Hyprland's ignore_alpha does the same)
        if (rev <= 0.001) return;
        popPaintGrown(m, grow, cx, cy, 1.0, [&] {
            roundedRect(m, l.px, l.py, l.pw, l.ph, kSwRadius);
            cairo_fill(m);
        });
    });
    wl_surface_attach(S.surface, buffer, 0, 0);
    wl_surface_damage_buffer(S.surface, 0, 0, W, H);
    wl_surface_commit(S.surface);
}

void closeSwitcher(App& app) {
    Switcher& S = app.sw;
    if (!S.open()) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    releaseBlurSurface(S.blur);
    wl_surface_destroy(S.surface);
    for (auto& w : S.wins) if (w.thumb) cairo_surface_destroy(w.thumb);
    const std::string dir = S.cacheDir;
    S = Switcher{};
    app.onSw = false;
    app.cursorShape = -1;
    updateCursor(app);
    if (!dir.empty()) runDetached({"bash", "-c", ("rm -rf '" + dir + "'").c_str()});
    malloc_trim(0);
}

void retractSwitcher(App& app) {
    Switcher& S = app.sw;
    if (!S.open() || popIsOut(S.anim)) return;
    if (!S.configured) { closeSwitcher(app); return; }
    popRetract(S.anim, std::chrono::steady_clock::now());
    S.hover = -1;
    drawSwitcher(app);
}
void tickSwitcher(App& app, std::chrono::steady_clock::time_point now) {
    Switcher& S = app.sw;
    if (!S.open() || !S.configured) return;
    bool moving = false;
    if (popTick(S.anim, now, moving)) { closeSwitcher(app); return; }
    if (moving) drawSwitcher(app);
}
std::chrono::steady_clock::time_point switcherWake(const App& app) {
    if (!app.sw.open() || !app.sw.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.sw.anim.phase, app.sw.anim.settleAt, std::chrono::steady_clock::now());
}

void swCommit(App& app) {
    Switcher& S = app.sw;
    if (!S.open()) return;
    const std::string addr = S.sel >= 0 && S.sel < static_cast<int>(S.wins.size()) ? S.wins[S.sel].address : "";
    retractSwitcher(app);
    if (addr.empty()) return;
    wmFocusWindow(addr);
}

void swMove(App& app, int delta) {
    Switcher& S = app.sw;
    const int n = static_cast<int>(S.wins.size());
    if (n == 0) return;
    S.sel = ((S.sel + delta) % n + n) % n;
    drawSwitcher(app);
}

bool swPoll(App& app) {
    Switcher& S = app.sw;
    if (!S.open()) return false;
    bool changed = false, waiting = false;
    for (auto& w : S.wins) {
        if (w.thumb) continue;
        const std::string path = S.cacheDir + "/" + w.stable + ".png";
        if (access(path.c_str(), R_OK) != 0) { waiting = true; continue; }
        GdkPixbuf* pb = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
        if (!pb) { waiting = true; continue; }
        w.thumb = surfaceFromPixbuf(pb);
        g_object_unref(pb);
        changed = true;
    }
    if (changed) drawSwitcher(app);
    const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - S.openedAt).count();
    return waiting && age < 4.0;
}

void swStep(App& app, int dir, bool sticky) {
    Switcher& S = app.sw;
    if (S.open() && !popIsOut(S.anim)) { swMove(app, dir); return; }
    if (S.open()) closeSwitcher(app);
    if (!app.compositor || !app.layerShell || !app.shm) return;
    const std::string list = wmWindowsTsv();
    std::vector<SwitcherWin> wins;
    std::istringstream in(list);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
        if (f.size() < 5) continue;
        SwitcherWin w;
        w.address = f[0]; w.cls = f[1]; w.stable = f[3]; w.workspace = std::atoi(f[4].c_str());
        w.title = f.size() > 5 ? f[5] : "";
        wins.push_back(std::move(w));
    }
    if (wins.size() < 2 && !sticky) {
        if (wins.size() == 1) wmFocusWindow(wins[0].address);
        return;
    }
    if (wins.empty()) return;
    if (anyPopupOpen(app)) { closeAllPopups(app); syncCatcher(app); drawAll(app); }
    S = Switcher{};
    popStartQuick(S.anim, 0.10, 0.09);
    S.wins = std::move(wins);
    S.sel = dir >= 0 ? (S.wins.size() > 1 ? 1 : 0) : static_cast<int>(S.wins.size()) - 1;
    S.holdFlavour = !sticky;
    S.openedAt = std::chrono::steady_clock::now();
    S.cacheDir = swCacheDir();
    std::filesystem::remove_all(S.cacheDir);
    std::filesystem::create_directories(S.cacheDir);
    std::string cmd;
    if (wmKind() == Wm::Hyprland) {
        for (const auto& w : S.wins)
            cmd += "(grim -s 0.25 -T " + w.stable + " '" + S.cacheDir + "/" + w.stable + ".part.png' && mv '" + S.cacheDir + "/" + w.stable + ".part.png' '" + S.cacheDir + "/" + w.stable + ".png') 2>/dev/null & ";
        cmd += "wait";
        runDetached({"bash", "-c", cmd.c_str()});
    }

    S.surface = wl_compositor_create_surface(app.compositor);
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-switcher");
    zwlr_layer_surface_v1_set_anchor(S.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(S.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    static const zwlr_layer_surface_v1_listener kSwListener = {
        [](void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
            auto* a = static_cast<App*>(d);
            zwlr_layer_surface_v1_ack_configure(layer, serial);
            a->sw.w = static_cast<int>(w);
            a->sw.h = static_cast<int>(h);
            if (!a->sw.configured) a->sw.anim.animStart = std::chrono::steady_clock::now();
            a->sw.configured = true;
            drawSwitcher(*a);
        },
        [](void* d, zwlr_layer_surface_v1*) { closeSwitcher(*static_cast<App*>(d)); }};
    zwlr_layer_surface_v1_add_listener(S.layer, &kSwListener, &app);
    wl_surface_commit(S.surface);
}

int swCardAt(const Switcher& S, double x, double y) {
    const SwLayout l = swLayout(S);
    for (int i = 0; i < static_cast<int>(S.wins.size()); i++) {
        const Rect r = l.card(i);
        if (x >= r.x - kSwGap / 2.0 && x < r.x + r.w + kSwGap / 2.0 && y >= r.y - kSwGap / 2.0 && y < r.y + r.h + kSwGap / 2.0) return i;
    }
    return -1;
}

void switcherMotion(App& app) {
    Switcher& S = app.sw;
    if (!S.open() || popIsOut(S.anim)) return;
    const int h = swCardAt(S, app.pointerX, app.pointerY);
    if (h != S.hover) { S.hover = h; drawSwitcher(app); updateCursor(app); }
}

void switcherButton(App& app, uint32_t button, bool pressed) {
    Switcher& S = app.sw;
    if (!S.open() || !pressed || popIsOut(S.anim)) return;
    if (button == 0x111) { retractSwitcher(app); return; }
    const int i = swCardAt(S, app.pointerX, app.pointerY);
    if (i < 0) { retractSwitcher(app); return; }
    S.sel = i;
    swCommit(app);
}

void switcherKey(App& app, uint32_t key) {
    constexpr uint32_t kKeyEsc = 1, kKeyEnter = 28, kKeyKpEnter = 96, kTab = 15, kLeft = 105, kRight = 106, kUp = 103, kDown = 108;
    Switcher& S = app.sw;
    if (popIsOut(S.anim)) return;
    const int cols = swLayout(S).cols;
    const bool shift = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (key == kKeyEsc) retractSwitcher(app);
    else if (key == kKeyEnter || key == kKeyKpEnter) swCommit(app);
    else if (key == kTab) swMove(app, shift ? -1 : 1);
    else if (key == kRight) swMove(app, 1);
    else if (key == kLeft) swMove(app, -1);
    else if (key == kDown) swMove(app, S.sel + cols < static_cast<int>(S.wins.size()) ? cols : 0);
    else if (key == kUp) swMove(app, S.sel - cols >= 0 ? -cols : 0);
}

void switcherModifiers(App& app) {
    Switcher& S = app.sw;
    if (!S.open() || !S.holdFlavour || !app.xkbState || popIsOut(S.anim)) return;
    const bool held = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0 ||
                      xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (held) S.sawHeld = true;
    else swCommit(app);
}
