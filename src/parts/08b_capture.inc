
enum CapBtn { bSelect = 0, bArrow, bLine, bRect, bEllipse, bBrush, bText, bNumber, bBlur, bUndo, bRedo, bCopy, bSave, bOcr, bLens, bQr, bClose, kCapBtnCount };
constexpr double kCapBtn = 36, kCapBtnGap = 2, kCapSep = 14, kCapBarPad = 8, kCapHandle = 5, kCapGrab = 10, kCapMin = 6;
enum { kCapIdle = 0, kCapCreate = 1, kCapMove = 2, kCapResize = 3, kCapDraw = 4 };
constexpr int kCapColors = 6, kCapSizes = 3;
const Color kCapPalette[kCapColors] = {{0.94, 0.27, 0.27}, {0.98, 0.80, 0.08}, {0.13, 0.77, 0.37}, {0.23, 0.51, 0.96}, {1, 1, 1}, {0.07, 0.07, 0.07}};
const double kCapWidths[kCapSizes] = {3, 5, 8};
const double kCapFonts[kCapSizes] = {20, 30, 44};
const double kCapNumR[kCapSizes] = {12, 16, 22};

struct CapGeom {
    Rect bar, btn[kCapBtnCount];
    Rect style, dot[kCapColors + kCapSizes];
    bool hasStyle = false;
};

Rect capSel(const Capture& c) { return {c.sx, c.sy, c.sw, c.sh}; }
bool capBarsShown(const Capture& c) { return c.has && (c.mode == kCapIdle || c.mode == kCapDraw); }

CapGeom capGeom(const Capture& c) {
    CapGeom g;
    const double w = 2 * kCapBarPad + kCapBtnCount * kCapBtn + (kCapBtnCount - 1) * kCapBtnGap + 3 * kCapSep;
    const double h = kCapBtn + 2 * kCapBarPad;
    const double x = std::clamp(c.sx + c.sw / 2 - w / 2, 8.0, std::max(8.0, c.w - w - 8.0));
    double y = c.sy + c.sh + 12;
    const bool below = y + h <= c.h - 8;
    if (!below) y = c.sy - 12 - h;
    if (y < 8) y = std::max(8.0, c.sy + c.sh - h - 12);
    g.bar = {x, y, w, h};
    double bx = x + kCapBarPad;
    for (int i = 0; i < kCapBtnCount; i++) {
        if (i == bUndo || i == bCopy || i == bOcr) bx += kCapSep;
        g.btn[i] = {bx, y + kCapBarPad, kCapBtn, kCapBtn};
        bx += kCapBtn + kCapBtnGap;
    }
    if (c.tool >= bArrow && c.tool <= bNumber) {
        const double dsz = 24, dg = 6, sw = 2 * 12 + kCapColors * dsz + (kCapColors - 1) * dg + 18 + kCapSizes * dsz + (kCapSizes - 1) * dg, sh = 40;
        const double sx = std::clamp(c.sx + c.sw / 2 - sw / 2, 8.0, std::max(8.0, c.w - sw - 8.0));
        double sy = below ? y + h + 8 : y - 8 - sh;
        if (sy + sh > c.h - 8) sy = y - 8 - sh;
        if (sy < 8) sy = y + h + 8;
        g.style = {sx, sy, sw, sh};
        double dx = sx + 12;
        for (int i = 0; i < kCapColors + kCapSizes; i++) {
            if (i == kCapColors) dx += 18 - dg;
            g.dot[i] = {dx, sy + (sh - dsz) / 2, dsz, dsz};
            dx += dsz + dg;
        }
        g.hasStyle = true;
    }
    return g;
}

enum RecBtn { rDesktop = 0, rMic, rFps0, rFps1, rFps2, rFps3, rStart, rClose, kRecBtnCount };
constexpr int kRecFps[4] = {0, 24, 30, 60};
struct RecGeom { Rect bar, btn[kRecBtnCount]; };
RecGeom capRecGeom(const Capture& c) {
    RecGeom g;
    const double pad = kCapBarPad, bh = 36, chipW = 44, chipH = 32, startW = 118, sep = 12, gap = 2;
    const double w = 2 * pad + 2 * bh + gap + sep + 4 * chipW + 3 * gap + sep + startW + gap + bh, h = bh + 2 * pad;
    const double x = std::clamp(c.sx + c.sw / 2 - w / 2, 8.0, std::max(8.0, c.w - w - 8.0));
    double y = c.sy + c.sh + 12;
    if (y + h > c.h - 8) y = c.sy - 12 - h;
    if (y < 8) y = std::max(8.0, c.sy + c.sh - h - 12);
    g.bar = {x, y, w, h};
    double bx = x + pad;
    g.btn[rDesktop] = {bx, y + pad, bh, bh};
    bx += bh + gap;
    g.btn[rMic] = {bx, y + pad, bh, bh};
    bx += bh + sep;
    for (int i = 0; i < 4; i++) { g.btn[rFps0 + i] = {bx, y + pad + (bh - chipH) / 2, chipW, chipH}; bx += chipW + gap; }
    bx += sep - gap;
    g.btn[rStart] = {bx, y + pad, startW, bh};
    bx += startW + gap;
    g.btn[rClose] = {bx, y + pad, bh, bh};
    return g;
}

void capHandlePos(const Capture& c, int i, double& hx, double& hy) {
    const double l = c.sx, r = c.sx + c.sw, t = c.sy, b = c.sy + c.sh, mx = (l + r) / 2, my = (t + b) / 2;
    static const int dx[8] = {0, 1, 2, 2, 2, 1, 0, 0}, dy[8] = {0, 0, 0, 1, 2, 2, 2, 1};
    hx = dx[i] == 0 ? l : dx[i] == 1 ? mx : r;
    hy = dy[i] == 0 ? t : dy[i] == 1 ? my : b;
}
int capHandleAt(const Capture& c, double x, double y) {
    if (!c.has || c.tool != bSelect) return -1;
    for (int i = 0; i < 8; i++) {
        double hx, hy;
        capHandlePos(c, i, hx, hy);
        if (std::abs(x - hx) <= kCapGrab && std::abs(y - hy) <= kCapGrab) return i;
    }
    return -1;
}
int capButtonAt(const Capture& c, double x, double y) {
    if (!capBarsShown(c)) return -1;
    if (c.record) {
        const RecGeom r = capRecGeom(c);
        for (int i = 0; i < kRecBtnCount; i++) if (r.btn[i].contains(x, y)) return i;
        return -1;
    }
    const CapGeom g = capGeom(c);
    for (int i = 0; i < kCapBtnCount; i++) if (g.btn[i].contains(x, y)) return i;
    return -1;
}
int capStyleAt(const Capture& c, double x, double y) {
    if (!capBarsShown(c) || c.record) return -1;
    const CapGeom g = capGeom(c);
    if (!g.hasStyle) return -1;
    for (int i = 0; i < kCapColors + kCapSizes; i++) if (g.dot[i].contains(x, y)) return i;
    return -1;
}

