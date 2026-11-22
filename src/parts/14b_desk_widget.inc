
constexpr int kDeskAlts = 4;
constexpr int kDeskRoom = 22;
constexpr int kDeskEditExtra = 48;
constexpr int kDeskEdges[8] = {1 | 4, 4, 2 | 4, 2, 2 | 8, 8, 1 | 8, 1};
constexpr int kDeskMove = 5003, kDeskTap = 5004, kDeskHandle = 5010, kDeskDone = 5020;

std::array<DeskWidget*, 9> deskList(App& app) { return {&app.todo, &app.weather, &app.mediaW, &app.resW, &app.calW, &app.deskClock, &app.userW, &app.imageW, deskTextWidget()}; }

std::string deskFile(const DeskWidget& d) {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/.local/state/end4pC/" + d.name + ".txt";
}

void deskSave(const DeskWidget& d) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(deskFile(d)).parent_path(), ec);
    std::ofstream out(deskFile(d), std::ios::trunc);
    if (!out) return;
    out << "enabled\t" << (d.enabled ? 1 : 0) << "\n" << "size\t" << d.w << "\t" << d.h << "\n" << "pos\t" << d.ml << "\t" << d.mt << "\n";
    if (!d.scrKey.empty()) out << "screen\t" << d.scrKey << "\n";
    if (d.pinned) out << "pinned\t1\n";
    for (const DeskAlt& a : d.alts) out << "alt\t" << a.key << "\t" << a.l << "\t" << a.t << "\t" << a.w << "\t" << a.h << "\t" << (a.pinned ? 1 : 0) << "\n";
    if (d.saveExtra) d.saveExtra(d, out);
}

void deskLoad(DeskWidget& d) {
    std::ifstream in(deskFile(d));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("enabled\t", 0) == 0) d.enabled = line.size() > 8 && line[8] == '1';
        else if (line.rfind("size\t", 0) == 0) {
            int w = 0, h = 0;
            if (d.resizable && std::sscanf(line.c_str() + 5, "%d\t%d", &w, &h) == 2) { d.w = std::clamp(w, d.minW, d.maxW); d.h = std::clamp(h, d.minH, d.maxH); }
        } else if (line.rfind("pos\t", 0) == 0) {
            int l = 0, t = 0;
            if (std::sscanf(line.c_str() + 4, "%d\t%d", &l, &t) == 2) { d.ml = std::clamp(l, -1, 4000); d.mt = std::clamp(t, 0, 4000); }
        } else if (line.rfind("screen\t", 0) == 0) {
            d.scrKey = line.substr(7);
        } else if (line.rfind("pinned\t", 0) == 0) {
            d.pinned = line.size() > 7 && line[7] == '1';
        } else if (line.rfind("alt\t", 0) == 0) {
            DeskAlt a;
            char key[128] = {};
            int pin = 0;
            if (d.alts.size() < static_cast<size_t>(kDeskAlts) && std::sscanf(line.c_str() + 4, "%127[^\t]\t%d\t%d\t%d\t%d\t%d", key, &a.l, &a.t, &a.w, &a.h, &pin) == 6) {
                a.key = key;
                a.pinned = pin != 0;
                d.alts.push_back(a);
            }
        } else if (d.loadLine) d.loadLine(d, line);
    }
}

bool deskFakeScreen(int& w, int& h) {
    static int fw = -2, fh = 0;
    if (fw == -2) {
        fw = -1;
        int a = 0, b = 0;
        if (const char* e = std::getenv("END4PC_FAKE_SCREEN"); e && std::sscanf(e, "%dx%d", &a, &b) == 2 && a >= 400 && b >= 300) { fw = a; fh = b; }
    }
    w = fw;
    h = fh;
    return fw > 0;
}
int deskScreenW(const App& app) { int w = 0, h = 0; if (deskFakeScreen(w, h)) return w; const Bar* b = widgetBar(const_cast<App&>(app)); return b && b->wpW > 0 ? static_cast<int>(b->wpW) : 1920; }
int deskScreenH(const App& app) { int w = 0, h = 0; if (deskFakeScreen(w, h)) return h; const Bar* b = widgetBar(const_cast<App&>(app)); return b && b->wpH > 0 ? static_cast<int>(b->wpH) : 1080; }
std::string deskScreenKey(const App& app) {
    int fw = 0, fh = 0;
    const Bar* b = widgetBar(const_cast<App&>(app));
    if (!b || (b->wpW == 0 && !deskFakeScreen(fw, fh))) return "";
    return (b->connector.empty() ? std::string("screen") : b->connector) + "@" + std::to_string(deskScreenW(app)) + "x" + std::to_string(deskScreenH(app));
}
int deskUsableW(const App& app, int ml, int minW) {
    const bool vertical = app.cfg.barPosition == "left" || app.cfg.barPosition == "right";
    return std::max(minW, deskScreenW(app) - 2 * app.cfg.frame - (vertical ? barThickness(app) : 0) - ml);
}
int deskUsableH(const App& app) {
    const bool horizontal = app.cfg.barPosition == "top" || app.cfg.barPosition == "bottom";
    return deskScreenH(app) - 2 * app.cfg.frame - (horizontal ? barThickness(app) : 0);
}

