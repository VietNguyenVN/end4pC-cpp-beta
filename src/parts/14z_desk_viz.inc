// The visualizer of the desktop: bars, waves or a ring that follow the music, drawn with cairo only (no shaders) on a surface of the background
// layer. That layer sits under the bottom one of the desktop widgets, and the surface lets every click through. It is fed by the cava stream the
// bar's visualizer already reads (13_media_panel.inc), so it costs nothing while nothing plays; with a window fullscreen on its screen it stops.

constexpr double kDvBarFill = 0.62, kDvGradientGain = 1.8, kDvAttack = 0.55, kDvRelease = 0.2, kDvPeakFall = 0.9;

std::string deskVizFile() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/.local/state/end4pC/visualizer.txt";
}
void deskVizSave(const App& app, bool on) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(deskVizFile()).parent_path(), ec);
    std::ofstream out(deskVizFile(), std::ios::trunc);
    out << "enabled\t" << (on ? 1 : 0) << "\n";
    if (app.dv.ringL >= 0) out << "ring\t" << app.dv.ringL << "\t" << app.dv.ringT << "\n";
}
bool deskVizLoad(App& app) {
    std::ifstream in(deskVizFile());
    std::string line;
    bool on = false;
    while (std::getline(in, line)) {
        int l = 0, t = 0;
        if (line == "enabled\t1") on = true;
        else if (std::sscanf(line.c_str(), "ring\t%d\t%d", &l, &t) == 2) { app.dv.ringL = std::clamp(l, 0, 8000); app.dv.ringT = std::clamp(t, 0, 8000); }
    }
    return on;
}

bool deskVizWanted(const App& app) { return app.dv.surface && app.dv.configured && !app.dv.covered; }

void freeDeskVizBuffers(DeskViz& V) {
    for (int i = 0; i < 2; i++) {
        if (V.cs[i]) cairo_surface_destroy(V.cs[i]);
        if (V.buf[i]) wl_buffer_destroy(V.buf[i]);
        V.cs[i] = nullptr;
        V.buf[i] = nullptr;
        V.busy[i] = false;
    }
    if (V.mem) munmap(V.mem, V.memSize);
    V.mem = nullptr;
    V.bufW = V.bufH = 0;
}

void deskVizBufferRelease(void* flag, wl_buffer*) { *static_cast<bool*>(flag) = false; }
const wl_buffer_listener kDeskVizBuffer = {deskVizBufferRelease};

bool allocDeskVizBuffers(App& app) {
    DeskViz& V = app.dv;
    const double sc = V.scale.factor();  // bufW x bufH: the buffers' pixels, the logical size times the scale (src/hidpi.h)
    const int PW = hidpi::bufferExtent(V.W, sc), PH = hidpi::bufferExtent(V.H, sc);
    if (V.bufW == PW && V.bufH == PH && V.buf[0]) return true;
    freeDeskVizBuffers(V);
    const int stride = PW * 4;
    const size_t one = static_cast<size_t>(stride) * PH;
    const int fd = memfd_create("end4pC-deskviz", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, one * 2) < 0) { if (fd >= 0) close(fd); return false; }
    void* data = mmap(nullptr, one * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    V.mem = static_cast<unsigned char*>(data);
    V.memSize = one * 2;
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, static_cast<int32_t>(one * 2));
    for (int i = 0; i < 2; i++) {
        V.cs[i] = cairo_image_surface_create_for_data(V.mem + i * one, CAIRO_FORMAT_ARGB32, PW, PH, stride);
        cairo_surface_set_device_scale(V.cs[i], sc, sc);
        V.buf[i] = wl_shm_pool_create_buffer(pool, static_cast<int32_t>(i * one), PW, PH, stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(V.buf[i], &kDeskVizBuffer, &V.busy[i]);
    }
    wl_shm_pool_destroy(pool);
    close(fd);
    V.bufW = PW;
    V.bufH = PH;
    return true;
}