void closeCapture(App& app) {
    Capture& c = app.cap;
    if (!c.open()) return;
    if (c.layer) zwlr_layer_surface_v1_destroy(c.layer);
    hidpi::detach(c.scale);
    wl_surface_destroy(c.surface);
    if (c.shot) cairo_surface_destroy(c.shot);
    if (c.base) cairo_surface_destroy(c.base);
    for (CapBuf& b : c.bufs) {
        if (b.buf) wl_buffer_destroy(b.buf);
        if (b.data) munmap(b.data, b.size);
    }
    if (!c.shotFile.empty()) unlink(c.shotFile.c_str());
    c = Capture{};
    app.onCap = false;
    app.cursorShape = -1;
    updateCursor(app);
    malloc_trim(0);
}

double capLuma(const Color& c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

void capMakeMosaic(const Capture& c, CapAnnot& a) {
    const double iw = cairo_image_surface_get_width(c.shot), ih = cairo_image_surface_get_height(c.shot), kx = iw / c.w, ky = ih / c.h;
    const double l = std::min(a.x0, a.x1), t = std::min(a.y0, a.y1), r = std::max(a.x0, a.x1), b = std::max(a.y0, a.y1);
    const int ix = std::clamp(static_cast<int>(std::floor(l * kx)), 0, static_cast<int>(iw) - 1), iy = std::clamp(static_cast<int>(std::floor(t * ky)), 0, static_cast<int>(ih) - 1);
    const int pw = std::clamp(static_cast<int>(std::ceil((r - l) * kx)), 1, static_cast<int>(iw) - ix), ph = std::clamp(static_cast<int>(std::ceil((b - t) * ky)), 1, static_cast<int>(ih) - iy);
    a.block = std::max(6, static_cast<int>(std::lround(std::min(pw, ph) / 14.0)));
    const int sw = (pw + a.block - 1) / a.block, sh = (ph + a.block - 1) / a.block;
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    cairo_t* cr = mkcr(small);
    cairo_scale(cr, 1.0 / a.block, 1.0 / a.block);
    cairo_set_source_surface(cr, c.shot, -ix, -iy);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    a.mosaic = std::shared_ptr<cairo_surface_t>(small, cairo_surface_destroy);
    a.iw = pw;
    a.ih = ph;
}

void capPaintAnnot(cairo_t* cr, const CapAnnot& a, bool editing) {
    setSource(cr, a.col);
    cairo_set_line_width(cr, a.w);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    switch (a.type) {
    case 1: {
        const double dx = a.x1 - a.x0, dy = a.y1 - a.y0, len = std::hypot(dx, dy);
        if (len < 1) break;
        const double head = std::min(len * 0.6, std::max(14.0, a.w * 4.2)), ang = std::atan2(dy, dx), spread = 0.5;
        const double bx = a.x1 - std::cos(ang) * head * 0.8, by = a.y1 - std::sin(ang) * head * 0.8;
        cairo_move_to(cr, a.x0, a.y0);
        cairo_line_to(cr, bx, by);
        cairo_stroke(cr);
        cairo_move_to(cr, a.x1, a.y1);
        cairo_line_to(cr, a.x1 - std::cos(ang - spread) * head, a.y1 - std::sin(ang - spread) * head);
        cairo_line_to(cr, a.x1 - std::cos(ang + spread) * head, a.y1 - std::sin(ang + spread) * head);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    }
    case 2:
        cairo_move_to(cr, a.x0, a.y0);
        cairo_line_to(cr, a.x1, a.y1);
        cairo_stroke(cr);
        break;
    case 3:
        cairo_rectangle(cr, std::min(a.x0, a.x1), std::min(a.y0, a.y1), std::abs(a.x1 - a.x0), std::abs(a.y1 - a.y0));
        cairo_stroke(cr);
        break;
    case 4: {
        const double rx = std::abs(a.x1 - a.x0) / 2, ry = std::abs(a.y1 - a.y0) / 2;
        if (rx < 0.5 || ry < 0.5) break;
        cairo_save(cr);
        cairo_translate(cr, (a.x0 + a.x1) / 2, (a.y0 + a.y1) / 2);
        cairo_scale(cr, rx, ry);
        cairo_new_sub_path(cr);
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_stroke(cr);
        break;
    }
    case 5:
        if (a.pts.empty()) break;
        cairo_move_to(cr, a.pts[0].first, a.pts[0].second);
        if (a.pts.size() == 1) cairo_line_to(cr, a.pts[0].first + 0.01, a.pts[0].second);
        for (size_t i = 1; i < a.pts.size(); i++) cairo_line_to(cr, a.pts[i].first, a.pts[i].second);
        cairo_stroke(cr);
        break;
    case 6: {
        PangoLayout* lay = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
        pango_font_description_set_absolute_size(fd, a.w * PANGO_SCALE);
        pango_layout_set_font_description(lay, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(lay, a.text.empty() ? " " : a.text.c_str(), -1);
        cairo_move_to(cr, a.x0, a.y0);
        pango_cairo_layout_path(cr, lay);
        const bool dark = capLuma(a.col) < 0.5;
        setSource(cr, dark ? Color{1, 1, 1} : Color{0, 0, 0}, 0.55);
        cairo_set_line_width(cr, std::max(2.0, a.w / 9));
        cairo_stroke_preserve(cr);
        setSource(cr, a.col);
        cairo_fill(cr);
        if (editing) {
            PangoRectangle strong;
            pango_layout_get_cursor_pos(lay, static_cast<int>(a.text.size()), &strong, nullptr);
            setSource(cr, a.col);
            cairo_rectangle(cr, a.x0 + strong.x / double(PANGO_SCALE) + 1, a.y0 + strong.y / double(PANGO_SCALE), 2, strong.height / double(PANGO_SCALE));
            cairo_fill(cr);
        }
        g_object_unref(lay);
        break;
    }
    case 7: {
        cairo_new_sub_path(cr);
        cairo_arc(cr, a.x0, a.y0, a.w, 0, 2 * M_PI);
        cairo_fill(cr);
        PangoLayout* lay = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
        pango_font_description_set_absolute_size(fd, a.w * 1.15 * PANGO_SCALE);
        pango_layout_set_font_description(lay, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(lay, std::to_string(a.n).c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(lay, &tw, &th);
        setSource(cr, capLuma(a.col) > 0.55 ? Color{0.07, 0.07, 0.07} : Color{1, 1, 1});
        cairo_move_to(cr, a.x0 - tw / 2.0, a.y0 - th / 2.0);
        pango_cairo_show_layout(cr, lay);
        g_object_unref(lay);
        break;
    }
    case 8: {
        const double l = std::min(a.x0, a.x1), t = std::min(a.y0, a.y1), w = std::abs(a.x1 - a.x0), h = std::abs(a.y1 - a.y0);
        if (a.mosaic) {
            cairo_save(cr);
            cairo_rectangle(cr, l, t, w, h);
            cairo_clip(cr);
            cairo_translate(cr, l, t);
            cairo_scale(cr, a.block * w / a.iw, a.block * h / a.ih);
            cairo_set_source_surface(cr, a.mosaic.get(), 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            const double dash[2] = {6, 4};
            cairo_set_dash(cr, dash, 2, 0);
            setSource(cr, Color{1, 1, 1}, 0.9);
            cairo_set_line_width(cr, 1.5);
            cairo_rectangle(cr, l, t, w, h);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);
        }
        break;
    }
    default: break;
    }
}

void capPaintAll(cairo_t* cr, const Capture& c) {
    for (size_t i = 0; i < c.items.size(); i++) capPaintAnnot(cr, c.items[i], static_cast<int>(i) == c.editing);
}

void drawCapture(App& app);
void captureBufRelease(void* d, wl_buffer*) {
    auto* b = static_cast<CapBuf*>(d);
    b->busy = false;
    if (b->app && b->app->cap.open() && b->app->cap.dirty && !b->app->cap.framePending) drawCapture(*b->app);
}
const wl_buffer_listener kCapBufListener = {captureBufRelease};

CapBuf* capFreeBuf(App& app) {
    Capture& c = app.cap;
    const double sc = c.scale.factor();  // the buffers have the screen's real pixels (src/hidpi.h)
    const int PW = hidpi::bufferExtent(c.w, sc), PH = hidpi::bufferExtent(c.h, sc);
    const size_t size = static_cast<size_t>(PW) * 4 * PH;
    for (CapBuf& b : c.bufs) {
        if (b.busy) continue;
        if (b.buf && b.size != size) {  // (made at another scale: the screen changed its scale since)
            wl_buffer_destroy(b.buf);
            munmap(b.data, b.size);
            b.buf = nullptr;
            b.data = nullptr;
            b.size = 0;
        }
        if (!b.buf) {
            const int fd = memfd_create("end4pC-capture", MFD_CLOEXEC);
            if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return nullptr; }
            void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
            b.buf = wl_shm_pool_create_buffer(pool, 0, PW, PH, PW * 4, WL_SHM_FORMAT_ARGB8888);
            wl_shm_pool_destroy(pool);
            close(fd);
            b.data = data;
            b.size = size;
            b.app = &app;
            wl_buffer_add_listener(b.buf, &kCapBufListener, &b);
        }
        return &b;
    }
    return nullptr;
}

void capMakeBase(Capture& c) {
    const double sc = c.scale.factor();  // (at buffer pixels: the screenshot keeps its full resolution)
    if (c.base && cairo_image_surface_get_width(c.base) == hidpi::bufferExtent(c.w, sc) && cairo_image_surface_get_height(c.base) == hidpi::bufferExtent(c.h, sc)) return;
    if (c.base) { cairo_surface_destroy(c.base); c.base = nullptr; }  // (the scale changed: made again at the new one)
    const double iw = cairo_image_surface_get_width(c.shot), ih = cairo_image_surface_get_height(c.shot);
    c.base = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hidpi::bufferExtent(c.w, sc), hidpi::bufferExtent(c.h, sc));
    cairo_surface_set_device_scale(c.base, sc, sc);
    cairo_t* bc = mkcr(c.base);
    if (std::abs(iw - c.w) > 0.5 || std::abs(ih - c.h) > 0.5) cairo_scale(bc, c.w / iw, c.h / ih);
    cairo_set_source_surface(bc, c.shot, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(bc), CAIRO_FILTER_BILINEAR);
    cairo_paint(bc);
    cairo_identity_matrix(bc);
    cairo_set_source_rgba(bc, 0, 0, 0, 0.52);
    cairo_paint(bc);
    cairo_destroy(bc);
}

void captureFrameDone(void* d, wl_callback* cb, uint32_t) {
    wl_callback_destroy(cb);
    App* app = static_cast<App*>(d);
    app->cap.framePending = false;
    if (app->cap.open() && app->cap.dirty) drawCapture(*app);
}
const wl_callback_listener kCapFrameListener = {captureFrameDone};

void captureRedraw(App& app) {
    app.cap.dirty = true;
    if (!app.cap.framePending) drawCapture(app);
}

void drawCapture(App& app) {
    Capture& c = app.cap;
    if (!c.open() || !c.configured || c.w <= 0 || c.h <= 0 || !c.shot) return;
    CapBuf* cb = capFreeBuf(app);
    if (!cb) return;
    capMakeBase(c);
    c.dirty = false;
    const int W = c.w, H = c.h;
    const double sc = c.scale.factor();
    const int PW = hidpi::bufferExtent(W, sc), PH = hidpi::bufferExtent(H, sc);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(cb->data), CAIRO_FORMAT_ARGB32, PW, PH, PW * 4);
    cairo_surface_set_device_scale(surface, sc, sc);
    cairo_t* cr = mkcr(surface);
    const double iw = cairo_image_surface_get_width(c.shot), ih = cairo_image_surface_get_height(c.shot);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, c.base, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (c.has) {
        cairo_save(cr);
        cairo_rectangle(cr, c.sx, c.sy, c.sw, c.sh);
        cairo_clip(cr);
        cairo_save(cr);
        if (std::abs(iw - W) > 0.5 || std::abs(ih - H) > 0.5) cairo_scale(cr, W / iw, H / ih);
        cairo_set_source_surface(cr, c.shot, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);
        capPaintAll(cr, c);
        cairo_restore(cr);
    }

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const std::string& t, const char* font, double x, double y, const Color& col, double a = 1.0) {
        PangoFontDescription* fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, t.c_str(), -1);
        setSource(cr, col, a);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(fd);
    };
    auto measure = [&](const std::string& t, const char* font) {
        PangoFontDescription* fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, t.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        pango_font_description_free(fd);
        return std::pair<int, int>(tw, th);
    };

    if (!c.has) {
        const std::string hint = c.record ? "Drag to select the area to record   ·   Enter: the whole screen   ·   Esc: cancel" : "Drag to select an area   ·   Enter: the whole screen   ·   Esc: cancel";
        const auto [tw, th] = measure(hint, "Sans Bold 11");
        const double bw = tw + 40, bh = th + 22, bx = (W - bw) / 2, by = 36;
        roundedRect(cr, bx, by, bw, bh, bh / 2);
        setSource(cr, app.color("surface_container"), 0.94);
        cairo_fill(cr);
        text(hint, "Sans Bold 11", bx + 20, by + 11, app.color("on_surface"));
    } else {
        cairo_rectangle(cr, c.sx - 1, c.sy - 1, c.sw + 2, c.sh + 2);
        setSource(cr, app.color("primary"));
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        if (c.tool == bSelect)
            for (int i = 0; i < 8; i++) {
                double hx, hy;
                capHandlePos(c, i, hx, hy);
                cairo_new_sub_path(cr);
                cairo_arc(cr, hx, hy, kCapHandle + 1.5, 0, 2 * M_PI);
                setSource(cr, app.color("surface"));
                cairo_fill(cr);
                cairo_new_sub_path(cr);
                cairo_arc(cr, hx, hy, kCapHandle - 0.5, 0, 2 * M_PI);
                setSource(cr, app.color("primary"));
                cairo_fill(cr);
            }
        {
            const double kx = iw / W, ky = ih / H;
            const std::string label = std::to_string(std::max(1, static_cast<int>(std::lround(c.sw * kx)))) + " × " + std::to_string(std::max(1, static_cast<int>(std::lround(c.sh * ky))));
            const auto [tw, th] = measure(label, "Sans Bold 10");
            const double bw = tw + 22, bh = th + 12, bx = std::clamp(c.sx, 6.0, std::max(6.0, W - bw - 6.0)), by = c.sy - bh - 8 >= 6 ? c.sy - bh - 8 : c.sy + 8;
            roundedRect(cr, bx, by, bw, bh, bh / 2);
            setSource(cr, app.color("surface_container"), 0.94);
            cairo_fill(cr);
            text(label, "Sans Bold 10", bx + 11, by + 6, app.color("on_surface"));
        }
        if (c.record && capBarsShown(c)) {
            const RecGeom g = capRecGeom(c);
            roundedRect(cr, g.bar.x + 1, g.bar.y + 3, g.bar.w, g.bar.h, g.bar.h / 2);
            setSource(cr, Color{0, 0, 0}, 0.28);
            cairo_fill(cr);
            roundedRect(cr, g.bar.x, g.bar.y, g.bar.w, g.bar.h, g.bar.h / 2);
            setSource(cr, app.color("surface_container"), 0.97);
            cairo_fill(cr);
            roundedRect(cr, g.bar.x + 0.5, g.bar.y + 0.5, g.bar.w - 1, g.bar.h - 1, g.bar.h / 2 - 0.5);
            setSource(cr, app.color("outline_variant"), 0.6);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            for (int i = 0; i < kRecBtnCount; i++) {
                const Rect& r = g.btn[i];
                const bool hot = c.hoverBtn == i;
                const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                if (i == rDesktop || i == rMic) {
                    const bool on = i == rDesktop ? c.recDesktop != 0 : c.recAudio != 0;
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cx, cy, r.w / 2, 0, 2 * M_PI);
                    if (on) setSource(cr, app.color("secondary_container"));
                    else if (hot) setSource(cr, app.color("surface_container_highest"));
                    else setSource(cr, app.color("surface_container"), 0);
                    cairo_fill(cr);
                    drawIcon(cr, i == rDesktop ? "volume-loud" : "microphone-3", cx - 11, cy - 11, 22, app.color(on ? "on_secondary_container" : "on_surface_variant"), on);
                    if (!on) {
                        setSource(cr, app.color("on_surface_variant"));
                        cairo_set_line_width(cr, 2);
                        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                        cairo_move_to(cr, cx - 9, cy + 9);
                        cairo_line_to(cr, cx + 9, cy - 9);
                        cairo_stroke(cr);
                    }
                } else if (i >= rFps0 && i <= rFps3) {
                    const int fps = kRecFps[i - rFps0];
                    const bool on = c.recFps == fps;
                    roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
                    if (on) setSource(cr, app.color("secondary_container"));
                    else if (hot) setSource(cr, app.color("surface_container_highest"));
                    else setSource(cr, app.color("surface_container"), 0);
                    cairo_fill(cr);
                    const std::string label = fps == 0 ? "Auto" : std::to_string(fps);
                    const auto [tw, th] = measure(label, "Sans Bold 10");
                    text(label, "Sans Bold 10", cx - tw / 2.0, cy - th / 2.0, app.color(on ? "on_secondary_container" : "on_surface_variant"));
                } else if (i == rStart) {
                    roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
                    setSource(cr, app.color(hot ? "error" : "error"), hot ? 1.0 : 0.9);
                    cairo_fill(cr);
                    const auto [tw, th] = measure("Record", "Sans Bold 11");
                    const double gx = cx - (18 + 8 + tw) / 2.0;
                    drawIcon(cr, "record-circle", gx, cy - 9, 18, app.color("on_error"), true);
                    text("Record", "Sans Bold 11", gx + 26, cy - th / 2.0, app.color("on_error"));
                } else {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cx, cy, r.w / 2, 0, 2 * M_PI);
                    setSource(cr, app.color("surface_container_highest"), hot ? 1.0 : 0);
                    cairo_fill(cr);
                    drawIcon(cr, "close-circle", cx - 11, cy - 11, 22, app.color("on_surface"), hot);
                }
            }
        }
        if (!c.record && capBarsShown(c)) {
            const CapGeom g = capGeom(c);
            auto pill = [&](const Rect& r) {
                roundedRect(cr, r.x + 1, r.y + 3, r.w, r.h, r.h / 2);
                setSource(cr, Color{0, 0, 0}, 0.28);
                cairo_fill(cr);
                roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
                setSource(cr, app.color("surface_container"), 0.97);
                cairo_fill(cr);
                roundedRect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, r.h / 2 - 0.5);
                setSource(cr, app.color("outline_variant"), 0.6);
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);
            };
            pill(g.bar);
            static const char* icons[kCapBtnCount] = {"cursor", "arrow-right-up", nullptr, nullptr, nullptr, "pen", "text", "hashtag-circle", nullptr, "undo-left", "undo-right", "copy", "download-minimalistic", "text-selection", "eye-scan", "qr-code", "close-circle"};
            for (int i = 0; i < kCapBtnCount; i++) {
                const Rect& r = g.btn[i];
                const bool hot = c.hoverBtn == i, main = i == bCopy, on = i <= bBlur && i == c.tool;
                const bool off = (i == bUndo && c.items.empty()) || (i == bRedo && c.redo.empty());
                const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx, cy, r.w / 2, 0, 2 * M_PI);
                if (main) setSource(cr, app.color(hot ? "primary_fixed_dim" : "primary"));
                else if (on) setSource(cr, app.color("secondary_container"));
                else if (hot && !off) setSource(cr, app.color("surface_container_highest"));
                else setSource(cr, app.color("surface_container"), 0);
                cairo_fill(cr);
                const Color ink = app.color(main ? "on_primary" : on ? "on_secondary_container" : "on_surface");
                const double alpha = off ? 0.3 : 1.0;
                if (icons[i]) {
                    cairo_push_group(cr);
                    drawIcon(cr, icons[i], cx - 11, cy - 11, 22, ink, main || on || hot);
                    cairo_pop_group_to_source(cr);
                    cairo_paint_with_alpha(cr, alpha);
                } else {
                    setSource(cr, ink, alpha);
                    cairo_set_line_width(cr, 2);
                    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                    if (i == bLine) { cairo_move_to(cr, cx - 8, cy + 8); cairo_line_to(cr, cx + 8, cy - 8); cairo_stroke(cr); }
                    else if (i == bRect) { roundedRect(cr, cx - 8, cy - 6.5, 16, 13, 2.5); cairo_stroke(cr); }
                    else if (i == bEllipse) { cairo_save(cr); cairo_translate(cr, cx, cy); cairo_scale(cr, 9, 7); cairo_new_sub_path(cr); cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI); cairo_restore(cr); cairo_stroke(cr); }
                    else if (i == bBlur) {
                        for (int yy = 0; yy < 3; yy++)
                            for (int xx = 0; xx < 3; xx++)
                                if ((xx + yy) % 2 == 0) { cairo_rectangle(cr, cx - 8 + xx * 5.4, cy - 8 + yy * 5.4, 5.2, 5.2); cairo_fill(cr); }
                    }
                }
            }
            if (g.hasStyle) {
                pill(g.style);
                for (int i = 0; i < kCapColors + kCapSizes; i++) {
                    const Rect& r = g.dot[i];
                    const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
                    const bool hot = c.hoverStyle == i;
                    if (i < kCapColors) {
                        cairo_new_sub_path(cr);
                        cairo_arc(cr, cx, cy, r.w / 2 - (hot ? 1 : 2.5), 0, 2 * M_PI);
                        setSource(cr, kCapPalette[i]);
                        cairo_fill(cr);
                        cairo_new_sub_path(cr);
                        cairo_arc(cr, cx, cy, r.w / 2 - (hot ? 1 : 2.5), 0, 2 * M_PI);
                        setSource(cr, app.color("outline"), 0.6);
                        cairo_set_line_width(cr, 1);
                        cairo_stroke(cr);
                        if (i == c.colorIdx) {
                            cairo_new_sub_path(cr);
                            cairo_arc(cr, cx, cy, r.w / 2 + 0.5, 0, 2 * M_PI);
                            setSource(cr, app.color("primary"));
                            cairo_set_line_width(cr, 2.5);
                            cairo_stroke(cr);
                        }
                    } else {
                        const int k = i - kCapColors;
                        if (k == c.sizeIdx || hot) {
                            cairo_new_sub_path(cr);
                            cairo_arc(cr, cx, cy, r.w / 2, 0, 2 * M_PI);
                            setSource(cr, app.color(k == c.sizeIdx ? "secondary_container" : "surface_container_highest"));
                            cairo_fill(cr);
                        }
                        cairo_new_sub_path(cr);
                        cairo_arc(cr, cx, cy, 2.5 + k * 2.2, 0, 2 * M_PI);
                        setSource(cr, app.color("on_surface"));
                        cairo_fill(cr);
                    }
                }
            }
        }
    }

    {
        static const char* names[kCapBtnCount] = {"Select the area", "Arrow", "Line", "Rectangle", "Ellipse", "Brush", "Text  (Enter to finish)", "Numbered marker", "Pixelate", "Undo  (Ctrl+Z)", "Redo  (Ctrl+Y)", "Copy  (Enter)", "Save  (Ctrl+S)", "Copy the text in the area (OCR)", "Search the area with Google Lens", "Read a QR code in the area", "Close  (Esc)"};
        if (c.record && c.hoverBtn >= 0 && c.hoverBtn < kRecBtnCount && capBarsShown(c)) {
            static const char* rec[kRecBtnCount] = {nullptr, nullptr, "Frame rate: automatic", "24 frames per second", "30 frames per second", "60 frames per second", "Start recording  (Enter)", "Close  (Esc)"};
            const Rect r = capRecGeom(c).btn[c.hoverBtn];
            const std::string tip = c.hoverBtn == rDesktop ? (c.recDesktop ? "Computer's sound on: click to turn it off" : "Computer's sound off: click to record it")
                                  : c.hoverBtn == rMic ? (c.recAudio ? "Microphone on: click to turn it off" : "Microphone off: click to record it") : rec[c.hoverBtn];
            bubbleShow(c.bubble, std::string("rec") + std::to_string(c.hoverBtn) + tip, tip, r.x, r.y, r.w, r.h, W, H);
        } else if (!c.record && c.hoverBtn >= 0 && c.hoverBtn < kCapBtnCount && capBarsShown(c)) {
            const Rect r = capGeom(c).btn[c.hoverBtn];
            bubbleShow(c.bubble, std::string("cap") + std::to_string(c.hoverBtn), names[c.hoverBtn], r.x, r.y, r.w, r.h, W, H);
        } else bubbleHide(c.bubble);
        const Color bg = app.color("inverse_surface"), fg = app.color("inverse_on_surface");
        c.bubbleMoving = bubbleDraw(cr, c.bubble, {bg.r, bg.g, bg.b}, {fg.r, fg.g, fg.b});
    }

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cb->busy = true;
    wl_surface_attach(c.surface, cb->buf, 0, 0);
    wl_surface_damage_buffer(c.surface, 0, 0, PW, PH);
    if (c.scale.viewport) wp_viewport_set_destination(c.scale.viewport, W, H);
    wl_callback* frame = wl_surface_frame(c.surface);
    wl_callback_add_listener(frame, &kCapFrameListener, &app);
    c.framePending = true;
    wl_surface_commit(c.surface);
}

