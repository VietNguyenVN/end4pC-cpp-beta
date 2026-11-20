constexpr double kCfW = 312, kCfPad = 24, kCfIcon = 56, kCfBtnH = 40, kCfBtnGap = 8, kCfBodyW = kCfW - 2 * kCfPad;

struct ConfirmGeom { Rect cancel, ok; double bodyY, h; };
ConfirmGeom confirmGeom(int bodyLines) {
    ConfirmGeom g;
    g.bodyY = kCfPad + kCfIcon + 16 + 26 + 12;
    const double by = g.bodyY + bodyLines * 20 + 24;
    g.h = by + kCfBtnH + kCfPad - 4;
    const double bw = 92;
    g.ok = {kCfW - kCfPad - bw, by, bw, kCfBtnH};
    g.cancel = {g.ok.x - kCfBtnGap - bw, by, bw, kCfBtnH};
    return g;
}

std::vector<std::string> cacheDirsToClear() { return {"thumbs", "online-thumbs", "preset-thumbs", "lyrics", "clip", "tools", "art"}; }
std::vector<std::string> cacheFilesToClear() { return {"capture-copy.png", "weather.json"}; }

uintmax_t cacheClearableBytes() {
    uintmax_t total = 0;
    std::error_code ec;
    const std::string root = capCacheDir();
    for (const std::string& d : cacheDirsToClear()) {
        for (std::filesystem::recursive_directory_iterator it(root + "/" + d, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2)) total += it->file_size(e2);
        }
        ec.clear();
    }
    for (const std::string& f : cacheFilesToClear()) {
        const auto n = std::filesystem::file_size(root + "/" + f, ec);
        if (!ec) total += n;
        ec.clear();
    }
    return total;
}

std::string confirmSizeText(uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", bytes / 1073741824.0);
    else if (bytes >= 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", bytes / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%.0f KB", bytes / 1024.0);
    return buf;
}

PangoLayout* confirmBodyLayout(cairo_t* cr, const std::string& body) {
    PangoLayout* L = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, 14 * PANGO_SCALE);
    pango_layout_set_font_description(L, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(L, static_cast<int>(kCfBodyW) * PANGO_SCALE);
    pango_layout_set_wrap(L, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_line_spacing(L, 1.25f);
    pango_layout_set_text(L, body.c_str(), static_cast<int>(body.size()));
    return L;
}

int confirmBodyLines(const std::string& body) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(s);
    PangoLayout* L = confirmBodyLayout(cr, body);
    const int n = pango_layout_get_line_count(L);
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return n;
}

std::string confirmBody(const ConfirmState& S) {
    return std::string(i18n::tr("Thumbnails, lyrics, clipboard images and other temporary files will be deleted. The shell rebuilds them when needed.")) + "\n" + std::string(i18n::tr("It will free")) + " " + confirmSizeText(S.bytes) + ".";
}

void confirmBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kConfirmBuffer = {confirmBufferRelease};