// ---- the levels ---------------------------------------------------------------------------------------------------------------------
// The cava points (50, low to high) are spread over the bars with a linear interpolation; a bar rises fast and falls slowly.
void deskVizUpdateLevels(App& app, bool playing) {
    DeskViz& V = app.dv;
    const int n = app.cfg.deskVizBars;
    V.shown.resize(n, 0.0);
    V.peaks.resize(n, 0.0);
    const auto& pts = app.media.points;
    for (int i = 0; i < n; i++) {
        double target = 0;
        if (playing && !pts.empty()) {
            const double pos = n > 1 ? static_cast<double>(i) * (pts.size() - 1) / (n - 1) : 0;
            const size_t a = static_cast<size_t>(pos), b = std::min(pts.size() - 1, a + 1);
            const double f = pos - a;
            target = std::clamp((pts[a] * (1 - f) + pts[b] * f) / 1000.0, 0.0, 1.0);
        }
        V.shown[i] += (target - V.shown[i]) * (target > V.shown[i] ? kDvAttack : kDvRelease);
        V.peaks[i] = std::max(V.peaks[i] * kDvPeakFall, V.shown[i]);
    }
}

// ---- the painting, in a frame where u runs along the base and v grows from the top to the base (Lw x Lh) ---------------------------------
Color deskVizTone(const App& app, double t) {
    const std::string& c = app.cfg.deskVizColor;
    if (c == "gradient") return mixColor(app.color("primary"), app.color("tertiary"), std::clamp(t, 0.0, 1.0));
    return app.color(c.c_str());
}
cairo_pattern_t* deskVizAcross(const App& app, double Lw) {
    cairo_pattern_t* p = cairo_pattern_create_linear(0, 0, Lw, 0);
    constexpr int stops = 6;
    for (int k = 0; k < stops; k++) {
        const Color c = deskVizTone(app, static_cast<double>(k) / (stops - 1));
        cairo_pattern_add_color_stop_rgba(p, static_cast<double>(k) / (stops - 1), c.r, c.g, c.b, 1);
    }
    return p;
}
// Paints with `src` through a vertical fade: opaque at yTop, transparent at yEnd.
void deskVizFadeFill(cairo_t* cr, cairo_pattern_t* src, double yTop, double yEnd, double aTop, double aEnd) {
    cairo_pattern_t* mask = cairo_pattern_create_linear(0, yTop, 0, yEnd);
    cairo_pattern_add_color_stop_rgba(mask, 0, 0, 0, 0, aTop);
    cairo_pattern_add_color_stop_rgba(mask, 1, 0, 0, 0, aEnd);
    cairo_set_source(cr, src);
    cairo_mask(cr, mask);
    cairo_pattern_destroy(mask);
}

// A bar that grows up from its base: only the top is rounded, the base is flat (the painting frame turns it to the side of the screen).
void deskVizBarTop(cairo_t* cr, double x, double y, double w, double h) {
    const double r = std::min(w / 2, h);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y + h);
    cairo_line_to(cr, x, y + r);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
    cairo_line_to(cr, x + w, y + h);
    cairo_close_path(cr);
}
// The same, hanging down from its base (the reflection): the top is flat, the bottom is round.
void deskVizBarBottom(cairo_t* cr, double x, double y, double w, double h) {
    const double r = std::min(w / 2, h);
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + w, y);
    cairo_line_to(cr, x + w, y + h - r);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * M_PI);
    cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
}
// The colour of one bar. The gradient is the one of the Quickshell visualizer: a low bar has the primary container's colour, a tall one the primary.
Color deskVizBarColor(const App& app, double intensity) {
    const std::string& c = app.cfg.deskVizColor;
    if (c == "gradient") return mixColor(app.color("primary"), app.color("primary_container"), std::clamp(intensity * kDvGradientGain, 0.0, 1.0));
    return app.color(c.c_str());
}

