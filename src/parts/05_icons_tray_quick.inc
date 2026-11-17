// Icons (Solar SVG), the system tray (StatusNotifierItem) and the quick actions.
// Part of main.cpp: included there, in order, inside its anonymous namespace (what comes first is used below).

std::string assetsDir() {
    static const std::string dir = [] {
        if (const char* e = std::getenv("END4PC_ASSETS")) return std::string(e);
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = 0;
            std::string p = exe;
            for (int i = 0; i < 2; i++) p = p.substr(0, p.rfind('/'));
            if (access((p + "/assets").c_str(), R_OK) == 0) return p + "/assets";
        }
        return homeDir() + "/.config/end4pC/assets";
    }();
    return dir;
}

const std::map<std::string, std::string>& iconMap() {
    static const auto m = [] {
        std::map<std::string, std::string> out;
        std::ifstream f(assetsDir() + "/icon-map.txt");
        std::string line;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
            out[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return out;
    }();
    return m;
}

std::vector<std::string> iconCandidates(const std::string& name, bool filled) {
    auto it = iconMap().find(name);
    std::string entry = it != iconMap().end() ? it->second : name;
    if (it == iconMap().end()) std::replace(entry.begin(), entry.end(), '_', '-');
    std::string first = filled ? "bold" : g_iconStyle;
    if (size_t bar = entry.find('|'); bar != std::string::npos) entry.resize(bar);
    std::vector<std::string> out;
    for (const char* style : {first.c_str(), "outline", "bold"}) {
        std::string p = assetsDir() + "/icons/" + style + "/" + entry + ".svg";
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}

cairo_surface_t* iconMask(const std::string& name, bool filled, int size) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = name + (filled ? "|f|" : "|" + g_iconStyle + "|") + std::to_string(size) + (matsym::enabled() ? "|m" : "");
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    cairo_surface_t* mask = matsym::makeMask(name, filled, size);
    for (const auto& path : iconCandidates(matsym::baseName(name), filled)) {
        if (mask) break;
        if (access(path.c_str(), R_OK) != 0) continue;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr);
        if (!h) continue;
        mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(mask);
        RsvgRectangle vp = {0, 0, double(size), double(size)};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
        break;
    }
    if (!mask) std::fprintf(stderr, "icon not found: %s\n", name.c_str());
    cache[key] = mask;
    return mask;
}

void drawIcon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, bool filled = false) {
    const int scaled = g_iconScale == 100 ? size : std::max(8, static_cast<int>(std::lround(size * g_iconScale / 100.0)));  // the icon keeps its centre
    x += (size - scaled) / 2.0;
    y += (size - scaled) / 2.0;
    cairo_surface_t* mask = iconMask(name, filled, scaled);
    if (!mask) return;
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 1.0);
    cairo_mask_surface(cr, mask, x, y);
}

double iconInkInset(const std::string& name, int size, bool right) {
    static std::map<std::string, std::pair<double, double>> cache;
    const std::string key = name + "@" + std::to_string(size);
    auto hit = cache.find(key);
    if (hit == cache.end()) {
        double left = 0, rightInset = 0;
        if (cairo_surface_t* mask = iconMask(name, false, size)) {
            cairo_surface_flush(mask);
            const int w = cairo_image_surface_get_width(mask), h = cairo_image_surface_get_height(mask), stride = cairo_image_surface_get_stride(mask);
            const unsigned char* px = cairo_image_surface_get_data(mask);
            int first = -1, last = -1;
            const bool argb = cairo_image_surface_get_format(mask) == CAIRO_FORMAT_ARGB32;
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++)
                    if ((argb ? px[y * stride + x * 4 + 3] : px[y * stride + x]) > 48) { if (first < 0) first = x; last = x; break; }
            if (first >= 0) { left = first; rightInset = w - 1 - last; }
        }
        hit = cache.emplace(key, std::make_pair(left, rightInset)).first;
    }
    return right ? hit->second.second : hit->second.first;
}

constexpr int kTrayIcon = 22;

std::string trayIconFile(const std::string& name, const std::string& themePath) {
    if (name.empty()) return "";
    if (name[0] == '/') return access(name.c_str(), R_OK) == 0 ? name : "";
    std::vector<std::string> roots;
    if (!themePath.empty()) roots.push_back(themePath);
    roots.push_back(homeDir() + "/.local/share/icons/hicolor");
    roots.push_back("/usr/share/icons/hicolor");
    const char* sizes[] = {"scalable", "48x48", "64x64", "32x32", "24x24", "22x22", "128x128", "256x256", "16x16"};
    const char* cats[] = {"apps", "status", "panel", "devices", "actions", "mimetypes", "categories"};
    for (const char* ext : {".svg", ".png"}) {
        for (const auto& root : roots) {
            const std::string flat = root + "/" + name + ext;
            if (access(flat.c_str(), R_OK) == 0) return flat;
            for (const char* sz : sizes)
                for (const char* cat : cats) {
                    const std::string f = root + "/" + sz + "/" + cat + "/" + name + ext;
                    if (access(f.c_str(), R_OK) == 0) return f;
                }
        }
        const std::string pix = std::string("/usr/share/pixmaps/") + name + ext;
        if (access(pix.c_str(), R_OK) == 0) return pix;
    }
    return "";
}