void deskSurfSize(const App& app, const DeskWidget& d, int& sw, int& sh) {
    sw = d.w + 2 * kDeskRoom;
    sh = d.h + 2 * kDeskRoom + (d.editing ? kDeskEditExtra : 0);
    if (wmKind() != Wm::Niri || d.ml < 0) return;
    sw = std::min(sw, std::max(1, deskUsableW(app, d.ml - kDeskRoom, 1)));
    sh = std::min(sh, std::max(1, deskUsableH(app) - (d.mt - kDeskRoom)));
}

Rect rectIntersect(const Rect& a, const Rect& b) {
    const double x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y), x2 = std::min(a.x + a.w, b.x + b.w), y2 = std::min(a.y + a.h, b.y + b.h);
    return {x1, y1, std::max(0.0, x2 - x1), std::max(0.0, y2 - y1)};
}

int deskGap(const App& app) { return std::clamp(app.cfg.widgetGap, 0, 48); }
DeskWidget* deskBlocker(App& app, const DeskWidget& d, int l, int t, int w, int h) {
    const int g = deskGap(app);
    for (DeskWidget* o : deskList(app)) {
        if (o == &d || !o->open() || o->ml < 0) continue;
        if (l < o->ml + o->w + g && o->ml < l + w + g && t < o->mt + o->h + g && o->mt < t + h + g) return o;
    }
    return nullptr;
}
bool deskOverlaps(App& app, const DeskWidget& d, int l, int t, int w, int h) { return deskBlocker(app, d, l, t, w, h) != nullptr; }

void deskDraw(App& app, DeskWidget& d, bool force = false);
void onDeskFrame(void* data, wl_callback* cb, uint32_t) {
    wl_callback_destroy(cb);
    auto* d = static_cast<DeskWidget*>(data);
    d->busy = false;
    if (d->dirty && d->app) { d->dirty = false; d->busy = true; d->busySince = std::chrono::steady_clock::now(); d->wantFrame = true; deskDraw(*d->app, *d); }
}
const wl_callback_listener kDeskFrameListener = {onDeskFrame};

