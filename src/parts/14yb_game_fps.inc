// PARKED (kGameFpsReady is false in 14y_game_overlay.inc): the card has no button, so none of this runs. What was tried for the numbers, on a test
// video of a known rate (24, 30 and 60 frames per second): counting the frame callbacks of a small surface read the refresh rate of the monitor
// whatever the window did (Hyprland schedules a repaint for each callback), and counting the frames a capture of the window delivers read 28, 34-40
// and 25-29 (the capture does not follow the rate of the window). A reliable count needs the program itself: MangoHud, or a Vulkan/GL layer.
// The pinning (the translucent copy), the state file and the card are kept for when there is a source of numbers.
//
// The FPS card of the game overlay: a counter. It shows how often the screen is redrawn (a game's frame rate when the game fills the screen), with the
// frame time and a graph of the last 30 seconds. Like the notes, the card can be pinned: it stays on the screen, translucent, when the overlay closes.
//
// How it counts: a 1x1 transparent layer surface, with no input, asks the compositor for a frame callback and asks again each time one arrives; the
// compositor sends one each time it redraws that screen. No game frame is copied, so the cost is a wake-up per frame. The surface exists only while
// the card is open (overlay up) or pinned. This file is included by 14y_game_overlay.inc after the notes.

constexpr double kFpsGraphH = 104;
constexpr size_t kFpsHistMax = 60;
constexpr std::chrono::milliseconds kFpsSample{500};

struct GameFps { bool loaded = false, placed = false, ghostDraw = false; };
GameFps g_gf;

void fpsSaveState(const App& app) {
    const GameOverlay& G = app.game;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(gnFile("x")).parent_path(), ec);
    std::ofstream(gnFile("game-fps.state"), std::ios::trunc) << static_cast<int>(G.wx[GwFps]) << " " << static_cast<int>(G.wy[GwFps]) << " " << (G.fpsPinned ? 1 : 0) << " " << (G.open[GwFps] ? 1 : 0) << "\n";
}
void fpsLoad(App& app) {
    if (g_gf.loaded) return;
    g_gf.loaded = true;
    GameOverlay& G = app.game;
    std::ifstream st(gnFile("game-fps.state"));
    int x = 0, y = 0, pin = 0, open = 0;
    if (st >> x >> y >> pin >> open) {
        G.wx[GwFps] = std::clamp(x, 0, 8000);
        G.wy[GwFps] = std::clamp(y, 0, 8000);
        G.fpsPinned = pin != 0;
        G.open[GwFps] = open != 0 && kGameFpsReady;
        g_gf.placed = true;
    }
}

// The card goes where it was, or under the notes the first time.
void fpsPlace(App& app) {
    GameOverlay& G = app.game;
    fpsLoad(app);
    if (!g_gf.placed) {
        G.wx[GwFps] = std::max(0.0, G.W - kGameCardW - 60);
        G.wy[GwFps] = std::min(150 + G.nh + 24, std::max(0.0, G.H - kGameFpsH));
        g_gf.placed = true;
    }
    G.wx[GwFps] = std::clamp(G.wx[GwFps], 0.0, std::max(0.0, G.W - kGameCardW));
    G.wy[GwFps] = std::clamp(G.wy[GwFps], 0.0, std::max(0.0, G.H - kGameFpsH));
}

// ---- the counter: the frames the active window presents, seen through a capture of it ------------------------------------------------------------
// ext-image-copy-capture (the live windows of the overview use it): a frame is delivered each time the window has new content, so the frames that
// arrive in a second are the frames per second of the window. The next one is asked for as soon as one arrives. The copy costs a little while it runs.
struct FpsCap {
    ext_image_copy_capture_session_v1* ses = nullptr;
    ext_image_copy_capture_frame_v1* frame = nullptr;
    wl_shm_pool* pool = nullptr;
    wl_buffer* buf = nullptr;
    void* map = nullptr;
    size_t mapSize = 0;
    int bw = 0, bh = 0, fails = 0;
    uint32_t fmt = 0;
    bool running = false, inFlight = false, realloc = false, dead = false;
    std::string id;  // the window being counted: hyprctl's stableId
    std::chrono::steady_clock::time_point retryAt{}, nextTarget{};
    App* app = nullptr;
};
FpsCap g_fcap;

