
constexpr double kPkW = 450, kPkPad = 28;

bool pkStartSession(App& app) {
    PolkitDlg& P = app.pk;
    if (P.fd >= 0) { close(P.fd); P.fd = -1; }
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::snprintf(a.sun_path, sizeof a.sun_path, "/run/polkit/agent-helper.socket");
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) { close(fd); return false; }
    const std::string hello = P.user + "\n" + P.req.cookie + "\n";
    if (write(fd, hello.data(), hello.size()) != static_cast<ssize_t>(hello.size())) { close(fd); return false; }
    P.fd = fd;
    P.rbuf.clear();
    P.gotAnything = false;
    P.prompting = false;
    P.busy = true;
    return true;
}

void drawPolkit(App& app);
void pkFinishRequest(App& app, bool ok);
void retractPolkitDialog(App& app);

void closePolkitDialog(App& app) {
    PolkitDlg& P = app.pk;
    if (P.layer) zwlr_layer_surface_v1_destroy(P.layer);
    releaseBlurSurface(P.blur);
    if (P.surface) wl_surface_destroy(P.surface);
    P.layer = nullptr;
    P.surface = nullptr;
    P.configured = false;
    P.anim = PopAnim{};
    app.onPk = false;
    app.cursorShape = -1;
    updateCursor(app);
}

void retractPolkitDialog(App& app) {
    PolkitDlg& P = app.pk;
    if (!P.open() || popIsOut(P.anim)) return;
    if (!P.configured) { closePolkitDialog(app); return; }
    popRetract(P.anim, std::chrono::steady_clock::now());
    P.hover = -1;
    drawPolkit(app);
}
void tickPolkit(App& app, std::chrono::steady_clock::time_point now) {
    PolkitDlg& P = app.pk;
    if (!P.open() || !P.configured) return;
    bool moving = false;
    if (popTick(P.anim, now, moving)) { closePolkitDialog(app); return; }
    if (moving) drawPolkit(app);
}
std::chrono::steady_clock::time_point polkitWake(const App& app) {
    if (!app.pk.open() || !app.pk.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.pk.anim.phase, app.pk.anim.settleAt, std::chrono::steady_clock::now());
}