cairo_surface_t* trayMakeSurface(const TrayIcon& t) {
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kTrayIcon, kTrayIcon);
    cairo_t* cr = mkcr(out);
    bool drawn = false;
    const std::string file = trayIconFile(t.iconName, t.themePath);
    if (!file.empty()) {
        if (GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_size(file.c_str(), kTrayIcon, kTrayIcon, nullptr)) {
            cairo_surface_t* im = surfaceFromPixbuf(pb);
            cairo_set_source_surface(cr, im, (kTrayIcon - gdk_pixbuf_get_width(pb)) / 2.0, (kTrayIcon - gdk_pixbuf_get_height(pb)) / 2.0);
            cairo_paint(cr);
            cairo_surface_destroy(im);
            g_object_unref(pb);
            drawn = true;
        }
    }
    if (!drawn && t.pw > 0 && t.ph > 0 && t.px.size() >= static_cast<size_t>(t.pw) * t.ph * 4) {
        cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, t.pw, t.ph);
        unsigned char* d = cairo_image_surface_get_data(src);
        const int stride = cairo_image_surface_get_stride(src);
        for (int y = 0; y < t.ph; y++)
            for (int x = 0; x < t.pw; x++) {
                const unsigned char* q = &t.px[(static_cast<size_t>(y) * t.pw + x) * 4];
                const unsigned a = q[0];
                uint32_t* px = reinterpret_cast<uint32_t*>(d + y * stride) + x;
                *px = (a << 24) | (((q[1] * a + 127) / 255) << 16) | (((q[2] * a + 127) / 255) << 8) | ((q[3] * a + 127) / 255);
            }
        cairo_surface_mark_dirty(src);
        const double sc = std::min(double(kTrayIcon) / t.pw, double(kTrayIcon) / t.ph);
        cairo_translate(cr, (kTrayIcon - t.pw * sc) / 2.0, (kTrayIcon - t.ph * sc) / 2.0);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_surface_destroy(src);
        drawn = true;
    }
    if (!drawn) {
        cairo_arc(cr, kTrayIcon / 2.0, kTrayIcon / 2.0, kTrayIcon / 2.0 - 1, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.5);
        cairo_fill(cr);
    }
    cairo_destroy(cr);
    return out;
}

// Reads every registered item again (GetAll on org.kde.StatusNotifierItem). Called from the main loop, never from
// inside a bus callback; a stuck app costs at most the bus's call timeout.
void refreshTray(App& app) {
    for (auto& t : app.tray) if (t.surf) cairo_surface_destroy(t.surf);
    app.tray.clear();
    if (!app.bus) return;
    for (int i = 0; i < traywatch_count(); i++) {
        const char* entry = traywatch_item(i);
        if (!entry) continue;
        const char* bar = std::strchr(entry, '|');
        if (!bar) continue;
        TrayIcon t;
        t.bus.assign(entry, bar - entry);
        t.path = bar + 1;
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* rep = nullptr;
        int r = sd_bus_call_method(app.bus, t.bus.c_str(), t.path.c_str(), "org.freedesktop.DBus.Properties", "GetAll", &err, &rep, "s", "org.kde.StatusNotifierItem");
        if (r < 0) { sd_bus_error_free(&err); continue; }
        if (sd_bus_message_enter_container(rep, 'a', "{sv}") >= 0) {
            while (sd_bus_message_enter_container(rep, 'e', "sv") > 0) {
                const char* key = nullptr;
                sd_bus_message_read(rep, "s", &key);
                char type = 0;
                const char* contents = nullptr;
                sd_bus_message_peek_type(rep, &type, &contents);
                sd_bus_message_enter_container(rep, 'v', contents);
                const std::string k = key ? key : "", c = contents ? contents : "";
                auto str = [&](std::string& out) { const char* v = nullptr; if (sd_bus_message_read(rep, "s", &v) >= 0 && v) out = v; };
                if (k == "Id" && c == "s") str(t.id);
                else if (k == "Title" && c == "s") str(t.title);
                else if (k == "Status" && c == "s") str(t.status);
                else if (k == "IconName" && c == "s") str(t.iconName);
                else if (k == "IconThemePath" && c == "s") str(t.themePath);
                else if (k == "Menu" && c == "o") { const char* v = nullptr; if (sd_bus_message_read(rep, "o", &v) >= 0 && v) t.menu = v; }
                else if (k == "ItemIsMenu" && c == "b") { int b = 0; sd_bus_message_read(rep, "b", &b); t.isMenu = b != 0; }
                else if (k == "IconPixmap" && c == "a(iiay)") {
                    sd_bus_message_enter_container(rep, 'a', "(iiay)");
                    while (sd_bus_message_enter_container(rep, 'r', "iiay") > 0) {
                        int w = 0, h = 0;
                        sd_bus_message_read(rep, "ii", &w, &h);
                        const void* data = nullptr;
                        size_t len = 0;
                        sd_bus_message_read_array(rep, 'y', &data, &len);
                        if (w > 0 && h > 0 && len >= static_cast<size_t>(w) * h * 4 && w * h > t.pw * t.ph) {
                            t.pw = w; t.ph = h;
                            t.px.assign(static_cast<const unsigned char*>(data), static_cast<const unsigned char*>(data) + static_cast<size_t>(w) * h * 4);
                        }
                        sd_bus_message_exit_container(rep);
                    }
                    sd_bus_message_exit_container(rep);
                } else sd_bus_message_skip(rep, contents);
                sd_bus_message_exit_container(rep);
                sd_bus_message_exit_container(rep);
            }
        }
        sd_bus_message_unref(rep);
        if (t.status == "Passive") continue;
        t.surf = trayMakeSurface(t);
        app.tray.push_back(std::move(t));
    }
    if (app.tray.empty()) app.trayOpen = false;
}