void capCommitText(Capture& c) {
    if (c.editing < 0 || c.editing >= static_cast<int>(c.items.size())) { c.editing = -1; return; }
    if (c.items[c.editing].text.empty()) c.items.erase(c.items.begin() + c.editing);
    c.editing = -1;
}
void capUndo(Capture& c) {
    capCommitText(c);
    if (c.items.empty()) return;
    c.redo.push_back(c.items.back());
    c.items.pop_back();
}
void capRedo(Capture& c) {
    capCommitText(c);
    if (c.redo.empty()) return;
    c.items.push_back(c.redo.back());
    c.redo.pop_back();
}
void capSelectTool(Capture& c, int tool) {
    capCommitText(c);
    c.tool = tool;
}

bool capWrite(const App& app, const std::string& file) {
    const Capture& c = app.cap;
    if (!c.shot || !c.has) return false;
    const double iw = cairo_image_surface_get_width(c.shot), ih = cairo_image_surface_get_height(c.shot), kx = iw / c.w, ky = ih / c.h;
    const int ix = std::clamp(static_cast<int>(std::lround(c.sx * kx)), 0, static_cast<int>(iw) - 1), iy = std::clamp(static_cast<int>(std::lround(c.sy * ky)), 0, static_cast<int>(ih) - 1);
    const int cw = std::clamp(static_cast<int>(std::lround(c.sw * kx)), 1, static_cast<int>(iw) - ix), ch = std::clamp(static_cast<int>(std::lround(c.sh * ky)), 1, static_cast<int>(ih) - iy);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, c.shot, -ix, -iy);
    cairo_paint(cr);
    cairo_translate(cr, -ix, -iy);
    cairo_scale(cr, kx, ky);
    for (const CapAnnot& a : c.items) capPaintAnnot(cr, a, false);
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(out, file.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(out);
    return ok;
}

