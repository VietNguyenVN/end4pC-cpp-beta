// The game overlay (SUPER + G): the screen dims, a taskbar sits at the top, and small cards (resources, volume, recorder) float over the game.
// The crosshair is a separate surface that stays on the screen after the overlay closes, and lets every click through. Only the way it opens
// comes from end-4's overlay: the content zooms in from 108% to 100% while the dimming fades in.
constexpr double kGameZoom = 1.08, kGameZoomMs = 320, kGameDimMs = 200, kGameCloseMs = 180, kGameDim = 0.5;
constexpr double kGameBarY = 50, kGameBtn = 44, kGameCardW = 340, kGameRadius = 28;
constexpr int kGameCross = 56;

struct GameHit { Rect r; int id = 0, arg = 0; };
enum GameId { GhNone = 0, GhTaskbar, GhClose, GhHead, GhVolTrack, GhVolMute, GhRecord, GhShot, GhNoteText, GhNoteCopy, GhResize, GhPin };
const char* const kGameNames[kGwCount] = {"Resources", "Volume", "Recorder", "Notes", "Crosshair"};
const char* const kGameIcons[kGwCount] = {"cpu", "volume-loud", "videocamera-record", "notebook-minimalistic", "target"};

double gameEase(double p) { p = std::clamp(p, 0.0, 1.0); return 1 - std::pow(1 - p, 4); }  // (fast start, long landing)

void gameBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }  // (the crosshair's one-shot buffers)
const wl_buffer_listener kGameBuffer = {gameBufferRelease};
void gamePoolRelease(void* flag, wl_buffer*) { *static_cast<bool*>(flag) = false; }
const wl_buffer_listener kGamePoolBuffer = {gamePoolRelease};

void freeGameBuffers(GameOverlay& G) {
    for (int i = 0; i < 2; i++) {
        if (G.cs[i]) cairo_surface_destroy(G.cs[i]);
        if (G.buf[i]) wl_buffer_destroy(G.buf[i]);
        G.cs[i] = nullptr;
        G.buf[i] = nullptr;
        G.busy[i] = false;
    }
    if (G.mem[0]) munmap(G.mem[0], G.memSize);
    G.mem[0] = G.mem[1] = nullptr;
    G.bufW = G.bufH = 0;
}