void trayChanged(void* d) { static_cast<App*>(d)->trayDirty = true; }

bool trayFocusWindow(App& app, const TrayIcon& t);

void trayClick(App& app, int idx, bool right) {
    if (idx < 0 || idx >= static_cast<int>(app.tray.size()) || !app.bus) return;
    const TrayIcon& t = app.tray[idx];
    if ((right || t.isMenu) && !t.menu.empty() && t.menu != "/NO_DBUSMENU") {
        openTrayMenu(app, idx);
        if (app.tmenu.open()) return;
    }
    if (!right && !t.isMenu && trayFocusWindow(app, t)) return;
    const char* method = right || t.isMenu ? "ContextMenu" : "Activate";
    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(app.bus, &m, t.bus.c_str(), t.path.c_str(), "org.kde.StatusNotifierItem", method) < 0) return;
    sd_bus_message_append(m, "ii", static_cast<int>(app.pointerX), static_cast<int>(app.pointerY));
    sd_bus_message_set_expect_reply(m, 0);
    sd_bus_send(app.bus, m, nullptr);
    sd_bus_message_unref(m);
    sd_bus_flush(app.bus);
}

void applyConfig(App& app);
bool refreshWsApps(App& app);
void drawAll(App& app);

Color styleFrame(cairo_t* cr, const App& app, const std::string& style, double x, double y, double w, double h, bool hot, bool side = false) {
    const double r = side ? 16 : h / 2;
    auto fill = [&](double rr, const Color& c, double a = 1) { roundedRect(cr, x, y, w, h, rr); setSource(cr, c, a); cairo_fill(cr); };
    if (style == "m3soft" || style == "m3filled") {
        const bool filled = style == "m3filled";
        fill(r, app.color(filled ? "primary" : "secondary_container"));
        if (hot) fill(r, app.color(filled ? "on_primary" : "on_secondary_container"), filled ? 0.1 : 0.08);
        return app.color(filled ? "on_primary" : "on_secondary_container");
    }
    if (style == "pill") { fill(r, app.color(hot ? "primary_container" : "surface_container_high")); return app.color(hot ? "on_primary_container" : "on_surface"); }
    if (style == "tonal") { fill(r, app.color(hot ? "secondary" : "secondary_container")); return app.color(hot ? "on_secondary" : "on_secondary_container"); }
    if (style == "badge") { fill(r, app.color(hot ? "primary_container" : "primary")); return app.color(hot ? "on_primary_container" : "on_primary"); }
    if (style == "square") { fill(std::min(10.0, h / 3), app.color(hot ? "primary_container" : "surface_container_highest")); return app.color(hot ? "on_primary_container" : "on_surface"); }
    if (style == "outline") {
        roundedRect(cr, x + 0.75, y + 0.75, w - 1.5, h - 1.5, std::max(0.0, r - 0.75));
        if (hot) { setSource(cr, app.color("primary_container"), 0.6); cairo_fill_preserve(cr); }
        setSource(cr, app.color("primary"), 0.9);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        return app.color("on_surface");
    }
    if (hot) fill(r, app.color("surface_container_high"));
    return app.color("on_surface");
}

Color roundButtonFrame(cairo_t* cr, const App& app, const std::string& style, double x, double y, double w, double h, bool hot) {
    if (style != "pill") return styleFrame(cr, app, style, x, y, w, h, hot);
    roundedRect(cr, x, y, w, h, h / 2);
    setSource(cr, app.color("secondary_container"));
    cairo_fill(cr);
    if (hot) {
        roundedRect(cr, x, y, w, h, h / 2);
        setSource(cr, app.color("on_secondary_container"), 0.08);
        cairo_fill(cr);
    }
    return app.color("on_secondary_container");
}