std::string capCacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC";
}

void captureFinish(App& app, bool save) {
    Capture& c = app.cap;
    if (!c.open()) return;
    capCommitText(c);
    if (!c.has) {
        c.has = true; c.sx = 0; c.sy = 0; c.sw = c.w; c.sh = c.h;
    }
    std::filesystem::create_directories(capCacheDir());
    std::string file = capCacheDir() + "/capture-copy.png";
    bool saved = false;
    if (save) {
        std::string dir = app.cfg.screenshotDir.empty() ? "" : expandHome(app.cfg.screenshotDir);
        if (dir.empty()) {
            std::string pics = captureArgv({"xdg-user-dir", "PICTURES"});
            while (!pics.empty() && (pics.back() == '\n' || pics.back() == ' ')) pics.pop_back();
            dir = (pics.empty() ? homeDir() + "/Pictures" : pics) + "/Screenshots";
        }
        std::filesystem::create_directories(dir);
        char stamp[64];
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        std::strftime(stamp, sizeof stamp, "Screenshot_%Y-%m-%d_%H.%M.%S.png", &tm);
        file = dir + "/" + stamp;
        saved = true;
    }
    const bool ok = capWrite(app, file);
    if (ok && (!save || app.cfg.screenshotCopy)) { const std::string cmd = "wl-copy --type image/png < '" + file + "'"; runDetached({"bash", "-c", cmd.c_str()}); }
    if (ok && saved) runDetached({"notify-send", "-a", "end4pC", "-i", file.c_str(), "Screenshot saved", file.c_str()});
    closeCapture(app);
}

