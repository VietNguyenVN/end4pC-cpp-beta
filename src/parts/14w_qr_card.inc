// The QR card: an overlay with the code made from a link or a text (qrencode writes one pixel per module; it is drawn scaled with no smoothing).
// Copy puts a PNG with the quiet zone on the clipboard; Save writes it to ~/Pictures.
constexpr double kQrPad = 24, kQrCode = 272, kQrCodePad = 16, kQrBtn = 44, kQrGap = 8, kQrClose = 32;
constexpr double kQrW = kQrPad * 2 + kQrCode, kQrH = kQrPad + kQrClose + 12 + kQrCode + 12 + 20 + 12 + kQrBtn + kQrPad;

struct QrGeom { Rect closeBtn, code, line, copyBtn, saveBtn; };
QrGeom qrGeom() {
    QrGeom g;
    g.closeBtn = {kQrW - kQrPad - kQrClose, kQrPad, kQrClose, kQrClose};
    g.code = {kQrPad, kQrPad + kQrClose + 12, kQrCode, kQrCode};
    g.line = {kQrPad, g.code.y + kQrCode + 12, kQrCode, 20};
    const double bw = (kQrCode - kQrGap) / 2, by = g.line.y + 20 + 12;
    g.copyBtn = {kQrPad, by, bw, kQrBtn};
    g.saveBtn = {kQrPad + bw + kQrGap, by, bw, kQrBtn};
    return g;
}

bool qrWrite(const std::string& file, const std::string& text, int pixelsPerModule, int margin) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        childSignalReset();
        const int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        const std::string s = std::to_string(pixelsPerModule), m = std::to_string(margin);
        execlp("qrencode", "qrencode", "-s", s.c_str(), "-m", m.c_str(), "-l", "M", "-o", file.c_str(), "--", text.c_str(), nullptr);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

void qrBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kQrBuffer = {qrBufferRelease};

void drawQrCard(App& app) {
    QrState& S = app.qr;
    if (!S.surface || !S.configured) return;
    const QrGeom G = qrGeom();
    const int W = static_cast<int>(kQrW), H = static_cast<int>(std::ceil(kQrH)), stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-qr", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    roundedRect(cr, 0, 0, kQrW, kQrH, 28);
    setSource(cr, app.color("surface_container"), 0.98);
    cairo_fill(cr);
    clkText(cr, L, "QR code", 18, true, kQrPad, G.closeBtn.y + G.closeBtn.h / 2.0, app.color("on_surface"), 1);
    const bool closeHot = S.hover == 0;
    roundedRect(cr, G.closeBtn.x, G.closeBtn.y, G.closeBtn.w, G.closeBtn.h, G.closeBtn.h / 2);
    const Color e = app.color("error"), high = app.color("surface_container_highest");
    setSource(cr, closeHot ? Color{e.r * 0.3 + high.r * 0.7, e.g * 0.3 + high.g * 0.7, e.b * 0.3 + high.b * 0.7} : high, 0.85);
    cairo_fill(cr);
    drawIcon(cr, "close-circle", G.closeBtn.x + (G.closeBtn.w - 20) / 2, G.closeBtn.y + (G.closeBtn.h - 20) / 2, 20, closeHot ? e : app.color("on_surface_variant"));
    roundedRect(cr, G.code.x, G.code.y, G.code.w, G.code.h, 20);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
    if (S.image && S.modules > 0) {
        const double side = kQrCode - 2 * kQrCodePad;
        cairo_save(cr);
        cairo_translate(cr, G.code.x + kQrCodePad, G.code.y + kQrCodePad);
        cairo_scale(cr, side / S.modules, side / S.modules);
        cairo_set_source_surface(cr, S.image, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    std::string shown = S.text;
    for (char& c : shown) if (c == '\n') c = ' ';
    while (shown.size() > 3 && clkTextWidth(L, shown, 13, false) > G.line.w) {
        size_t cut = shown.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) cut--;
        shown.resize(cut);
        if (clkTextWidth(L, shown + "…", 13, false) <= G.line.w) { shown += "…"; break; }
    }
    clkText(cr, L, shown, 13, false, G.line.x, G.line.y + G.line.h / 2.0, app.color("on_surface_variant"), 1);
    const auto pill = [&](const Rect& r, const char* icon, const char* label, bool hot, bool filled) {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        setSource(cr, app.color(filled ? "primary" : "secondary_container"));
        cairo_fill(cr);
        if (hot) { roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2); setSource(cr, app.color(filled ? "on_primary" : "on_secondary_container"), 0.1); cairo_fill(cr); }
        const Color ink = app.color(filled ? "on_primary" : "on_secondary_container");
        const int tw = clkTextWidth(L, label, 14, true);
        const double total = 20 + 8 + tw, x0 = r.x + (r.w - total) / 2;
        drawIcon(cr, icon, x0, r.y + (r.h - 20) / 2, 20, ink);
        clkText(cr, L, label, 14, true, x0 + 28, r.y + r.h / 2.0, ink, 1);
    };
    pill(G.copyBtn, "copy", "Copy", S.hover == 1, true);
    pill(G.saveBtn, "download-minimalistic", "Save", S.hover == 2, false);
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kQrBuffer, nullptr);
    wl_surface_attach(S.surface, buffer, 0, 0);
    wl_surface_damage_buffer(S.surface, 0, 0, W, H);
    wl_surface_commit(S.surface);
}