void drawConfirmCard(App& app) {
    ConfirmState& S = app.confirm;
    if (!S.surface || !S.configured) return;
    const std::string body = confirmBody(S);
    const ConfirmGeom G = confirmGeom(confirmBodyLines(body));
    const int W = static_cast<int>(kCfW), H = static_cast<int>(std::ceil(G.h));
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, S.scale.factor(), "end4pC-confirm")) return;
    cairo_surface_t* surf = hf.surface;
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    roundedRect(cr, 0, 0, kCfW, G.h, 28);
    setSource(cr, app.color("surface_container_high"), 0.99);
    cairo_fill(cr);
    const double cx = kCfW / 2;
    cairo_new_path(cr);
    cairo_arc(cr, cx, kCfPad + kCfIcon / 2, kCfIcon / 2, 0, 2 * M_PI);
    setSource(cr, app.color("error_container"));
    cairo_fill(cr);
    drawIcon(cr, "trash-bin-minimalistic", cx - 15, kCfPad + kCfIcon / 2 - 15, 30, app.color("on_error_container"));
    clkText(cr, L, i18n::tr("Clear cache?"), 22, false, kCfPad, kCfPad + kCfIcon + 16 + 13, app.color("on_surface"), 1, kCfBodyW, 1);
    PangoLayout* B = confirmBodyLayout(cr, body);
    pango_layout_set_alignment(B, PANGO_ALIGN_LEFT);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, kCfPad, G.bodyY);
    pango_cairo_show_layout(cr, B);
    g_object_unref(B);
    const auto button = [&](const Rect& r, const char* label, bool hot, bool danger) {
        if (hot) {
            roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            setSource(cr, app.color(danger ? "error" : "primary"), 0.12);
            cairo_fill(cr);
        }
        const Color ink = app.color(danger ? "error" : "primary");
        const int tw = clkTextWidth(L, label, 14, true);
        clkText(cr, L, label, 14, true, r.x + (r.w - tw) / 2.0, r.y + r.h / 2.0, ink, 1);
    };
    button(G.cancel, i18n::tr("Cancel"), S.hover == 0, false);
    button(G.ok, i18n::tr("Clear"), S.hover == 1, true);
    g_object_unref(L);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kConfirmBuffer, nullptr);
    hidpi::present(S.surface, buffer, hf, S.scale);
}

void closeConfirmCard(App& app) {
    ConfirmState& S = app.confirm;
    if (!S.surface) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    hidpi::detach(S.scale);
    wl_surface_destroy(S.surface);
    S.surface = nullptr;
    S.layer = nullptr;
    S.configured = false;
    S.hover = -1;
    app.onConfirm = false;
}

void onConfirmConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->confirm.configured = true;
    drawConfirmCard(*app);
}
void onConfirmClosed(void* d, zwlr_layer_surface_v1*) { closeConfirmCard(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kConfirmListener = {onConfirmConfigure, onConfirmClosed};

void openClearCacheConfirm(App& app) {
    ConfirmState& S = app.confirm;
    if (!app.compositor || !app.layerShell || !app.shm) return;
    closeConfirmCard(app);
    S.bytes = cacheClearableBytes();
    S.hover = -1;
    const ConfirmGeom G = confirmGeom(confirmBodyLines(confirmBody(S)));
    const Bar* bar = mainBar(app);
    S.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, S.scale, S.surface, [&app] { drawConfirmCard(app); });
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-confirm");
    zwlr_layer_surface_v1_set_anchor(S.layer, 0);
    zwlr_layer_surface_v1_set_size(S.layer, static_cast<uint32_t>(kCfW), static_cast<uint32_t>(std::ceil(G.h)));
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(S.layer, &kConfirmListener, &app);
    wl_surface_commit(S.surface);
}

void clearShellCache() {
    std::error_code ec;
    const std::string root = capCacheDir();
    const uintmax_t before = cacheClearableBytes();
    for (const std::string& d : cacheDirsToClear()) std::filesystem::remove_all(root + "/" + d, ec);
    for (const std::string& f : cacheFilesToClear()) std::filesystem::remove(root + "/" + f, ec);
    const std::string freed = confirmSizeText(before);
    runDetached({"notify-send", "-a", "end4pC", "-i", "edit-clear-all", i18n::tr("Cache cleared"), (std::string(i18n::tr("Freed")) + " " + freed).c_str()});
}

int confirmHit(App& app, double x, double y) {
    const ConfirmGeom G = confirmGeom(confirmBodyLines(confirmBody(app.confirm)));
    return G.cancel.contains(x, y) ? 0 : G.ok.contains(x, y) ? 1 : -1;
}

void confirmMotion(App& app) {
    const int over = confirmHit(app, app.pointerX, app.pointerY);
    if (over != app.confirm.hover) { app.confirm.hover = over; drawConfirmCard(app); }
}

void confirmButton(App& app, uint32_t button, bool pressed) {
    if (button != 0x110 || !pressed) return;
    const int hit = confirmHit(app, app.pointerX, app.pointerY);
    if (hit < 0) return;
    closeConfirmCard(app);
    if (hit == 1) clearShellCache();
}