void drawPolkit(App& app);
void openPolkitDialog(App& app) {
    PolkitDlg& P = app.pk;
    if (P.open() && popIsOut(P.anim)) {
        popReverse(P.anim, std::chrono::steady_clock::now());
        drawPolkit(app);
        return;
    }
    if (P.open() || !app.compositor || !app.layerShell || !app.shm) return;
    if (anyPopupOpen(app)) { closeAllPopups(app); syncCatcher(app); drawAll(app); }
    P.anim = PopAnim{};
    popStartQuick(P.anim, 0.16, 0.11);
    P.surface = wl_compositor_create_surface(app.compositor);
    P.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, P.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-polkit");
    zwlr_layer_surface_v1_set_anchor(P.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(P.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(P.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(P.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    static const zwlr_layer_surface_v1_listener kPkListener = {
        [](void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
            auto* a = static_cast<App*>(d);
            zwlr_layer_surface_v1_ack_configure(layer, serial);
            a->pk.w = static_cast<int>(w);
            a->pk.h = static_cast<int>(h);
            if (!a->pk.configured) a->pk.anim.animStart = std::chrono::steady_clock::now();
            a->pk.configured = true;
            drawPolkit(*a);
        },
        [](void* d, zwlr_layer_surface_v1*) { pkFinishRequest(*static_cast<App*>(d), false); }};
    zwlr_layer_surface_v1_add_listener(P.layer, &kPkListener, &app);
    wl_surface_commit(P.surface);
}

void pkBeginNext(App& app) {
    PolkitDlg& P = app.pk;
    if (P.queue.empty()) { P.active = false; retractPolkitDialog(app); return; }
    P.req = std::move(P.queue.front());
    P.queue.erase(P.queue.begin());
    P.active = true;
    P.uid = P.req.uids.empty() ? 0 : P.req.uids[0];
    for (uint32_t u : P.req.uids) if (u == getuid()) P.uid = u;
    const passwd* pw = getpwuid(P.uid);
    P.user = pw ? pw->pw_name : std::to_string(P.uid);
    P.text.clear();
    P.note.clear();
    P.noteErr = false;
    P.prompt = "Password";
    P.hover = -1;
    if (!pkStartSession(app)) {
        P.note = "Could not reach polkit's password helper.";
        P.noteErr = true;
    }
    openPolkitDialog(app);
    drawPolkit(app);
}

void pkFinishRequest(App& app, bool ok) {
    PolkitDlg& P = app.pk;
    if (!P.active) { closePolkitDialog(app); return; }
    if (P.fd >= 0) { close(P.fd); P.fd = -1; }
    polkitd_finish(P.req.handle, ok ? 1 : 0);
    P.req = PolkitReq{};
    P.active = false;
    pkBeginNext(app);
}

void pkSubmit(App& app) {
    PolkitDlg& P = app.pk;
    if (!P.active || !P.prompting || P.busy || P.fd < 0) return;
    const std::string line = P.text + "\n";
    if (write(P.fd, line.data(), line.size()) < 0) return;
    P.text.clear();
    P.prompting = false;
    P.busy = true;
    P.note.clear();
    drawPolkit(app);
}

void pkHandleLine(App& app, const std::string& line) {
    PolkitDlg& P = app.pk;
    P.gotAnything = true;
    auto rest = [&](size_t n) { return line.size() > n ? line.substr(n + 1) : std::string(); };
    if (line.rfind("PAM_PROMPT_ECHO_OFF", 0) == 0 || line.rfind("PAM_PROMPT_ECHO_ON", 0) == 0) {
        P.echo = line.rfind("PAM_PROMPT_ECHO_ON", 0) == 0;
        std::string p = rest(line.find(' ') == std::string::npos ? line.size() : line.find(' '));
        while (!p.empty() && (p.back() == ' ' || p.back() == ':')) p.pop_back();
        P.prompt = p.empty() ? "Password" : p;
        P.prompting = true;
        P.busy = false;
        P.text.clear();
    } else if (line.rfind("PAM_ERROR_MSG", 0) == 0) {
        P.note = rest(13);
        P.noteErr = true;
    } else if (line.rfind("PAM_TEXT_INFO", 0) == 0) {
        P.note = rest(13);
        P.noteErr = false;
    } else if (line == "SUCCESS") {
        pkFinishRequest(app, true);
        return;
    } else if (line == "FAILURE") {
        P.note = "Sorry, that didn't work. Please try again.";
        P.noteErr = true;
        if (!pkStartSession(app)) { pkFinishRequest(app, false); return; }
    }
    drawPolkit(app);
}

void pkRead(App& app) {
    PolkitDlg& P = app.pk;
    if (P.fd < 0) return;
    char buf[1024];
    for (;;) {
        const ssize_t n = read(P.fd, buf, sizeof buf);
        if (n > 0) { P.rbuf.append(buf, n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        for (size_t nl; (nl = P.rbuf.find('\n')) != std::string::npos;) {
            const std::string line = P.rbuf.substr(0, nl);
            P.rbuf.erase(0, nl + 1);
            pkHandleLine(app, line);
            if (!P.active || P.fd < 0) return;
        }
        if (P.fd >= 0) { close(P.fd); P.fd = -1; }
        if (!P.gotAnything) { P.note = "The password helper stopped."; P.noteErr = true; pkFinishRequest(app, false); }
        else pkHandleLine(app, "FAILURE");
        return;
    }
    for (size_t nl; (nl = P.rbuf.find('\n')) != std::string::npos;) {
        const std::string line = P.rbuf.substr(0, nl);
        P.rbuf.erase(0, nl + 1);
        pkHandleLine(app, line);
        if (!P.active || P.fd < 0) return;
    }
}

void pkOnBegin(void* d, const PolkitRequest* r) {
    App& app = *static_cast<App*>(d);
    PolkitReq q;
    q.action = r->action_id ? r->action_id : "";
    q.message = r->message ? r->message : "";
    q.icon = r->icon_name ? r->icon_name : "";
    q.cookie = r->cookie ? r->cookie : "";
    q.uids.assign(r->uids, r->uids + r->n_uids);
    q.handle = r->handle;
    app.pk.queue.push_back(std::move(q));
    if (!app.pk.active) pkBeginNext(app);
}

void pkOnCancel(void* d, const char* cookie) {
    App& app = *static_cast<App*>(d);
    PolkitDlg& P = app.pk;
    if (P.active && P.req.cookie == cookie) { pkFinishRequest(app, false); return; }
    for (auto it = P.queue.begin(); it != P.queue.end(); ++it)
        if (it->cookie == cookie) { polkitd_finish(it->handle, 0); P.queue.erase(it); return; }
}

void startPolkit(App& app) {
    static const PolkitHandlers handlers = {pkOnBegin, pkOnCancel, nullptr};
    static App* self = &app;
    PolkitHandlers h = handlers;
    h.userdata = self;
    const int r = polkitd_start(&app.pkBus, &h);
    if (r < 0) {
        std::fprintf(stderr, "polkit: could not register the authentication agent (%s); is another agent running?\n", std::strerror(-r));
        app.pkBus = nullptr;
    }
}

struct PkLayout {
    double x = 0, y = 0, w = kPkW, h = 0;
    double msgY = 0, msgH = 0, fieldY = 0, noteY = 0, noteH = 0, btnY = 0;
};

void drawPolkit(App& app) {
    PolkitDlg& P = app.pk;
    if (!P.open() || !P.configured || P.w <= 0 || P.h <= 0) return;
    const int W = P.w, H = P.h, stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-polkit", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const double rev = std::clamp(P.anim.reveal, 0.0, 1.0);
    setSource(cr, app.color("scrim"), 0.45 * rev);
    cairo_paint(cr);
    popGroupBegin(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](const char* d) {
        PangoFontDescription* f = pango_font_description_from_string(d);
        pango_layout_set_font_description(layout, f);
        pango_font_description_free(f);
    };
    const double inner = kPkW - 2 * kPkPad;
    std::string msg = P.req.message;
    if (msg.empty()) msg = "Authentication is required.";
    const bool other = P.uid != getuid();
    pango_layout_set_width(layout, static_cast<int>(inner * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    font("Sans 11");
    pango_layout_set_text(layout, msg.c_str(), -1);
    int mw = 0, mh = 0;
    pango_layout_get_pixel_size(layout, &mw, &mh);
    int nh = 0;
    if (!P.note.empty()) {
        font("Sans 10");
        pango_layout_set_text(layout, P.note.c_str(), -1);
        int nw = 0;
        pango_layout_get_pixel_size(layout, &nw, &nh);
    }
    PkLayout l;
    double y = kPkPad;
    const double iconY = y;
    y += 30 + 10;
    const double titleY = y;
    y += 40;
    l.msgY = y;
    l.msgH = mh;
    y += mh + (other ? 20 : 0) + 18;
    l.fieldY = y;
    y += 52;
    if (nh > 0) { y += 8; l.noteY = y; l.noteH = nh; y += nh; }
    y += 18;
    l.btnY = y;
    y += 40 + 20;
    l.h = y;
    l.x = (W - l.w) / 2;
    l.y = (H - l.h) / 2;

    roundedRect(cr, l.x, l.y, l.w, l.h, 28);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);

    drawIcon(cr, "shield-keyhole", l.x + (l.w - 30) / 2, l.y + iconY, 30, app.color("secondary"), true);
    pango_layout_set_width(layout, -1);
    font("Sans Bold 16");
    pango_layout_set_text(layout, "Authentication", -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, l.x + (l.w - tw) / 2.0, l.y + titleY);
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_width(layout, static_cast<int>(inner * PANGO_SCALE));
    font("Sans 11");
    pango_layout_set_text(layout, msg.c_str(), -1);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, l.x + kPkPad, l.y + l.msgY);
    pango_cairo_show_layout(cr, layout);
    if (other) {
        font("Sans Bold 10");
        pango_layout_set_text(layout, ("Authenticating as " + P.user).c_str(), -1);
        setSource(cr, app.color("primary"));
        cairo_move_to(cr, l.x + kPkPad, l.y + l.msgY + mh + 6);
        pango_cairo_show_layout(cr, layout);
    }

    const double fx = l.x + kPkPad, fy = l.y + l.fieldY;
    roundedRect(cr, fx, fy, inner, 52, 26);
    setSource(cr, app.color("surface_container_highest"));
    cairo_fill(cr);
    if (P.busy && !P.prompting) {
        font("Sans 11");
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, "Checking...", -1);
        int cw = 0, ch = 0;
        pango_layout_get_pixel_size(layout, &cw, &ch);
        setSource(cr, app.color("on_surface_variant"), 0.8);
        cairo_move_to(cr, fx + 22, fy + (52 - ch) / 2.0);
        pango_cairo_show_layout(cr, layout);
    } else if (P.text.empty()) {
        font("Sans 11");
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, P.prompt.c_str(), -1);
        int cw = 0, ch = 0;
        pango_layout_get_pixel_size(layout, &cw, &ch);
        setSource(cr, app.color("on_surface_variant"), 0.8);
        cairo_move_to(cr, fx + 22, fy + (52 - ch) / 2.0);
        pango_cairo_show_layout(cr, layout);
    } else if (P.echo) {
        font("Sans 12");
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, P.text.c_str(), -1);
        int cw = 0, ch = 0;
        pango_layout_get_pixel_size(layout, &cw, &ch);
        setSource(cr, app.color("on_surface"));
        cairo_move_to(cr, fx + 22, fy + (52 - ch) / 2.0);
        pango_cairo_show_layout(cr, layout);
    } else {
        size_t count = 0;
        for (unsigned char c : P.text) if ((c & 0xC0) != 0x80) count++;
        setSource(cr, app.color("on_surface"));
        const double maxDots = std::floor((inner - 44) / 14.0);
        for (size_t i = 0; i < count && i < static_cast<size_t>(maxDots); i++) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, fx + 28 + i * 14.0, fy + 26, 4, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    if (!P.note.empty()) {
        font("Sans 10");
        pango_layout_set_width(layout, static_cast<int>(inner * PANGO_SCALE));
        pango_layout_set_text(layout, P.note.c_str(), -1);
        setSource(cr, app.color(P.noteErr ? "error" : "on_surface_variant"));
        cairo_move_to(cr, fx + 6, l.y + l.noteY);
        pango_cairo_show_layout(cr, layout);
    }

    font("Sans Bold 11");
    pango_layout_set_width(layout, -1);
    const double bh = 40, by = l.y + l.btnY;
    auto button = [&](const char* label, double right, bool filled, bool hot, double* out) {
        pango_layout_set_text(layout, label, -1);
        int bw = 0, bth = 0;
        pango_layout_get_pixel_size(layout, &bw, &bth);
        const double w = bw + 36, x = right - w;
        if (filled) { roundedRect(cr, x, by, w, bh, bh / 2); setSource(cr, app.color("primary"), hot ? 0.88 : 1); cairo_fill(cr); }
        else if (hot) { roundedRect(cr, x, by, w, bh, bh / 2); setSource(cr, app.color("primary"), 0.14); cairo_fill(cr); }
        setSource(cr, app.color(filled ? "on_primary" : "primary"));
        cairo_move_to(cr, x + 18, by + (bh - bth) / 2.0);
        pango_cairo_show_layout(cr, layout);
        out[0] = x; out[1] = by; out[2] = w; out[3] = bh;
        return x;
    };
    const double okX = button("OK", l.x + l.w - kPkPad, true, P.hover == 1, P.okR);
    button("Cancel", okX - 8, false, P.hover == 0, P.cancelR);

    g_object_unref(layout);
    if (rev > 0.001) popFadeEnd(cr, rev, W / 2.0, H / 2.0, 0.97);
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, P.surface, P.blur, W, H, [&](cairo_t* m) {  // the whole screen: the scrim is what is blurred
        if (rev > 0.001) cairo_paint(m);
    });
    wl_surface_attach(P.surface, buffer, 0, 0);
    wl_surface_damage_buffer(P.surface, 0, 0, W, H);
    wl_surface_commit(P.surface);
}

int pkButtonAt(const PolkitDlg& P, double x, double y) {
    auto in = [&](const double* r) { return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; };
    return in(P.cancelR) ? 0 : in(P.okR) ? 1 : -1;
}

void polkitMotion(App& app) {
    PolkitDlg& P = app.pk;
    if (!P.open() || popIsOut(P.anim)) return;
    const int h = pkButtonAt(P, app.pointerX, app.pointerY);
    if (h != P.hover) { P.hover = h; drawPolkit(app); updateCursor(app); }
}

void polkitButton(App& app, uint32_t button, bool pressed) {
    PolkitDlg& P = app.pk;
    if (!P.open() || !pressed || button != 0x110 || popIsOut(P.anim)) return;
    const int b = pkButtonAt(P, app.pointerX, app.pointerY);
    if (b == 0) pkFinishRequest(app, false);
    else if (b == 1) pkSubmit(app);
}

bool polkitKey(App& app, uint32_t key) {
    constexpr uint32_t kEsc = 1, kBackspace = 14, kEnter = 28, kKpEnter = 96, kU = 22;
    PolkitDlg& P = app.pk;
    if (popIsOut(P.anim)) return false;
    if (key == kEsc) { pkFinishRequest(app, false); return false; }
    if (!P.prompting || P.busy) return false;
    const bool ctrl = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (key == kEnter || key == kKpEnter) { pkSubmit(app); return false; }
    if (key == kBackspace) {
        if (!P.text.empty()) {
            size_t n = P.text.size() - 1;
            while (n > 0 && (static_cast<unsigned char>(P.text[n]) & 0xC0) == 0x80) n--;  // a whole UTF-8 letter
            P.text.resize(n);
            drawPolkit(app);
        }
        return true;
    }
    if (ctrl && key == kU) { P.text.clear(); drawPolkit(app); return false; }
    if (!ctrl && app.xkbState) {
        char buf[16];
        const int n = xkb_state_key_get_utf8(app.xkbState, key + 8, buf, sizeof buf);
        if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && P.text.size() < 256) {
            P.text.append(buf, n);
            if (!P.note.empty() && P.noteErr) P.note.clear();
            drawPolkit(app);
        }
    }
    return false;
}