void deskDraw(App& app, DeskWidget& d, bool force) {
    if (!d.open() || !d.configured || d.w <= 0 || d.h <= 0) return;
    if (d.full && !d.fullReady) return;
    if (d.settling && !force) {
        if (std::chrono::steady_clock::now() - d.settlingSince < std::chrono::milliseconds(400)) return;
        d.settling = false;
    }
    const bool big = d.full && d.fullReady;
    int cardSW = 0, cardSH = 0;
    deskSurfSize(app, d, cardSW, cardSH);
    const int W = d.w, H = d.h, SW = big ? d.fullW : cardSW, SH = big ? d.fullH : cardSH;
    const double ox = big ? d.ml : kDeskRoom, oy = big ? d.mt : kDeskRoom;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, SW, SH, d.scale.factor(), "end4pC-widget")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (app.cfg.widgetShadow && !d.noShadow && !(d.drag && !d.moveMode)) {
        if (!d.shadow || d.shadowW != W || d.shadowH != H) {
            if (d.shadow) cairo_surface_destroy(d.shadow);
            d.shadow = makeShadowMask(W, H, d.radius, false);
            d.shadowW = W;
            d.shadowH = H;
        }
        setSource(cr, app.color("shadow"), 0.5);
        cairo_mask_surface(cr, d.shadow, ox - kLaunShadow, oy - kLaunShadow + 3);
    }
    cairo_translate(cr, ox, oy);

    const bool unlocked = !app.cfg.widgetsLocked;
    d.hits.clear();
    const bool handles = unlocked && !d.noMove;
    const double tapW = W, tapH = H;
    if (!handles && d.tap) d.hits.push_back({kDeskTap, Rect{0, 0, tapW, tapH}});
    if (handles) d.hits.push_back({kDeskMove, Rect{0, 0, static_cast<double>(W), d.moveZone < 0 ? static_cast<double>(H) : std::min<double>(d.moveZone, H)}});
    if (d.paint) d.paint(app, d, cr, W, H);

    if (handles && !d.editing) {
        const bool hot = d.hover == kDeskMove || (d.drag && d.moveMode);
        roundedRect(cr, W / 2.0 - 18, 8, 36, 4, 2);
        if (d.lightHandle) cairo_set_source_rgba(cr, 1, 1, 1, hot ? 0.95 : 0.55);
        else setSource(cr, app.color(hot ? "primary" : "outline"), hot ? 1.0 : 0.5);
        cairo_fill(cr);
    }
    if (handles && d.resizable && d.editing) {
        const double r = std::min<double>(d.radius, std::min(W, H) / 2.0), o = r * (1 - std::cos(M_PI / 4));
        const Color accent = app.color("primary"), ring = app.color("on_primary");
        roundedRect(cr, 1, 1, W - 2, H - 2, std::max(0.0, r - 1));
        setSource(cr, accent);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        const double px[8] = {o, W / 2.0, W - o, W - 1.0, W - o, W / 2.0, o, 1.0}, py[8] = {o, 1.0, o, H / 2.0, H - o, H - 1.0, H - o, H / 2.0};
        for (int i = 0; i < 8; i++) {
            const bool hot = d.hover == kDeskHandle + i || (d.drag && !d.moveMode && d.edge == kDeskEdges[i]);
            cairo_new_sub_path(cr);
            cairo_arc(cr, px[i], py[i], hot ? 9 : 7.5, 0, 2 * M_PI);
            setSource(cr, ring);
            cairo_fill_preserve(cr);
            setSource(cr, accent);
            cairo_set_line_width(cr, 3);
            cairo_stroke_preserve(cr);
            setSource(cr, accent);
            cairo_fill(cr);
            d.hits.push_back({kDeskHandle + i, Rect{px[i] - 11, py[i] - 11, 22, 22}});
        }
        const Rect done{W / 2.0 - 44, H + 12.0, 88, 34};
        const bool dh = d.hover == kDeskDone;
        roundedRect(cr, done.x, done.y, done.w, done.h, done.h / 2);
        setSource(cr, accent, dh ? 1.0 : 0.92);
        cairo_fill(cr);
        PangoLayout* dl = pango_cairo_create_layout(cr);
        PangoFontDescription* dfd = pango_font_description_from_string("Sans Bold 11");
        pango_layout_set_font_description(dl, dfd);
        pango_font_description_free(dfd);
        pango_layout_set_text(dl, i18n::tr("Done"), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(dl, &tw, &th);
        setSource(cr, ring);
        cairo_move_to(cr, done.x + (done.w - tw) / 2, done.y + (done.h - th) / 2);
        pango_cairo_show_layout(cr, dl);
        g_object_unref(dl);
        d.hits.push_back({kDeskDone, done});
    }

    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    if (d.wantFrame) {
        d.wantFrame = false;
        wl_callback_add_listener(wl_surface_frame(d.surface), &kDeskFrameListener, &d);
    }
    if (d.blurCard)
        updateBlurRegion(app, d.surface, d.bgBlur, SW, SH, [&](cairo_t* m) {
            roundedRect(m, ox, oy, W, H, d.radius);
            cairo_fill(m);
        });
    wl_region* region = wl_compositor_create_region(app.compositor);
    if (big) wl_region_add(region, 0, 0, SW, SH);
    else if (d.editing) wl_region_add(region, kDeskRoom - 12, kDeskRoom - 12, W + 24, H + 24 + kDeskEditExtra);
    else wl_region_add(region, kDeskRoom, kDeskRoom, W, H);
    wl_surface_set_input_region(d.surface, region);
    wl_region_destroy(region);
    hidpi::present(d.surface, buffer, hf, d.scale);
}

void onDeskConfigure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* d = static_cast<DeskWidget*>(data);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (d->full) {
        d->armed = true;
        const bool same = d->fullReady && d->fullW == static_cast<int>(w) && d->fullH == static_cast<int>(h);
        d->fullW = static_cast<int>(w);
        d->fullH = static_cast<int>(h);
        d->fullReady = w > 0 && h > 0;
        if (!same) deskDraw(*d->app, *d);
        return;
    }
    if (d->reqW && (static_cast<int>(w) != d->reqW || static_cast<int>(h) != d->reqH)) return;
    if (!d->reqW && w > 0) d->w = static_cast<int>(w) - 2 * kDeskRoom;
    if (!d->reqW && h > 0) d->h = static_cast<int>(h) - 2 * kDeskRoom - (d->editing ? kDeskEditExtra : 0);
    const bool wasSettling = d->settling;
    d->pending = d->settling = false;
    d->configured = true;
    if (!wasSettling) deskDraw(*d->app, *d);
}
void onDeskClosed(void* data, zwlr_layer_surface_v1*) {
    auto* d = static_cast<DeskWidget*>(data);
    d->layer = nullptr;
    releaseBlurSurface(d->bgBlur);
    hidpi::detach(d->scale);
    if (d->surface) wl_surface_destroy(d->surface);
    d->surface = nullptr;
    d->configured = false;
    if (d->app && d->app->onDesk == d) d->app->onDesk = nullptr;
}
const zwlr_layer_surface_v1_listener kDeskListener = {onDeskConfigure, onDeskClosed};

