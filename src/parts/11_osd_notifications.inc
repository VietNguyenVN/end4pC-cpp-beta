

constexpr int kOsdBarW = 64, kOsdBarH = 232, kOsdShadowRoom = 20;
constexpr int kOsdSurfW = kOsdBarW + 2 * kOsdShadowRoom, kOsdSurfH = kOsdBarH + 2 * kOsdShadowRoom;
constexpr double kOsdGap = 4;

bool osdHorizontal(const App& app) { return app.cfg.osdPosition == "top" || app.cfg.osdPosition == "bottom"; }

double osdLevel(const App& app, int kind) {
    if (kind == OsdBrightness) return std::clamp(app.brightness, 0, 100) / 100.0;
    return app.muted ? 0.0 : std::clamp(app.volume, 0, 100) / 100.0;
}

void drawOsd(App& app) {
    Osd& o = app.osd;
    if (!o.open() || !o.configured) return;
    const bool horiz = osdHorizontal(app);
    const int W = horiz ? kOsdSurfH : kOsdSurfW, H = horiz ? kOsdSurfW : kOsdSurfH;
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-osd", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    popGroupBegin(cr);

    const double x = 0, y = 0, w = kOsdBarW, h = kOsdBarH;
    cairo_save(cr);
    cairo_translate(cr, kOsdShadowRoom, kOsdShadowRoom);
    if (horiz) {
        cairo_matrix_t flip;
        cairo_matrix_init(&flip, 0, 1, -1, 0, h, 0);
        cairo_transform(cr, &flip);
    }

    const double v = std::clamp(o.shown, 0.0, 1.0);
    const double fillH = v > 0 ? std::max(h * v, w) : 0;
    const Color track = app.color("secondary_container"), fill = app.color("primary");
    const double trackH = std::max(0.0, h - fillH - (v > 0 ? kOsdGap : 0));
    if (trackH > 0) {
        roundedRect(cr, x, y, w, trackH, std::min(w / 2, trackH / 2));
        setSource(cr, track);
        cairo_fill(cr);
        cairo_arc(cr, x + w / 2, y + std::min(16.0, trackH / 2), 4.0, 0, 2 * M_PI);
        setSource(cr, fill);
        cairo_fill(cr);
    }
    if (fillH > 1) {
        roundedRect(cr, x, y + h - fillH, w, fillH, std::min(w / 2, fillH / 2));
        setSource(cr, fill);
        cairo_fill(cr);
    }

    cairo_restore(cr);
    const double ox = kOsdShadowRoom, oy = kOsdShadowRoom;

    {
        const int percent = o.kind == OsdBrightness ? std::clamp(app.brightness, 0, 100) : (app.muted ? 0 : std::clamp(app.volume, 0, 100));
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string("Sans Bold 9");
        pango_layout_set_font_description(layout, fd);
        const std::string label = std::to_string(percent) + "%";
        pango_layout_set_text(layout, label.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        const bool onTrack = trackH >= 16 + 10 + th + 8;
        setSource(cr, app.color(onTrack ? "on_secondary_container" : "on_primary"));
        if (horiz) cairo_move_to(cr, ox + h - 16 - 10 - tw, oy + (w - th) / 2.0);
        else cairo_move_to(cr, ox + (w - tw) / 2.0, oy + 16 + 10);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(fd);
        g_object_unref(layout);
    }

    const char* icon;
    if (o.kind == OsdBrightness) icon = "brightness_6";
    else icon = app.muted || app.volume == 0 ? "volume_off" : app.volume < 50 ? "volume_down" : "volume_up";
    const double isz = 28, ix = horiz ? ox + 17 : ox + (w - isz) / 2, iy = horiz ? oy + (w - isz) / 2 : oy + h - isz - 17;
    const bool onFill = fillH > isz + 17 + 6;
    drawIcon(cr, icon, ix, iy, static_cast<int>(isz), app.color(onFill ? "on_primary" : "on_secondary_container"), true);

    {
        const std::string& pos = app.cfg.osdPosition;
        const double rev = std::clamp(o.anim.reveal, 0.0, 1.0), dist = 36;
        const double dx = pos == "left" ? -dist : pos == "right" ? dist : 0, dy = pos == "top" ? -dist : pos == "bottom" ? dist : 0;
        if (rev > 0.001) popSlideEnd(cr, rev, dx, dy);
        else cairo_pattern_destroy(cairo_pop_group(cr));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(o.surface, buffer, 0, 0);
    wl_surface_damage_buffer(o.surface, 0, 0, W, H);
    wl_surface_commit(o.surface);
}

void closeOsd(App& app) {
    Osd& o = app.osd;
    if (!o.open()) return;
    if (o.layer) zwlr_layer_surface_v1_destroy(o.layer);
    wl_surface_destroy(o.surface);
    if (o.shadow) cairo_surface_destroy(o.shadow);
    o = Osd{};
    malloc_trim(0);
}

void onOsdConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->osd.configured) app->osd.anim.animStart = std::chrono::steady_clock::now();
    app->osd.configured = true;
    drawOsd(*app);
}
void onOsdClosed(void* d, zwlr_layer_surface_v1*) { closeOsd(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kOsdListener = {onOsdConfigure, onOsdClosed};

void osdPlace(const App& app, Osd& o) {
    const std::string& pos = app.cfg.osdPosition;
    const bool horiz = osdHorizontal(app);
    const uint32_t edge = pos == "left" ? ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT : pos == "top" ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                        : pos == "bottom" ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const int gap = std::max(0, app.cfg.frame + 16 + (app.cfg.barPosition == pos ? barSpan(app) : 0) - kOsdShadowRoom);
    zwlr_layer_surface_v1_set_anchor(o.layer, edge);
    zwlr_layer_surface_v1_set_margin(o.layer, pos == "top" ? gap : 0, pos == "right" ? gap : 0, pos == "bottom" ? gap : 0, pos == "left" ? gap : 0);
    zwlr_layer_surface_v1_set_size(o.layer, horiz ? kOsdSurfH : kOsdSurfW, horiz ? kOsdSurfW : kOsdSurfH);
}

void retractOsd(App& app) {
    Osd& o = app.osd;
    if (!o.open() || popIsOut(o.anim)) return;
    if (!o.configured) { closeOsd(app); return; }
    popRetract(o.anim, std::chrono::steady_clock::now());
    drawOsd(app);
}
void tickOsd(App& app, std::chrono::steady_clock::time_point now) {
    Osd& o = app.osd;
    if (!o.open() || !o.configured) return;
    bool moving = false;
    if (popTick(o.anim, now, moving)) { closeOsd(app); return; }
    if (moving) drawOsd(app);
}

bool islandLiveOsd(App& app, int kind);
void islandLiveDnd(App& app, bool on);
void showOsd(App& app, int kind) {
    if (islandLiveOsd(app, kind)) return;
    Osd& o = app.osd;
    o.kind = kind;
    o.target = osdLevel(app, kind);
    o.hideAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app.cfg.osdTimeout);
    if (o.open()) {
        if (popIsOut(o.anim)) popReverse(o.anim, std::chrono::steady_clock::now());
        drawOsd(app);
        return;
    }
    popStartQuick(o.anim, 0.22, 0.18);
    o.shown = o.target;
    o.surface = wl_compositor_create_surface(app.compositor);
    o.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, o.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-osd");
    osdPlace(app, o);
    zwlr_layer_surface_v1_set_exclusive_zone(o.layer, -1);
    zwlr_layer_surface_v1_add_listener(o.layer, &kOsdListener, &app);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(o.surface, none);
    wl_region_destroy(none);
    wl_surface_commit(o.surface);
}

void startBrightnessJob(App& app, const char* verb, int percent, const std::string& out) {
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const std::string script = assetsDir() + "/../scripts/brightness.sh", arg = std::to_string(percent);
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execl(script.c_str(), script.c_str(), verb, arg.c_str(), out.c_str(), nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    app.brightFd = p[0];
    app.brightBuf.clear();
    app.brightJobIsGet = std::strcmp(verb, "get") == 0;
    app.brightJobOut = out;
}

// The monitor that has the focus (the one the pointer is on): its name, asked of the compositor at most every 300 ms.
std::string focusedOutputName() {
    static std::string name;
    static std::chrono::steady_clock::time_point at{};
    const auto now = std::chrono::steady_clock::now();
    if (now - at < std::chrono::milliseconds(300) && !name.empty()) return name;
    const std::string info = wmFocusedOutputInfo();
    name = info.substr(0, info.find_first_of(" \n"));
    at = now;
    return name;
}

// The screen of a laptop goes fully dark at 0% (nothing left to find the slider by), so it never goes under this.
constexpr int kMinLaptopBrightness = 5;
bool isLaptopOutput(const std::string& o) { return o.rfind("eDP", 0) == 0 || o.rfind("LVDS", 0) == 0 || o.rfind("DSI", 0) == 0; }

// Changes the brightness of one monitor: `output` (the one under the pointer when it comes from a bar or a corner), or the focused one.
void changeBrightness(App& app, int delta, int absolute, std::string output) {
    if (output.empty()) output = focusedOutputName();
    app.brightOut = output;
    const auto known = app.brightVal.find(output);
    if (known == app.brightVal.end()) {  // its value is not known yet: ask, and apply the change when it arrives
        auto& pend = app.brightPend[output];
        pend.first += delta;
        pend.second = absolute;
        if (app.brightFd < 0) startBrightnessJob(app, "get", 0, output);
        return;
    }
    known->second = std::clamp(absolute >= 0 ? absolute : known->second + delta, isLaptopOutput(output) ? kMinLaptopBrightness : 0, 100);
    app.brightness = known->second;
    app.brightDirtyOuts.insert(output);
    app.brightDirty = true;
    app.brightSetAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    showOsd(app, OsdBrightness);
}

// The control center and the OSD follow the monitor in focus: its value is shown, and read when it is not known.
void brightnessFollowFocus(App& app) {
    const std::string out = focusedOutputName();
    app.brightOut = out;
    const auto known = app.brightVal.find(out);
    app.brightness = known == app.brightVal.end() ? -1 : known->second;
    if (known == app.brightVal.end() && app.brightFd < 0) startBrightnessJob(app, "get", 0, out);
}

void brightnessJobDone(App& app) {
    close(app.brightFd);
    app.brightFd = -1;
    if (!app.brightJobIsGet) return;
    int cur = 0, max = 0;
    if (std::sscanf(app.brightBuf.c_str(), "%d %d", &cur, &max) != 2 || max <= 0) { app.brightPend.erase(app.brightJobOut); return; }
    const std::string out = app.brightJobOut;
    app.brightVal[out] = std::clamp(cur * 100 / max, 0, 100);
    if (app.brightOut == out) app.brightness = app.brightVal[out];
    const auto pend = app.brightPend.find(out);
    if (pend != app.brightPend.end()) {
        const int d = pend->second.first, a = pend->second.second;
        app.brightPend.erase(pend);
        if (d != 0 || a >= 0) changeBrightness(app, d, a, out);
    }
}

constexpr int kNotifCardW = 410;
constexpr int kNotifSurfH = 880;
constexpr int kNotifPad = 14, kNotifIcon = 38, kNotifGapY = 8, kNotifMaxVisible = 5, kNotifHistory = 50;
constexpr double kNotifRadius = 20;
constexpr const char* kNotifTitleFont = "Sans Bold 10";
constexpr const char* kNotifBodyFont = "Sans 10";
constexpr const char* kNotifAppFont = "Sans 8";

App* g_notifApp = nullptr;

void drawNotifPopup(App& app);
void closeNotifPopup(App& app);
void drawNotifPanel(App& app);

void placeNotifSurface(App& app) {
    if (!app.np.layer) return;
    const std::string& pos = app.cfg.notificationPosition;
    const bool bottom = pos.compare(0, 6, "bottom") == 0, left = pos.size() > 4 && pos.compare(pos.size() - 4, 4, "left") == 0, right = pos.size() > 5 && pos.compare(pos.size() - 5, 5, "right") == 0;
    const bool floating = app.cfg.barStyle == "float" || app.cfg.barStyle == "island";
    auto edge = [&](const char* side) {
        int m = app.cfg.frame + 12;
        if (app.cfg.barPosition == side) m += (std::string(side) == "top" || std::string(side) == "bottom") ? kBarHeight + (floating ? kBarGap : 0) : barSpan(app);
        return m;
    };
    uint32_t anchor = bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (left) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (right) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(app.np.layer, anchor);
    zwlr_layer_surface_v1_set_margin(app.np.layer, bottom ? 0 : edge("top"), right ? edge("right") : 0, bottom ? edge("bottom") : 0, left ? edge("left") : 0);
    if (app.np.configured) wl_surface_commit(app.np.surface);
}

void onNotifConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->np.configured = true;
    drawNotifPopup(*app);
}
void onNotifClosed(void* d, zwlr_layer_surface_v1*) { closeNotifPopup(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kNotifListener = {onNotifConfigure, onNotifClosed};

cairo_surface_t* notifImageFromData(const NotifyRequest& r) {
    cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.imgW, r.imgH);
    unsigned char* dst = cairo_image_surface_get_data(src);
    const int drs = cairo_image_surface_get_stride(src);
    for (int y = 0; y < r.imgH; y++) {
        const unsigned char* row = r.imgData + static_cast<size_t>(y) * r.imgRowstride;
        uint32_t* out = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < r.imgW; x++) {
            const unsigned char* px = row + x * r.imgChannels;
            const uint32_t a = r.imgChannels == 4 ? px[3] : 255;
            out[x] = (a << 24) | ((px[0] * a / 255) << 16) | ((px[1] * a / 255) << 8) | (px[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(src);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kNotifIcon, kNotifIcon);
    cairo_t* cr = mkcr(out);
    const double scale = std::max(double(kNotifIcon) / r.imgW, double(kNotifIcon) / r.imgH);
    cairo_translate(cr, (kNotifIcon - r.imgW * scale) / 2.0, (kNotifIcon - r.imgH * scale) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);
    return out;
}

Notif* findNotif(App& app, uint32_t id) {
    for (auto& n : app.notifs)
        if (n.id == id) return &n;
    return nullptr;
}

void dismissNotif(App& app, uint32_t id, uint32_t reason) {
    Notif* n = findNotif(app, id);
    if (!n || !n->popup || n->leaving) return;
    n->leaving = true;
    n->leaveAt = std::chrono::steady_clock::now();
    if (reason != 1 && app.bus && !n->closedSent) { notifyd_emit_closed(app.bus, id, reason); n->closedSent = true; }
    drawNotifPopup(app);
}

constexpr double kNotifInSeconds = 0.26, kNotifOutSeconds = 0.18;
double notifCardReveal(const Notif& n, std::chrono::steady_clock::time_point now) {
    if (n.leaving) {
        const double t = std::min(1.0, std::chrono::duration<double>(now - n.leaveAt).count() / kNotifOutSeconds);
        return 1 - t * t * t;
    }
    const double u = std::min(1.0, std::chrono::duration<double>(now - n.shownAt).count() / kNotifInSeconds) - 1;
    return 1 + u * u * u;
}
void notifSlideOffset(const App& app, double& dx, double& dy) {
    const std::string& pos = app.cfg.notificationPosition;
    const bool bottom = pos.compare(0, 6, "bottom") == 0, left = pos.size() > 4 && pos.compare(pos.size() - 4, 4, "left") == 0, right = pos.size() > 5 && pos.compare(pos.size() - 5, 5, "right") == 0;
    dx = right ? 56 : left ? -56 : 0;
    dy = left || right ? 0 : bottom ? 40 : -40;
}
bool notifPopupAnimating(const App& app, std::chrono::steady_clock::time_point now) {
    for (const auto& n : app.notifs)
        if (n.popup && (n.leaving || std::chrono::duration<double>(now - n.shownAt).count() < kNotifInSeconds)) return true;
    return false;
}

void setDoNotDisturb(App& app, bool on) {
    app.cfg.doNotDisturb = on ? 1 : 0;
    islandLiveDnd(app, on);
    saveConfig(app);
    if (on) {
        std::vector<uint32_t> up;
        for (const auto& n : app.notifs)
            if (n.popup) up.push_back(n.id);
        for (uint32_t id : up) dismissNotif(app, id, 2);
    }
    drawAll(app);
    if (app.panel.open()) drawNotifPanel(app);
}

void islandNotify(App& app, uint32_t id);
void notifInvoke(App& app, uint32_t id, const std::string& key);

uint32_t onNotifyRequest(void*, const NotifyRequest* r) {
    App& app = *g_notifApp;
    Notif* n = r->replaces ? findNotif(app, r->replaces) : nullptr;
    const bool existed = n != nullptr;
    if (!n) {
        app.notifs.insert(app.notifs.begin(), Notif{});
        n = &app.notifs.front();
        n->id = app.nextNotifId++;
        app.notifCount++;
        if (app.notifs.size() > kNotifHistory) {
            if (app.notifs.back().image) cairo_surface_destroy(app.notifs.back().image);
            if (!app.notifs.back().closedSent && app.bus) notifyd_emit_closed(app.bus, app.notifs.back().id, 2);
            app.notifs.pop_back();
        }
    } else if (n->image) {
        cairo_surface_destroy(n->image);
        n->image = nullptr;
    }
    n->app = r->app ? r->app : "";
    n->icon = r->icon ? r->icon : "";
    n->summary = r->summary ? r->summary : "";
    n->body = r->body ? r->body : "";
    n->imagePath = r->imagePath ? r->imagePath : "";
    n->desktopEntry = r->desktopEntry ? r->desktopEntry : "";
    n->urgency = r->urgency;
    n->actions.clear();
    for (int i = 0; i < r->nactions; i++) n->actions.emplace_back(r->actions[i].key, r->actions[i].label);
    if (r->hasImage) n->image = notifImageFromData(*r);
    n->time = std::time(nullptr);
    const bool wasUp = existed && n->popup && !n->leaving;
    const bool inIsland = !app.cfg.doNotDisturb && islandActive(app);
    n->popup = !app.cfg.doNotDisturb && !inIsland;
    n->leaving = false;
    if (!wasUp) n->shownAt = std::chrono::steady_clock::now();
    n->sticky = r->urgency >= 2;
    n->hideAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app.cfg.notificationTimeout);
    if (!n->popup) {
    } else if (!app.np.open()) {
        app.np.surface = wl_compositor_create_surface(app.compositor);
        app.np.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, app.np.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-notifications");
        placeNotifSurface(app);
        zwlr_layer_surface_v1_set_size(app.np.layer, kNotifCardW, kNotifSurfH);
        zwlr_layer_surface_v1_set_exclusive_zone(app.np.layer, -1);
        zwlr_layer_surface_v1_add_listener(app.np.layer, &kNotifListener, &app);
        wl_surface_commit(app.np.surface);
    } else {
        drawNotifPopup(app);
    }
    if (app.panel.open()) drawNotifPanel(app);
    drawAll(app);
    if (inIsland) islandNotify(app, n->id);
    return n->id;
}

void onCloseRequest(void*, uint32_t id) { dismissNotif(*g_notifApp, id, 3); }

void startNotifications(App& app) {
    g_notifApp = &app;
    static const NotifyHandlers handlers = {onNotifyRequest, onCloseRequest, nullptr};
    const int r = notifyd_start(&app.bus, &handlers);
    if (r < 0) {
        std::fprintf(stderr, "notifications: could not own org.freedesktop.Notifications (%s); is another daemon running?\n", std::strerror(-r));
        app.bus = nullptr;
    }
}

std::string stripMarkup(const std::string& in) {
    std::string out;
    bool tag = false;
    for (char c : in) {
        if (c == '<') tag = true;
        else if (c == '>' && tag) tag = false;
        else if (!tag) out += c;
    }
    for (const auto& [from, to] : {std::pair<const char*, const char*>{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}}) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    return out;
}

void setNotifBody(PangoLayout* layout, const std::string& body) {
    char* text = nullptr;
    PangoAttrList* attrs = nullptr;
    GError* err = nullptr;
    if (pango_parse_markup(body.c_str(), -1, 0, &attrs, &text, nullptr, &err)) {
        pango_layout_set_text(layout, text, -1);
        pango_layout_set_attributes(layout, attrs);
        g_free(text);
        pango_attr_list_unref(attrs);
    } else {
        if (err) g_error_free(err);
        pango_layout_set_attributes(layout, nullptr);
        pango_layout_set_text(layout, stripMarkup(body).c_str(), -1);
    }
}

cairo_surface_t* notifPicture(App& app, const Notif& n) {
    if (n.image) return n.image;
    auto isFile = [](const std::string& v) { return !v.empty() && (v[0] == '/' || v.rfind("file://", 0) == 0); };
    const std::string key = isFile(n.imagePath) ? n.imagePath : isFile(n.icon) ? n.icon : "";
    if (key.empty()) return nullptr;
    if (auto it = app.np.icons.find(key); it != app.np.icons.end()) return it->second;
    const std::string path = key.rfind("file://", 0) == 0 ? percentDecode(key.substr(7)) : key;
    cairo_surface_t* surf = access(path.c_str(), R_OK) == 0 ? loadIconSurface(path, 64) : nullptr;
    app.np.icons[key] = surf;
    return surf;
}

// The icon of the app that sent a notification, for one without a picture (notificationAppIcons): the Icon of its desktop entry,
// else the app icon name it sent (image-path, then app_icon), else its name as a desktop entry or an app icon. Only app icons
// resolve (lookupIcon searches the Applications folders), so a status name like dialog-information keeps the symbol.
// Null when nothing is found (the symbol is drawn then).
cairo_surface_t* notifAppIcon(App& app, const Notif& n) {
    const std::string key = "app:" + n.desktopEntry + "|" + n.icon + "|" + n.imagePath + "|" + n.app;
    if (auto it = app.np.icons.find(key); it != app.np.icons.end()) return it->second;
    auto stem = [](std::string v) {
        if (v.size() > 8 && v.compare(v.size() - 8, 8, ".desktop") == 0) v.resize(v.size() - 8);
        return v;
    };
    auto fromEntry = [](const std::string& id) -> std::string {
        if (id.empty() || id.find('/') != std::string::npos) return "";  // (an id, never a path)
        const std::string file = findDesktopFile(id);
        return file.empty() ? "" : lookupIcon(desktopValue(file, "Icon"), 64);
    };
    auto isName = [](const std::string& v) { return !v.empty() && v[0] != '/' && v.rfind("file://", 0) != 0; };
    std::string appId = lowerCopy(n.app);
    std::replace(appId.begin(), appId.end(), ' ', '-');
    std::string path = fromEntry(stem(n.desktopEntry));
    if (path.empty()) path = fromEntry(lowerCopy(stem(n.desktopEntry)));
    if (path.empty() && isName(n.imagePath)) path = lookupIcon(n.imagePath, 64);  // (image-path may also be an icon name: notify-send -i sends it there)
    if (path.empty() && isName(n.icon)) path = lookupIcon(n.icon, 64);
    if (path.empty()) path = fromEntry(appId);
    if (path.empty() && !appId.empty()) path = lookupIcon(appId, 64);
    cairo_surface_t* surf = path.empty() ? nullptr : loadIconSurface(path, 64);
    app.np.icons[key] = surf;
    return surf;
}

// Paints an app icon (a square surface) at size px, centered in the shape of side box at (x, y) and clipped to it.
void paintNotifAppIcon(cairo_t* cr, cairo_surface_t* icon, const std::string& shape, double x, double y, double box, double size) {
    cairo_save(cr);
    shapePath(cr, shape, x, y, box);
    cairo_clip(cr);
    cairo_translate(cr, x + (box - size) / 2.0, y + (box - size) / 2.0);
    cairo_scale(cr, size / cairo_image_surface_get_width(icon), size / cairo_image_surface_get_height(icon));
    cairo_set_source_surface(cr, icon, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

std::string notifAppLabel(const Notif& n) {
    if (n.app.empty()) return "Notification";
    return lowerCopy(n.app) == "end4pc" ? "Shell" : n.app;
}

std::string notifSolarIcon(const Notif& n) {
    const std::string hay = lowerCopy(n.icon + " " + n.imagePath + " " + n.app + " " + n.desktopEntry);
    static const std::vector<std::pair<std::vector<const char*>, const char*>> rules = {
        {{"error", "dialog-error"}, "danger-circle"},
        {{"warning"}, "danger-triangle"},
        {{"security", "shield", "polkit", "authent"}, "shield-warning"},
        {{"battery", "power-"}, "battery-low"},
        {{"telegram"}, "plain-3"},
        {{"mail", "thunderbird", "gmail", "evolution"}, "letter"},
        {{"discord", "message", "chat", "sms", "signal", "whatsapp", "im-", "slack", "element"}, "chat-round-dots"},
        {{"phone", "call", "dialer"}, "phone-calling"},
        {{"alarm", "clock", "timer", "reminder"}, "alarm"},
        {{"calendar"}, "calendar"},
        {{"download", "transmission", "qbittorrent"}, "download-minimalistic"},
        {{"volume", "audio", "sound", "pulse", "pipewire"}, "volume-loud"},
        {{"camera", "screenshot", "photo", "image", "gallery"}, "camera"},
        {{"music", "audacious", "spotify", "player", "rhythmbox"}, "music-note"},
        {{"bluetooth"}, "bluetooth"},
        {{"network", "wifi", "nm-", "vpn", "wireless"}, "wi-fi-router"},
        {{"firefox", "chrome", "chromium", "browser", "brave"}, "earth"},
        {{"update", "software", "discover", "upgrade", "pamac"}, "refresh-circle"},
        {{"information", "dialog-info", "info"}, "info-circle"},
    };
    for (const auto& [words, icon] : rules)
        for (const char* w : words)
            if (hay.find(w) != std::string::npos) return icon;
    return n.urgency >= 2 ? "danger-triangle" : "notifications";
}

void drawNotifPopup(App& app) {
    NotifPopup& np = app.np;
    if (!np.open() || !np.configured) return;
    const int W = kNotifCardW, H = kNotifSurfH;
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-notifications", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_push_group(cr);
    const auto drawNow = std::chrono::steady_clock::now();
    double slideDx = 0, slideDy = 0;
    notifSlideOffset(app, slideDx, slideDy);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto useFont = [&](const char* name) {
        PangoFontDescription* fd2 = pango_font_description_from_string(name);
        pango_layout_set_font_description(layout, fd2);
        pango_font_description_free(fd2);
    };
    const double textX = kNotifPad + kNotifIcon + 12, textW = W - textX - kNotifPad;

    np.cards.clear();
    double y = 0;
    int shown = 0;
    for (const Notif& n : app.notifs) {
        if (!n.popup || shown >= kNotifMaxVisible) continue;
        pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_attributes(layout, nullptr);
        int tw = 0, appH = 0, titleH = 0, bodyH = 0;
        useFont(kNotifAppFont);
        pango_layout_set_height(layout, -1);
        pango_layout_set_text(layout, notifAppLabel(n).c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &appH);
        useFont(kNotifTitleFont);
        pango_layout_set_height(layout, -2);
        pango_layout_set_text(layout, n.summary.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &titleH);
        useFont(kNotifBodyFont);
        pango_layout_set_height(layout, -4);
        setNotifBody(layout, n.body);
        pango_layout_get_pixel_size(layout, &tw, &bodyH);
        if (n.body.empty()) bodyH = 0;
        std::vector<std::pair<std::string, std::string>> shownActions;
        for (const auto& a : n.actions)
            if (!a.second.empty()) shownActions.push_back(a);
        const double textBlock = appH + 2 + titleH + (bodyH ? 2 + bodyH : 0);
        const double actionsH = shownActions.empty() ? 0 : 8 + 30;
        const double h = kNotifPad * 2 + std::max<double>(kNotifIcon, textBlock) + actionsH;
        if (y + h > H) break;

        NotifPopup::Card card;
        card.id = n.id;
        card.leaving = n.leaving;
        card.reveal = notifCardReveal(n, drawNow);
        cairo_push_group(cr);
        card.rect = {0, y, double(W), h};
        card.close = {W - kNotifPad - 24.0, y + 10, 24, 24};
        const bool hot = np.hoverId == n.id;

        roundedRect(cr, 0, y, W, h, kNotifRadius);
        setSource(cr, app.color(n.urgency >= 2 ? "error_container" : "surface"), app.cfg.barOpacity / 100.0);
        cairo_fill(cr);
        if (hot) {
            roundedRect(cr, 0, y, W, h, kNotifRadius);
            setSource(cr, app.color("on_surface"), 0.05);
            cairo_fill(cr);
        }

        const double ix = kNotifPad, iy = y + kNotifPad;
        const std::string shape = n.urgency >= 2 ? "SoftBurst" : app.cfg.notificationShape;
        shapePath(cr, shape, ix, iy, kNotifIcon);
        setSource(cr, app.color(n.urgency >= 2 ? "primary_container" : "secondary_container"));
        cairo_fill(cr);
        if (cairo_surface_t* pic = notifPicture(app, n)) {
            const double pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
            const double box = kNotifIcon;
            cairo_save(cr);
            shapePath(cr, shape, ix, iy, kNotifIcon);
            cairo_clip(cr);
            cairo_translate(cr, ix + (kNotifIcon - box) / 2.0, iy + (kNotifIcon - box) / 2.0);
            cairo_scale(cr, box / pw, box / ph);
            cairo_set_source_surface(cr, pic, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else if (cairo_surface_t* appIcon = app.cfg.notificationAppIcons ? notifAppIcon(app, n) : nullptr) {
            paintNotifAppIcon(cr, appIcon, shape, ix, iy, kNotifIcon, 24);
        } else {
            drawIcon(cr, notifSolarIcon(n), ix + 9, iy + 9, 20, app.color(n.urgency >= 2 ? "on_primary_container" : "on_secondary_container"), true);
        }

        double ty = y + kNotifPad;
        auto line = [&](const char* font, int maxLines, const std::string& text, bool markup, const Color& c, double alpha, int h2) {
            useFont(font);
            pango_layout_set_height(layout, maxLines);
            if (markup) setNotifBody(layout, text);
            else { pango_layout_set_attributes(layout, nullptr); pango_layout_set_text(layout, text.c_str(), -1); }
            setSource(cr, c, alpha);
            cairo_move_to(cr, textX, ty);
            pango_cairo_show_layout(cr, layout);
            ty += h2;
        };
        line(kNotifAppFont, -1, notifAppLabel(n), false, app.color("on_surface_variant"), 1.0, appH + 2);
        line(kNotifTitleFont, -2, n.summary, false, app.color("on_surface"), 1.0, titleH + (bodyH ? 2 : 0));
        if (bodyH) line(kNotifBodyFont, -4, n.body, true, app.color("on_surface_variant"), 1.0, bodyH);

        const double by = y + h - kNotifPad - 30;
        double bx = textX;
        {
            double row = 0;
            for (size_t i = 0; i < shownActions.size(); i++) {
                useFont(kNotifBodyFont);
                pango_layout_set_height(layout, -1);
                pango_layout_set_width(layout, -1);
                pango_layout_set_attributes(layout, nullptr);
                pango_layout_set_text(layout, shownActions[i].second.c_str(), -1);
                int lw = 0, lh = 0;
                pango_layout_get_pixel_size(layout, &lw, &lh);
                row += lw + 28 + (i ? 6 : 0);
            }
            bx = std::max(textX, W - kNotifPad - row);
        }
        for (size_t i = 0; i < shownActions.size(); i++) {
            useFont(kNotifBodyFont);
            pango_layout_set_height(layout, -1);
            pango_layout_set_width(layout, -1);
            pango_layout_set_attributes(layout, nullptr);
            pango_layout_set_text(layout, shownActions[i].second.c_str(), -1);
            int lw = 0, lh = 0;
            pango_layout_get_pixel_size(layout, &lw, &lh);
            const double bw = lw + 28;
            if (bx + bw > W - kNotifPad) break;
            const Rect r{bx, by, bw, 30};
            card.buttons.push_back({r, shownActions[i].first});
            const bool onBtn = np.hoverId == n.id && np.hoverButton == static_cast<int>(i);
            roundedRect(cr, r.x, r.y, r.w, r.h, 15);
            setSource(cr, app.color(n.urgency >= 2 ? "primary" : "secondary_container"));
            cairo_fill(cr);
            if (onBtn) {
                roundedRect(cr, r.x, r.y, r.w, r.h, 15);
                setSource(cr, app.color("on_surface"), 0.1);
                cairo_fill(cr);
            }
            setSource(cr, app.color(n.urgency >= 2 ? "on_primary" : "on_secondary_container"));
            cairo_move_to(cr, r.x + 14, r.y + (r.h - lh) / 2.0);
            pango_cairo_show_layout(cr, layout);
            bx += bw + 6;
            pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
        }

        if (hot) {
            const bool onClose = np.hoverButton == -2;
            cairo_new_sub_path(cr);
        cairo_arc(cr, card.close.x + 12, card.close.y + 12, 12, 0, 2 * M_PI);
            setSource(cr, app.color("on_surface"), onClose ? 0.16 : 0.08);
            cairo_fill(cr);
            drawIcon(cr, "close", card.close.x + 4, card.close.y + 4, 16, app.color("on_surface_variant"), onClose);
        }

        if (card.reveal > 0.001) popSlideEnd(cr, card.reveal, slideDx, slideDy);
        else cairo_pattern_destroy(cairo_pop_group(cr));
        np.cards.push_back(std::move(card));
        y += h + kNotifGapY;
        shown++;
    }

    g_object_unref(layout);
    {
        const double used = y > 0 ? y - kNotifGapY : 0;
        const double dy = app.cfg.notificationPosition.compare(0, 6, "bottom") == 0 ? H - used : 0;
        cairo_pattern_t* group = cairo_pop_group(cr);
        cairo_translate(cr, 0, dy);
        cairo_set_source(cr, group);
        cairo_paint(cr);
        cairo_pattern_destroy(group);
        for (auto& c : np.cards) {
            c.rect.y += dy;
            c.close.y += dy;
            for (auto& b : c.buttons) b.first.y += dy;
        }
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    updateBlurRegion(app, np.surface, np.blur, W, H, [&](cairo_t* m) {
        for (const auto& c : np.cards) {
            if (c.reveal <= 0.001) continue;
            cairo_save(m);
            cairo_translate(m, -slideDx * (1 - c.reveal), -slideDy * (1 - c.reveal));
            roundedRect(m, 0, c.rect.y, W, c.rect.h, kNotifRadius);
            cairo_fill(m);
            cairo_restore(m);
        }
    });
    wl_region* region = wl_compositor_create_region(app.compositor);
    for (const auto& c : np.cards)
        if (!c.leaving) wl_region_add(region, 0, static_cast<int>(c.rect.y), W, static_cast<int>(c.rect.h));
    wl_surface_set_input_region(np.surface, region);
    wl_region_destroy(region);

    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(np.surface, buffer, 0, 0);
    wl_surface_damage_buffer(np.surface, 0, 0, W, H);
    wl_surface_commit(np.surface);
}

void closeNotifPopup(App& app) {
    NotifPopup& np = app.np;
    if (!np.open()) return;
    if (np.layer) zwlr_layer_surface_v1_destroy(np.layer);
    releaseBlurSurface(np.blur);
    wl_surface_destroy(np.surface);
    for (auto& [k, surf] : np.icons)
        if (surf) cairo_surface_destroy(surf);
    np = NotifPopup{};
    app.onNotif = false;
    updateCursor(app);
    malloc_trim(0);
}

void notifMotion(App& app) {
    NotifPopup& np = app.np;
    uint32_t hoverId = 0;
    int hoverButton = -1;
    for (const auto& c : np.cards) {
        if (c.leaving || !c.rect.contains(app.pointerX, app.pointerY)) continue;
        hoverId = c.id;
        if (c.close.contains(app.pointerX, app.pointerY)) hoverButton = -2;
        for (size_t i = 0; i < c.buttons.size(); i++)
            if (c.buttons[i].first.contains(app.pointerX, app.pointerY)) hoverButton = static_cast<int>(i);
    }
    if (hoverId == np.hoverId && hoverButton == np.hoverButton) return;
    np.hoverId = hoverId;
    np.hoverButton = hoverButton;
    drawNotifPopup(app);
    updateCursor(app);
}

void notifClick(App& app) {
    NotifPopup& np = app.np;
    for (const auto& c : np.cards) {
        if (c.leaving || !c.rect.contains(app.pointerX, app.pointerY)) continue;
        const uint32_t id = c.id;
        if (np.hoverButton == -2) {
            dismissNotif(app, id, 2);
        } else if (np.hoverButton >= 0 && np.hoverButton < static_cast<int>(c.buttons.size())) {
            const std::string key = c.buttons[np.hoverButton].second;
            notifInvoke(app, id, key);
            dismissNotif(app, id, 2);
        } else {
            if (Notif* n = findNotif(app, id))
                for (const auto& a : n->actions)
                    if (a.first == "default") notifInvoke(app, id, "default");
            dismissNotif(app, id, 2);
        }
        np.hoverId = 0;
        np.hoverButton = -1;
        return;
    }
}

void expireNotifs(App& app, std::chrono::steady_clock::time_point now) {
    std::vector<uint32_t> expired;
    for (auto& n : app.notifs) {
        if (!n.popup || n.sticky || n.leaving) continue;
        if (app.np.hoverId == n.id) { n.hideAt = now + std::chrono::milliseconds(app.cfg.notificationTimeout); continue; }
        if (now >= n.hideAt) expired.push_back(n.id);
    }
    for (uint32_t id : expired) dismissNotif(app, id, 1);
}

void notifPopupTick(App& app, std::chrono::steady_clock::time_point now) {
    if (!app.np.open() || !app.np.configured) return;
    bool gone = false;
    for (auto& n : app.notifs)
        if (n.popup && n.leaving && std::chrono::duration<double>(now - n.leaveAt).count() >= kNotifOutSeconds) { n.popup = false; n.leaving = false; gone = true; }
    const bool any = std::any_of(app.notifs.begin(), app.notifs.end(), [](const Notif& x) { return x.popup; });
    if (!any) { closeNotifPopup(app); return; }
    if (gone || notifPopupAnimating(app, now)) drawNotifPopup(app);
}