// tesseract's names for the languages this shell is shown in, besides English
std::string capOcrLanguages(const App& app) {
    std::string lang = app.cfg.language;
    if (lang.empty()) { const char* l = std::getenv("LANG"); lang = l ? l : "en"; }
    static const std::map<std::string, std::string> codes = {{"es", "spa"}, {"fr", "fra"}, {"de", "deu"}, {"it", "ita"}, {"pt", "por"}, {"ru", "rus"}, {"ja", "jpn"}, {"ko", "kor"},
        {"zh-Hans", "chi_sim"}, {"zh-Hant", "chi_tra"}, {"zh", "chi_sim"}, {"ar", "ara"}, {"he", "heb"}, {"nl", "nld"}, {"pl", "pol"}, {"sv", "swe"}, {"tr", "tur"}, {"uk", "ukr"},
        {"cs", "ces"}, {"hu", "hun"}, {"vi", "vie"}, {"ca", "cat"}, {"be", "bel"}, {"gl", "glg"}, {"nn", "nor"}};
    std::string out = "eng";
    for (const auto& [k, v] : codes)
        if (lang.compare(0, k.size(), k) == 0) { out = v + " eng"; break; }
    return out;
}

// tesseract reads dark text on a light ground best: the area becomes grey, light-on-dark ones are inverted, and small ones are enlarged.
bool capPrepareForOcr(const std::string& file) {
    cairo_surface_t* in = cairo_image_surface_create_from_png(file.c_str());
    if (cairo_surface_status(in) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(in); return false; }
    const int w = cairo_image_surface_get_width(in), h = cairo_image_surface_get_height(in);
    const double scale = std::clamp(1600.0 / std::max(1, w), 1.0, 3.0);
    const int ow = static_cast<int>(std::lround(w * scale)), oh = static_cast<int>(std::lround(h * scale));
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, ow, oh);
    cairo_t* cr = mkcr(out);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, in, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(out);
    unsigned char* px = cairo_image_surface_get_data(out);
    const int stride = cairo_image_surface_get_stride(out);
    double sum = 0;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            const unsigned char* p = px + y * stride + x * 4;
            sum += 0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2];
        }
    const bool invert = sum / std::max(1.0, double(ow) * oh) < 128;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            unsigned char* p = px + y * stride + x * 4;
            unsigned char grey = static_cast<unsigned char>(0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2]);
            if (invert) grey = 255 - grey;
            p[0] = p[1] = p[2] = grey;
        }
    cairo_surface_mark_dirty(out);
    const bool ok = cairo_surface_write_to_png(out, file.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(in);
    cairo_surface_destroy(out);
    return ok;
}