void closeQrCard(App& app) {
    QrState& S = app.qr;
    if (!S.surface) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    wl_surface_destroy(S.surface);
    S.surface = nullptr;
    S.layer = nullptr;
    S.configured = false;
    S.hover = -1;
    if (S.image) { cairo_surface_destroy(S.image); S.image = nullptr; }
    if (!S.png.empty()) { std::error_code ec; std::filesystem::remove(S.png, ec); S.png.clear(); }
    app.onQr = false;
}

void onQrConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->qr.configured = true;
    drawQrCard(*app);
}
void onQrClosed(void* d, zwlr_layer_surface_v1*) { closeQrCard(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kQrListener = {onQrConfigure, onQrClosed};

void openQrCard(App& app, const std::string& text) {
    QrState& S = app.qr;
    if (text.empty() || !app.compositor || !app.layerShell || !app.shm) return;
    closeQrCard(app);
    std::filesystem::create_directories(capCacheDir());
    const std::string file = capCacheDir() + "/qr-card.png";
    if (!qrWrite(file, text, 1, 0)) { runDetached({"notify-send", "-a", "end4pC", "QR code", "qrencode is not installed or the text is too long"}); return; }
    S.image = cairo_image_surface_create_from_png(file.c_str());
    if (cairo_surface_status(S.image) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(S.image); S.image = nullptr; return; }
    S.modules = cairo_image_surface_get_width(S.image);
    S.png = file;
    S.text = text;
    S.hover = -1;
    const Bar* bar = mainBar(app);
    S.surface = wl_compositor_create_surface(app.compositor);
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-qr");
    zwlr_layer_surface_v1_set_anchor(S.layer, 0);  // (no anchor: centred on the screen)
    zwlr_layer_surface_v1_set_size(S.layer, static_cast<uint32_t>(kQrW), static_cast<uint32_t>(std::ceil(kQrH)));
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(S.layer, &kQrListener, &app);
    wl_surface_commit(S.surface);
}

int qrHit(double x, double y) {
    const QrGeom G = qrGeom();
    return G.closeBtn.contains(x, y) ? 0 : G.copyBtn.contains(x, y) ? 1 : G.saveBtn.contains(x, y) ? 2 : -1;
}

void qrMotion(App& app) {
    const int over = qrHit(app.pointerX, app.pointerY);
    if (over != app.qr.hover) { app.qr.hover = over; drawQrCard(app); }
}

void qrButton(App& app, uint32_t button, bool pressed) {
    if (button != 0x110 || !pressed) return;
    const int hit = qrHit(app.pointerX, app.pointerY);
    if (hit < 0) return;
    const std::string text = app.qr.text;
    if (hit == 0) { closeQrCard(app); return; }
    std::filesystem::create_directories(capCacheDir());
    if (hit == 1) {
        const std::string big = capCacheDir() + "/qr-copy.png";
        if (qrWrite(big, text, 10, 4)) {
            runDetached({"sh", "-c", "wl-copy -t image/png < \"$1\"; rm -f \"$1\"; notify-send -a end4pC 'QR code' 'Image copied'", "sh", big.c_str()});
        }
    } else {
        const std::string dir = homeDir() + "/Pictures";
        std::filesystem::create_directories(dir);
        const std::string out = dir + "/qr-" + std::to_string(std::time(nullptr)) + ".png";
        if (qrWrite(out, text, 10, 4)) runDetached({"notify-send", "-a", "end4pC", "QR code", ("Saved to " + out).c_str()});
    }
    closeQrCard(app);
}