void deskVizBars(App& app, cairo_t* cr, double Lw, double Lh, bool reflect) {
    const DeskViz& V = app.dv;
    const int n = static_cast<int>(V.shown.size());
    const double pitch = Lw / n, bw = std::max(2.0, pitch * kDvBarFill), x0 = (pitch - bw) / 2;
    const double base = reflect ? Lh * 0.66 : Lh;
    const double reach = reflect ? base * 0.94 : Lh * 0.94;
    for (int i = 0; i < n; i++) {
        const double h = std::max(bw, V.shown[i] * reach), x = x0 + i * pitch;
        deskVizBarTop(cr, x, base - h, bw, h);
        setSource(cr, deskVizBarColor(app, V.shown[i]));
        cairo_fill(cr);
    }
    if (reflect) {  // the reflection: the same bars upside down, fading away under the base
        const double room = Lh - base - 2;
        cairo_push_group(cr);
        for (int i = 0; i < n; i++) {
            const double h = std::min(room, std::max(bw, V.shown[i] * reach) * 0.6), x = x0 + i * pitch;
            deskVizBarBottom(cr, x, base + 3, bw, h);
            setSource(cr, deskVizBarColor(app, V.shown[i]));
            cairo_fill(cr);
        }
        cairo_pattern_t* group = cairo_pop_group(cr);
        deskVizFadeFill(cr, group, base, base + std::max(8.0, room), 0.45, 0.0);
        cairo_pattern_destroy(group);
    }
}

void deskVizWave(App& app, cairo_t* cr, double Lw, double Lh) {
    const DeskViz& V = app.dv;
    const int n = static_cast<int>(V.shown.size());
    const double reach = Lh * 0.9, step = Lw / (n - 1);
    std::vector<double> y(n);
    for (int i = 0; i < n; i++) y[i] = Lh - 3 - std::max(0.02, V.shown[i]) * reach;
    const auto top = [&](bool move) {  // a Catmull-Rom spline through the points, as cubic curves
        if (move) cairo_move_to(cr, 0, y[0]);
        else cairo_line_to(cr, 0, y[0]);
        for (int i = 0; i < n - 1; i++) {
            const double y0 = y[std::max(0, i - 1)], y1 = y[i], y2 = y[i + 1], y3 = y[std::min(n - 1, i + 2)], x1 = i * step, x2 = (i + 1) * step;
            cairo_curve_to(cr, x1 + step / 6, y1 + (y2 - y0) / 6, x2 - step / 6, y2 - (y3 - y1) / 6, x2, y2);
        }
    };
    cairo_pattern_t* across = deskVizAcross(app, Lw);
    cairo_push_group(cr);
    top(true);
    cairo_line_to(cr, Lw, Lh);
    cairo_line_to(cr, 0, Lh);
    cairo_close_path(cr);
    cairo_set_source(cr, across);
    cairo_fill(cr);
    cairo_pattern_t* area = cairo_pop_group(cr);
    deskVizFadeFill(cr, area, Lh - reach, Lh, 0.7, 0.04);
    cairo_pattern_destroy(area);
    top(true);
    cairo_set_source(cr, across);
    cairo_set_line_width(cr, 3);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_pattern_destroy(across);
}

void deskVizDots(App& app, cairo_t* cr, double Lw, double Lh) {
    const DeskViz& V = app.dv;
    const int n = static_cast<int>(V.shown.size());
    const double pitch = Lw / n, d = std::max(3.0, pitch * 0.5), vp = d * 1.5, reach = Lh * 0.94;
    const int rows = std::max(1, static_cast<int>(reach / vp));
    cairo_pattern_t* across = deskVizAcross(app, Lw);
    for (int i = 0; i < n; i++) {  // only the lit dots: the ones that are off are not drawn
        const int lit = std::max(1, static_cast<int>(std::lround(V.shown[i] * rows)));
        const double cx = (i + 0.5) * pitch;
        for (int r = 0; r < lit && r < rows; r++) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, Lh - d / 2 - r * vp, d / 2, 0, 2 * M_PI);
        }
    }
    cairo_set_source(cr, across);
    cairo_fill(cr);
    cairo_pattern_destroy(across);
}

