
struct IdSurf {
    App* app = nullptr;
    wl_surface* surface = nullptr;
    hidpi::SurfaceScale scale;
    zwlr_layer_surface_v1* layer = nullptr;
    std::string num, name;
};
std::vector<std::unique_ptr<IdSurf>> g_idSurfs;
std::chrono::steady_clock::time_point g_idUntil{};
constexpr int kIdSize = 280;
constexpr double kIdSeconds = 3.0;

void drawIdentify(IdSurf& s) {
    App& app = *s.app;
    const int W = kIdSize, H = kIdSize;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, s.scale.factor(), "end4pC-identify")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    roundedRect(cr, 0, 0, W, H, 56);
    setSource(cr, app.color("primary_container"), 0.96);
    cairo_fill(cr);
    auto line = [&](const std::string& t, double px, bool bold, double y, const Color& c, bool draw = true) {
        PangoLayout* l = pango_cairo_create_layout(cr);
        PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
        pango_layout_set_text(l, t.c_str(), -1);
        pango_layout_set_width(l, (W - 40) * PANGO_SCALE);
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(l, &tw, &th);
        if (draw) {
            setSource(cr, c);
            cairo_move_to(cr, 20, y);
            pango_cairo_show_layout(cr, l);
        }
        g_object_unref(l);
        return th;
    };
    const Color ink = app.color("on_primary_container");
    const int nh = line(s.num, 150, true, 0, ink, false);
    const double top = (H - nh - 30) / 2.0;
    line(s.num, 150, true, top, ink);
    line(s.name, 22, false, top + nh + 2, ink);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    hidpi::present(s.surface, buffer, hf, s.scale);
}

void onIdentifyConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    drawIdentify(*static_cast<IdSurf*>(d));
}
void onIdentifyClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kIdentifyListener = {onIdentifyConfigure, onIdentifyClosed};

void closeIdentify() {
    for (auto& s : g_idSurfs) {
        if (s->layer) zwlr_layer_surface_v1_destroy(s->layer);
        hidpi::detach(s->scale);
        if (s->surface) wl_surface_destroy(s->surface);
    }
    g_idSurfs.clear();
}

void openIdentify(App& app, const std::string& args) {
    closeIdentify();
    if (!app.layerShell || !app.compositor || !app.shm) return;
    std::map<std::string, std::string> wanted;
    for (size_t p = 0; p < args.size();) {
        size_t e = args.find(' ', p);
        if (e == std::string::npos) e = args.size();
        const std::string item = args.substr(p, e - p);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && eq > 0 && eq + 1 < item.size()) wanted[item.substr(0, eq)] = item.substr(eq + 1);
        p = e + 1;
    }
    int index = 0;
    for (auto& o : app.outputs) {
        index++;
        std::string num = std::to_string(index);
        if (!wanted.empty()) {
            const auto it = wanted.find(o->connector);
            if (it == wanted.end()) continue;
            num = it->second.substr(0, 3);
        }
        auto s = std::make_unique<IdSurf>();
        s->app = &app;
        s->num = num;
        s->name = o->connector;
        s->surface = wl_compositor_create_surface(app.compositor);
        attachScale(app, s->scale, s->surface, [p = s.get()] { drawIdentify(*p); });
        s->layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, s->surface, o->output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-identify");
        zwlr_layer_surface_v1_set_size(s->layer, kIdSize, kIdSize);
        zwlr_layer_surface_v1_set_anchor(s->layer, 0);
        zwlr_layer_surface_v1_set_exclusive_zone(s->layer, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(s->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_add_listener(s->layer, &kIdentifyListener, s.get());
        wl_region* none = wl_compositor_create_region(app.compositor);
        wl_surface_set_input_region(s->surface, none);
        wl_region_destroy(none);
        wl_surface_commit(s->surface);
        g_idSurfs.push_back(std::move(s));
    }
    if (!g_idSurfs.empty()) g_idUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long>(kIdSeconds * 1000));
    wl_display_flush(app.display);
}

void identifyTick(std::chrono::steady_clock::time_point now, int& timeoutMs) {
    if (g_idSurfs.empty()) return;
    if (now >= g_idUntil) { closeIdentify(); return; }
    const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(g_idUntil - now).count()) + 5;
    timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
}