deskplace::Item deskItem(const DeskWidget& d) {
    deskplace::Item it;
    it.name = d.name;
    it.w = d.w;
    it.h = d.h;
    it.minW = d.minW;
    it.minH = d.minH;
    it.resizable = d.resizable;
    it.pinned = d.pinned;
    it.l = d.ml;
    it.t = d.mt;
    return it;
}
std::vector<deskplace::Rect> deskObstacles(App& app, const DeskWidget& d) {
    std::vector<deskplace::Rect> v;
    for (DeskWidget* o : deskList(app)) if (o != &d && o->open() && o->ml >= 0) v.push_back({o->ml, o->mt, o->w, o->h});
    return v;
}

bool deskFit(App& app, DeskWidget& d) {
    const int g = deskGap(app);
    const int maxL = std::max(g, deskUsableW(app, 0, d.minW) - d.w - g), maxT = std::max(g, deskUsableH(app) - d.h - g);
    if (d.ml >= 0) {
        const int nl = std::clamp(d.ml, g, maxL), nt = std::clamp(d.mt, g, maxT);
        if (nl == d.ml && nt == d.mt) return false;
        d.ml = nl;
        d.mt = nt;
        return true;
    }
    deskplace::Item it = deskItem(d);
    const std::vector<deskplace::Rect> obstacles = deskObstacles(app, d);
    const int W = deskUsableW(app, 0, d.minW), H = deskUsableH(app);
    int overlapped = 0;
    if (!deskplace::placeOne(W, H, g, obstacles, it, overlapped)) {
        const deskplace::Rect r{it.l, it.t, it.w, it.h};
        overlapped = 0;
        for (const deskplace::Rect& o : obstacles) if (deskplace::touches(r, o, 0)) overlapped++;
        it = deskItem(d);
        deskplace::placeOne(W, H, g, obstacles, it, overlapped);
    }
    d.ml = it.l;
    d.mt = it.t;
    if (d.resizable) { d.w = it.w; d.h = it.h; }
    return true;
}

bool deskTakeAlt(DeskWidget& d, const std::string& key) {
    for (auto it = d.alts.begin(); it != d.alts.end(); ++it) {
        if (it->key != key) continue;
        d.ml = it->l; d.mt = it->t; d.pinned = it->pinned;
        if (d.resizable) { d.w = std::clamp(it->w, d.minW, d.maxW); d.h = std::clamp(it->h, d.minH, d.maxH); }
        d.alts.erase(it);
        return true;
    }
    return false;
}
void deskStashAlt(DeskWidget& d) {
    if (d.scrKey.empty() || d.ml < 0) return;
    std::erase_if(d.alts, [&](const DeskAlt& a) { return a.key == d.scrKey; });
    d.alts.insert(d.alts.begin(), DeskAlt{d.scrKey, d.ml, d.mt, d.w, d.h, d.pinned});
    if (d.alts.size() > static_cast<size_t>(kDeskAlts)) d.alts.resize(kDeskAlts);
}

void deskArrangeOne(App& app, DeskWidget& d) {
    const std::string key = deskScreenKey(app);
    if (key.empty() || d.ml < 0 || d.scrKey == key) { if (!key.empty() && d.ml >= 0) d.scrKey = key; return; }
    if (d.scrKey.empty()) {
        const deskplace::Rect r{d.ml, d.mt, d.w, d.h};
        bool ok = d.ml + d.w <= deskUsableW(app, 0, d.minW) && d.mt + d.h <= deskUsableH(app);
        for (const deskplace::Rect& o : deskObstacles(app, d)) if (deskplace::touches(r, o, 0)) ok = false;
        if (!ok && !d.pinned) d.ml = -1;
    } else {
        deskStashAlt(d);
        if (!deskTakeAlt(d, key) && !d.pinned) d.ml = -1;
    }
    d.scrKey = key;
}