void deskVizWinamp(App& app, cairo_t* cr, double Lw, double Lh) {
    DeskViz& V = app.dv;
    const int n = static_cast<int>(V.shown.size());
    const double pitch = Lw / n, bw = std::max(2.0, pitch * kDvBarFill), x0 = (pitch - bw) / 2, segH = std::max(3.0, bw * 0.4), gap = std::max(1.5, segH * 0.5), seg = segH + gap;
    const int rows = std::max(2, static_cast<int>(Lh * 0.94 / seg));
    const Color low = app.color("primary"), mid = app.color("secondary"), high = app.color("tertiary");
    for (int r = 0; r < rows; r++) {
        const double f = static_cast<double>(r) / (rows - 1), k = f < 0.5 ? f / 0.5 : (f - 0.5) / 0.5;
        const Color c = f < 0.5 ? mixColor(low, mid, k) : mixColor(mid, high, k);
        const double y = Lh - segH - r * seg;
        for (int i = 0; i < n; i++) {  // only the lit segments and the peak caps: the ones that are off are not drawn
            const int lit = static_cast<int>(std::lround(V.shown[i] * rows)), cap = std::min(rows - 1, static_cast<int>(V.peaks[i] * rows));
            if (r < lit || (r == cap && cap >= lit)) cairo_rectangle(cr, x0 + i * pitch, y, bw, segH);
        }
        setSource(cr, c, 0.95);
        cairo_fill(cr);
    }
}

void deskVizLoadArt(App& app, int px) {
    DeskViz& V = app.dv;
    const std::string path = mediaCoverPath(app), key = app.musicArtUrl + "@" + std::to_string(px);
    if (key == V.artKey && (V.art || path.empty())) return;
    if (V.art) { cairo_surface_destroy(V.art); V.art = nullptr; }
    V.artKey = key;
    if (!path.empty()) V.art = loadSquare(path, px * 2);
}

void deskVizRing(App& app, cairo_t* cr, double ox, double oy, double S, double barAlpha) {
    const DeskViz& V = app.dv;
    const int n = static_cast<int>(V.shown.size());
    const double c = S / 2, r0 = S * 0.31, reach = S / 2 - r0 - 6, bw = std::clamp(2 * M_PI * r0 / n * kDvBarFill, 2.0, 14.0);
    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, bw);
    for (int i = 0; i < n; i++) {  // symmetric, so the ring has no seam: the first half mirrors on the second
        const int src = i < n / 2 ? i : n - 1 - i, half = std::max(1, n / 2);
        const double a = 2 * M_PI * i / n - M_PI / 2, len = std::max(bw / 2, V.shown[std::min(n - 1, src * 2)] * reach), cs = std::cos(a), sn = std::sin(a);
        cairo_move_to(cr, c + cs * r0, c + sn * r0);
        cairo_line_to(cr, c + cs * (r0 + len), c + sn * (r0 + len));
        setSource(cr, deskVizTone(app, static_cast<double>(src) / half), 0.95 * barAlpha);
        cairo_stroke(cr);
    }
    // the cover in the middle, round
    const double ar = r0 - bw - 6;
    cairo_arc(cr, c, c, ar, 0, 2 * M_PI);
    deskVizLoadArt(app, static_cast<int>(std::ceil(2 * ar)));
    if (app.dv.art) {
        cairo_save(cr);
        cairo_clip(cr);
        const double w = cairo_image_surface_get_width(app.dv.art), h = cairo_image_surface_get_height(app.dv.art), sc = 2 * ar / std::min(w, h);
        cairo_translate(cr, c - w * sc / 2, c - h * sc / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.dv.art, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        drawIcon(cr, "music_note", c - ar * 0.4, c - ar * 0.4, ar * 0.8, app.color("on_secondary_container"));
    }
    cairo_restore(cr);
}