// Text recognition (tesseract, the text goes to the clipboard) or a Google Lens search (the area is uploaded to uguu.se, which keeps it a few hours).
void captureRecognize(App& app, int mode) {
    const bool lens = mode == 1;
    Capture& c = app.cap;
    if (!c.open()) return;
    capCommitText(c);
    if (!c.has) { c.has = true; c.sx = 0; c.sy = 0; c.sw = c.w; c.sh = c.h; }
    std::filesystem::create_directories(capCacheDir());
    const std::string file = capCacheDir() + "/capture-search.png";
    bool ok = capWrite(app, file);
    if (ok && mode == 0) capPrepareForOcr(file);
    const std::string langs = capOcrLanguages(app);
    closeCapture(app);
    if (!ok) return;
    if (mode == 2) {
        static const char* kScript =
            "f=\"$1\"; text=$(ZXingReader -single -bytes \"$f\" 2>/dev/null || zbarimg -q --raw \"$f\" 2>/dev/null); rm -f \"$f\"; "
            "if [ -n \"$text\" ]; then printf %s \"$text\" | wl-copy; notify-send -a end4pC 'QR code read, copied' \"$(printf %s \"$text\" | head -c 240)\"; "
            "else notify-send -a end4pC 'No QR code found' 'Try a selection that holds the whole code'; fi";
        runDetached({"bash", "-c", kScript, "bash", file.c_str()});
        return;
    }
    if (lens) {
        static const char* kScript =
            "f=\"$1\"; notify-send -a end4pC 'Google Lens' 'Uploading the area to uguu.se (kept for about 3 hours)'; "
            "url=$(curl -sf -m 30 -F \"files[]=@$f\" https://uguu.se/upload | jq -r '.files[0].url // empty'); rm -f \"$f\"; "
            "if [ -n \"$url\" ]; then xdg-open \"https://lens.google.com/uploadbyurl?url=$url\"; else notify-send -a end4pC 'Google Lens' 'The upload failed'; fi";
        runDetached({"bash", "-c", kScript, "bash", file.c_str()});
        return;
    }
    static const char* kScript =
        "f=\"$1\"; want=\"$2\"; have=$(tesseract --list-langs 2>/dev/null | tail -n +2); langs=''; "
        "for l in $want; do printf '%s\\n' \"$have\" | grep -qx \"$l\" && langs=\"${langs:+$langs+}$l\"; done; [ -n \"$langs\" ] || langs=eng; "
        "text=$(tesseract \"$f\" stdout -l \"$langs\" 2>/dev/null | sed -e 's/[[:space:]]*$//' | cat -s); rm -f \"$f\"; "
        "if [ -n \"$(printf %s \"$text\" | tr -d '[:space:]')\" ]; then printf %s \"$text\" | wl-copy; "
        "notify-send -a end4pC 'Text copied' \"$(printf %s \"$text\" | head -c 240)\"; "
        "else notify-send -a end4pC 'No text found' 'Try a larger or sharper selection'; fi";
    runDetached({"bash", "-c", kScript, "bash", file.c_str(), langs.c_str()});
}