bool allocGameBuffers(App& app) {
    GameOverlay& G = app.game;
    if (G.bufW == G.W && G.bufH == G.H && G.buf[0]) return true;
    freeGameBuffers(G);
    const int stride = G.W * 4;
    const size_t one = static_cast<size_t>(stride) * G.H;
    const int fd = memfd_create("end4pC-game", 0);
    if (fd < 0 || ftruncate(fd, one * 2) < 0) { if (fd >= 0) close(fd); return false; }
    void* data = mmap(nullptr, one * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    G.memSize = one * 2;
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, static_cast<int32_t>(one * 2));
    for (int i = 0; i < 2; i++) {
        G.mem[i] = static_cast<unsigned char*>(data) + i * one;
        G.cs[i] = cairo_image_surface_create_for_data(static_cast<unsigned char*>(G.mem[i]), CAIRO_FORMAT_ARGB32, G.W, G.H, stride);
        G.buf[i] = wl_shm_pool_create_buffer(pool, static_cast<int32_t>(i * one), G.W, G.H, stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(G.buf[i], &kGamePoolBuffer, &G.busy[i]);
    }
    wl_shm_pool_destroy(pool);
    close(fd);
    G.bufW = G.W;
    G.bufH = G.H;
    return true;
}

void gameFrameDone(void* d, wl_callback* cb, uint32_t);
const wl_callback_listener kGameFrame = {gameFrameDone};


constexpr double kGameTile = 76, kGameTileGap = 8, kGameTempH = 52, kGameCardPad = 12, kGameBadge = 44;
double gameCardH(const GameOverlay& G, int w) {
    if (w == GwNotes) return G.nh;
    const double head = kGameCardPad + kGameBadge + 12;
    if (w == GwResources) return head + 3 * kGameTile + 3 * kGameTileGap + kGameTempH + kGameCardPad;
    if (w == GwVolume) return head + 84 + kGameCardPad;
    return head + 2 * 56 + kGameTileGap + kGameCardPad;  // recorder
}

Rect gameCardRect(const GameOverlay& G, int w) { return {G.wx[w], G.wy[w], w == GwNotes ? G.nw : kGameCardW, gameCardH(G, w)}; }

Rect gameTaskbarRect(const App& app, const GameOverlay& G) {
    const double w = 8 * 2 + kGwCount * kGameBtn + (kGwCount - 1) * 4 + 12 + 1 + 12 + 70 + (app.batPresent ? 60 : 0);
    return {(G.W - w) / 2, kGameBarY, w, kGameBtn + 16};
}

// An M3 progress bar in the expressive form: the filled part, a gap, the track, and a dot at the end of the track.
void gameBar(App& app, cairo_t* cr, double x, double y, double w, double h, double f) {
    f = std::clamp(f, 0.0, 1.0);
    const double fill = std::max(h, (w - 4 - h) * f), gap = 4;
    roundedRect(cr, x, y, fill, h, h / 2);
    setSource(cr, app.color("primary"));
    cairo_fill(cr);
    const double tx = x + fill + gap;
    if (tx + h / 2 < x + w) {
        roundedRect(cr, tx, y, x + w - tx, h, h / 2);
        setSource(cr, app.color("secondary_container"), 0.75);
        cairo_fill(cr);
        cairo_arc(cr, x + w - h / 2, y + h / 2, h / 4, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
}

#include "14ya_game_notes.inc"

void drawGameCard(App& app, cairo_t* cr, PangoLayout* L, int w, std::vector<GameHit>& hits) {
    GameOverlay& G = app.game;
    const Rect r = gameCardRect(G, w);
    const Color ink = app.color("on_surface"), dim = app.color("on_surface_variant");
    static cairo_surface_t* shadow[kGwCount] = {};  // (a soft shadow under each card; not rebuilt while the notes card is being resized)
    static int shadowW[kGwCount] = {}, shadowH[kGwCount] = {};
    if (G.rsMode == 0 && (!shadow[w] || shadowW[w] != static_cast<int>(r.w) || shadowH[w] != static_cast<int>(r.h))) {
        if (shadow[w]) cairo_surface_destroy(shadow[w]);
        shadow[w] = makeShadowMask(r.w, r.h, kGameRadius);
        shadowW[w] = static_cast<int>(r.w);
        shadowH[w] = static_cast<int>(r.h);
    }
    if (shadow[w]) {
        setSource(cr, app.color("shadow"), 0.5);
        cairo_mask_surface(cr, shadow[w], r.x - kLaunShadow, r.y - kLaunShadow + 3);
    }
    roundedRect(cr, r.x, r.y, r.w, r.h, kGameRadius);
    setSource(cr, app.color("surface_container"), 0.98);
    cairo_fill(cr);
    const bool ghost = w == GwNotes && g_gn.ghostDraw;  // (pinned, the overlay closed: only the box of text)
    // the header: an M3 shape badge with the icon, a title and a line under it, and the close button; the header drags the card
    const double headRight = w == GwNotes ? 112 : 64;
    if (!ghost) hits.push_back({{r.x, r.y, r.w - headRight, kGameCardPad + kGameBadge + 8}, GhHead, w});
    const double hx = r.x + 16, hy = r.y + kGameCardPad;
    if (!ghost) {
        shapePath(cr, kM3IconShape, hx, hy, kGameBadge);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        drawIcon(cr, kGameIcons[w], hx + (kGameBadge - 24) / 2, hy + (kGameBadge - 24) / 2, 24, app.color("on_primary_container"));
        const char* sub = w == GwResources ? "Live" : w == GwVolume ? (app.muted ? "Muted" : "Output") : w == GwNotes ? (g_gn.dirty ? "Saving..." : "Saved") : (app.recording ? "Recording" : "Ready");
        const double tx = hx + kGameBadge + 14, tw = r.w - (w == GwNotes ? 180 : 120);
        clkText(cr, L, i18n::tr(kGameNames[w]), 16, true, tx, hy + 14, ink, 1, tw);
        clkText(cr, L, i18n::tr(sub), 12.5, false, tx, hy + 33, app.recording && w == GwRecorder ? app.color("error") : dim, 1, tw);
        const Rect x{r.x + r.w - 16 - 40, hy + 2, 40, 40};
        hits.push_back({x, GhClose, w});
        cairo_arc(cr, x.x + 20, x.y + 20, 20, 0, 2 * M_PI);
        setSource(cr, app.color("surface_container_highest"), G.hover == GhClose * 10 + w ? 1 : 0.7);
        cairo_fill(cr);
        drawIcon(cr, "close-circle", x.x + 10, x.y + 10, 20, dim);
        if (w == GwNotes) {  // pinned: it stays on the screen, translucent, when the overlay closes
            const Rect pin{x.x - 8 - 40, x.y, 40, 40};
            hits.push_back({pin, GhPin, 0});
            cairo_arc(cr, pin.x + 20, pin.y + 20, 20, 0, 2 * M_PI);
            setSource(cr, app.color(G.notesPinned ? "primary" : "surface_container_highest"), G.notesPinned || G.hover == GhPin * 10 ? 1 : 0.7);
            cairo_fill(cr);
            drawIcon(cr, "pin", pin.x + 10, pin.y + 10, 20, app.color(G.notesPinned ? "on_primary" : "on_surface_variant"));
        }
    }
    const double bx = r.x + kGameCardPad, bw = r.w - 2 * kGameCardPad;
    double y = hy + kGameBadge + 12;
    const auto tile = [&](double h) {
        roundedRect(cr, bx, y, bw, h, 20);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
    };
    if (w == GwNotes) {
        drawGameNotes(app, cr, L, r, hits);
        return;
    }
    if (w == GwResources) {
        const App::Resources& R = app.res;
        char ram[40];
        std::snprintf(ram, sizeof ram, "%.1f / %.1f GB", R.ramUsedGb, R.ramTotalGb);
        const struct { const char* name; const char* icon; double v; std::string txt; } rows[3] = {
            {"CPU", "cpu", R.cpu, std::to_string(static_cast<int>(std::lround(R.cpu * 100))) + "%"},
            {"Memory", "server-square", R.ram, ram},
            {"Disk", "database", R.disk, std::to_string(static_cast<int>(std::lround(R.disk * 100))) + "%"}};
        for (const auto& row : rows) {
            tile(kGameTile);
            cairo_arc(cr, bx + 12 + 20, y + 12 + 20, 20, 0, 2 * M_PI);
            setSource(cr, app.color("secondary_container"));
            cairo_fill(cr);
            drawIcon(cr, row.icon, bx + 12 + 8, y + 12 + 8, 24, app.color("on_secondary_container"));
            const double lx = bx + 12 + 40 + 12;
            clkText(cr, L, i18n::tr(row.name), 12.5, false, lx, y + 12 + 10, dim, 1, bw - 120);
            clkText(cr, L, row.txt, 17, true, lx, y + 12 + 30, ink, 1, bw - 80);
            gameBar(app, cr, bx + 14, y + kGameTile - 12 - 8, bw - 28, 8, row.v);
            y += kGameTile + kGameTileGap;
        }
        tile(kGameTempH);
        cairo_arc(cr, bx + 12 + 14, y + kGameTempH / 2, 14, 0, 2 * M_PI);
        setSource(cr, app.color("tertiary_container"));
        cairo_fill(cr);
        drawIcon(cr, "thermometer", bx + 12 + 5, y + kGameTempH / 2 - 9, 18, app.color("on_tertiary_container"));
        clkText(cr, L, i18n::tr("Temperature"), 13, false, bx + 12 + 28 + 12, y + kGameTempH / 2.0, dim, 1, bw * 0.5);
        char b[32];
        std::snprintf(b, sizeof b, R.tempC >= 0 ? "%.0f °C" : "—", R.tempC);
        const int tw = clkTextWidth(L, b, 17, true);
        clkText(cr, L, b, 17, true, bx + bw - 16 - tw, y + kGameTempH / 2.0, ink, 1);
    } else if (w == GwVolume) {
        tile(84);
        const Rect mute{bx + 14, y + 20, 44, 44};
        hits.push_back({mute, GhVolMute, 0});
        cairo_arc(cr, mute.x + 22, mute.y + 22, 22, 0, 2 * M_PI);
        setSource(cr, app.color(app.muted ? "error_container" : "secondary_container"));
        cairo_fill(cr);
        if (G.hover == GhVolMute * 10) { cairo_arc(cr, mute.x + 22, mute.y + 22, 22, 0, 2 * M_PI); setSource(cr, app.color(app.muted ? "on_error_container" : "on_secondary_container"), 0.1); cairo_fill(cr); }
        drawIcon(cr, app.muted ? "volume-cross" : "volume-loud", mute.x + 11, mute.y + 11, 22, app.color(app.muted ? "on_error_container" : "on_secondary_container"));
        const std::string pct = std::to_string(app.volume) + "%";
        const int pw = clkTextWidth(L, pct, 17, true);
        const double sx = mute.x + 44 + 14, sw = bx + bw - 14 - pw - 14 - sx;
        hits.push_back({{sx - 8, y + 20, sw + 16, 44}, GhVolTrack, 0});
        const double f = std::clamp(app.volume / 100.0, 0.0, 1.0), fill = std::max(16.0, (sw - 8) * f);
        roundedRect(cr, sx, y + 42 - 8, fill, 16, 8);  // the expressive slider: filled part, a gap with the handle, then the track
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        const double hx2 = sx + fill + 4;
        roundedRect(cr, hx2 - 1, y + 42 - 16, 4, 32, 2);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        if (hx2 + 9 < sx + sw) {
            roundedRect(cr, hx2 + 7, y + 42 - 8, sx + sw - hx2 - 7, 16, 8);
            setSource(cr, app.color("secondary_container"), 0.75);
            cairo_fill(cr);
        }
        clkText(cr, L, pct, 17, true, bx + bw - 14 - pw, y + 42, ink, 1);
    } else {
        const bool rec = app.recording;
        const auto button = [&](const Rect& p, const char* icon, const std::string& label, const char* bg, const char* fgName, int id) {
            hits.push_back({p, id, 0});
            roundedRect(cr, p.x, p.y, p.w, p.h, p.h / 2);
            setSource(cr, app.color(bg));
            cairo_fill(cr);
            if (G.hover == id * 10) { roundedRect(cr, p.x, p.y, p.w, p.h, p.h / 2); setSource(cr, app.color(fgName), 0.1); cairo_fill(cr); }
            const Color fg = app.color(fgName);
            const int tw = clkTextWidth(L, label, 16, true);
            const double x0 = p.x + (p.w - 30 - tw) / 2;
            drawIcon(cr, icon, x0, p.y + (p.h - 22) / 2, 22, fg);
            clkText(cr, L, label, 16, true, x0 + 30, p.y + p.h / 2.0, fg, 1);
        };
        button({bx, y, bw, 56}, rec ? "stop-circle" : "videocamera-record", i18n::tr(rec ? "Stop recording" : "Record the screen"), rec ? "error" : "primary", rec ? "on_error" : "on_primary", GhRecord);
        button({bx, y + 56 + kGameTileGap, bw, 56}, "crop", i18n::tr("Screenshot"), "secondary_container", "on_secondary_container", GhShot);
    }
}

void drawGame(App& app) {
    GameOverlay& G = app.game;
    if (!G.surface || !G.configured || G.W <= 0) return;
    const bool partial = G.partialRequest && G.pendValid;
    G.partialRequest = false;
    if (G.framePending) {  // one redraw per frame: this one waits for the compositor
        G.dirty = true;
        if (!partial) G.needFull = true;
        return;
    }
    int idx = G.lastBuf == 0 ? 1 : 0;
    if (!allocGameBuffers(app)) return;
    if (G.busy[idx]) idx = 1 - idx;
    if (G.busy[idx]) { G.dirty = true; G.needFull = true; return; }  // (both still held by the compositor)
    const auto now = std::chrono::steady_clock::now();
    const double el = std::chrono::duration<double, std::milli>(now - G.t0).count();
    double zoom = 1, dim = 1, content = 1;
    if (G.closing) {
        const double p = std::clamp(el / kGameCloseMs, 0.0, 1.0);
        dim = content = 1 - p;
        G.animating = p < 1;
    } else {
        zoom = kGameZoom - (kGameZoom - 1) * gameEase(el / kGameZoomMs);
        dim = std::clamp(el / kGameDimMs, 0.0, 1.0);
        content = std::clamp(el / (kGameDimMs * 0.8), 0.0, 1.0);
        G.animating = el < kGameZoomMs;
    }
    const int W = G.W, H = G.H;
    cairo_t* cr = mkcr(G.cs[idx]);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0, 0, 0, kGameDim * dim);
    cairo_paint(cr);
    std::vector<GameHit> hits;
    PangoLayout* L = pango_cairo_create_layout(cr);
    cairo_save(cr);
    cairo_translate(cr, W / 2.0, H / 2.0);
    cairo_scale(cr, zoom, zoom);
    cairo_translate(cr, -W / 2.0, -H / 2.0);
    cairo_push_group(cr);
    {  // the taskbar
        const Rect t = gameTaskbarRect(app, G);
        roundedRect(cr, t.x, t.y, t.w, t.h, 26);
        setSource(cr, app.color("surface_container"), 0.97);
        cairo_fill_preserve(cr);
        setSource(cr, app.color("outline_variant"), 0.6);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        hits.push_back({t, 99, 0});  // (a click on the bar itself does nothing, and does not close)
        double x = t.x + 8;
        for (int w = 0; w < kGwCount; w++) {
            const Rect b{x, t.y + 8, kGameBtn, kGameBtn};
            hits.push_back({b, GhTaskbar, w});
            const bool on = w == GwCrosshair ? G.crossOn : G.open[w], hot = G.hover == GhTaskbar * 10 + w;
            cairo_arc(cr, b.x + kGameBtn / 2, b.y + kGameBtn / 2, kGameBtn / 2, 0, 2 * M_PI);
            setSource(cr, app.color(on ? "primary" : "surface_container_highest"), on ? 1 : (hot ? 1 : 0.6));
            cairo_fill(cr);
            drawIcon(cr, kGameIcons[w], b.x + (kGameBtn - 22) / 2, b.y + (kGameBtn - 22) / 2, 22, app.color(on ? "on_primary" : "on_surface"));
            x += kGameBtn + 4;
        }
        x += 8;
        setSource(cr, app.color("outline_variant"));
        cairo_rectangle(cr, x, t.y + 14, 1, t.h - 28);
        cairo_fill(cr);
        x += 13;
        clkText(cr, L, app.clockTime, 21, true, x, t.y + t.h / 2.0, app.color("on_surface"), 1);
        if (app.batPresent) {
            const std::string b = std::to_string(app.batPct) + "%";
            const int tw = clkTextWidth(L, b, 14, true);
            clkText(cr, L, b, 14, true, t.x + t.w - 16 - tw, t.y + t.h / 2.0, app.color(app.batCharging ? "primary" : "on_surface_variant"), 1);
        }
    }
    for (int w = 0; w < GwCrosshair; w++) if (G.open[w]) drawGameCard(app, cr, L, w, hits);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, content);
    cairo_restore(cr);
    G.hits = std::move(hits);
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_flush(G.cs[idx]);
    G.busy[idx] = true;
    G.lastBuf = idx;
    wl_surface_attach(G.surface, G.buf[idx], 0, 0);
    if (partial) {
        const int x0 = std::max(0, static_cast<int>(std::floor(G.pendX0)) - 4), y0 = std::max(0, static_cast<int>(std::floor(G.pendY0)) - 4);
        const int x1 = std::min(W, static_cast<int>(std::ceil(G.pendX1)) + 4), y1 = std::min(H, static_cast<int>(std::ceil(G.pendY1)) + 4);
        wl_surface_damage_buffer(G.surface, x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
    } else {
        wl_surface_damage_buffer(G.surface, 0, 0, W, H);
    }
    G.pendValid = false;
    G.dirty = G.needFull = false;
    G.framePending = true;
    wl_callback* cb = wl_surface_frame(G.surface);
    wl_callback_add_listener(cb, &kGameFrame, &app);
    wl_surface_commit(G.surface);
}

void gameFrameDone(void* d, wl_callback* cb, uint32_t) {
    auto* app = static_cast<App*>(d);
    wl_callback_destroy(cb);
    GameOverlay& G = app->game;
    G.framePending = false;
    if (!G.surface || !G.dirty) return;
    G.partialRequest = !G.needFull && G.pendValid;
    drawGame(*app);
}

// ---- the crosshair: its own small surface, centred, with no input ---------------------------------------------------------------
void drawCrosshair(App& app) {
    GameOverlay& G = app.game;
    if (!G.crossSurface || !G.crossConfigured) return;
    const int S = kGameCross, stride = S * 4, size = stride * S;
    const int fd = memfd_create("end4pC-cross", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, S, S, stride);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const double c = S / 2.0;
    const auto arms = [&](double width, double r, double g, double b, double a) {
        cairo_set_source_rgba(cr, r, g, b, a);
        cairo_set_line_width(cr, width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
        for (int k = 0; k < 4; k++) {
            const double dx = k == 0 ? 1 : k == 1 ? -1 : 0, dy = k == 2 ? 1 : k == 3 ? -1 : 0;
            cairo_move_to(cr, c + dx * 6, c + dy * 6);
            cairo_line_to(cr, c + dx * 16, c + dy * 16);
            cairo_stroke(cr);
        }
    };
    arms(4, 0, 0, 0, 0.55);   // outline
    const Color p = app.color("primary");
    arms(2, p.r, p.g, p.b, 1);
    cairo_arc(cr, c, c, 1.6, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, 1);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, S, S, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kGameBuffer, nullptr);
    wl_surface_attach(G.crossSurface, buffer, 0, 0);
    wl_surface_damage_buffer(G.crossSurface, 0, 0, S, S);
    wl_surface_commit(G.crossSurface);
}

void onCrossConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->game.crossConfigured = true;
    drawCrosshair(*app);
}
void closeCrosshair(App& app);
void onCrossClosed(void* d, zwlr_layer_surface_v1*) { closeCrosshair(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kCrossListener = {onCrossConfigure, onCrossClosed};

void closeCrosshair(App& app) {
    GameOverlay& G = app.game;
    if (G.crossLayer) zwlr_layer_surface_v1_destroy(G.crossLayer);
    if (G.crossSurface) wl_surface_destroy(G.crossSurface);
    G.crossLayer = nullptr;
    G.crossSurface = nullptr;
    G.crossConfigured = false;
    G.crossOn = false;
}

void openCrosshair(App& app) {
    GameOverlay& G = app.game;
    if (G.crossSurface || !app.compositor || !app.layerShell || !app.shm) return;
    const Bar* bar = mainBar(app);
    G.crossSurface = wl_compositor_create_surface(app.compositor);
    G.crossLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, G.crossSurface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-crosshair");
    zwlr_layer_surface_v1_set_anchor(G.crossLayer, 0);  // (no anchor: the middle of the screen)
    zwlr_layer_surface_v1_set_size(G.crossLayer, kGameCross, kGameCross);
    zwlr_layer_surface_v1_set_exclusive_zone(G.crossLayer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(G.crossLayer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(G.crossLayer, &kCrossListener, &app);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(G.crossSurface, none);  // every click goes to the game
    wl_region_destroy(none);
    wl_surface_commit(G.crossSurface);
    G.crossOn = true;
}

// ---- the overlay --------------------------------------------------------------------------------------------------------------------
void closeGameNow(App& app) {
    GameOverlay& G = app.game;
    if (!G.surface) return;
    if (G.layer) zwlr_layer_surface_v1_destroy(G.layer);
    wl_surface_destroy(G.surface);
    G.surface = nullptr;
    G.layer = nullptr;
    G.configured = false;
    G.closing = G.animating = false;
    G.hover = 0;
    G.dragW = -1;
    G.volDrag = false;
    G.hits.clear();
    G.framePending = G.dirty = false;
    freeGameBuffers(G);
    app.onGame = false;
    g_gn.selDrag = false;
    G.rsMode = 0;
    gameNotesGhostSync(app);
}

void onGameConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    GameOverlay& G = app->game;
    G.W = static_cast<int>(w);
    G.H = static_cast<int>(h);
    if (!G.configured) {
        G.t0 = std::chrono::steady_clock::now();
        if (!G.placed) {  // first time: the cards sit in a row under the taskbar
            for (int i = 0; i < GwNotes; i++) { G.wx[i] = 60 + i * (kGameCardW + 24); G.wy[i] = 150; }
            G.placed = true;
        }
        gnPlace(*app);
    }
    G.configured = true;
    drawGame(*app);
}
void onGameClosed(void* d, zwlr_layer_surface_v1*) { closeGameNow(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kGameListener = {onGameConfigure, onGameClosed};

void openGame(App& app) {
    GameOverlay& G = app.game;
    if (!app.compositor || !app.layerShell || !app.shm) return;
    if (G.surface) {  // (closing: bring it back)
        if (G.closing) { G.closing = false; G.t0 = std::chrono::steady_clock::now(); drawGame(app); }
        return;
    }
    closeAllPopups(app);
    syncCatcher(app);
    closeNotesGhost(app);
    const Bar* bar = mainBar(app);
    G.hover = 0;
    G.closing = false;
    G.surface = wl_compositor_create_surface(app.compositor);
    G.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, G.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-game-overlay");
    zwlr_layer_surface_v1_set_anchor(G.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(G.layer, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(G.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(G.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(G.layer, &kGameListener, &app);
    wl_surface_commit(G.surface);
}

void retractGame(App& app) {
    GameOverlay& G = app.game;
    if (!G.surface || G.closing) return;
    G.closing = true;
    G.t0 = std::chrono::steady_clock::now();
    G.dragW = -1;
    G.volDrag = false;
    drawGame(app);
}
void toggleGame(App& app) { if (app.game.surface && !app.game.closing) retractGame(app); else openGame(app); }

void tickGame(App& app, std::chrono::steady_clock::time_point now) {
    GameOverlay& G = app.game;
    if (!G.surface || !G.configured) return;
    if (G.closing && now - G.t0 >= std::chrono::milliseconds(static_cast<int>(kGameCloseMs))) { closeGameNow(app); return; }
    bool redraw = G.animating;
    gameNotesTick(app, now, redraw);
    if (!G.closing && now >= G.nextRes) {  // the numbers of the resources card
        G.nextRes = now + std::chrono::seconds(1);
        updateResources(app);
        redraw = true;
    }
    if (redraw) drawGame(app);
}
int gameCursorShape(const App& app) {
    const int over = app.game.hover;
    if (g_gn.selDrag || over == GhNoteText * 10) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    const int id = over / 10, arg = over % 10;
    if (app.game.rsMode == 3 || (id == GhResize && arg == 3)) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
    if (app.game.rsMode == 1 || (id == GhResize && arg == 1)) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
    if (app.game.rsMode == 2 || (id == GhResize && arg == 2)) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
    return id == GhNone ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
}
std::chrono::steady_clock::time_point gameWake(const App& app) {
    const GameOverlay& G = app.game;
    if (!G.surface || !G.configured) return std::chrono::steady_clock::time_point::max();
    if (G.animating || G.closing) return std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    return std::min(G.nextRes, gameNotesWake());
}

// ---- input --------------------------------------------------------------------------------------------------------------------------
int gameHitAt(const GameOverlay& G, double x, double y, GameHit* out = nullptr) {
    for (auto it = G.hits.rbegin(); it != G.hits.rend(); ++it)
        if (it->r.contains(x, y)) { if (out) *out = *it; return it->id * 10 + std::min(it->arg, 9); }
    return 0;
}

void gameSetVolume(App& app, double x, const GameHit& track) {
    const double f = std::clamp((x - (track.r.x + 8)) / (track.r.w - 16), 0.0, 1.0);
    const int v = static_cast<int>(std::lround(f * 100));
    if (v == app.volume) return;
    app.volume = v;
    const auto now = std::chrono::steady_clock::now();
    if (now - app.game.lastVolSet < std::chrono::milliseconds(40)) return;
    app.game.lastVolSet = now;
    const std::string pct = std::to_string(v) + "%";
    runDetached({"wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", pct.c_str()});
}

void gameMotion(App& app) {
    GameOverlay& G = app.game;
    if (g_gn.selDrag) {
        gnDragSelect(app);
        drawGame(app);
        return;
    }
    if (G.rsMode != 0) {  // resizing the notes card from its right edge, its bottom edge or its corner
        const Rect before = gameCardRect(G, GwNotes);
        if (G.rsMode & 1) G.nw = std::clamp(G.rsW + app.pointerX - G.rsX, kGnMinW, std::max(kGnMinW, G.W - G.wx[GwNotes]));
        if (G.rsMode & 2) G.nh = std::clamp(G.rsH + app.pointerY - G.rsY, kGnMinH, std::max(kGnMinH, G.H - G.wy[GwNotes]));
        const Rect after = gameCardRect(G, GwNotes);
        const double x0 = before.x - kLaunShadow - 4, y0 = before.y - kLaunShadow - 4, x1 = std::max(before.x + before.w, after.x + after.w) + kLaunShadow + 4, y1 = std::max(before.y + before.h, after.y + after.h) + kLaunShadow + 4;
        if (G.pendValid) { G.pendX0 = std::min(G.pendX0, x0); G.pendY0 = std::min(G.pendY0, y0); G.pendX1 = std::max(G.pendX1, x1); G.pendY1 = std::max(G.pendY1, y1); }
        else { G.pendX0 = x0; G.pendY0 = y0; G.pendX1 = x1; G.pendY1 = y1; G.pendValid = true; }
        G.partialRequest = true;
        drawGame(app);
        return;
    }
    if (G.dragW >= 0) {  // only the part the card passed over is damaged
        const Rect before = gameCardRect(G, G.dragW);
        G.wx[G.dragW] = std::clamp(app.pointerX - G.grabX, 0.0, std::max(0.0, G.W - before.w));
        G.wy[G.dragW] = std::clamp(app.pointerY - G.grabY, 0.0, std::max(0.0, G.H - before.h));
        const Rect after = gameCardRect(G, G.dragW);
        const double x0 = std::min(before.x, after.x), y0 = std::min(before.y, after.y), x1 = std::max(before.x + before.w, after.x + after.w), y1 = std::max(before.y + before.h, after.y + after.h);
        if (G.pendValid) { G.pendX0 = std::min(G.pendX0, x0); G.pendY0 = std::min(G.pendY0, y0); G.pendX1 = std::max(G.pendX1, x1); G.pendY1 = std::max(G.pendY1, y1); }
        else { G.pendX0 = x0; G.pendY0 = y0; G.pendX1 = x1; G.pendY1 = y1; G.pendValid = true; }
        G.partialRequest = true;
        drawGame(app);
        return;
    }
    if (G.volDrag) {
        for (const GameHit& h : G.hits) if (h.id == GhVolTrack) { gameSetVolume(app, app.pointerX, h); break; }
        drawGame(app);
        return;
    }
    const int over = gameHitAt(G, app.pointerX, app.pointerY);
    if (over != G.hover) { G.hover = over; drawGame(app); updateCursor(app); }
}

void gameButton(App& app, uint32_t button, bool pressed) {
    GameOverlay& G = app.game;
    if (button != 0x110) return;
    if (!pressed) {
        const bool was = G.dragW >= 0 || G.volDrag || G.rsMode != 0 || g_gn.selDrag;
        if (G.rsMode != 0 || G.dragW == GwNotes) { G.rsMode = 0; gnSaveState(app); }
        g_gn.selDrag = false;
        G.dragW = -1;
        G.volDrag = false;
        if (was) drawGame(app);
        return;
    }
    if (G.closing) return;
    GameHit h;
    const int over = gameHitAt(G, app.pointerX, app.pointerY, &h);
    if (over == 0) { retractGame(app); return; }  // (the dimmed screen: a click closes)
    if (h.id != GhNoteText && h.id != GhNoteCopy) g_gn.focus = false;
    switch (h.id) {
        case GhTaskbar:
            if (h.arg == GwCrosshair) { if (G.crossOn) closeCrosshair(app); else openCrosshair(app); }
            else {
                G.open[h.arg] = !G.open[h.arg];
                if (h.arg == GwNotes) { g_gn.focus = G.open[GwNotes]; gnSaveState(app); }
            }
            break;
        case GhClose: G.open[h.arg] = false; if (h.arg == GwNotes) gnSaveState(app); break;
        case GhNoteText: gnClick(app, false); break;
        case GhNoteCopy:
            if (h.arg >= 0 && h.arg < static_cast<int>(g_gn.copies.size())) copyToClipboard(g_gn.copies[h.arg]);
            break;
        case GhPin: G.notesPinned = !G.notesPinned; gnSaveState(app); break;
        case GhResize: G.rsMode = h.arg; G.rsX = app.pointerX; G.rsY = app.pointerY; G.rsW = G.nw; G.rsH = G.nh; break;
        case GhHead: G.dragW = h.arg; G.grabX = app.pointerX - G.wx[h.arg]; G.grabY = app.pointerY - G.wy[h.arg]; break;
        case GhVolTrack: G.volDrag = true; gameSetVolume(app, app.pointerX, h); break;
        case GhVolMute: app.muted = !app.muted; runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}); break;
        case GhRecord:
            if (app.recording) stopRecording(app); else { retractGame(app); startRecording(app); }
            break;
        case GhShot: retractGame(app); openCapture(app); break;
        default: break;
    }
    drawGame(app);
}