inline const char* const kM3IconShape = "Cookie9Sided";
std::string wsNumberText(const App& app, int id) {
    static const char* const kHan[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九", "十", "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十"};
    static const char* const kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX"};
    if (id >= 1 && id <= 20) {
        if (app.cfg.workspaceNumbers == "han") return kHan[id - 1];
        if (app.cfg.workspaceNumbers == "roman") return kRoman[id - 1];
    }
    return std::to_string(id);
}
void setWsNumber(PangoLayout* layout, PangoFontDescription* base, const std::string& text) {
    PangoFontDescription* f = pango_font_description_copy(base);
    const size_t n = text.size();
    const double shrink = n >= 4 ? 0.7 : n == 3 ? 0.85 : 1.0;
    if (shrink < 1) pango_font_description_set_size(f, static_cast<gint>(pango_font_description_get_size(base) * shrink));
    pango_layout_set_font_description(layout, f);
    pango_font_description_free(f);
    pango_layout_set_text(layout, text.c_str(), -1);
}

Color m3Circle(cairo_t* cr, const App& app, double x, double y, double box, bool hot, const char* fill, const char* hover, const char* ink) {
    for (const bool layer : {false, true}) {
        if (layer && !hot) break;
        shapePath(cr, kM3IconShape, x, y, box);
        if (layer) setSource(cr, app.color(hover), 0.1); else setSource(cr, app.color(fill));
        cairo_fill(cr);
    }
    return app.color(ink);
}

Color m3ShapedCircle(cairo_t* cr, const App& app, double x, double y, double box, bool hot, const char* circle, const char* shape, const char* hover, const char* shapeName) {
    roundedRect(cr, x, y, box, box, box / 2);
    setSource(cr, app.color(circle));
    cairo_fill(cr);
    shapePath(cr, shapeName, x + 2, y + 2, box - 4);
    setSource(cr, app.color(shape));
    cairo_fill(cr);
    if (hot) {
        shapePath(cr, shapeName, x + 2, y + 2, box - 4);
        setSource(cr, app.color(hover), 0.12);
        cairo_fill(cr);
    }
    return app.color(circle);
}

std::string kbdCodeText(const App& app) {
    if (app.kbd.list.empty()) return "";
    std::string c = app.kbd.list[std::clamp(app.kbd.active, 0, static_cast<int>(app.kbd.list.size()) - 1)].code;
    for (char& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return c;
}
void awinIcon(cairo_t* cr, const App& app, double x, double y, double size, const Color& color) {
    if (!app.awin.icon) { drawIcon(cr, "window-frame", x, y, size, color); return; }
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size / cairo_image_surface_get_width(app.awin.icon), size / cairo_image_surface_get_width(app.awin.icon));
    cairo_set_source_surface(cr, app.awin.icon, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}
void awinPaintM3(cairo_t* cr, const App& app, PangoLayout* layout, double x, double y, double w, double h) {
    PangoFontDescription* clsFont = pango_font_description_from_string("Sans 9");
    PangoFontDescription* ttlFont = pango_font_description_from_string("Sans 11");
    pango_layout_set_width(layout, static_cast<int>((w - 2 * kFrameGutter) * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_font_description(layout, clsFont);
    pango_layout_set_text(layout, app.awin.cls.c_str(), -1);
    int cw = 0, ch = 0;
    pango_layout_get_pixel_size(layout, &cw, &ch);
    pango_layout_set_font_description(layout, ttlFont);
    pango_layout_set_text(layout, app.awin.title.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double top = y + (h - (ch + th - 4)) / 2.0;
    pango_layout_set_font_description(layout, clsFont);
    pango_layout_set_text(layout, app.awin.cls.c_str(), -1);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, x + kFrameGutter, top);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_font_description(layout, ttlFont);
    pango_layout_set_text(layout, app.awin.title.c_str(), -1);
    setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, x + kFrameGutter, top + ch - 4);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_font_description_free(clsFont);
    pango_font_description_free(ttlFont);
}
void sideStateLayer(cairo_t* cr, const App& app, double x, double y, double w, double h, bool lit) {
    if (!lit) return;
    roundedRect(cr, x, y, w, h, 16);
    setSource(cr, app.color("on_surface"), 0.08);
    cairo_fill(cr);
}
void awinPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double y, double w, double h, bool side, bool hot = false) {
    const std::string style = widgetStyle(app, "activewindow");
    if (side) {
        const bool m3 = style == "m3";
        if (m3) sideStateLayer(cr, app, x, y, w, h, hot);
        const Color c = m3 ? app.color("on_surface") : styleFrame(cr, app, style, x, y, w, h, false, true);
        const Color iconInk = genericFrame(style) || m3 ? c : app.color("on_surface_variant");
        if (app.awin.icon) awinIcon(cr, app, x + (w - 22) / 2.0, y + (h - 22) / 2.0, 22, iconInk);
        else drawIcon(cr, "window-frame", x + (w - 20) / 2.0, y + (h - 20) / 2.0, 20, iconInk);
        return;
    }
    if (style == "m3") { awinPaintM3(cr, app, layout, x, y, w, h); return; }
    if (style == "chip") {
        roundedRect(cr, x, y, w, h, h / 2);
        setSource(cr, app.color("surface_container_highest"));
        cairo_fill(cr);
        awinIcon(cr, app, x + 4, y + (h - 18) / 2.0, 18, app.color("on_surface"));
        PangoFontDescription* chipFont = pango_font_description_from_string("Sans 9");
        pango_layout_set_font_description(layout, chipFont);
        pango_layout_set_text(layout, app.awin.cls.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface"));
        cairo_move_to(cr, x + 4 + 18 + 4, y + (h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(chipFont);
        return;
    }
    const Color ink = styleFrame(cr, app, style, x, y, w, h, false);
    const double gutter = genericFrame(style) ? kFrameGutter : 0;
    const double textX = x + gutter + 6, textW = w - 2 * gutter - 12;
    const Color dim = genericFrame(style) ? ink : app.color("on_surface_variant");
    pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    PangoFontDescription* small = pango_font_description_from_string("Sans 8");
    PangoFontDescription* big = pango_font_description_from_string("Sans 10");
    if (style == "title") {
        pango_layout_set_font_description(layout, big);
        pango_layout_set_text(layout, app.awin.title.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, ink);
        cairo_move_to(cr, textX, y + (h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    } else {
        pango_layout_set_font_description(layout, small);
        pango_layout_set_text(layout, app.awin.cls.c_str(), -1);
        int tw = 0, ah = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &ah);
        pango_layout_set_font_description(layout, big);
        pango_layout_set_text(layout, app.awin.title.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        const double top = y + (h - (ah + th - 4)) / 2.0;
        pango_layout_set_font_description(layout, small);
        pango_layout_set_text(layout, app.awin.cls.c_str(), -1);
        setSource(cr, dim, 0.85);
        cairo_move_to(cr, textX, top);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_font_description(layout, big);
        pango_layout_set_text(layout, app.awin.title.c_str(), -1);
        setSource(cr, ink);
        cairo_move_to(cr, textX, top + ah - 4);
        pango_cairo_show_layout(cr, layout);
    }
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_font_description_free(small);
    pango_font_description_free(big);
}

void kbdPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double y, double w, double h, bool side, bool hot) {
    const bool lit = hot || (app.clockP.kind == 5 && app.clockP.open());
    const std::string style = widgetStyle(app, "keyboard");
    if (style == "icon") {
        const Color iconInk = styleFrame(cr, app, "plain", x, y, w, h, lit, side);
        drawIcon(cr, "keyboard", x + (w - 18) / 2.0, y + (h - 18) / 2.0, 18, iconInk);
        return;
    }
    if (style == "m3") {
        if (side) sideStateLayer(cr, app, x, y, w, h, lit);
        const auto& layouts = app.kbd.list;
        const std::string raw = layouts[std::clamp(app.kbd.active, 0, static_cast<int>(layouts.size()) - 1)].code;
        if (!side) {  // the pill of the updates widget: the keyboard on the disc, the layout beside it
            const double cy = y + h / 2.0;
            roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
            setSource(cr, app.color("primary_container"));
            cairo_fill(cr);
            if (lit) {
                roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
                setSource(cr, app.color("on_primary_container"), 0.08);
                cairo_fill(cr);
            }
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + kM3Inset + kM3Disc / 2.0, cy, kM3Disc / 2.0, 0, 2 * M_PI);
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
            constexpr double kDiscIcon = 16;
            drawIcon(cr, "keyboard", x + kM3Inset + (kM3Disc - kDiscIcon) / 2.0, cy - kDiscIcon / 2.0, kDiscIcon, app.color("on_primary"));
            PangoFontDescription* bf = pango_font_description_from_string("Sans");
            pango_font_description_set_absolute_size(bf, 15 * PANGO_SCALE);
            pango_layout_set_font_description(layout, bf);
            pango_layout_set_text(layout, raw.c_str(), -1);
            int bw = 0, bh = 0;
            pango_layout_get_pixel_size(layout, &bw, &bh);
            setSource(cr, app.color("on_primary_container"));
            cairo_move_to(cr, x + kM3Inset + kM3Disc + 6, cy - bh / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_font_description_free(bf);
            return;
        }
        PangoFontDescription* m3f = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, m3f);
        pango_layout_set_text(layout, raw.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface"));
        cairo_move_to(cr, x + (w - tw) / 2.0, y + (h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(m3f);
        return;
    }
    const std::string code = kbdCodeText(app);
    PangoFontDescription* f = pango_font_description_from_string(side && code.size() > 3 ? "Sans Bold 9" : side ? "Sans Bold 11" : "Sans Bold 10");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, code.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    Color ink = app.color("on_surface");
    if (style == "code") {
        roundedRect(cr, x, y, w, h, 12);
        setSource(cr, app.color(lit ? "primary_container" : "surface_container_highest"));
        cairo_fill(cr);
        ink = app.color(lit ? "on_primary_container" : "on_surface");
        cairo_move_to(cr, x + (w - tw) / 2.0, y + (h - th) / 2.0);
    } else {
        ink = styleFrame(cr, app, style, x, y, w, h, lit, side);
        if (side) {
            cairo_move_to(cr, x + (w - tw) / 2.0, y + (h - th) / 2.0);
        } else {
            const double gutter = genericFrame(style) ? kFrameGutter : 0;
            const Color iconInk = style == "plain" ? app.color("on_surface_variant") : ink;
            drawIcon(cr, "keyboard", x + gutter + 4, y + (h - 18) / 2.0, 18, iconInk);
            cairo_move_to(cr, x + gutter + 4 + 18 + 4, y + (h - th) / 2.0);
        }
    }
    setSource(cr, ink);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(f);
}

constexpr double kM3TextNudge = 1;
constexpr double kM3LineOverlap = 5;
// A disc that fills like a pie with the progress, the icon punched out of it: the M3 look of the resources.
void drawProgressDisc(cairo_t* cr, const App&, double cx, double cy, double radius, double frac, const char* icon, double iconSize, const Color& ink) {
    cairo_push_group(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    setSource(cr, ink, 0.5);
    cairo_fill(cr);
    if (frac > 0.005) {
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, radius - 0.5, -M_PI / 2, -M_PI / 2 + 2 * M_PI * std::min(frac, 0.9999));
        cairo_close_path(cr);
        setSource(cr, ink);
        cairo_fill(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    drawIcon(cr, icon, cx - iconSize / 2, cy - iconSize / 2, iconSize, ink);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}
double mediaPositionNow(const MediaPanel& M);  // (13_media_panel.inc)
void musicPaintProgress(cairo_t* cr, const App& app, const Bar& bar, PangoLayout* layout, PangoFontDescription* restore) {
    const double cy = static_cast<double>(bar.height) / 2.0, x0 = musicX(bar), R = kProgRing / 2.0;
    const bool playing = app.musicStatus == "Playing";
    const double frac = app.media.length > 0 ? std::clamp(mediaPositionNow(app.media) / app.media.length, 0.0, 1.0) : 0.0;
    constexpr double line = 2.5;
    cairo_save(cr);
    cairo_set_line_width(cr, line);
    cairo_new_path(cr);
    cairo_arc(cr, x0 + R, cy, R - line / 2, 0, 2 * M_PI);
    setSource(cr, app.color("on_surface"), 0.16);
    cairo_stroke(cr);
    if (frac > 0) {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(cr);
        cairo_arc(cr, x0 + R, cy, R - line / 2, -M_PI / 2, -M_PI / 2 + 2 * M_PI * frac);
        setSource(cr, app.color("primary"));
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    constexpr double icon = 12;
    drawIcon(cr, playing ? "pause" : "music_note", x0 + R - icon / 2, cy - icon / 2, icon, app.color("on_surface"), true);

    PangoFontDescription* font = pango_font_description_from_string(kProgFont);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_width(layout, std::max(app.musicLineW, 1) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(layout, musicLineShown(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color(playing ? "on_surface" : "on_surface_variant"));
    cairo_move_to(cr, x0 + kProgRing + kProgGap, cy - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_font_description(layout, restore);
}
void musicPaintM3(cairo_t* cr, const App& app, const Bar& bar, PangoLayout* layout, PangoFontDescription* restore) {
    const Hit hit = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
    const double cy = static_cast<double>(bar.height) / 2.0, x0 = musicX(bar);
    const double artD = kM3Disc, artR = artD / 2.0, cov0 = x0 + kM3SidePad, px = x0 + musicCtrlX(app), nx = px + kMediaNextX;

    roundedRect(cr, x0, cy - kM3Box / 2.0, musicW(app), kM3Box, kM3Box / 2.0);
    setSource(cr, app.color("primary_container"));
    cairo_fill(cr);

    roundedRect(cr, cov0, cy - artR, artD, artD, artR);
    setSource(cr, app.color("secondary_container"));
    cairo_fill(cr);
    if (app.musicArt) {
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_arc(cr, cov0 + artR, cy, artR, 0, 2 * M_PI);
        cairo_clip(cr);
        cairo_translate(cr, cov0, cy - artR);
        cairo_scale(cr, artD / kMusicArt, artD / kMusicArt);  // (the cover is cached at kMusicArt)
        cairo_set_source_surface(cr, app.musicArt, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        drawIcon(cr, "music_note", cov0 + (artD - kMediaIcon) / 2.0, cy - kMediaIcon / 2.0, kMediaIcon, app.color("on_secondary_container"));
    }

    PangoFontDescription* artistFont = pango_font_description_from_string("Sans 9");
    PangoFontDescription* titleFont = pango_font_description_from_string("Sans 9.75");
    pango_layout_set_width(layout, std::max(app.musicM3TextW, 1) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    const std::string artistText = musicArtistShown(app), titleText = musicTitleShown(app);
    const bool hasArtist = !artistText.empty();
    const double tx = cov0 + artD + kM3TextGap;
    auto measure = [&](PangoFontDescription* font, const std::string& text) {
        pango_layout_set_font_description(layout, font);
        pango_layout_set_text(layout, text.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        return h;
    };
    const int ah = hasArtist ? measure(artistFont, artistText) : 0;
    const int th = measure(titleFont, titleText);
    const double top = cy + kM3TextNudge - (hasArtist ? ah + th - kM3LineOverlap : th) / 2.0;
    const Color ink = app.color("on_primary_container");
    if (hasArtist) {
        measure(artistFont, artistText);
        setSource(cr, ink);
        cairo_move_to(cr, tx, top);
        pango_cairo_show_layout(cr, layout);
    }
    measure(titleFont, titleText);
    setSource(cr, ink, 0.7);
    cairo_move_to(cr, tx, hasArtist ? top + ah - kM3LineOverlap : top);
    pango_cairo_show_layout(cr, layout);

    const bool playing = app.musicStatus == "Playing";
    const Color playInk = app.color(playing ? "on_primary" : "on_primary_container");
    const double py = cy - kM3Disc / 2.0;
    roundedRect(cr, px, py, kMediaPillW, kM3Disc, kM3Disc / 2);
    setSource(cr, app.color(playing ? "primary" : "on_primary_container"), playing ? 1 : 0.12);
    cairo_fill(cr);
    if (hit == HitPlay) {
        roundedRect(cr, px, py, kMediaPillW, kM3Disc, kM3Disc / 2);
        setSource(cr, playInk, 0.08);
        cairo_fill(cr);
    }
    drawIcon(cr, playing ? "pause" : "play_arrow", px + (kMediaPillW - kMediaM3Icon) / 2.0, cy - kMediaM3Icon / 2.0, kMediaM3Icon, playInk, true);

    if (hit == HitNext) {
        cairo_new_path(cr);
        cairo_arc(cr, nx + kM3Disc / 2.0, cy, kM3Disc / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color("on_primary_container"), 0.12);
        cairo_fill(cr);
    }
    drawIcon(cr, "skip_next", nx + (kM3Disc - kMediaM3Icon) / 2.0, cy - kMediaM3Icon / 2.0, kMediaM3Icon, ink, true);

    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_font_description(layout, restore);
    pango_font_description_free(artistFont);
    pango_font_description_free(titleFont);
}

void trayTogglePin(App& app, const std::string& id) {
    auto ex = app.cfg.trayExceptions;
    const auto it = std::find(ex.begin(), ex.end(), id);
    if (it != ex.end()) ex.erase(it);
    else ex.push_back(id);
    std::string v;
    for (const auto& e : ex) v += (v.empty() ? "" : ",") + e;
    if (setConfig(app, "trayExceptions", v.empty() ? "-" : v).empty()) { applyConfig(app); drawAll(app); }
}

std::string cmdFirstLine(const std::string& cmd) {
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[256];
        if (fgets(buf, sizeof buf, f)) out = buf;
        pclose(f);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}
bool recSafeName(const std::string& n) {
    return !n.empty() && n.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") == std::string::npos;
}
std::string recModulesFile() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC/rec-modules.txt";
}
void recAudioCleanup() {
    std::ifstream f(recModulesFile());
    std::string id;
    bool any = false;
    while (std::getline(f, id)) {
        if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
        runDetached({"pactl", "unload-module", id.c_str()});
        any = true;
    }
    f.close();
    if (any || access(recModulesFile().c_str(), F_OK) == 0) std::remove(recModulesFile().c_str());
}
std::string recMixedSource() {
    const std::string sink = cmdFirstLine("pactl get-default-sink 2>/dev/null"), mic = cmdFirstLine("pactl get-default-source 2>/dev/null");
    if (!recSafeName(sink) || !recSafeName(mic)) return "";
    const std::string m1 = cmdFirstLine("pactl load-module module-null-sink sink_name=end4pC_rec sink_properties=device.description=end4pC-recording 2>/dev/null");
    if (m1.empty()) return "";
    const std::string m2 = cmdFirstLine("pactl load-module module-loopback source=" + sink + ".monitor sink=end4pC_rec latency_msec=30 2>/dev/null");
    const std::string m3 = cmdFirstLine("pactl load-module module-loopback source=" + mic + " sink=end4pC_rec latency_msec=30 2>/dev/null");
    std::filesystem::create_directories(std::filesystem::path(recModulesFile()).parent_path());
    std::ofstream f(recModulesFile(), std::ios::trunc);
    for (const std::string& m : {m1, m2, m3}) if (!m.empty()) f << m << "\n";
    return "end4pC_rec.monitor";
}

void islandLiveRecording(App& app, bool started);
void startRecording(App& app, const std::string& geometry = "", const std::string& output = "") {
    std::string dir = app.cfg.recordDir.empty() ? homeDir() + "/Videos" : app.cfg.recordDir;
    if (dir.rfind("~/", 0) == 0) dir = homeDir() + dir.substr(1);
    std::filesystem::create_directories(dir);
    char stamp[48];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof stamp, "Recording_%Y-%m-%d_%H.%M.%S.mp4", &tm);
    const std::string file = dir + "/" + stamp;

    recAudioCleanup();
    std::string audioDev;
    const bool wantDesktop = app.cfg.recordDesktop != 0, wantMic = app.cfg.recordAudio != 0;
    if (wantDesktop && wantMic) {
        audioDev = recMixedSource();
        if (audioDev.empty()) audioDev = "-";
    } else if (wantDesktop) {
        const std::string sink = cmdFirstLine("pactl get-default-sink 2>/dev/null");
        if (recSafeName(sink)) audioDev = sink + ".monitor";
    } else if (wantMic) audioDev = "-";
    if (wantDesktop && (audioDev.empty() || audioDev == "-"))
        runDetached({"notify-send", "-a", "end4pC", "Recording", "The computer's sound could not be found: recording without it."});

    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        setsid();
        fcntl(p[1], F_SETFD, 0);  // keep the write end across exec: it is the "recorder is alive" signal
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDIN_FILENO);
        dup2(nul, STDOUT_FILENO);
        std::vector<std::string> args = {"wf-recorder"};
        if (audioDev == "-") args.push_back("-a");
        else if (!audioDev.empty()) args.push_back("--audio=" + audioDev);
        if (app.cfg.recordFps > 0) { args.push_back("-r"); args.push_back(std::to_string(app.cfg.recordFps)); }
        if (!geometry.empty()) { args.push_back("-g"); args.push_back(geometry); }
        else if (!output.empty()) { args.push_back("-o"); args.push_back(output); }
        args.push_back("-f");
        args.push_back(file);
        std::vector<char*> av;
        for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
        av.push_back(nullptr);
        execvp("wf-recorder", av.data());
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    app.recPid = pid;
    app.recFd = p[0];
    app.recording = true;
    islandLiveRecording(app, true);
}

// SIGINT lets wf-recorder finish the file properly.
void stopRecording(App& app) {
    if (app.recording && app.recPid > 0) kill(app.recPid, SIGINT);
}

void runScreenshot(const App& app, const char* mode) {
    const Color c = app.color("primary");
    char hex[16];
    std::snprintf(hex, sizeof hex, "#%02x%02x%02x", int(c.r * 255 + 0.5), int(c.g * 255 + 0.5), int(c.b * 255 + 0.5));
    const std::string script = assetsDir() + "/../scripts/screenshot.sh";
    runDetached({script.c_str(), mode, hex, app.cfg.screenshotDir.empty() ? "-" : app.cfg.screenshotDir.c_str(), app.cfg.screenshotCopy ? "1" : "0"});
}

void runPalette(const App& app, const std::string& wallpaper) {
    const std::string script = assetsDir() + "/../matugen/apply.sh";
    runDetached({script.c_str(), wallpaper.c_str(), app.cfg.colorMode.c_str(), app.cfg.colorScheme.c_str(), app.cfg.accent.c_str(), app.cfg.theme.c_str(),
               app.cfg.themePrimary.c_str(), app.cfg.themeSecondary.c_str(), app.cfg.themeTertiary.c_str()});
}

// Window borders from the palette: written to Hyprland's overrides file (scripts/hyprconf.py) or niri's (scripts/niriconf.py) and
// applied at once. Called when the setting changes and every time the palette does, since the colours are roles, not fixed values.
// Off: the lines go away (Hyprland reloads), so the borders come back from hyprland/colors.lua or niri/end4pC-colors.kdl.
std::string g_borderApplied;
void applyBorders(const App& app, bool force = false) {
    const bool niri = wmKind() == Wm::Niri;
    if (wmKind() != Wm::Hyprland && !niri) return;
    const Config& c = app.cfg;
    char a[16], i[16], key[160];
    auto rgba = [&](const std::string& role, int pct, char* out) {
        const Color col = app.color(role.c_str());
        std::snprintf(out, 16, niri ? "#%02x%02x%02x%02x" : "rgba(%02x%02x%02x%02x)", int(col.r * 255 + 0.5), int(col.g * 255 + 0.5), int(col.b * 255 + 0.5), int(pct * 255 / 100.0 + 0.5));
    };
    rgba(c.borderActive, c.borderActiveOpacity, a);
    rgba(c.borderInactive, c.borderInactiveOpacity, i);
    std::snprintf(key, sizeof key, "%d %s %s", c.borderColor, c.borderColor ? a : "", c.borderColor ? i : "");
    if (!force && g_borderApplied == key) return;
    if (g_borderApplied.empty() && !c.borderColor && !force) { g_borderApplied = key; return; }
    g_borderApplied = key;
    if (niri) {
        const std::string nscript = assetsDir() + "/../scripts/niriconf.py";
        if (c.borderColor) runDetached({"python3", nscript.c_str(), "--set", "borderActive", a, "borderInactive", i});
        else runDetached({"python3", nscript.c_str(), "--reset", "borderActive", "borderInactive"});
        return;
    }
    const std::string script = assetsDir() + "/../scripts/hyprconf.py";
    if (c.borderColor) runDetached({"python3", script.c_str(), "--set", "general:col:active_border", a, "--set", "general:col:inactive_border", i});
    else {
        const std::string cmd = "python3 '" + script + "' --reset general:col:active_border --reset general:col:inactive_border && hyprctl reload";
        runDetached({"bash", "-c", cmd.c_str()});
    }
}

void pickAccent() {
    const std::string script = assetsDir() + "/../scripts/pickaccent.sh";
    runDetached({script.c_str()});
}

std::string readPerfProfile() {
    const std::string out = cmdFirstLine("busctl --system get-property net.hadess.PowerProfiles /net/hadess/PowerProfiles net.hadess.PowerProfiles ActiveProfile 2>/dev/null");
    const size_t a = out.find('"'), b = out.rfind('"');
    return a != std::string::npos && b > a ? out.substr(a + 1, b - a - 1) : "";
}
bool readMicMuted() { return cmdFirstLine("wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null").find("MUTED") != std::string::npos; }

void toggleOsk(App& app);  // (14v_osk.inc)
void openQrCard(App& app, const std::string& text);  // (14w_qr_card.inc)
void closeQrCard(App& app);
void quickAction(App& app, int index) {
    if (index == kQaPicker) {
        runDetached({"hyprpicker", "-a"});
    } else if (index == kQaRecord) {
        if (app.recording) stopRecording(app);
        else openCapture(app, true);
    } else if (index == kQaScreenshot) {
        openCapture(app);
    } else if (index == kQaWallpaper) {
        toggleSelector(app);
    } else if (index == kQaMic) {
        runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle"});
        app.micMuted = !app.micMuted;
    } else if (index == kQaDark) {
        if (setConfig(app, "colorMode", app.cfg.colorMode == "dark" ? "light" : "dark").empty()) {
            applyConfig(app);
            if (!app.wallpaper.empty()) runPalette(app, app.wallpaper);
        }
    } else if (index == kQaOsk) {
        toggleOsk(app);
    } else if (index == kQaPerf) {
        const std::string cur = readPerfProfile();
        if (cur.empty()) return;
        const char* next = cur == "power-saver" ? "balanced" : cur == "balanced" ? "performance" : "power-saver";
        const std::string cmd = std::string("busctl --system set-property net.hadess.PowerProfiles /net/hadess/PowerProfiles net.hadess.PowerProfiles ActiveProfile s ") + next + " 2>/dev/null";
        std::system(cmd.c_str());
        app.perfProfile = readPerfProfile();
    }
}