void captureRecordStart(App& app) {
    Capture& c = app.cap;
    if (!c.open()) return;
    std::string geometry, output;
    if (c.has && c.sw >= 2 && c.sh >= 2) {
        const int x = c.monX + static_cast<int>(std::lround(c.sx)), y = c.monY + static_cast<int>(std::lround(c.sy));
        const int w = std::max(2, static_cast<int>(std::lround(c.sw)) / 2 * 2), h = std::max(2, static_cast<int>(std::lround(c.sh)) / 2 * 2);
        char b[64];
        std::snprintf(b, sizeof b, "%d,%d %dx%d", x, y, w, h);
        geometry = b;
    } else output = c.monName;
    closeCapture(app);
    usleep(220000);
    startRecording(app, geometry, output);
    drawAll(app);
}

void openCapture(App& app, bool record) {
    Capture& c = app.cap;
    if (c.open() || !app.compositor || !app.layerShell || !app.shm) return;
    if (anyPopupOpen(app)) { closeAllPopups(app); syncCatcher(app); drawAll(app); usleep(120000); }
    std::string info = wmFocusedOutputInfo();
    while (!info.empty() && (info.back() == '\n' || info.back() == ' ')) info.pop_back();
    std::string mon;
    int monX = 0, monY = 0;
    {
        std::istringstream in(info);
        in >> mon >> monX >> monY;
    }
    if (mon.empty()) return;
    std::filesystem::create_directories(capCacheDir());
    const std::string shot = capCacheDir() + "/capture-shot.png";
    unlink(shot.c_str());
    captureArgv({"grim", "-o", mon, shot});
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(shot.c_str(), nullptr);
    if (!pb) { runDetached({"notify-send", "-a", "end4pC", "Screenshot", "The screen could not be captured (is grim installed?)"}); return; }
    c = Capture{};
    c.record = record;
    c.recAudio = app.cfg.recordAudio;
    c.recDesktop = app.cfg.recordDesktop;
    c.recFps = app.cfg.recordFps;
    c.monName = mon;
    c.monX = monX;
    c.monY = monY;
    c.shot = surfaceFromPixbuf(pb);
    g_object_unref(pb);
    c.shotFile = shot;
    c.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, c.scale, c.surface, [&app] { app.cap.dirty = true; drawCapture(app); });
    c.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, c.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-capture");
    zwlr_layer_surface_v1_set_anchor(c.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(c.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(c.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(c.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    static const zwlr_layer_surface_v1_listener kCaptureListener = {
        [](void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
            auto* a = static_cast<App*>(d);
            zwlr_layer_surface_v1_ack_configure(layer, serial);
            a->cap.w = static_cast<int>(w);
            a->cap.h = static_cast<int>(h);
            a->cap.configured = true;
            drawCapture(*a);
        },
        [](void* d, zwlr_layer_surface_v1*) { closeCapture(*static_cast<App*>(d)); }};
    zwlr_layer_surface_v1_add_listener(c.layer, &kCaptureListener, &app);
    wl_surface_commit(c.surface);
}

double capInX(const Capture& c, double x) { return std::clamp(x, c.sx, c.sx + c.sw); }
double capInY(const Capture& c, double y) { return std::clamp(y, c.sy, c.sy + c.sh); }

void captureMotion(App& app) {
    Capture& c = app.cap;
    if (!c.open()) return;
    const double x = std::clamp(app.pointerX, 0.0, static_cast<double>(c.w)), y = std::clamp(app.pointerY, 0.0, static_cast<double>(c.h));
    if (c.mode == kCapCreate) {
        c.sx = std::min(c.ax, x); c.sy = std::min(c.ay, y);
        c.sw = std::abs(x - c.ax); c.sh = std::abs(y - c.ay);
        c.has = c.sw >= 1 || c.sh >= 1;
    } else if (c.mode == kCapMove) {
        const double dx = std::clamp(x - c.ax, 0.0, c.w - c.sw) - c.sx, dy = std::clamp(y - c.ay, 0.0, c.h - c.sh) - c.sy;
        c.sx += dx; c.sy += dy;
        for (CapAnnot& a : c.items) {
            a.x0 += dx; a.x1 += dx; a.y0 += dy; a.y1 += dy;
            for (auto& p : a.pts) { p.first += dx; p.second += dy; }
        }
    } else if (c.mode == kCapResize) {
        double l = c.start.x, t = c.start.y, r = c.start.x + c.start.w, b = c.start.y + c.start.h;
        const int hnd = c.handle;
        if (hnd == 0 || hnd == 6 || hnd == 7) l = x;
        if (hnd == 2 || hnd == 3 || hnd == 4) r = x;
        if (hnd == 0 || hnd == 1 || hnd == 2) t = y;
        if (hnd == 4 || hnd == 5 || hnd == 6) b = y;
        c.sx = std::min(l, r); c.sy = std::min(t, b);
        c.sw = std::abs(r - l); c.sh = std::abs(b - t);
    } else if (c.mode == kCapDraw && !c.items.empty()) {
        CapAnnot& a = c.items.back();
        const double px = capInX(c, x), py = capInY(c, y);
        if (a.type == 5) {
            if (a.pts.empty() || std::hypot(px - a.pts.back().first, py - a.pts.back().second) >= 1.5) a.pts.push_back({px, py});
        } else { a.x1 = px; a.y1 = py; }
    }
    c.hoverBtn = capButtonAt(c, app.pointerX, app.pointerY);
    c.hoverStyle = capStyleAt(c, app.pointerX, app.pointerY);
    captureRedraw(app);
    updateCursor(app);
}

void captureButton(App& app, uint32_t button, bool pressed) {
    Capture& c = app.cap;
    if (!c.open()) return;
    constexpr uint32_t kLeft = 0x110, kRight = 0x111;
    if (pressed && button == kRight) { closeCapture(app); return; }
    if (button != kLeft) return;
    const double x = app.pointerX, y = app.pointerY;
    if (pressed) {
        const int b = capButtonAt(c, x, y);
        if (b >= 0 && c.record) {
            if (b == rStart) { captureRecordStart(app); return; }
            if (b == rClose) { closeCapture(app); return; }
            if (b == rDesktop) { c.recDesktop = !c.recDesktop; app.cfg.recordDesktop = c.recDesktop; saveConfig(app); }
            else if (b == rMic) { c.recAudio = !c.recAudio; app.cfg.recordAudio = c.recAudio; saveConfig(app); }
            else { c.recFps = kRecFps[b - rFps0]; app.cfg.recordFps = c.recFps; saveConfig(app); }
            captureRedraw(app);
            return;
        }
        if (b >= 0) {
            if (b == bCopy) { captureFinish(app, false); return; }
            if (b == bSave) { captureFinish(app, true); return; }
            if (b == bOcr || b == bLens || b == bQr) { captureRecognize(app, b == bLens ? 1 : b == bQr ? 2 : 0); return; }
            if (b == bClose) { closeCapture(app); return; }
            if (b == bUndo) capUndo(c);
            else if (b == bRedo) capRedo(c);
            else capSelectTool(c, b);
            captureRedraw(app);
            updateCursor(app);
            return;
        }
        const int st = capStyleAt(c, x, y);
        if (st >= 0) {
            if (st < kCapColors) c.colorIdx = st;
            else c.sizeIdx = st - kCapColors;
            if (c.editing >= 0 && c.editing < static_cast<int>(c.items.size())) {
                CapAnnot& a = c.items[c.editing];
                a.col = kCapPalette[c.colorIdx];
                a.w = kCapFonts[c.sizeIdx];
            }
            captureRedraw(app);
            return;
        }
        if (c.tool != bSelect && c.has) {
            if (!capSel(c).contains(x, y)) { capCommitText(c); captureRedraw(app); return; }
            capCommitText(c);
            CapAnnot a;
            a.type = c.tool;
            a.col = kCapPalette[c.colorIdx];
            a.w = c.tool == bText ? kCapFonts[c.sizeIdx] : c.tool == bNumber ? kCapNumR[c.sizeIdx] : kCapWidths[c.sizeIdx];
            a.x0 = a.x1 = capInX(c, x);
            a.y0 = a.y1 = capInY(c, y);
            if (c.tool == bBrush) a.pts.push_back({a.x0, a.y0});
            if (c.tool == bNumber) {
                a.n = 1;
                for (const CapAnnot& o : c.items) if (o.type == bNumber) a.n++;
                c.items.push_back(a);
                c.redo.clear();
            } else if (c.tool == bText) {
                c.items.push_back(a);
                c.editing = static_cast<int>(c.items.size()) - 1;
                c.redo.clear();
            } else {
                c.items.push_back(a);
                c.redo.clear();
                c.mode = kCapDraw;
            }
            captureRedraw(app);
            return;
        }
        const int hnd = capHandleAt(c, x, y);
        c.start = capSel(c);
        if (hnd >= 0) { c.mode = kCapResize; c.handle = hnd; }
        else if (c.has && capSel(c).contains(x, y)) { c.mode = kCapMove; c.ax = x - c.sx; c.ay = y - c.sy; }
        else { c.mode = kCapCreate; c.ax = std::clamp(x, 0.0, static_cast<double>(c.w)); c.ay = std::clamp(y, 0.0, static_cast<double>(c.h)); c.has = false; c.sw = c.sh = 0; c.items.clear(); c.redo.clear(); }
        captureRedraw(app);
    } else if (c.mode == kCapDraw) {
        c.mode = kCapIdle;
        if (!c.items.empty()) {
            CapAnnot& a = c.items.back();
            const bool tiny = std::abs(a.x1 - a.x0) < 3 && std::abs(a.y1 - a.y0) < 3;
            if (a.type != 5 && tiny) c.items.pop_back();
            else if (a.type == 8) capMakeMosaic(c, a);
        }
        c.hoverBtn = capButtonAt(c, x, y);
        captureRedraw(app);
        updateCursor(app);
    } else if (c.mode != kCapIdle) {
        const bool created = c.mode == kCapCreate;
        c.mode = kCapIdle;
        if (c.sw < kCapMin || c.sh < kCapMin) { c.has = false; c.items.clear(); c.redo.clear(); }
        if (created && c.has && c.autoAct) { captureRecognize(app, c.autoAct == 2 ? 1 : c.autoAct == 3 ? 2 : 0); return; }  // "screenshot ocr": letting go of the area is the action
        c.hoverBtn = capButtonAt(c, x, y);
        captureRedraw(app);
        updateCursor(app);
    }
}

void captureKey(App& app, uint32_t key) {
    constexpr uint32_t kEsc = 1, kBackspace = 14, kEnter = 28, kKpEnter = 96, kS = 31, kC = 46, kZ = 44, kY = 21;
    Capture& c = app.cap;
    const bool ctrl = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    const bool shift = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (c.editing >= 0 && c.editing < static_cast<int>(c.items.size())) {
        CapAnnot& a = c.items[c.editing];
        if (key == kEsc) { capCommitText(c); captureRedraw(app); return; }
        if ((key == kEnter || key == kKpEnter) && !shift) { capCommitText(c); captureRedraw(app); return; }
        if (key == kEnter || key == kKpEnter) a.text += "\n";
        else if (key == kBackspace) {
            if (!a.text.empty()) {
                size_t n = a.text.size() - 1;
                while (n > 0 && (static_cast<unsigned char>(a.text[n]) & 0xC0) == 0x80) n--;  // a whole UTF-8 letter
                a.text.resize(n);
            }
        } else if (ctrl && key == kZ) { capUndo(c); }
        else if (!ctrl && app.xkbState) {
            char buf[16];
            const int n = xkb_state_key_get_utf8(app.xkbState, key + 8, buf, sizeof buf);
            if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && a.text.size() < 400) a.text.append(buf, n);
        }
        captureRedraw(app);
        return;
    }
    if (c.record) {
        if (key == kEsc) closeCapture(app);
        else if (key == kEnter || key == kKpEnter) captureRecordStart(app);
        return;
    }
    if (key == kEsc) closeCapture(app);
    else if (ctrl && key == kZ) { if (shift) capRedo(c); else capUndo(c); captureRedraw(app); }
    else if (ctrl && key == kY) { capRedo(c); captureRedraw(app); }
    else if (key == kEnter || key == kKpEnter || (ctrl && key == kC)) captureFinish(app, false);
    else if (ctrl && key == kS) captureFinish(app, true);
}

int captureCursorShape(const App& app) {
    const Capture& c = app.cap;
    static const int resize[8] = {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE,
                                  WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE,
                                  WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE};
    if (c.mode == kCapResize) return resize[c.handle];
    if (c.mode == kCapMove) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
    if (c.mode == kCapCreate || c.mode == kCapDraw) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
    if (capButtonAt(c, app.pointerX, app.pointerY) >= 0 || capStyleAt(c, app.pointerX, app.pointerY) >= 0) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (c.tool != bSelect && c.has) {
        if (!capSel(c).contains(app.pointerX, app.pointerY)) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        return c.tool == bText ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
    }
    const int hnd = capHandleAt(c, app.pointerX, app.pointerY);
    if (hnd >= 0) return resize[hnd];
    if (c.has && capSel(c).contains(app.pointerX, app.pointerY)) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE;
    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
}