void drawFpsGhost(App& app);
void fpsCapRequest();

void fpsCapFreeBuffer() {
    if (g_fcap.buf) wl_buffer_destroy(g_fcap.buf);
    if (g_fcap.pool) wl_shm_pool_destroy(g_fcap.pool);
    if (g_fcap.map) munmap(g_fcap.map, g_fcap.mapSize);
    g_fcap.buf = nullptr; g_fcap.pool = nullptr; g_fcap.map = nullptr; g_fcap.mapSize = 0;
}
void fpsCapAllocBuffer() {
    FpsCap& c = g_fcap;
    fpsCapFreeBuffer();
    if (!c.app->shm || c.bw <= 0 || c.bh <= 0 || c.bw > 16384 || c.bh > 16384 || !c.fmt) { c.dead = true; return; }
    const int stride = c.bw * 4;
    const size_t size = static_cast<size_t>(stride) * c.bh;
    const int fd = memfd_create("end4pC-fps", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); c.dead = true; return; }
    c.map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (c.map == MAP_FAILED) { c.map = nullptr; close(fd); c.dead = true; return; }
    c.mapSize = size;
    c.pool = wl_shm_create_pool(c.app->shm, fd, size);
    close(fd);
    c.buf = wl_shm_pool_create_buffer(c.pool, 0, c.bw, c.bh, stride, c.fmt);
}

void fpsSesSize(void*, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h) { g_fcap.bw = static_cast<int>(w); g_fcap.bh = static_cast<int>(h); }
void fpsSesShm(void*, ext_image_copy_capture_session_v1*, uint32_t f) { if (f == WL_SHM_FORMAT_ARGB8888 || (f == WL_SHM_FORMAT_XRGB8888 && !g_fcap.fmt)) g_fcap.fmt = f; }
void fpsSesDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
void fpsSesDmabuf(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}
void fpsSesDone(void*, ext_image_copy_capture_session_v1*) {
    if (g_fcap.inFlight) { g_fcap.realloc = true; return; }
    fpsCapAllocBuffer();
    if (!g_fcap.dead) fpsCapRequest();
}
void fpsSesStopped(void*, ext_image_copy_capture_session_v1*) { g_fcap.dead = true; }
const ext_image_copy_capture_session_v1_listener kFpsSession = {fpsSesSize, fpsSesShm, fpsSesDevice, fpsSesDmabuf, fpsSesDone, fpsSesStopped};

void fpsFrameEnd() {
    g_fcap.inFlight = false;
    if (g_fcap.frame) { ext_image_copy_capture_frame_v1_destroy(g_fcap.frame); g_fcap.frame = nullptr; }
    if (g_fcap.realloc) { g_fcap.realloc = false; fpsCapAllocBuffer(); }
}
void fpsFrameTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}
void fpsFrameDamage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {}
void fpsFrameTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {}
void fpsFrameReady(void*, ext_image_copy_capture_frame_v1*) {
    GameOverlay& G = g_fcap.app->game;
    const auto now = std::chrono::steady_clock::now();
    G.fpsFrames.push_back(now);
    while (!G.fpsFrames.empty() && now - G.fpsFrames.front() > std::chrono::seconds(1)) G.fpsFrames.pop_front();
    g_fcap.fails = 0;
    fpsFrameEnd();
    if (g_fcap.running && !g_fcap.dead) fpsCapRequest();  // (the next one right away: it arrives when the window has something new)
}
void fpsFrameFailed(void*, ext_image_copy_capture_frame_v1*, uint32_t reason) {
    fpsFrameEnd();
    const bool resized = reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS;  // (the session sends the new size, then done)
    if (!resized && ++g_fcap.fails >= 6) g_fcap.dead = true;
    g_fcap.retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(resized ? 120 : 400);
}
const ext_image_copy_capture_frame_v1_listener kFpsFrame = {fpsFrameTransform, fpsFrameDamage, fpsFrameTime, fpsFrameReady, fpsFrameFailed};