void deskOpen(App& app, DeskWidget& d) {
    if (d.open() || !app.compositor || !app.layerShell || !app.shm) return;
    d.enabled = true;
    d.configured = false;
    d.app = &app;
    deskArrangeOne(app, d);
    deskFit(app, d);
    d.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, d.scale, d.surface, [&app, w = &d] { deskDraw(app, *w, true); });
    const std::string ns = std::string("end4pC-widget-") + d.name;
    const Bar* home = widgetBar(app);
    d.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, d.surface, home ? home->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ns.c_str());
    zwlr_layer_surface_v1_set_anchor(d.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    deskSurfSize(app, d, d.reqW, d.reqH);
    d.full = d.fullReady = d.settling = false;
    zwlr_layer_surface_v1_set_size(d.layer, d.reqW, d.reqH);
    zwlr_layer_surface_v1_set_margin(d.layer, d.mt - kDeskRoom, 0, 0, d.ml - kDeskRoom);
    zwlr_layer_surface_v1_set_exclusive_zone(d.layer, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(d.layer, d.wantsKeys ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(d.layer, &kDeskListener, &d);
    wl_surface_commit(d.surface);
    if (d.onToggle) d.onToggle(app, true);
    deskSave(d);
}

void deskDropSurface(App& app, DeskWidget& d) {
    if (d.open()) {
        if (d.layer) zwlr_layer_surface_v1_destroy(d.layer);
        releaseBlurSurface(d.bgBlur);
        hidpi::detach(d.scale);
        wl_surface_destroy(d.surface);
        d.layer = nullptr;
        d.surface = nullptr;
        d.configured = false;
        d.drag = d.busy = d.full = d.fullReady = d.settling = false;
        if (app.onDesk == &d) app.onDesk = nullptr;
        if (d.onToggle) d.onToggle(app, false);
        updateCursor(app);
    }
}

void deskClose(App& app, DeskWidget& d) {
    d.enabled = false;
    deskDropSurface(app, d);
    deskSave(d);
}

void deskRestore(App& app) {
    std::vector<DeskWidget*> want;
    for (DeskWidget* d : deskList(app)) if (d->enabled && !d->open()) want.push_back(d);
    if (want.empty()) return;
    std::stable_sort(want.begin(), want.end(), [](const DeskWidget* a, const DeskWidget* b) { return deskplace::presetFor(a->name).priority < deskplace::presetFor(b->name).priority; });
    const std::string key = deskScreenKey(app);
    const int W = deskUsableW(app, 0, 1), H = deskUsableH(app), g = deskGap(app);
    std::vector<deskplace::Item> items;
    std::vector<deskplace::Rect> openNow;
    for (DeskWidget* o : deskList(app)) if (o->open() && o->ml >= 0) openNow.push_back({o->ml, o->mt, o->w, o->h});
    if (!key.empty()) {
        std::vector<deskplace::Item> legacy;
        for (DeskWidget* d : want) if (d->ml >= 0 && d->scrKey.empty()) legacy.push_back(deskItem(*d));
        for (const deskplace::Rect& r : openNow) { deskplace::Item it; it.w = r.w; it.h = r.h; it.l = r.l; it.t = r.t; legacy.push_back(it); }
        const bool legacyFits = deskplace::layoutFits(W, H, legacy);
        for (DeskWidget* d : want) {
            if (d->ml >= 0 && d->scrKey == key) continue;
            if (d->ml >= 0 && d->scrKey.empty()) {
                if (!legacyFits && !d->pinned) d->ml = -1;
            } else {
                deskStashAlt(*d);
                if (!deskTakeAlt(*d, key) && !d->pinned) d->ml = -1;
            }
            d->scrKey = key;
        }
    }
    for (DeskWidget* d : want) {
        deskplace::Item it = deskItem(*d);
        it.keep = d->ml >= 0 && !d->pinned;
        items.push_back(it);
    }
    for (const deskplace::Rect& r : openNow) { deskplace::Item it; it.name = "?"; it.w = r.w; it.h = r.h; it.l = r.l; it.t = r.t; it.keep = true; items.push_back(it); }
    deskplace::placeAll(W, H, g, items);
    for (size_t i = 0; i < want.size(); i++) {
        DeskWidget& d = *want[i];
        d.ml = items[i].l;
        d.mt = items[i].t;
        if (d.resizable) { d.w = items[i].w; d.h = items[i].h; }
    }
    for (DeskWidget* d : want) { d->enabled = false; deskOpen(app, *d); }
}

DeskWidget* deskFromSurface(App& app, wl_surface* s) {
    for (DeskWidget* d : deskList(app)) if (d->surface && d->surface == s) return d;
    return nullptr;
}

int deskIdAt(const DeskWidget& d, double x, double y) {
    for (auto it = d.hits.rbegin(); it != d.hits.rend(); ++it) if (it->second.contains(x, y)) return it->first;
    return -1;
}

int deskCursorShape(const App& app) {
    const DeskWidget* d = app.onDesk;
    if (!d) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (d->drag && d->moveMode) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    if (d->hover == kDeskMove) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    const int handle = d->drag && !d->moveMode ? -1 : d->hover - kDeskHandle;
    static const int kShapes[8] = {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE,
                                   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE,
                                   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE};
    if (d->drag && !d->moveMode) {
        for (int i = 0; i < 8; i++) if (kDeskEdges[i] == d->edge) return kShapes[i];
    }
    if (handle >= 0 && handle < 8) return kShapes[handle];
    if (d->hover == kDeskDone) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (d->cursor && d->hover >= 0) {
        const int s = d->cursor(*d, d->hover);
        if (s >= 0) return s;
    }
    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
}

void deskRequestSize(App& app, DeskWidget& d, int w, int h) {
    d.w = w;
    d.h = h;
    deskSurfSize(app, d, d.reqW, d.reqH);
    d.settling = true;
    d.settlingSince = std::chrono::steady_clock::now();
    zwlr_layer_surface_v1_set_size(d.layer, d.reqW, d.reqH);
    deskDraw(app, d, true);
}

// Starts a drag: the surface becomes the whole usable area and moves to the TOP layer, so the pointer stays inside it
// and the dragged widget is drawn in front. The surface is never replaced during the drag: Hyprland stops sending pointer
// movements if it is. The full-size picture is sent with the request.
void deskRaiseFull(App& app, DeskWidget& d) {
    d.full = true;
    d.fullW = deskUsableW(app, 0, 0);
    d.fullH = deskUsableH(app);
    d.fullReady = true;
    d.armed = false;
    d.frameMode = 0;
    d.frameOx = d.ml - kDeskRoom;
    d.frameOy = d.mt - kDeskRoom;
    d.lastGx = d.pressX + d.frameOx;  // (the press happened on the small surface)
    d.lastGy = d.pressY + d.frameOy;
    d.reqW = d.reqH = 0;
    constexpr uint32_t all = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_layer(d.layer, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
    zwlr_layer_surface_v1_set_anchor(d.layer, all);
    zwlr_layer_surface_v1_set_size(d.layer, 0, 0);
    zwlr_layer_surface_v1_set_margin(d.layer, 0, 0, 0, 0);
    deskDraw(app, d, true);
}

void deskLeaveFull(App& app, DeskWidget& d) {
    if (!d.full) return;
    d.full = d.fullReady = false;
    constexpr uint32_t tl = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    zwlr_layer_surface_v1_set_layer(d.layer, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
    zwlr_layer_surface_v1_set_anchor(d.layer, tl);
    deskSurfSize(app, d, d.reqW, d.reqH);
    d.settling = true;
    d.settlingSince = std::chrono::steady_clock::now();
    zwlr_layer_surface_v1_set_size(d.layer, d.reqW, d.reqH);
    zwlr_layer_surface_v1_set_margin(d.layer, d.mt - kDeskRoom, 0, 0, d.ml - kDeskRoom);
    d.configured = true;
    deskDraw(app, d, true);
}

constexpr double kDeskGlideSeconds = 0.55;

// Moves an open widget to another place and size with a glide. The widget takes the whole-screen surface that a drag uses, so no
// surface is resized on the way; the last frame gives the small surface back.
bool deskGlideTo(App& app, DeskWidget& d, int w, int h, int l, int t) {
    if (!d.open() || !d.configured || d.ml < 0 || d.drag || d.editing || d.glide.on) return false;
    w = d.resizable ? std::clamp(w, d.minW, d.maxW) : d.w;
    h = d.resizable ? std::clamp(h, d.minH, d.maxH) : d.h;
    if (w == d.w && h == d.h && l == d.ml && t == d.mt) return true;
    d.glide = {true, std::chrono::steady_clock::now(), d.ml, d.mt, d.w, d.h, l, t, w, h};
    d.pinned = true;
    if (!d.full) deskRaiseFull(app, d);
    return true;
}

void deskGlideStep(App& app, DeskWidget& d, std::chrono::steady_clock::time_point now) {
    auto& g = d.glide;
    const double raw = std::chrono::duration<double>(now - g.t0).count() / kDeskGlideSeconds;
    const double u = std::clamp(raw, 0.0, 1.0), e = 1 - std::pow(1 - u, 4);
    auto mix = [&](int a, int b) { return a + static_cast<int>(std::lround((b - a) * e)); };
    const int ow = d.w, oh = d.h;
    d.ml = mix(g.fl, g.tl);
    d.mt = mix(g.ft, g.tt);
    d.w = mix(g.fw, g.tw);
    d.h = mix(g.fh, g.th);
    if ((d.w != ow || d.h != oh) && d.onResize) d.onResize(app, d, ow, oh);
    if (u >= 1.0) {
        g.on = false;
        d.ml = g.tl; d.mt = g.tt; d.w = g.tw; d.h = g.th;
        deskLeaveFull(app, d);
        deskSave(d);
        return;
    }
    d.busy = true;
    d.busySince = now;
    d.wantFrame = true;
    deskDraw(app, d);
}

void deskGlideTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    for (DeskWidget* d : deskList(app)) {
        if (!d->glide.on) continue;
        if (!d->open()) { d->glide.on = false; continue; }
        timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        if (!d->fullReady) continue;
        if (d->busy && now - d->busySince < std::chrono::milliseconds(100)) continue;  // (the compositor has not shown the last picture yet)
        deskGlideStep(app, *d, now);
    }
}

void deskMotion(App& app, DeskWidget& d) {
    if (d.motion && d.motion(app, d)) return;
    if (d.pressed && !d.drag) {
        if (std::hypot(app.pointerX - d.pressX, app.pointerY - d.pressY) < 4) return;
        d.pressed = false;
        d.drag = true;
        d.moved = true;
        deskRaiseFull(app, d);
        updateCursor(app);
    }
    if (d.drag) {
        if (!d.fullReady) return;
        // The position reported is measured from the small surface or from the full one, and the compositor switches between the two at a moment
        // the shell cannot see. A pointer moves continuously, so every step takes the reading that lies closer to the last one.
        const double fullX = app.pointerX, fullY = app.pointerY, smallX = app.pointerX + d.frameOx, smallY = app.pointerY + d.frameOy;
        const bool small = std::hypot(smallX - d.lastGx, smallY - d.lastGy) <= std::hypot(fullX - d.lastGx, fullY - d.lastGy);
        const double px = small ? smallX : fullX;
        const double py = small ? smallY : fullY;
        d.lastGx = px;
        d.lastGy = py;
        const int gap = deskGap(app);
        if (d.moveMode) {
            const int nl = std::clamp(static_cast<int>(px - d.gx), gap, std::max(gap, deskUsableW(app, 0, d.minW) - d.w - gap));
            const int nt = std::clamp(static_cast<int>(py - d.gy), gap, std::max(gap, deskUsableH(app) - d.h - gap));
            const int fl = nl, ft = nt;
            if (fl == d.ml && ft == d.mt) return;
            d.moved = true;
            d.ml = fl;
            d.mt = ft;
        } else {
            const double dx = px - d.rpx, dy = py - d.rpy;
            const double usableW = deskUsableW(app, 0, d.minW), usableH = deskUsableH(app);
            const double left0 = d.rsl, right0 = d.rsl + d.rsw, top0 = d.rst, bottom0 = d.rst + d.rsh;
            const bool L = d.edge & 1, R = d.edge & 2, T = d.edge & 4, B = d.edge & 8;
            double left = L ? left0 + dx : left0, right = R ? right0 + dx : right0, top = T ? top0 + dy : top0, bottom = B ? bottom0 + dy : bottom0;
            left = std::max<double>(left, gap); right = std::min<double>(right, usableW - gap);
            top = std::max<double>(top, gap); bottom = std::min<double>(bottom, usableH - gap);
            double w = right - left, h = bottom - top;
            if (d.keepAspect > 0) {
                const bool horiz = L || R, vert = T || B;
                double s = horiz && vert ? std::max(w, h * d.keepAspect) : horiz ? w : h * d.keepAspect;
                double room = horiz ? (L ? right0 - gap : R ? usableW - gap - left0 : usableW) : usableW;
                double roomH = vert ? (T ? bottom0 - gap : B ? usableH - gap - top0 : usableH) : usableH;
                s = std::min({s, room, roomH * d.keepAspect, static_cast<double>(d.maxW), d.maxH * d.keepAspect});
                s = std::max({s, static_cast<double>(d.minW), d.minH * d.keepAspect});
                w = s;
                h = s / d.keepAspect;
                left = L ? right0 - w : R ? left0 : left0 + (d.rsw - w) / 2;
                top = T ? bottom0 - h : B ? top0 : top0 + (d.rsh - h) / 2;
            } else {
                w = std::clamp(w, static_cast<double>(d.minW), static_cast<double>(d.maxW));
                h = std::clamp(h, static_cast<double>(d.minH), static_cast<double>(d.maxH));
                left = L ? right0 - w : left0;
                top = T ? bottom0 - h : top0;
            }
            left = std::clamp(left, static_cast<double>(gap), std::max<double>(gap, usableW - gap - w));
            top = std::clamp(top, static_cast<double>(gap), std::max<double>(gap, usableH - gap - h));
            const int nw = static_cast<int>(w + 0.5), nh = static_cast<int>(h + 0.5), nl = static_cast<int>(left + 0.5), nt = static_cast<int>(top + 0.5);
            if (nw == d.w && nh == d.h && nl == d.ml && nt == d.mt) return;
            const int ow = d.w, oh = d.h;
            d.moved = true;
            d.w = nw;
            d.h = nh;
            d.ml = nl;
            d.mt = nt;
            if (d.onResize) d.onResize(app, d, ow, oh);
        }
        if (d.busy && std::chrono::steady_clock::now() - d.busySince < std::chrono::milliseconds(100)) { d.dirty = true; return; }
        d.busy = true;
        d.busySince = std::chrono::steady_clock::now();
        d.wantFrame = true;
        d.dirty = false;
        deskDraw(app, d);
        return;
    }
    const int id = deskIdAt(d, app.pointerX - kDeskRoom, app.pointerY - kDeskRoom);
    if (id != d.hover) {
        d.hover = id;
        deskDraw(app, d);
        updateCursor(app);
    }
}

void deskClick(App& app, DeskWidget& d) {
    const int id = deskIdAt(d, app.pointerX - kDeskRoom, app.pointerY - kDeskRoom);
    if (id == kDeskTap) { if (d.tap) d.tap(app, d); return; }
    if (id == kDeskDone) { d.editing = false; deskRequestSize(app, d, d.w, d.h); updateCursor(app); return; }
    if (id == kDeskMove || (id >= kDeskHandle && id < kDeskHandle + 8)) {
        d.moved = false;
        d.pressX = app.pointerX;
        d.pressY = app.pointerY;
        d.moveMode = id == kDeskMove;
        if (!d.moveMode) {
            d.edge = kDeskEdges[id - kDeskHandle];
            d.rsl = d.ml; d.rst = d.mt; d.rsw = d.w; d.rsh = d.h;
            d.rpx = d.ml + (app.pointerX - kDeskRoom);
            d.rpy = d.mt + (app.pointerY - kDeskRoom);
        }
        d.gx = app.pointerX - kDeskRoom;
        d.gy = app.pointerY - kDeskRoom;
        d.grabRight = d.w - d.gx;
        d.grabBottom = d.h - d.gy;
        d.busy = d.dirty = false;
        d.drag = true;
        if (!d.moveMode) d.moved = true;
        deskRaiseFull(app, d);
        updateCursor(app);
        return;
    }
    if (d.click) d.click(app, d, id);
}

void deskToggleEdit(App& app, DeskWidget& d) {
    if (d.drag || d.pressed || app.cfg.widgetsLocked || d.noMove || !d.resizable) return;
    d.editing = !d.editing;
    if (d.full) deskDraw(app, d);
    else deskRequestSize(app, d, d.w, d.h);
    updateCursor(app);
}

void deskRelease(App& app) {
    for (DeskWidget* d : deskList(app)) {
        if (d->release) d->release(app, *d);
        if (d->pressed && !d->drag) {
            d->pressed = false;
            deskDraw(app, *d);
            if (d->moveMode && d->tap) d->tap(app, *d);
            continue;
        }
        if (!d->drag) continue;
        const bool tapped = d->moveMode && !d->moved && d->tap;
        d->drag = d->moveMode = false;
        d->busy = d->dirty = false;
        if (d->moved) d->pinned = true;
        deskSave(*d);
        deskLeaveFull(app, *d);
        if (tapped) d->tap(app, *d);
    }
    updateCursor(app);
}

void deskLeave(App& app, DeskWidget& d) {
    if (app.onDesk == &d) app.onDesk = nullptr;
    if (d.hover != -1) { d.hover = -1; deskDraw(app, d); }
}

void deskScroll(App& app, DeskWidget& d, int dir) {
    if (d.scroll) d.scroll(app, d, dir);
}

void deskSync(App& app) {
    deskVizSync(app);
    static std::string lastKey;
    const std::string key = deskScreenKey(app);
    if (!key.empty() && key != lastKey) {
        lastKey = key;
        bool changed = false;
        for (DeskWidget* d : deskList(app)) if (d->open() && d->scrKey != key) changed = true;
        if (changed) {
            for (DeskWidget* d : deskList(app)) if (d->open()) { d->enabled = true; deskDropSurface(app, *d); }
            deskRestore(app);
        }
    }
    static int lastLocked = -1, lastShadow = -1, lastGap = -1;
    static std::string lastLook;
    const std::string look = std::to_string(app.cfg.clock24) + std::to_string(app.cfg.calendarMonday) + app.cfg.customImagePath + std::to_string(app.cfg.cookieSides) + std::to_string(app.cfg.cookieTime) + app.cfg.cookieDial + app.cfg.cookieHour +
                             app.cfg.cookieMinute + app.cfg.cookieSecond + app.cfg.cookieDate + app.cfg.customTextFont + app.cfg.customTextFontName + app.cfg.customTextColor + app.cfg.customTextAlign + std::to_string(app.cfg.customTextFit) +
                             std::to_string(app.cfg.customTextSize) + std::to_string(app.cfg.customTextBold) + std::to_string(app.cfg.customTextItalic) + std::to_string(app.cfg.customTextShadow);
    if (lastLook != look) { lastLook = look; for (DeskWidget* d : deskList(app)) if (d->open() && !d->drag) deskDraw(app, *d); }
    if (lastLocked == app.cfg.widgetsLocked && lastShadow == app.cfg.widgetShadow && lastGap == app.cfg.widgetGap) return;
    const bool gapChanged = lastGap != app.cfg.widgetGap;
    lastLocked = app.cfg.widgetsLocked;
    lastShadow = app.cfg.widgetShadow;
    lastGap = app.cfg.widgetGap;
    for (DeskWidget* d : deskList(app)) {
        if (!d->open()) continue;
        if (app.cfg.widgetsLocked) {
            d->pressed = false;
            if (d->editing) { d->editing = false; if (!d->full) deskRequestSize(app, *d, d->w, d->h); }
        }
        if (app.cfg.widgetsLocked && d->drag) { d->drag = d->moveMode = false; deskSave(*d); deskLeaveFull(app, *d); }
        if (gapChanged && deskFit(app, *d)) {
            zwlr_layer_surface_v1_set_margin(d->layer, d->mt - kDeskRoom, 0, 0, d->ml - kDeskRoom);
            deskSave(*d);
        }
        deskDraw(app, *d);
    }
}

void deskRedrawAll(App& app) {
    if (app.cfg.hotCornerVisible) applyCorners(app, true);  // (the corners follow the colors of the theme)
    app.dv.sig.clear();
    deskVizSync(app);
    for (DeskWidget* d : deskList(app)) if (d->open()) deskDraw(app, *d);
}