void deskVizPaint(App& app, cairo_t* cr, bool playing) {
    DeskViz& V = app.dv;
    if (V.shown.empty() || !playing) return;  // (nothing is shown while no music plays)
    const std::string& style = app.cfg.deskVizStyle;
    if (style == "ring") {
        deskVizRing(app, cr, 0, 0, std::min(V.W, V.H), 1.0);
        return;
    }
    cairo_push_group(cr);
    {
        const std::string& pos = app.cfg.deskVizPos;
        const bool vertical = pos == "left" || pos == "right";
        const double Lw = vertical ? V.H : V.W, Lh = vertical ? V.W : V.H;
        cairo_matrix_t m;
        if (pos == "top") cairo_matrix_init(&m, 1, 0, 0, -1, 0, Lh);
        else if (pos == "left") cairo_matrix_init(&m, 0, 1, -1, 0, Lh, 0);
        else if (pos == "right") cairo_matrix_init(&m, 0, 1, 1, 0, 0, 0);
        else cairo_matrix_init_identity(&m);
        cairo_transform(cr, &m);
        if (style == "wave") deskVizWave(app, cr, Lw, Lh);
        else if (style == "dots") deskVizDots(app, cr, Lw, Lh);
        else if (style == "winamp") deskVizWinamp(app, cr, Lw, Lh);
        else deskVizBars(app, cr, Lw, Lh, style == "reflect");
    }
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

void deskVizFrameDone(void* d, wl_callback* cb, uint32_t);
const wl_callback_listener kDeskVizFrame = {deskVizFrameDone};

void deskVizDraw(App& app) {
    DeskViz& V = app.dv;
    if (!deskVizWanted(app) || V.W <= 0 || V.H <= 0) return;
    if (V.framePending) { V.dirty = true; return; }
    if (!allocDeskVizBuffers(app)) return;
    int idx = V.lastBuf == 0 ? 1 : 0;
    if (V.busy[idx]) idx = 1 - idx;
    if (V.busy[idx]) { V.dirty = true; return; }  // (both still held by the compositor)
    cairo_t* cr = mkcr(V.cs[idx]);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    deskVizPaint(app, cr, app.musicStatus == "Playing" && app.media.cavaPid > 0);
    cairo_destroy(cr);
    V.lastBuf = idx;
    V.busy[idx] = true;
    V.framePending = true;
    V.dirty = false;
    wl_callback_add_listener(wl_surface_frame(V.surface), &kDeskVizFrame, &app);
    wl_surface_attach(V.surface, V.buf[idx], 0, 0);
    wl_surface_damage_buffer(V.surface, 0, 0, V.bufW, V.bufH);
    if (V.scale.viewport) wp_viewport_set_destination(V.scale.viewport, V.W, V.H);
    wl_surface_commit(V.surface);
}

void deskVizFrameDone(void* d, wl_callback* cb, uint32_t) {
    auto* app = static_cast<App*>(d);
    wl_callback_destroy(cb);
    app->dv.framePending = false;
    if (app->dv.surface && app->dv.dirty) deskVizDraw(*app);
}

// One step of the animation, called with each cava frame (vizFrame).
void deskVizFrame(App& app, std::chrono::steady_clock::time_point now) {
    DeskViz& V = app.dv;
    if (!deskVizWanted(app) || now < V.last + std::chrono::milliseconds(16)) return;
    V.last = now;
    V.idleDrawn = false;
    deskVizUpdateLevels(app, true);
    deskVizDraw(app);
}

// The music stopped: the bars fall and rest (one drawing, no more frames until it plays again).
void deskVizIdle(App& app) {
    DeskViz& V = app.dv;
    if (!deskVizWanted(app) || V.idleDrawn) return;
    V.idleDrawn = true;
    V.shown.assign(app.cfg.deskVizBars, 0.0);
    V.peaks.assign(app.cfg.deskVizBars, 0.0);
    deskVizDraw(app);
}

// ---- a fullscreen window on the screen of the visualizer: it stops (Hyprland: asked after the events that can change it) ------------------
bool deskVizCoveredNow(App& app) {
    if (wmKind() != Wm::Hyprland) return false;
    const Bar* home = widgetBar(app);
    const std::string conn = home ? home->connector : "";
    const std::string out = captureArgv({"bash", "-c",
        "m=$(hyprctl -j monitors 2>/dev/null); hyprctl -j clients 2>/dev/null | jq -r --argjson m \"${m:-[]}\" --arg n \"$1\" "
        "'($m | map(select(.name == $n or $n == \"\")) | .[0].activeWorkspace.id // -1) as $w | any(.[]; .mapped and .fullscreen > 0 and .workspace.id == $w)'",
        "sh", conn});
    return out.rfind("true", 0) == 0;
}
void deskVizCheckCovered(App& app) {
    DeskViz& V = app.dv;
    if (!V.surface) return;
    const bool now = deskVizCoveredNow(app);
    if (now == V.covered) return;
    V.covered = now;
    if (!now) { V.idleDrawn = false; syncCava(app); deskVizDraw(app); }
    else syncCava(app);
}

// ---- the surface -------------------------------------------------------------------------------------------------------------------------
// The ring is a widget of its own: it keeps its place (ringL, ringT: margins from the screen corner) and, with the widgets unlocked, is dragged
// by hand. The other designs fill an edge of the screen and let every click through.
void deskVizRingClamp(App& app) {
    DeskViz& V = app.dv;
    const int S = app.cfg.deskVizSize, sw = deskScreenW(app), sh = deskScreenH(app);
    if (V.ringL < 0) { V.ringL = std::max(0, (sw - S) / 2); V.ringT = std::max(0, (sh - S) / 2); }
    V.ringL = std::clamp(V.ringL, 0, std::max(0, sw - S));
    V.ringT = std::clamp(V.ringT, 0, std::max(0, sh - S));
}

void deskVizLayout(App& app) {
    DeskViz& V = app.dv;
    if (!V.layer) return;
    const std::string& pos = app.cfg.deskVizPos;
    const int S = app.cfg.deskVizSize;
    const bool ring = app.cfg.deskVizStyle == "ring", vertical = pos == "left" || pos == "right";
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    uint32_t anchor = 0;
    int w = 0, h = 0, top = 0, left = 0;
    if (ring) {
        w = h = S;
        anchor = T | L;
        deskVizRingClamp(app);
        top = V.ringT;
        left = V.ringL;
    } else if (vertical) {
        w = S;
        anchor = T | B | (pos == "left" ? L : R);
    } else {
        h = S;
        anchor = L | R | (pos == "bottom" ? B : pos == "top" ? T : 0);
    }
    zwlr_layer_surface_v1_set_anchor(V.layer, anchor);
    zwlr_layer_surface_v1_set_size(V.layer, w, h);
    zwlr_layer_surface_v1_set_exclusive_zone(V.layer, ring ? -1 : 0);
    zwlr_layer_surface_v1_set_margin(V.layer, top, 0, 0, left);
    wl_region* region = wl_compositor_create_region(app.compositor);
    if (ring && !app.cfg.widgetsLocked) wl_region_add(region, 0, 0, S, S);
    wl_surface_set_input_region(V.surface, region);  // (empty: the clicks go to the wallpaper and the widgets)
    wl_region_destroy(region);
    V.layoutSig = app.cfg.deskVizStyle + ":" + pos + ":" + std::to_string(S) + ":" + std::to_string(app.cfg.widgetsLocked);
    wl_surface_commit(V.surface);
}

// One move per frame, as the keyboard's drag does: the pointer is relative to the surface, which moves with it.
void deskVizMoveDone(void* d, wl_callback* cb, uint32_t) {
    wl_callback_destroy(cb);
    static_cast<App*>(d)->dv.movePending = false;
}
const wl_callback_listener kDeskVizMove = {deskVizMoveDone};

void deskVizMotion(App& app) {
    DeskViz& V = app.dv;
    if (!V.dragging || V.movePending || !V.layer) return;
    const double dx = app.pointerX - V.grabX, dy = app.pointerY - V.grabY;
    if (std::fabs(dx) < 0.5 && std::fabs(dy) < 0.5) return;
    V.ringL += static_cast<int>(std::lround(dx));
    V.ringT += static_cast<int>(std::lround(dy));
    deskVizRingClamp(app);
    zwlr_layer_surface_v1_set_margin(V.layer, V.ringT, 0, 0, V.ringL);
    V.movePending = true;
    wl_callback_add_listener(wl_surface_frame(V.surface), &kDeskVizMove, &app);
    wl_surface_commit(V.surface);
}

void deskVizButton(App& app, uint32_t button, bool pressed) {
    DeskViz& V = app.dv;
    if (button != 0x110) return;
    if (!pressed) {
        if (V.dragging) { V.dragging = false; deskVizSave(app, true); }
        return;
    }
    if (app.cfg.deskVizStyle != "ring" || app.cfg.widgetsLocked) return;
    V.dragging = true;
    V.grabX = app.pointerX;
    V.grabY = app.pointerY;
}

void onDeskVizConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    DeskViz& V = app->dv;
    V.W = static_cast<int>(w);
    V.H = static_cast<int>(h);
    V.configured = true;
    V.idleDrawn = false;
    V.framePending = false;
    deskVizUpdateLevels(*app, false);
    deskVizDraw(*app);
}
void deskVizClose(App& app, bool forget);
void onDeskVizClosed(void* d, zwlr_layer_surface_v1*) { deskVizClose(*static_cast<App*>(d), false); }
const zwlr_layer_surface_v1_listener kDeskVizListener = {onDeskVizConfigure, onDeskVizClosed};