void fpsCapRequest() {
    FpsCap& c = g_fcap;
    if (!c.ses || !c.buf || c.inFlight || c.dead) return;
    c.frame = ext_image_copy_capture_session_v1_create_frame(c.ses);
    ext_image_copy_capture_frame_v1_add_listener(c.frame, &kFpsFrame, nullptr);
    ext_image_copy_capture_frame_v1_attach_buffer(c.frame, c.buf);
    ext_image_copy_capture_frame_v1_damage_buffer(c.frame, 0, 0, c.bw, c.bh);
    ext_image_copy_capture_frame_v1_capture(c.frame);
    c.inFlight = true;
}

// hyprctl's stableId of the window with the focus ("" when there is none), the id the toplevel list of the compositor uses.
std::string fpsActiveWindowId() {
    const std::string aw = hyprRequest("j/activewindow");
    const size_t p = aw.find("\"stableId\": \"");
    return p == std::string::npos ? "" : aw.substr(p + 13, aw.find('"', p + 13) - p - 13);
}

void fpsCapStop(App& app) {
    FpsCap& c = g_fcap;
    if (c.frame) ext_image_copy_capture_frame_v1_destroy(c.frame);
    if (c.ses) ext_image_copy_capture_session_v1_destroy(c.ses);
    c.frame = nullptr;
    c.ses = nullptr;
    c.inFlight = c.realloc = c.dead = false;
    c.fails = 0;
    c.bw = c.bh = 0;
    c.fmt = 0;
    fpsCapFreeBuffer();
    app.game.fpsFrames.clear();
}

// Starts counting the frames of the window with this id.
void fpsCapStart(App& app, const std::string& id) {
    FpsCap& c = g_fcap;
    c.app = &app;
    if (!app.ftSrcMgr || !app.copyCapMgr || id.empty()) return;
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    for (const FtHandle& f : app.ftHandles) if (f.id == id) handle = f.h;
    if (!handle) return;
    ext_image_capture_source_v1* src = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(app.ftSrcMgr, handle);
    c.ses = ext_image_copy_capture_manager_v1_create_session(app.copyCapMgr, src, 0);
    ext_image_capture_source_v1_destroy(src);
    ext_image_copy_capture_session_v1_add_listener(c.ses, &kFpsSession, nullptr);
    c.id = id;
}

// The counter runs while the FPS card is open with the overlay up, or pinned with the overlay closed.
void fpsProbeSync(App& app) {
    GameOverlay& G = app.game;
    const bool needed = kGameFpsReady && G.open[GwFps] && ((G.surface && !G.closing) || (G.fpsPinned && !G.surface));
    if (needed && !g_fcap.running) {
        g_fcap.running = true;
        g_fcap.app = &app;
        g_fcap.nextTarget = {};
        G.fpsSampled = {};
    } else if (!needed && g_fcap.running) {
        g_fcap.running = false;
        fpsCapStop(app);
        g_fcap.id.clear();
        G.fpsHist.clear();
        G.fpsNow = 0;
    }
}

// Every loop: follow the window that has the focus, ask again after a failure, and twice a second take the number and draw it.
void tickFps(App& app, std::chrono::steady_clock::time_point now) {
    GameOverlay& G = app.game;
    FpsCap& c = g_fcap;
    if (!c.running) return;
    if (now >= c.nextTarget) {
        c.nextTarget = now + std::chrono::seconds(1);
        const std::string id = fpsActiveWindowId();  // (empty while the overlay has the focus: the window being counted stays)
        if ((!id.empty() && id != c.id) || (c.dead && !c.id.empty())) {
            const std::string next = id.empty() ? c.id : id;
            fpsCapStop(app);
            fpsCapStart(app, next);
        } else if (c.id.empty() && !id.empty()) {
            fpsCapStart(app, id);
        }
    }
    if (!c.dead && c.ses && c.buf && !c.inFlight && now >= c.retryAt) fpsCapRequest();
    if (G.fpsSampled == std::chrono::steady_clock::time_point{}) G.fpsSampled = now;
    if (now - G.fpsSampled >= kFpsSample) {
        G.fpsSampled = now;
        while (!G.fpsFrames.empty() && now - G.fpsFrames.front() > std::chrono::seconds(1)) G.fpsFrames.pop_front();
        G.fpsNow = static_cast<double>(G.fpsFrames.size());
        G.fpsHist.push_back(static_cast<float>(G.fpsNow));
        if (G.fpsHist.size() > kFpsHistMax) G.fpsHist.erase(G.fpsHist.begin());
        if (G.fpsGhostSurface) drawFpsGhost(app);
        if (G.surface && !G.closing && G.open[GwFps]) drawGame(app);
    }
}
std::chrono::steady_clock::time_point fpsWake(const App& app) {
    if (!g_fcap.running) return std::chrono::steady_clock::time_point::max();
    return app.game.fpsSampled + kFpsSample;
}

