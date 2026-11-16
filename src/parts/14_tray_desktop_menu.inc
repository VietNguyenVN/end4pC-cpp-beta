

constexpr double kTmRoom = 20, kTmPad = 8, kTmRow = 40, kTmSep = 13, kTmRadius = 24, kTmIndent = 18;

std::string tmCleanLabel(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '_') { if (i + 1 < in.size() && in[i + 1] == '_') { out += '_'; i++; } continue; }
        out += in[i];
    }
    return out;
}

bool tmReadNode(sd_bus_message* m, std::vector<TrayMenu::Node>& nodes, int parent, int depth) {
    if (sd_bus_message_enter_container(m, 'r', "ia{sv}av") < 0) return false;
    TrayMenu::Node n;
    n.parent = parent;
    n.depth = depth;
    sd_bus_message_read(m, "i", &n.id);
    if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(m, "s", &key);
            char type = 0;
            const char* contents = nullptr;
            sd_bus_message_peek_type(m, &type, &contents);
            sd_bus_message_enter_container(m, 'v', contents);
            const std::string k = key ? key : "", c = contents ? contents : "";
            auto str = [&](std::string& out) { const char* v = nullptr; if (sd_bus_message_read(m, "s", &v) >= 0 && v) out = v; };
            auto boolean = [&](bool& out) { int b = 0; if (sd_bus_message_read(m, "b", &b) >= 0) out = b != 0; };
            if (k == "label" && c == "s") str(n.label);
            else if (k == "type" && c == "s") str(n.type);
            else if (k == "enabled" && c == "b") boolean(n.enabled);
            else if (k == "visible" && c == "b") boolean(n.visible);
            else if (k == "toggle-type" && c == "s") { std::string v; str(v); n.toggle = v == "checkmark" ? 1 : v == "radio" ? 2 : 0; }
            else if (k == "toggle-state" && c == "i") { int v = 0; sd_bus_message_read(m, "i", &v); n.checked = v == 1; }
            else if (k == "shortcut" && c == "aas") {
                if (sd_bus_message_enter_container(m, 'a', "as") >= 0) {
                    bool first = true;
                    while (sd_bus_message_enter_container(m, 'a', "s") > 0) {
                        std::string combo;
                        const char* part = nullptr;
                        while (sd_bus_message_read(m, "s", &part) > 0 && part) {
                            std::string pp = part == std::string("Control") ? "Ctrl" : part;
                            combo += (combo.empty() ? "" : "+") + pp;
                        }
                        if (first) n.shortcut = combo;
                        first = false;
                        sd_bus_message_exit_container(m);
                    }
                    sd_bus_message_exit_container(m);
                }
            } else sd_bus_message_skip(m, contents);
            sd_bus_message_exit_container(m);
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    const int idx = static_cast<int>(nodes.size());
    nodes.push_back(n);
    if (sd_bus_message_enter_container(m, 'a', "v") >= 0) {
        while (sd_bus_message_enter_container(m, 'v', "(ia{sv}av)") > 0) {
            tmReadNode(m, nodes, idx, depth + 1);
            nodes[idx].hasChildren = true;
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

void tmLayout(TrayMenu& T) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    T.rows.clear();
    T.rects.clear();
    std::vector<bool> shown(T.nodes.size(), false);
    double widest = 0, y = kTmPad;
    for (size_t i = 1; i < T.nodes.size(); i++) {
        const auto& n = T.nodes[i];
        const bool parentOk = n.parent == 0 || (n.parent > 0 && shown[n.parent] && T.nodes[n.parent].expanded);
        if (!n.visible || !parentOk) continue;
        const bool sep = n.type == "separator";
        if (!sep && n.label.empty()) continue;
        shown[i] = true;
        T.rows.push_back(static_cast<int>(i));
        const double h = sep ? kTmSep : kTmRow;
        T.rects.push_back({kTmPad, y, 0, h});
        y += h;
        if (!sep) {
            int tw = 0, th = 0, sw = 0;
            pango_layout_set_text(layout, tmCleanLabel(n.label).c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            if (!n.shortcut.empty()) { pango_layout_set_text(layout, n.shortcut.c_str(), -1); pango_layout_get_pixel_size(layout, &sw, &th); }
            widest = std::max(widest, 12.0 + 28 + (n.depth - 1) * kTmIndent + tw + (sw ? 28 + sw : 0) + (n.hasChildren ? 28 : 0) + 12);
        }
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    T.cardW = static_cast<int>(std::clamp(widest + 2 * kTmPad, 220.0, 440.0));
    T.cardH = static_cast<int>(y + kTmPad);
    for (auto& r : T.rects) r.w = T.cardW - 2 * kTmPad;
}

struct PopupFrame {
    int fd = -1, size = 0, stride = 0, W = 0, H = 0;
    void* data = nullptr;
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;
};

bool popupCanvasBegin(PopupFrame& f, const char* name, int cardW, int cardH) {
    const int room = static_cast<int>(kTmRoom);
    f.W = cardW + 2 * room;
    f.H = cardH + 2 * room;
    f.stride = f.W * 4;
    f.size = f.stride * f.H;
    f.fd = memfd_create(name, 0);
    if (f.fd < 0 || ftruncate(f.fd, f.size) < 0) return false;
    f.data = mmap(nullptr, f.size, PROT_READ | PROT_WRITE, MAP_SHARED, f.fd, 0);
    f.surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(f.data), CAIRO_FORMAT_ARGB32, f.W, f.H, f.stride);
    f.cr = mkcr(f.surface);
    cairo_set_operator(f.cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(f.cr, 0, 0, 0, 0);
    cairo_paint(f.cr);
    cairo_set_operator(f.cr, CAIRO_OPERATOR_OVER);
    cairo_translate(f.cr, room, room);
    return true;
}

void popupCardPaint(App& app, cairo_t* cr, int cardW, int cardH, double radius) {
    cairo_surface_t* shadow = makeShadowMask(cardW, cardH, radius);
    setSource(cr, app.color("shadow"), 0.3);
    cairo_mask_surface(cr, shadow, -kLaunShadow, -kLaunShadow + 2);
    cairo_surface_destroy(shadow);
    cairo_save(cr);
    roundedRect(cr, 0, 0, cardW, cardH, radius);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_restore(cr);
    roundedRect(cr, 0, 0, cardW, cardH, radius);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);
}

bool popupFrameBegin(App& app, PopupFrame& f, const char* name, int cardW, int cardH, double radius) {
    if (!popupCanvasBegin(f, name, cardW, cardH)) return false;
    popupCardPaint(app, f.cr, cardW, cardH, radius);
    return true;
}

void popupFrameCommit(App& app, PopupFrame& f, wl_surface* surface, BlurSurface& blur, int cardW, int cardH, double radius) {
    cairo_destroy(f.cr);
    cairo_surface_destroy(f.surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, f.fd, f.size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, f.W, f.H, f.stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(f.data, f.size);
    close(f.fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, surface, blur, f.W, f.H, [&](cairo_t* m) {
        cairo_translate(m, kTmRoom, kTmRoom);
        roundedRect(m, 0, 0, cardW, cardH, radius);
        cairo_fill(m);
    });
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, f.W, f.H);
    wl_surface_commit(surface);
}

void drawTrayMenu(App& app) {
    TrayMenu& T = app.tmenu;
    if (!T.open() || !T.configured) return;
    PopupFrame frame;
    if (!popupFrameBegin(app, frame, "end4pC-tray-menu", T.cardW, T.cardH, kTmRadius)) return;
    cairo_t* cr = frame.cr;

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd2 = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(layout, fd2);
    pango_font_description_free(fd2);
    const Color onSurf = app.color("on_surface"), variant = app.color("on_surface_variant"), primary = app.color("primary");
    for (size_t k = 0; k < T.rows.size(); k++) {
        const auto& n = T.nodes[T.rows[k]];
        const Rect r = T.rects[k];
        if (n.type == "separator") {
            setSource(cr, app.color("outline_variant"), 0.6);
            cairo_rectangle(cr, r.x + 12, r.y + r.h / 2 - 0.5, r.w - 24, 1);
            cairo_fill(cr);
            continue;
        }
        const bool hot = T.hover == static_cast<int>(k) && n.enabled;
        if (hot) {
            roundedRect(cr, r.x, r.y + 2, r.w, r.h - 4, 14);
            setSource(cr, app.color("secondary_container"), 0.85);
            cairo_fill(cr);
        }
        const double alpha = n.enabled ? 1.0 : 0.38;
        const Color ink = hot ? app.color("on_secondary_container") : onSurf;
        const double x0 = r.x + 12 + (n.depth - 1) * kTmIndent;
        if (n.toggle == 1 && n.checked) drawIcon(cr, "check-circle", x0, r.y + (r.h - 20) / 2.0, 20, hot ? ink : primary, true);
        else if (n.toggle == 2 && n.checked) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, x0 + 10, r.y + r.h / 2, 5, 0, 2 * M_PI);
            setSource(cr, hot ? ink : primary);
            cairo_fill(cr);
        }
        int tw = 0, th = 0;
        pango_layout_set_text(layout, tmCleanLabel(n.label).c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, ink, alpha);
        cairo_move_to(cr, x0 + 28, r.y + (r.h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        if (n.hasChildren) {
            drawIcon(cr, n.expanded ? "alt-arrow-down" : "alt-arrow-right", r.x + r.w - 12 - 18, r.y + (r.h - 18) / 2.0, 18, hot ? ink : variant);
        } else if (!n.shortcut.empty()) {
            pango_layout_set_text(layout, n.shortcut.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            setSource(cr, variant, alpha);
            cairo_move_to(cr, r.x + r.w - 12 - tw, r.y + (r.h - th) / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
    }
    g_object_unref(layout);
    popupFrameCommit(app, frame, T.surface, T.blur, T.cardW, T.cardH, kTmRadius);
}

void closeTrayMenu(App& app) {
    TrayMenu& T = app.tmenu;
    if (!T.open()) return;
    if (T.layer) zwlr_layer_surface_v1_destroy(T.layer);
    releaseBlurSurface(T.blur);
    wl_surface_destroy(T.surface);
    T = TrayMenu{};
    app.onTmenu = false;
    updateCursor(app);
    malloc_trim(0);
}

void onTmenuConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->tmenu.configured = true;
    drawTrayMenu(*app);
}
void onTmenuClosed(void* d, zwlr_layer_surface_v1*) { closeTrayMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kTmenuListener = {onTmenuConfigure, onTmenuClosed};

struct PopOrigin {
    double x = 0, y = 0;
};

PopOrigin placePopupBeside(App& app, zwlr_layer_surface_v1* layer, double ax, double ay, int cardW, int cardH, const Bar* bar) {
    const bool vert = sideBar(app), atBottom = app.cfg.barPosition == "bottom";
    const int sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    const double screenLen = bar ? bar->width + 2.0 * sideMargin : 1920;
    const int room = static_cast<int>(kTmRoom);
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    PopOrigin origin;
    if (vert) {
        const double gy = ay + sideMargin;
        const int topM = static_cast<int>(std::clamp(gy - cardH / 2.0, double(app.cfg.frame + 12), screenLen - app.cfg.frame - 12 - cardH)) - room;
        const int side = std::max(0, sideCardEdge(app) - room);
        const bool right = app.cfg.barPosition == "right";
        zwlr_layer_surface_v1_set_anchor(layer, kT | (right ? kR : kL));
        zwlr_layer_surface_v1_set_margin(layer, topM, right ? side : 0, 0, right ? 0 : side);
        const double wOff = sideMargin + barThickness(app) / 2.0;
        origin.x = right ? cardW + (sideCardEdge(app) - wOff) : wOff - sideCardEdge(app);
        origin.y = gy - (topM + room);
    } else {
        const double gx = ax + sideMargin;
        const int leftM = static_cast<int>(std::clamp(gx - cardW / 2.0, double(app.cfg.frame + 12), screenLen - app.cfg.frame - 12 - cardW)) - room;
        int edge = app.cfg.frame + kBarHeight + 12;
        if (app.cfg.barStyle == "float" || app.cfg.barStyle == "island") edge += kBarGap;
        const int margin = edge - room;
        zwlr_layer_surface_v1_set_anchor(layer, (atBottom ? kB : kT) | kL);
        zwlr_layer_surface_v1_set_margin(layer, atBottom ? 0 : margin, 0, atBottom ? margin : 0, leftM);
        const double wOff = sideMargin + barThickness(app) / 2.0;
        origin.x = gx - (leftM + room);
        origin.y = atBottom ? cardH + (edge - wOff) : wOff - edge;
    }
    zwlr_layer_surface_v1_set_size(layer, cardW + 2 * room, cardH + 2 * room);
    return origin;
}

void popIconCentre(const App& app, double ax, double ay, double& ix, double& iy) {
    const double sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    const double widgetMid = sideMargin + barThickness(app) / 2.0;
    ix = sideBar(app) ? widgetMid : ax + sideMargin;
    iy = sideBar(app) ? ay + sideMargin : widgetMid;
}

void openTrayMenu(App& app, int idx) {
    if (idx < 0 || idx >= static_cast<int>(app.tray.size()) || !app.bus || !app.layerShell) return;
    const TrayIcon t = app.tray[idx];
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* rep = nullptr;
        sd_bus_call_method(app.bus, t.bus.c_str(), t.menu.c_str(), "com.canonical.dbusmenu", "AboutToShow", &err, &rep, "i", 0);
        sd_bus_error_free(&err);
        sd_bus_message_unref(rep);
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* rep = nullptr;
    if (sd_bus_call_method(app.bus, t.bus.c_str(), t.menu.c_str(), "com.canonical.dbusmenu", "GetLayout", &err, &rep, "iias", 0, -1, 0) < 0) {
        std::fprintf(stderr, "end4pC: tray menu of %s: GetLayout failed: %s\n", t.bus.c_str(), err.message ? err.message : "?");
        sd_bus_error_free(&err);
        return;
    }
    std::vector<TrayMenu::Node> nodes;
    uint32_t revision = 0;
    sd_bus_message_read(rep, "u", &revision);
    const bool ok = tmReadNode(rep, nodes, -1, 0);
    sd_bus_message_unref(rep);
    if (ok && nodes.size() >= 2) {
        TrayMenu::Node sep, pin;
        sep.id = -2; sep.parent = 0; sep.depth = 1; sep.type = "separator";
        pin.id = -1; pin.parent = 0; pin.depth = 1;
        pin.label = trayPinned(app, t) ? "Move behind the arrow" : "Pin to the bar";
        nodes.push_back(sep);
        nodes.push_back(pin);
    }
    if (!ok || nodes.size() < 2) { std::fprintf(stderr, "end4pC: tray menu of %s: unreadable layout (%zu nodes)\n", t.bus.c_str(), nodes.size()); return; }

    closeControlCenter(app); closePowerMenu(app); closeSelector(app); closeLauncher(app); closeNotifPanel(app); closeMediaPanel(app); closeClockPanel(app); closeAudioMenu(app); closeBtMenu(app);
    closeTrayMenu(app);
    TrayMenu& T = app.tmenu;
    T = TrayMenu{};
    T.bus = t.bus;
    T.path = t.menu;
    T.trayId = t.id;
    T.nodes = std::move(nodes);
    tmLayout(T);
    if (T.rows.empty()) { T = TrayMenu{}; return; }

    T.surface = wl_compositor_create_surface(app.compositor);
    T.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, T.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-tray-menu");
    placePopupBeside(app, T.layer, app.pointerX, app.pointerY, T.cardW, T.cardH, mainBar(app));
    zwlr_layer_surface_v1_set_exclusive_zone(T.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(T.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(T.layer, &kTmenuListener, &app);
    wl_surface_commit(T.surface);
}

void trayMenuMotion(App& app) {
    TrayMenu& T = app.tmenu;
    const double x = app.pointerX - kTmRoom, y = app.pointerY - kTmRoom;
    int hover = -1;
    for (size_t k = 0; k < T.rows.size(); k++)
        if (T.rects[k].contains(x, y) && T.nodes[T.rows[k]].type != "separator") hover = static_cast<int>(k);
    if (hover == T.hover) return;
    T.hover = hover;
    drawTrayMenu(app);
    updateCursor(app);
}

void trayMenuClick(App& app) {
    TrayMenu& T = app.tmenu;
    const double x = app.pointerX - kTmRoom, y = app.pointerY - kTmRoom;
    if (!Rect{0, 0, double(T.cardW), double(T.cardH)}.contains(x, y)) { closeTrayMenu(app); return; }
    if (T.hover < 0 || T.hover >= static_cast<int>(T.rows.size())) return;
    const int ni = T.rows[T.hover];
    if (!T.nodes[ni].enabled) return;
    if (T.nodes[ni].hasChildren) {
        T.nodes[ni].expanded = !T.nodes[ni].expanded;
        tmLayout(T);
        T.hover = -1;
        zwlr_layer_surface_v1_set_size(T.layer, T.cardW + 2 * static_cast<int>(kTmRoom), T.cardH + 2 * static_cast<int>(kTmRoom));
        wl_surface_commit(T.surface);
        return;
    }
    if (T.nodes[ni].id == -1) {
        const std::string id = T.trayId;
        closeTrayMenu(app);
        trayTogglePin(app, id);
        return;
    }
    sd_bus_message* m = nullptr;
    if (app.bus && sd_bus_message_new_method_call(app.bus, &m, T.bus.c_str(), T.path.c_str(), "com.canonical.dbusmenu", "Event") >= 0) {
        sd_bus_message_append(m, "is", T.nodes[ni].id, "clicked");
        sd_bus_message_open_container(m, 'v', "i");
        sd_bus_message_append(m, "i", 0);
        sd_bus_message_close_container(m);
        sd_bus_message_append(m, "u", static_cast<uint32_t>(std::time(nullptr)));
        sd_bus_message_set_expect_reply(m, 0);
        sd_bus_send(app.bus, m, nullptr);
        sd_bus_message_unref(m);
        sd_bus_flush(app.bus);
    }
    closeTrayMenu(app);
}

constexpr double kDmRoom = 20, kDmCardW = 348, kDmSubW = 324, kDmGap = 8, kDmCarH = 160, kDmRowH = 56, kDmSubH = 430;
constexpr double kDmCardH = 8 + kDmCarH + 4 + 3 * kDmRowH + 2 * 2 + 8;
constexpr double kDmModeH = 56, kDmTogH = 48, kDmSepH = 16, kDmWSubH = 12 + 2 * (kDmTogH + 2) + kDmSepH + 9 * (kDmTogH + 2) + 10;
constexpr double kDmItemRadius = 28, kDmItemGap = 8, kDmCarPad = 8, kDmCarRadius = 36, kDmSmallMin = 40, kDmSmallMax = 56;
constexpr int kDmCount = 6;

cairo_surface_t* dmThumb(DesktopMenu& D, const std::string& path) {
    if (auto it = D.thumbs.find(path); it != D.thumbs.end()) return it->second;
    GdkPixbuf* pb = nullptr;
    const std::string cached = thumbCachePath(path);
    if (access(cached.c_str(), R_OK) == 0) pb = gdk_pixbuf_new_from_file(cached.c_str(), nullptr);
    if (!pb) pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 640, 360, TRUE, nullptr);
    cairo_surface_t* surf = pb ? surfaceFromPixbuf(pb) : nullptr;
    if (pb) g_object_unref(pb);
    D.thumbs[path] = surf;
    return surf;
}

double dmTargetWidth(double inner, int index, int focus) {
    const double small = std::clamp(inner * 0.15, kDmSmallMin, kDmSmallMax), medium = inner * 0.30;
    if (index == focus) return inner - medium - small - 2 * kDmItemGap;
    if (focus > 0) return index == focus - 1 ? medium : small;
    return index == 1 ? medium : small;
}

std::map<std::string, std::array<Color, 3>> dmSchemeColors() {
    std::map<std::string, std::array<Color, 3>> out;
    const char* x = std::getenv("XDG_CACHE_HOME");
    std::ifstream f((x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC/schemes.txt");
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::vector<std::string> p;
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, '|')) p.push_back(t);
        if (p.size() < 4) continue;
        std::array<Color, 3> c{};
        for (int k = 0; k < 3; k++) {
            unsigned v = 0;
            if (std::sscanf(p[1 + k].c_str(), "#%x", &v) == 1) c[k] = Color{((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
        }
        out[p[0]] = c;
    }
    return out;
}

struct DmSpring { double x = 0, v = 0, target = 0; };
std::map<int, DmSpring> g_dmSprings;
bool g_dmSpringsMoving = false;
std::chrono::steady_clock::time_point g_dmSpringLast{};
void dmStepSprings() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = g_dmSpringLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(now - g_dmSpringLast).count());
    g_dmSpringLast = now;
    constexpr double kStiffness = 900, kDamping = 46, kSubStep = 1.0 / 240;
    g_dmSpringsMoving = false;
    for (auto& [id, a] : g_dmSprings) {
        if (std::fabs(a.target - a.x) < 0.0008 && std::fabs(a.v) < 0.01) { a.x = a.target; a.v = 0; continue; }
        for (double left = dt; left > 1e-9; left -= kSubStep) {
            const double h = std::min(kSubStep, left);
            a.v += (kStiffness * (a.target - a.x) - kDamping * a.v) * h;
            a.x += a.v * h;
        }
        g_dmSpringsMoving = true;
    }
}
double dmSpring(int id, double target) {
    auto it = g_dmSprings.find(id);
    if (it == g_dmSprings.end()) { g_dmSprings[id] = {target, 0, target}; return target; }
    if (it->second.target != target) { it->second.target = target; g_dmSpringsMoving = true; }
    return it->second.x;
}

void retractDesktopMenu(App& app);

void drawDesktopMenu(App& app) {
    DesktopMenu& D = app.dmenu;
    if (!D.open() || !D.configured) return;
    dmStepSprings();
    const int W = D.surfW, H = D.surfH, stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-desktop-menu", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const bool grown = D.anim.phase != kPopOpen;
    const double rev = std::max(0.0, D.anim.reveal);
    if (grown) popGroupBegin(cr);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](const char* name) {
        PangoFontDescription* f = pango_font_description_from_string(name);
        pango_layout_set_font_description(layout, f);
        pango_font_description_free(f);
    };
    const double alpha = app.cfg.barOpacity / 100.0;
    const Color onSurf = app.color("on_surface"), variant = app.color("on_surface_variant"), primary = app.color("primary"), onPrimary = app.color("on_primary");

    const double bx = D.card.x + 8, by = D.card.y + 8, bw = kDmCardW - 16, bh = kDmCarH;
    roundedRect(cr, bx, by, bw, bh, kDmCarRadius);
    setSource(cr, app.color("surface"), alpha);
    cairo_fill(cr);
    {
        const double inner = bw - 2 * kDmCarPad, ih = bh - 2 * kDmCarPad, ix = bx + kDmCarPad, iy = by + kDmCarPad;
        const int n = static_cast<int>(D.files.size()), focus = D.current;
        std::vector<double> pos(n + 1, 0);
        for (int i = 0; i < n; i++) pos[i + 1] = pos[i] + D.widths[i] + kDmItemGap;
        const int lo = std::clamp(static_cast<int>(std::floor(D.curAnim)), 0, std::max(0, n - 1)), hi = std::min(lo + 1, n - 1);
        const double fr = D.curAnim - lo, origin = pos[lo] * (1 - fr) + pos[hi] * fr;
        (void)focus;
        cairo_save(cr);
        roundedRect(cr, ix, iy, inner, ih, kDmItemRadius);
        cairo_clip(cr);
        for (int i = 0; i < n; i++) {
            const double x = ix + pos[i] - origin, w = D.widths[i];
            if (x + w < ix - 2 || x > ix + inner + 2) continue;
            if (!D.carMoving && (x < ix - 1 || x + w > ix + inner + 1)) continue;
            cairo_save(cr);
            roundedRect(cr, x, iy, w, ih, std::min(kDmItemRadius, w / 2));
            ClipGroup cutItem;
            beginClipGroup(cr, cutItem);
            setSource(cr, app.color("surface_container_high"));
            cairo_paint(cr);
            if (cairo_surface_t* th = dmThumb(D, D.files[i])) {
                const double tw = cairo_image_surface_get_width(th), tH = cairo_image_surface_get_height(th);
                const double sc = std::max(w / tw, ih / tH);
                cairo_save(cr);
                cairo_translate(cr, x + (w - tw * sc) / 2, iy + (ih - tH * sc) / 2);
                cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, th, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            endClipGroup(cr, cutItem);
            cairo_restore(cr);
            if (i == 0 && w > 40) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x + w / 2, iy + ih / 2, 16, 0, 2 * M_PI);
                setSource(cr, primary);
                cairo_fill(cr);
                drawIcon(cr, "check-circle", x + w / 2 - 11, iy + ih / 2 - 11, 22, onPrimary, true);
            }
        }
        cairo_restore(cr);
    }

    const char* labels[3] = {"Wallpaper & style", "Widgets", "Settings"};
    const char* icons[3] = {"palette", "widget", "settings"};
    for (int i = 0; i < 3; i++) {
        const double ry = D.card.y + 8 + kDmCarH + 4 + i * (kDmRowH + 2), rx = D.card.x + 8, rw = kDmCardW - 16;
        const bool hot = D.hoverRow == i || (i < 2 && D.subOpen && D.subKind == i);
        roundedRectCorners(cr, rx, ry, rw, kDmRowH, i == 0 ? 18 : 6, i == 0 ? 18 : 6, i == 2 ? 18 : 6, i == 2 ? 18 : 6);
        setSource(cr, app.color(hot ? "surface_container_highest" : "surface"), hot ? std::max(alpha, 0.85) : alpha);
        cairo_fill(cr);
        drawIcon(cr, icons[i], rx + 20, ry + (kDmRowH - 24) / 2, 24, onSurf);
        font("Sans 12");
        pango_layout_set_text(layout, labels[i], -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, onSurf);
        cairo_move_to(cr, rx + 56, ry + (kDmRowH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        drawIcon(cr, "alt-arrow-right", rx + rw - 20 - 18, ry + (kDmRowH - 18) / 2, 18, variant);
    }

    D.subHits.clear();
    Bubble* tipFor = nullptr;
    auto toggleRow = [&](double rx, double ry, double rw, int id, const char* icon, const char* name, bool on) {
        D.subHits.push_back({id, Rect{rx, ry, rw, kDmTogH}});
        if (D.hoverSub == id) {
            roundedRect(cr, rx, ry, rw, kDmTogH, 18);
            setSource(cr, onSurf, 0.08);
            cairo_fill(cr);
        }
        drawIcon(cr, icon, rx + 18, ry + (kDmTogH - 22) / 2, 22, onSurf);
        font("Sans 11");
        pango_layout_set_text(layout, name, -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, onSurf);
        cairo_move_to(cr, rx + 52, ry + (kDmTogH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        const double sw = 52, sh = 32, sx = rx + rw - 18 - sw, sy = ry + (kDmTogH - sh) / 2;
        const double p = dmSpring(id, on ? 1.0 : 0.0), pc = std::clamp(p, 0.0, 1.0);
        const Color edge = app.color("outline"), off = app.color("surface_container_highest");
        const Color trackFill{primary.r * pc + off.r * (1 - pc), primary.g * pc + off.g * (1 - pc), primary.b * pc + off.b * (1 - pc)};
        roundedRect(cr, sx, sy, sw, sh, sh / 2);
        setSource(cr, trackFill);
        cairo_fill(cr);
        if (pc < 1) {
            roundedRect(cr, sx + 1, sy + 1, sw - 2, sh - 2, sh / 2 - 1);
            setSource(cr, edge, 1 - pc);
            cairo_set_line_width(cr, 2);
            cairo_stroke(cr);
        }
        const double kx = sx + 16 + 20 * p, ky = sy + sh / 2, kr = (16 + 8 * p) / 2;
        if (D.hoverSub == id) {
            cairo_new_path(cr);
            cairo_arc(cr, kx, ky, 20, 0, 2 * M_PI);
            setSource(cr, on ? primary : onSurf, 0.08);
            cairo_fill(cr);
        }
        cairo_new_path(cr);
        cairo_arc(cr, kx, ky, std::max(1.0, kr), 0, 2 * M_PI);
        setSource(cr, Color{onPrimary.r * pc + edge.r * (1 - pc), onPrimary.g * pc + edge.g * (1 - pc), onPrimary.b * pc + edge.b * (1 - pc)});
        cairo_fill(cr);
    };
    if (D.subOpen && D.subKind == 1) {
        const Rect S = D.sub;
        roundedRect(cr, S.x, S.y, S.w, S.h, 26);
        setSource(cr, app.color("surface"), alpha);
        cairo_fill(cr);
        const bool states[11] = {app.cfg.widgetsLocked != 0, app.cfg.widgetShadow != 0, app.todo.open(), app.weather.open(), app.mediaW.open(), app.resW.open(), app.calW.open(), app.deskClock.open(), app.userW.open(), app.imageW.open(), app.dv.surface != nullptr};
        const char* names[11] = {"Lock widgets", "Widget shadow", "To-Do", "Weather", "Player", "Resources", "Calendar", "Clock", "User card", "Image", "Visualizer"};
        const char* tIcons[11] = {"lock", "layers", "checklist", "cloud-sun", "music_note", "cpu", "calendar-date", "clock-circle", "user-circle", "gallery", "soundwave"};
        const double rx = S.x + 8, rw = S.w - 16;
        for (int i = 0; i < 11; i++) {
            const double ry = S.y + 12 + i * (kDmTogH + 2) + (i >= 2 ? kDmSepH : 0);
            toggleRow(rx, ry, rw, 400 + i, tIcons[i], names[i], states[i]);
        }
        setSource(cr, app.color("outline_variant"), 0.8);
        cairo_rectangle(cr, rx + 18, S.y + 12 + 2 * (kDmTogH + 2) + kDmSepH / 2 - 2, rw - 36, 1.5);
        cairo_fill(cr);
    }
    if (D.subOpen && D.subKind == 0) {
        {
            const double wIn0 = D.sub.w - 24;
            font("Sans 10");
            int rows = 1;
            double x = 0;
            for (const char* t : {"None", "Fade", "Wipe", "Circle", "Slide", "Blinds", "Diamond", "Split", "Random"}) {
                pango_layout_set_text(layout, t, -1);
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(layout, &tw, &th);
                const double cw = tw + 24;
                if (x > 0 && x + cw > wIn0) { x = 0; rows++; }
                x += cw + 6;
            }
            font("Sans Bold 9");
            pango_layout_set_text(layout, "Color mode", -1);
            int lw = 0, lh = 0;
            pango_layout_get_pixel_size(layout, &lw, &lh);
            D.sub.h = std::min(kDmWSubH, 14 + 3 * (lh + 8) + kDmModeH + 6 + 6 + 2 * 36 + 6 + rows * 36 + 6 + 2 * (kDmTogH + 2) + 12);
        }
        const Rect S = D.sub;
        roundedRect(cr, S.x, S.y, S.w, S.h, 26);
        setSource(cr, app.color("surface"), alpha);
        cairo_fill(cr);
        double y = S.y + 14;
        const double x0 = S.x + 12, wIn = S.w - 24;
        auto label = [&](const char* t) {
            font("Sans Bold 9");
            pango_layout_set_text(layout, t, -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            setSource(cr, variant);
            cairo_move_to(cr, x0 + 4, y);
            pango_cairo_show_layout(cr, layout);
            y += th + 8;
        };
        auto chips = [&](const std::vector<std::pair<int, std::string>>& items, int selectedId) {
            double x = x0;
            font("Sans 10");
            for (const auto& [id, text] : items) {
                pango_layout_set_text(layout, text.c_str(), -1);
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(layout, &tw, &th);
                const double cw = tw + 24;
                if (x > x0 && x + cw > x0 + wIn) { x = x0; y += 36; }
                const Rect r{x, y, cw, 30};
                D.subHits.push_back({id, r});
                const bool sel = id == selectedId, hot = D.hoverSub == id;
                roundedRect(cr, r.x, r.y, r.w, r.h, 15);
                setSource(cr, sel ? primary : app.color("surface_container_highest"), sel ? 1.0 : hot ? 1.0 : 0.8);
                cairo_fill(cr);
                if (hot && !sel) { roundedRect(cr, r.x, r.y, r.w, r.h, 15); setSource(cr, onSurf, 0.08); cairo_fill(cr); }
                setSource(cr, sel ? onPrimary : onSurf);
                cairo_move_to(cr, r.x + 12, r.y + (r.h - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                x += cw + 6;
            }
            y += 36;
        };
        label("Color mode");
        {
            const double cw = (wIn - 8) / 2;
            for (int k = 0; k < 2; k++) {
                const bool dark = k == 0, sel = (app.cfg.colorMode == "dark") == dark, hot = D.hoverSub == 100 + k;
                const Rect r{x0 + k * (cw + 8), y, cw, kDmModeH};
                D.subHits.push_back({100 + k, r});
                const Color bg = dark ? Color{0.105, 0.105, 0.115} : Color{0.975, 0.97, 0.985}, fg = dark ? Color{0.9, 0.9, 0.93} : Color{0.12, 0.12, 0.15};
                roundedRect(cr, r.x, r.y, r.w, r.h, 14);
                setSource(cr, bg);
                cairo_fill(cr);
                roundedRect(cr, r.x + 10, r.y + 10, 34, 6, 3);
                setSource(cr, primary);
                cairo_fill(cr);
                for (int line = 0; line < 2; line++) {
                    roundedRect(cr, r.x + 10, r.y + 22 + line * 8, line == 0 ? r.w * 0.45 : r.w * 0.3, 4, 2);
                    setSource(cr, fg, 0.28);
                    cairo_fill(cr);
                }
                cairo_new_sub_path(cr);
                cairo_arc(cr, r.x + r.w - 18, r.y + 20, 8, 0, 2 * M_PI);
                setSource(cr, primary, 0.9);
                cairo_fill(cr);
                font("Sans Bold 9");
                pango_layout_set_text(layout, dark ? "Dark" : "Light", -1);
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, fg);
                cairo_move_to(cr, r.x + 10, r.y + r.h - th - 7);
                pango_cairo_show_layout(cr, layout);
                if (sel || hot) {
                    roundedRect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 13);
                    setSource(cr, sel ? primary : onSurf, sel ? 1.0 : 0.35);
                    cairo_set_line_width(cr, 2);
                    cairo_stroke(cr);
                }
            }
            y += kDmModeH + 6;
        }
        y += 6;
        label("Color scheme");
        {
            static std::map<std::string, std::array<Color, 3>> colors;
            static time_t stamp = -1;
            struct stat st {};
            const char* xc = std::getenv("XDG_CACHE_HOME");
            const std::string file = (xc && *xc ? std::string(xc) : homeDir() + "/.cache") + "/end4pC/schemes.txt";
            const time_t m = stat(file.c_str(), &st) == 0 ? st.st_mtime : 0;
            if (m != stamp) { stamp = m; colors = dmSchemeColors(); }
            const double tileW = (wIn - 4 * 6) / 5.0;
            for (size_t i = 0; i < kSchemes.size(); i++) {
                const Rect r{x0 + (i % 5) * (tileW + 6), y + (i / 5) * 36, tileW, 30};
                const int id = 200 + static_cast<int>(i);
                D.subHits.push_back({id, r});
                const bool sel = app.cfg.colorScheme == kSchemes[i], hot = D.hoverSub == id;
                roundedRect(cr, r.x, r.y, r.w, r.h, 15);
                setSource(cr, sel ? app.color("secondary_container") : app.color("surface_container_highest"), sel || hot ? 1.0 : 0.8);
                cairo_fill(cr);
                if (sel) { roundedRect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 14); setSource(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
                const auto it = colors.find(kSchemes[i]);
                for (int k = 0; k < 3; k++) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, r.x + r.w / 2 + (k - 1) * 10, r.y + r.h / 2, 6.5, 0, 2 * M_PI);
                    if (it != colors.end()) setSource(cr, it->second[k]); else setSource(cr, onSurf, 0.15);
                    cairo_fill_preserve(cr);
                    setSource(cr, sel ? app.color("secondary_container") : app.color("surface_container_highest"));
                    cairo_set_line_width(cr, 1.5);
                    cairo_stroke(cr);
                }
                if (hot) bubbleShow(D.bubble, kSchemes[i], schemeLabel(kSchemes[i]), r.x, r.y, r.w, r.h, S.x + S.w, S.y + S.h);
            }
            y += 2 * 36 + 6;
            if (D.hoverSub < 200 || D.hoverSub >= 200 + static_cast<int>(kSchemes.size())) bubbleHide(D.bubble);
            else tipFor = &D.bubble;
        }
        label("Wallpaper transition");
        chips({{300, "None"}, {301, "Fade"}, {302, "Wipe"}, {303, "Circle"}, {304, "Slide"}, {305, "Blinds"}, {306, "Diamond"}, {307, "Split"}, {308, "Random"}},
              300 + (app.cfg.wallpaperTransition == "random" ? 8 : [&] {
                  for (int i = 0; i < 8; i++) if (app.cfg.wallpaperTransition == kWallpaperTransitions[i]) return i;
                  return 0;
              }()));
        y += 6;
        toggleRow(S.x + 8, y, S.w - 16, 500, "gallery", "Centered wallpaper", app.cfg.centeredWallpaper != 0);
        toggleRow(S.x + 8, y + kDmTogH + 2, S.w - 16, 501, "layers", "Multiple wallpapers", app.cfg.collage != 0);
    }
    D.animating = g_dmSpringsMoving;
    if (tipFor) {
        const Color bg = app.color("inverse_surface"), fg = app.color("inverse_on_surface");
        D.animating = bubbleDraw(cr, D.bubble, {bg.r, bg.g, bg.b}, {fg.r, fg.g, fg.b}) || g_dmSpringsMoving;
    }
    g_object_unref(layout);
    if (grown) {
        if (rev > 0.001) popGroupEnd(cr, rev, D.ox, D.oy, D.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 6));
        else cairo_pop_group_to_source(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, D.surface, D.blur, W, H, [&](cairo_t* m) {
        if (grown && rev <= 0.001) return;
        const auto pieces = [&] {
            roundedRect(m, D.card.x + 8, D.card.y + 8, kDmCardW - 16, kDmCarH, kDmCarRadius);
            cairo_fill(m);
            for (int i = 0; i < 3; i++) {
                roundedRectCorners(m, D.card.x + 8, D.card.y + 8 + kDmCarH + 4 + i * (kDmRowH + 2), kDmCardW - 16, kDmRowH, i == 0 ? 18 : 6, i == 0 ? 18 : 6, i == 2 ? 18 : 6, i == 2 ? 18 : 6);
                cairo_fill(m);
            }
            if (D.subOpen) {
                roundedRect(m, D.sub.x, D.sub.y, D.sub.w, D.sub.h, 26);
                cairo_fill(m);
            }
        };
        if (grown) popPaintGrown(m, rev, D.ox, D.oy, 1.0, pieces);
        else pieces();
    });
    wl_surface_attach(D.surface, buffer, 0, 0);
    wl_surface_damage_buffer(D.surface, 0, 0, W, H);
    wl_region* region = wl_compositor_create_region(app.compositor);
    wl_region_add(region, static_cast<int>(D.card.x), static_cast<int>(D.card.y), static_cast<int>(kDmCardW), static_cast<int>(kDmCardH));
    if (D.subOpen) wl_region_add(region, static_cast<int>(D.sub.x), static_cast<int>(D.sub.y), static_cast<int>(D.sub.w), static_cast<int>(D.sub.h));
    wl_surface_set_input_region(D.surface, region);
    wl_region_destroy(region);
    wl_surface_commit(D.surface);
}

double dmScrollTarget(int n, int focus, double inner) {
    if (n <= 0) return 0;
    std::vector<double> pos(n + 1, 0);
    for (int i = 0; i < n; i++) pos[i + 1] = pos[i] + dmTargetWidth(inner, i, focus) + kDmItemGap;
    int first = std::max(0, focus - 1);
    while (first < focus && pos[focus + 1] - kDmItemGap - pos[first] > inner + 0.5) first++;
    return first;
}

bool dmStep(App& app) {
    DesktopMenu& D = app.dmenu;
    const auto now = std::chrono::steady_clock::now();
    const double dt = D.last == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.05, std::chrono::duration<double>(now - D.last).count());
    D.last = now;
    const double inner = kDmCardW - 16 - 2 * kDmCarPad, k = 1 - std::exp(-dt * 16);
    const int focus = D.current;
    bool moving = false;
    for (size_t i = 0; i < D.widths.size(); i++) {
        const double target = dmTargetWidth(inner, static_cast<int>(i), focus), d = target - D.widths[i];
        if (std::abs(d) > 0.3) { D.widths[i] += d * k; moving = true; } else D.widths[i] = target;
    }
    const double scroll = dmScrollTarget(static_cast<int>(D.widths.size()), focus, inner), dc = scroll - D.curAnim;
    if (std::abs(dc) > 0.01) { D.curAnim += dc * k; moving = true; } else D.curAnim = scroll;
    D.carMoving = moving;
    return moving;
}

void closeDesktopMenu(App& app) {
    DesktopMenu& D = app.dmenu;
    if (!D.open()) return;
    if (D.layer) zwlr_layer_surface_v1_destroy(D.layer);
    releaseBlurSurface(D.blur);
    wl_surface_destroy(D.surface);
    for (auto& [k, surf] : D.thumbs) if (surf) cairo_surface_destroy(surf);
    D = DesktopMenu{};
    app.onDmenu = false;
    updateCursor(app);
    malloc_trim(0);
}

void retractDesktopMenu(App& app) {
    DesktopMenu& D = app.dmenu;
    if (!D.open() || popIsOut(D.anim)) return;
    if (!D.configured) { closeDesktopMenu(app); return; }
    popRetract(D.anim, std::chrono::steady_clock::now());
    D.hovered = D.hoverRow = D.hoverSub = -1;
    bubbleHide(D.bubble);
    drawDesktopMenu(app);
}

void dmenuMotion(App& app);
void tickDesktopMenu(App& app, std::chrono::steady_clock::time_point now) {
    DesktopMenu& D = app.dmenu;
    if (!D.open() || !D.configured) return;
    const int before = D.anim.phase;
    bool moving = false;
    if (popTick(D.anim, now, moving)) { closeDesktopMenu(app); return; }
    if (moving || before != D.anim.phase) drawDesktopMenu(app);
    if (before == kPopIn && D.anim.phase == kPopOpen) dmenuMotion(app);
}
std::chrono::steady_clock::time_point desktopMenuWake(const App& app) {
    if (!app.dmenu.open() || !app.dmenu.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.dmenu.anim.phase, app.dmenu.anim.settleAt, std::chrono::steady_clock::now());
}

void onDmenuConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->dmenu.configured) app->dmenu.anim.animStart = std::chrono::steady_clock::now();
    app->dmenu.configured = true;
    drawDesktopMenu(*app);
}
void onDmenuClosed(void* d, zwlr_layer_surface_v1*) { closeDesktopMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kDmenuListener = {onDmenuConfigure, onDmenuClosed};

void openDesktopMenu(App& app, Bar* bar) {
    if (!bar || !app.layerShell) return;
    closeControlCenter(app); closePowerMenu(app); closeSelector(app); closeLauncher(app); closeNotifPanel(app); closeMediaPanel(app); closeClockPanel(app); closeTrayMenu(app); closeAudioMenu(app); closeBtMenu(app);
    closeDesktopMenu(app);
    DesktopMenu& D = app.dmenu;
    D = DesktopMenu{};
    g_dmSprings.clear();
    D.files.push_back(app.wallpaper);
    std::vector<std::string> all = listWallpapers(expandHome(app.cfg.wallpaperDir));
    all.erase(std::remove(all.begin(), all.end(), app.wallpaper), all.end());
    std::mt19937 rng(std::random_device{}());
    std::shuffle(all.begin(), all.end(), rng);
    for (size_t i = 0; i < all.size() && D.files.size() < kDmCount; i++) D.files.push_back(all[i]);
    if (app.wallpaper.empty() && D.files.size() > 1) D.files.erase(D.files.begin());
    D.widths.assign(D.files.size(), 0);
    for (size_t i = 0; i < D.files.size(); i++) D.widths[i] = dmTargetWidth(kDmCardW - 16 - 2 * kDmCarPad, static_cast<int>(i), 0);
    D.last = std::chrono::steady_clock::now();
    D.anim.overshoot = 1.05;
    D.anim.inSeconds = 0.24;
    popStart(D.anim);
    const double sw = bar->wpW, sh = bar->wpH;
    const double cx = std::clamp(app.pointerX - kDmCardW / 2, 8.0, std::max(8.0, sw - kDmCardW - 8));
    const double reach = std::max({kDmCardH, kDmSubH + 40, kDmWSubH + 8});  // (the submenus can be taller than the card: the whole thing must fit, so it moves up)
    const double cy = std::clamp(app.pointerY - kDmCardH / 2, 8.0, std::max(8.0, sh - reach - 8));
    D.subLeft = cx + kDmCardW + kDmGap + kDmSubW + 8 > sw;
    D.surfW = static_cast<int>(2 * kDmRoom + 2 * (kDmSubW + kDmGap) + kDmCardW);
    D.surfH = static_cast<int>(2 * kDmRoom + std::max({kDmCardH, kDmSubH + 40, kDmWSubH + 8}));
    const double ox = kDmRoom + kDmSubW + kDmGap;
    D.card = {ox, kDmRoom, kDmCardW, kDmCardH};
    D.sub = {D.subLeft ? ox - kDmGap - kDmSubW : ox + kDmCardW + kDmGap, kDmRoom, kDmSubW, kDmSubH};
    D.ox = std::clamp(app.pointerX - cx + ox, D.card.x, D.card.x + kDmCardW);
    D.oy = std::clamp(app.pointerY - cy + kDmRoom, D.card.y, D.card.y + kDmCardH);
    D.surface = wl_compositor_create_surface(app.compositor);
    D.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, D.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-desktop-menu");
    constexpr uint32_t kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    zwlr_layer_surface_v1_set_anchor(D.layer, kT | kL);
    zwlr_layer_surface_v1_set_margin(D.layer, static_cast<int>(cy - kDmRoom), 0, 0, static_cast<int>(cx - ox));
    zwlr_layer_surface_v1_set_size(D.layer, D.surfW, D.surfH);
    zwlr_layer_surface_v1_set_exclusive_zone(D.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(D.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(D.layer, &kDmenuListener, &app);
    wl_surface_commit(D.surface);
}

void dmenuMotion(App& app) {
    DesktopMenu& D = app.dmenu;
    if (D.anim.phase != kPopOpen) return;
    const double x = app.pointerX, y = app.pointerY;
    int hovered = -1, row = -1, sub = -1;
    const double bx = D.card.x + 8 + kDmCarPad, by = D.card.y + 8 + kDmCarPad, ih = kDmCarH - 2 * kDmCarPad, inner = kDmCardW - 16 - 2 * kDmCarPad;
    if (Rect{bx, by, inner, ih}.contains(x, y)) {
        const int n = static_cast<int>(D.files.size());
        std::vector<double> pos(n + 1, 0);
        for (int i = 0; i < n; i++) pos[i + 1] = pos[i] + D.widths[i] + kDmItemGap;
        const int lo = std::clamp(static_cast<int>(std::floor(D.curAnim)), 0, std::max(0, n - 1)), hi = std::min(lo + 1, n - 1);
        const double fr = D.curAnim - lo, origin = pos[lo] * (1 - fr) + pos[hi] * fr;
        for (int i = 0; i < n; i++) {
            const double l = bx + pos[i] - origin, r = l + D.widths[i];
            if (!D.carMoving && (l < bx - 1 || r > bx + inner + 1)) continue;
            if (x >= l && x < r) hovered = i;
        }
    }
    for (int i = 0; i < 3; i++)
        if (Rect{D.card.x, D.card.y + 8 + kDmCarH + 4 + i * (kDmRowH + 2), kDmCardW, kDmRowH}.contains(x, y)) row = i;
    if (D.subOpen) for (const auto& [id, r] : D.subHits) if (r.contains(x, y)) sub = id;
    const bool overSub = D.subOpen && D.sub.contains(x, y);
    const int kind = row == 0 || row == 1 ? row : D.subKind;
    const bool subOpen = row == 0 || row == 1 || (D.subOpen && (overSub || (row < 0 && hovered < 0 && D.hoverRow == D.subKind && !D.card.contains(x, y))));
    if (hovered == D.hovered && row == D.hoverRow && sub == D.hoverSub && subOpen == D.subOpen && kind == D.subKind) return;
    D.hovered = hovered; D.hoverRow = row; D.hoverSub = sub; D.subOpen = subOpen;
    if (kind != D.subKind) { D.subKind = kind; D.sub.h = kind == 0 ? kDmSubH : kDmWSubH; D.hoverSub = sub = -1; }
    D.animating = true;
    drawDesktopMenu(app);
    updateCursor(app);
}

void dmenuClick(App& app) {
    DesktopMenu& D = app.dmenu;
    const double x = app.pointerX, y = app.pointerY;
    if (popIsOut(D.anim)) return;
    if (D.anim.phase != kPopOpen) {
        if (!D.card.contains(x, y)) retractDesktopMenu(app);
        return;
    }
    if (D.hoverSub >= 0) {
        const int id = D.hoverSub;
        if (id >= 400 && id <= 410) {
            const int i = id - 400;
            if (i < 2) {
                if (setConfig(app, i == 0 ? "widgetsLocked" : "widgetShadow", (i == 0 ? app.cfg.widgetsLocked : app.cfg.widgetShadow) ? "0" : "1").empty()) { applyConfig(app); drawAll(app); }
            } else if (i == 10) {
                std::string ignored;
                deskVizToggle(app, "toggle", ignored);
            } else {
                DeskWidget& w = i == 2 ? static_cast<DeskWidget&>(app.todo) : i == 3 ? static_cast<DeskWidget&>(app.weather) : i == 4 ? static_cast<DeskWidget&>(app.mediaW) : i == 5 ? static_cast<DeskWidget&>(app.resW) : i == 6 ? static_cast<DeskWidget&>(app.calW) : i == 7 ? static_cast<DeskWidget&>(app.deskClock) : i == 8 ? static_cast<DeskWidget&>(app.userW) : static_cast<DeskWidget&>(app.imageW);
                if (w.open()) deskClose(app, w); else deskOpen(app, w);
            }
        } else if (id == 500) {
            if (setConfig(app, "centeredWallpaper", app.cfg.centeredWallpaper ? "0" : "1").empty()) { applyConfig(app); drawAll(app); }
        } else if (id == 501) {
            collageCommand(app, app.cfg.collage ? "off" : "on");
        } else if (id == 100 || id == 101) {
            app.cfg.colorMode = id == 100 ? "dark" : "light";
            app.sel.modeTarget = id == 100 ? 1 : 0;
            saveConfig(app);
            applyTheme(app, app.wallpaper);
        } else if (id >= 200 && id < 200 + static_cast<int>(kSchemes.size())) {
            app.cfg.colorScheme = kSchemes[id - 200];
            saveConfig(app);
            applyTheme(app, app.wallpaper);
        } else if (id >= 300 && id <= 308) {
            app.cfg.wallpaperTransition = id == 308 ? "random" : kWallpaperTransitions[id - 300];
            saveConfig(app);
        }
        drawDesktopMenu(app);
        return;
    }
    if (D.hovered >= 0 && D.hovered < static_cast<int>(D.files.size())) {
        const std::string path = D.files[D.hovered];
        retractDesktopMenu(app);
        if (path != app.wallpaper) applyTheme(app, path);
        return;
    }
    if (D.hoverRow == 2) { retractDesktopMenu(app); toggleSettings(); return; }
    if (D.hoverRow == 0 || D.hoverRow == 1) {
        const bool same = D.subOpen && D.subKind == D.hoverRow;
        D.subKind = D.hoverRow;
        D.sub.h = D.subKind == 0 ? kDmSubH : kDmWSubH;
        D.subOpen = !same;
        drawDesktopMenu(app);
        return;
    }
    if (!D.card.contains(x, y) && !(D.subOpen && D.sub.contains(x, y))) retractDesktopMenu(app);
}

void dmenuScroll(App& app, int dir) {
    DesktopMenu& D = app.dmenu;
    if (D.anim.phase != kPopOpen) return;
    D.current = std::clamp(D.current + dir, 0, std::max(0, static_cast<int>(D.files.size()) - 1));
    D.animating = true;
    drawDesktopMenu(app);
}

bool clickable(Hit h) { return (h >= kHitDtp && h < kHitDtp + 100) || h == HitClock || h == HitKeyboard || h == HitWeather || (h >= kHitAi && h < kHitAi + 2) || (h >= kHitRes && h < kHitRes + 5) || h == HitNotif || h == HitVolume || h == HitBluetooth || h == HitDrives || h == HitPrev || h == HitPlay || h == HitNext || h == HitMedia || h == HitSettings || h == HitLauncher || h == HitPower || h == HitTools || h == HitUpdates || h == HitAvatar || h == HitTray || (h >= kHitStatus && h < kHitStatus + 2) || h == kHitStatus + 3 || h >= kHitWs; }

void updateCursor(App& app) {
    if (!app.cursorMgr || !app.pointer) return;
    if (!app.cursorDev) app.cursorDev = wp_cursor_shape_manager_v1_get_pointer(app.cursorMgr, app.pointer);
    const Hit h = app.hoverBar ? hitTest(*app.hoverBar, app.pointerX, app.pointerY) : HitNone;
    const bool overSelectorItem = (app.onSelector && (app.sel.hoverTile >= 0 || app.sel.hoverChip >= 0 || app.sel.hoverMode >= 0 || app.sel.hoverNav >= 0 || app.sel.hoverBtn >= 0)) ||
                                  (app.onLauncher && (app.launcher.hover >= 0 || app.launcher.btnHover >= 0)) || (app.onPower && app.power.hover >= 0) || (app.onCc && ((app.cc.page == CcPageNone && app.cc.hover != 0 && app.cc.hover != CcMedia) || (app.cc.page != CcPageNone && !app.cc.radio.hover.empty()))) || (app.onNotif && app.np.hoverId != 0) ||
                                  (app.onPanel && (app.panel.hoverId != 0 || app.panel.hoverButton == -3 || app.panel.hoverButton == -4)) ||
                                  (app.onMedia && app.media.hover != 0) || (app.onClockP && app.clockP.hover != 0) || (app.onTmenu && app.tmenu.hover >= 0) || (app.onAmenu && app.amenu.hover >= 0) || (app.onBtMenu && app.btMenu.hover >= 0) || (app.onIsland && app.ic.hover >= 0) || (app.onDmenu && (app.dmenu.hovered >= 0 || app.dmenu.hoverRow >= 0 || app.dmenu.hoverSub >= 0)) ||
                                  (app.onDock && (app.dock.hover >= 0 || app.dock.menuHover >= 0));
    const bool overSwCard = app.onSw && app.sw.hover >= 0;
    const int shape = app.onPk ? (app.pk.hover >= 0 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT) : app.onSw ? (overSwCard ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT) : app.onCap ? captureCursorShape(app) : app.onDesk ? deskCursorShape(app) : clickable(h) || overSelectorItem ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (shape == app.cursorShape) return;
    app.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(app.cursorDev, app.enterSerial, shape);
}