void deskVizOpen(App& app) {
    DeskViz& V = app.dv;
    if (V.surface || !app.compositor || !app.layerShell || !app.shm) return;
    const Bar* home = widgetBar(app);
    V.output = home ? home->output : nullptr;
    V.enabled = true;
    V.covered = false;
    V.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, V.scale, V.surface, [&app] { deskVizDraw(app); });
    V.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, V.surface, V.output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "end4pC-desk-viz");
    zwlr_layer_surface_v1_set_exclusive_zone(V.layer, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(V.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(V.layer, &kDeskVizListener, &app);
    deskVizLayout(app);
    deskVizSave(app, true);
    V.sig.clear();
    deskVizCheckCovered(app);
    syncCava(app);
}

void deskVizClose(App& app, bool forget) {
    DeskViz& V = app.dv;
    if (V.layer) zwlr_layer_surface_v1_destroy(V.layer);
    hidpi::detach(V.scale);
    if (V.surface) wl_surface_destroy(V.surface);
    V.layer = nullptr;
    V.surface = nullptr;
    V.configured = V.framePending = V.dirty = V.dragging = V.movePending = false;
    app.onViz = false;
    freeDeskVizBuffers(V);
    V.shown.clear();
    V.peaks.clear();
    V.sig.clear();
    if (V.art) { cairo_surface_destroy(V.art); V.art = nullptr; }
    V.artKey.clear();
    if (forget) { V.enabled = false; deskVizSave(app, false); }
    syncCava(app);
}

// A new output creates a wallpaper surface that would sit above this one: the visualizer is made again after it.
void deskVizReopen(App& app) {
    if (!app.dv.surface) return;
    deskVizClose(app, false);
    deskVizOpen(app);
}

void deskVizRestore(App& app) {
    if (deskVizLoad(app)) deskVizOpen(app);
}

void deskVizSync(App& app) {
    DeskViz& V = app.dv;
    if (!V.surface) return;
    const Bar* home = widgetBar(app);
    if (home && home->output != V.output) { deskVizReopen(app); return; }
    const std::string sig = app.cfg.deskVizStyle + ":" + app.cfg.deskVizPos + ":" + std::to_string(app.cfg.deskVizSize) + ":" + app.cfg.deskVizColor + ":" + std::to_string(app.cfg.deskVizBars) + ":" + std::to_string(app.cfg.widgetsLocked);
    if (sig == V.sig) return;  // (drawAll calls this often: only a change does anything)
    const std::string layout = app.cfg.deskVizStyle + ":" + app.cfg.deskVizPos + ":" + std::to_string(app.cfg.deskVizSize) + ":" + std::to_string(app.cfg.widgetsLocked);
    if (layout != V.layoutSig) deskVizLayout(app);
    V.sig = sig;
    V.idleDrawn = false;
    deskVizUpdateLevels(app, app.musicStatus == "Playing" && app.media.cavaPid > 0);
    deskVizDraw(app);
}

void deskVizToggle(App& app, const std::string& action, std::string& reply) {
    const bool open = app.dv.surface != nullptr;
    if (action == "show" || (action != "hide" && !open)) deskVizOpen(app);
    else deskVizClose(app, true);
    reply = app.dv.surface ? "shown" : "hidden";
}