// ---- the card ----------------------------------------------------------------------------------------------------------------------------------
void drawGameFps(App& app, cairo_t* cr, PangoLayout* L, double bx, double bw, double y) {
    const GameOverlay& G = app.game;
    const Color ink = app.color("on_surface"), dim = app.color("on_surface_variant");
    // the number, and the time a frame takes
    roundedRect(cr, bx, y, bw, 84, 20);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);
    const bool known = G.fpsNow > 0;
    const std::string value = known ? std::to_string(static_cast<int>(std::lround(G.fpsNow))) : "–";
    clkText(cr, L, value, 36, true, bx + 20, y + 36, ink, 1);
    const int vw = clkTextWidth(L, value, 36, true);
    clkText(cr, L, "FPS", 13, true, bx + 20 + vw + 8, y + 46, dim, 1);
    char ms[24];
    std::snprintf(ms, sizeof ms, known ? "%.1f ms" : "– ms", known ? 1000.0 / G.fpsNow : 0.0);
    const int mw = clkTextWidth(L, ms, 17, true);
    clkText(cr, L, ms, 17, true, bx + bw - 20 - mw, y + 42, ink, 0.85);
    y += 84 + 8;
    // the graph: the last 30 seconds
    roundedRect(cr, bx, y, bw, kFpsGraphH, 20);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);
    const double gx = bx + 16, gy = y + 18, gw = bw - 32, gh = kFpsGraphH - 36;
    float top = 30;
    for (float v : G.fpsHist) top = std::max(top, v);
    top = std::ceil(top * 1.1f / 10.0f) * 10.0f;
    cairo_set_line_width(cr, 1);
    setSource(cr, app.color("outline_variant"), 0.5);
    cairo_move_to(cr, gx, gy + gh + 0.5);
    cairo_line_to(cr, gx + gw, gy + gh + 0.5);
    cairo_stroke(cr);
    if (G.fpsHist.size() >= 2) {
        const double step = gw / static_cast<double>(kFpsHistMax - 1), x0 = gx + gw - step * static_cast<double>(G.fpsHist.size() - 1);
        cairo_new_path(cr);
        for (size_t i = 0; i < G.fpsHist.size(); i++) {
            const double px = x0 + step * static_cast<double>(i), py = gy + gh - gh * std::clamp(static_cast<double>(G.fpsHist[i]) / top, 0.0, 1.0);
            if (i == 0) cairo_move_to(cr, px, py); else cairo_line_to(cr, px, py);
        }
        const double lastX = x0 + step * static_cast<double>(G.fpsHist.size() - 1);
        cairo_line_to(cr, lastX, gy + gh);
        cairo_line_to(cr, x0, gy + gh);
        cairo_close_path(cr);
        setSource(cr, app.color("primary"), 0.22);
        cairo_fill(cr);
        cairo_new_path(cr);
        for (size_t i = 0; i < G.fpsHist.size(); i++) {
            const double px = x0 + step * static_cast<double>(i), py = gy + gh - gh * std::clamp(static_cast<double>(G.fpsHist[i]) / top, 0.0, 1.0);
            if (i == 0) cairo_move_to(cr, px, py); else cairo_line_to(cr, px, py);
        }
        setSource(cr, app.color("primary"));
        cairo_set_line_width(cr, 2);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke(cr);
    }
    char label[16];
    std::snprintf(label, sizeof label, "%.0f", static_cast<double>(top));
    clkText(cr, L, label, 11, false, bx + 16, y + 9, dim, 0.8);
}

// ---- pinned: the card stays after the overlay closes, translucent, and the clicks go through it ----------------------------------------------
void onFpsGhostConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t);
void closeFpsGhost(App& app) {
    GameOverlay& G = app.game;
    if (G.fpsGhostLayer) zwlr_layer_surface_v1_destroy(G.fpsGhostLayer);
    hidpi::detach(G.fpsGhostScale);
    if (G.fpsGhostSurface) wl_surface_destroy(G.fpsGhostSurface);
    G.fpsGhostLayer = nullptr;
    G.fpsGhostSurface = nullptr;
    G.fpsGhostConfigured = false;
}
void onFpsGhostClosed(void* d, zwlr_layer_surface_v1*) { closeFpsGhost(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kFpsGhostListener = {onFpsGhostConfigure, onFpsGhostClosed};

void drawFpsGhost(App& app) {
    GameOverlay& G = app.game;
    if (!G.fpsGhostSurface || !G.fpsGhostConfigured) return;
    const int R = kLaunShadow + 4, W = static_cast<int>(kGameCardW) + 2 * R, H = static_cast<int>(kGameFpsH) + 2 * R;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, G.fpsGhostScale.factor(), "end4pC-fps-ghost")) return;
    cairo_surface_t* surf = hf.surface;
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    std::vector<GameHit> ignored;
    g_gf.ghostDraw = true;
    const int hover = G.hover;
    G.hover = 0;
    cairo_push_group(cr);
    cairo_translate(cr, R - G.wx[GwFps], R - G.wy[GwFps]);
    drawGameCard(app, cr, L, GwFps, ignored);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, kGnGhostAlpha);
    G.hover = hover;
    g_gf.ghostDraw = false;
    g_object_unref(L);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kGameBuffer, nullptr);
    hidpi::present(G.fpsGhostSurface, buffer, hf, G.fpsGhostScale);
}

void onFpsGhostConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->game.fpsGhostConfigured = true;
    drawFpsGhost(*app);
}

void openFpsGhost(App& app) {
    GameOverlay& G = app.game;
    if (G.fpsGhostSurface || !app.compositor || !app.layerShell || !app.shm) return;
    fpsLoad(app);
    const Bar* bar = mainBar(app);
    const int R = kLaunShadow + 4;
    // (a position saved on another screen size must not put the card off the screen)
    if (bar && bar->wpW > 0) G.wx[GwFps] = std::clamp(G.wx[GwFps], 0.0, std::max(0.0, static_cast<double>(bar->wpW) - kGameCardW));
    if (bar && bar->wpH > 0) G.wy[GwFps] = std::clamp(G.wy[GwFps], 0.0, std::max(0.0, static_cast<double>(bar->wpH) - kGameFpsH));
    G.fpsGhostSurface = wl_compositor_create_surface(app.compositor);
    attachScale(app, G.fpsGhostScale, G.fpsGhostSurface, [&app] { drawFpsGhost(app); });
    G.fpsGhostLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, G.fpsGhostSurface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-game-fps");
    zwlr_layer_surface_v1_set_anchor(G.fpsGhostLayer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(G.fpsGhostLayer, static_cast<int>(kGameCardW) + 2 * R, static_cast<int>(kGameFpsH) + 2 * R);
    zwlr_layer_surface_v1_set_margin(G.fpsGhostLayer, static_cast<int>(G.wy[GwFps]) - R, 0, 0, static_cast<int>(G.wx[GwFps]) - R);
    zwlr_layer_surface_v1_set_exclusive_zone(G.fpsGhostLayer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(G.fpsGhostLayer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(G.fpsGhostSurface, none);
    wl_region_destroy(none);
    zwlr_layer_surface_v1_add_listener(G.fpsGhostLayer, &kFpsGhostListener, &app);
    wl_surface_commit(G.fpsGhostSurface);
}

// The ghost is there while the card is pinned and open and the overlay is not.
void gameFpsGhostSync(App& app) {
    GameOverlay& G = app.game;
    fpsLoad(app);
    if (G.fpsPinned && G.open[GwFps] && !G.surface) { if (!G.fpsGhostSurface) openFpsGhost(app); }
    else closeFpsGhost(app);
    fpsProbeSync(app);
}
