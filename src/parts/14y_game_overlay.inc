// The game overlay (SUPER + G): the screen dims, a taskbar sits at the top, and small cards (resources, volume, recorder) float over the game.
// The crosshair is a separate surface that stays on the screen after the overlay closes, and lets every click through. Only the way it opens
// comes from end-4's overlay: the content zooms in from 108% to 100% while the dimming fades in.
constexpr double kGameZoom = 1.08, kGameZoomMs = 320, kGameDimMs = 200, kGameCloseMs = 180, kGameDim = 0.5;
constexpr double kGameBarY = 50, kGameBtn = 44, kGameCardW = 340, kGameRadius = 28;
constexpr int kGameCross = 56;

struct GameHit { Rect r; int id = 0, arg = 0; };
enum GameId { GhNone = 0, GhTaskbar, GhClose, GhHead, GhVolTrack, GhVolMute, GhRecord, GhShot, GhNoteText, GhNoteCopy, GhResize, GhPin,
              GhMixTab, GhMixDev, GhMixPick, GhMixApp, GhMixAppMute, GhMicMute, GhMicTrack };
// The FPS card is parked: no way of counting the frames of another window was accurate enough (see 14yb_game_fps.inc), so it has no button yet.
constexpr bool kGameFpsReady = false;
const char* const kGameNames[kGwCount] = {"Resources", "Volume", "Recorder", "Notes", "FPS", "Crosshair"};
const char* const kGameIcons[kGwCount] = {"cpu", "volume-loud", "videocamera-record", "notebook-minimalistic", "playback-speed", "target"};

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
    const double sc = G.scale.factor();  // bufW x bufH: the buffers' pixels, the logical size times the scale (src/hidpi.h)
    const int PW = hidpi::bufferExtent(G.W, sc), PH = hidpi::bufferExtent(G.H, sc);
    if (G.bufW == PW && G.bufH == PH && G.buf[0]) return true;
    freeGameBuffers(G);
    const int stride = PW * 4;
    const size_t one = static_cast<size_t>(stride) * PH;
    const int fd = memfd_create("end4pC-game", 0);
    if (fd < 0 || ftruncate(fd, one * 2) < 0) { if (fd >= 0) close(fd); return false; }
    void* data = mmap(nullptr, one * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    G.memSize = one * 2;
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, static_cast<int32_t>(one * 2));
    for (int i = 0; i < 2; i++) {
        G.mem[i] = static_cast<unsigned char*>(data) + i * one;
        G.cs[i] = cairo_image_surface_create_for_data(static_cast<unsigned char*>(G.mem[i]), CAIRO_FORMAT_ARGB32, PW, PH, stride);
        cairo_surface_set_device_scale(G.cs[i], sc, sc);
        G.buf[i] = wl_shm_pool_create_buffer(pool, static_cast<int32_t>(i * one), PW, PH, stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(G.buf[i], &kGamePoolBuffer, &G.busy[i]);
    }
    wl_shm_pool_destroy(pool);
    close(fd);
    G.bufW = PW;
    G.bufH = PH;
    return true;
}

void gameFrameDone(void* d, wl_callback* cb, uint32_t);
const wl_callback_listener kGameFrame = {gameFrameDone};


constexpr double kGameTile = 76, kGameTileGap = 8, kGameTempH = 52, kGameCardPad = 12, kGameBadge = 44;
constexpr double kGameFpsH = kGameCardPad + kGameBadge + 12 + 84 + 8 + 104 + kGameCardPad;  // the FPS card: head, the number, the graph
double gameCardH(const GameOverlay& G, int w) {
    if (w == GwNotes) return G.nh;
    if (w == GwFps) return kGameFpsH;
    const double head = kGameCardPad + kGameBadge + 12;
    if (w == GwResources) return head + (G.gpuRow ? 4 : 3) * (kGameTile + kGameTileGap) + kGameTempH + kGameCardPad;
    if (w == GwVolume) return G.volH;
    return head + 2 * 56 + kGameTileGap + kGameCardPad;  // recorder
}

Rect gameCardRect(const GameOverlay& G, int w) { return {G.wx[w], G.wy[w], w == GwNotes ? G.nw : kGameCardW, gameCardH(G, w)}; }

Rect gameTaskbarRect(const App& app, const GameOverlay& G) {
    const int buttons = kGwCount - (kGameFpsReady ? 0 : 1);
    const double w = 8 * 2 + buttons * kGameBtn + (buttons - 1) * 4 + 12 + 1 + 12 + 70 + (app.batPresent ? 60 : 0);
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
#include "14yb_game_fps.inc"

// ---- the volume card as a mixer: output / input, the devices to switch to, and the volume of each app ------------------------------------
constexpr double kMixTabH = 40, kMixTile = 84, kMixDevH = 52, kMixDevRow = 40, kMixAppH = 64, kMixNoteH = 52, kMixGap = 8, kMixAppGap = 6, kMixMoreH = 20;
constexpr size_t kMixMaxApps = 4;

// The devices (indexes into app.amenu.devs) the current tab can switch to.
std::vector<int> gameMixDevices(const App& app) {
    std::vector<int> out;
    const bool sink = app.game.mixTab == 0;
    for (size_t i = 0; i < app.amenu.devs.size(); i++) {
        const AudioDevice& d = app.amenu.devs[i];
        if (d.sink == sink && d.action == "set") out.push_back(static_cast<int>(i));
    }
    return out;
}

// The height of the card with what is on it now (drawGameCard makes the same sums as it goes down the card).
constexpr size_t kMixMaxDevices = 8;
size_t gameMixAppsShown(const App& app) {  // up to four apps, fewer when the card would not fit the screen (the first one always)
    const GameOverlay& G = app.game;
    const size_t apps = std::min(app.amenu.apps.size(), kMixMaxApps);
    if (apps == 0 || G.H <= 0) return apps;
    double base = kGameCardPad + kGameBadge + 12 + kMixTabH + kMixGap + kMixTile + kMixGap + kMixDevH + kMixGap + kGameCardPad;
    if (G.mixDevOpen) base += static_cast<double>(std::min(gameMixDevices(app).size(), kMixMaxDevices)) * kMixDevRow + 6;
    const double room = G.H - 150 - base;  // (the taskbar is at the top)
    size_t fit = room > 0 ? static_cast<size_t>((room + kMixAppGap) / (kMixAppH + kMixAppGap)) : 0;
    return std::clamp<size_t>(fit, 1, apps);
}
double gameVolCardH(const App& app) {
    const GameOverlay& G = app.game;
    const size_t apps = app.amenu.apps.size(), shown = gameMixAppsShown(app);
    double h = kGameCardPad + kGameBadge + 12;
    h += kMixTabH + kMixGap + kMixTile + kMixGap + kMixDevH;
    if (G.mixDevOpen) h += static_cast<double>(std::min(gameMixDevices(app).size(), kMixMaxDevices)) * kMixDevRow + 6;
    if (G.mixTab == 0) {
        h += kMixGap;
        h += shown ? shown * kMixAppH + (shown - 1) * kMixAppGap : kMixNoteH;
        if (apps > shown) h += kMixAppGap + kMixMoreH;
    }
    return h + kGameCardPad;
}

// The expressive slider of the cards: the filled part, a gap with the handle, then the track (and a mark where 100 % is, when it goes higher).
void gameSlider(App& app, cairo_t* cr, double sx, double cy, double sw, double f, bool loud, bool muted, double bh) {
    const double fill = std::max(bh, (sw - 8) * f);
    const Color color = muted ? app.color("outline") : app.color(loud ? "tertiary" : "primary");
    roundedRect(cr, sx, cy - bh / 2, fill, bh, bh / 2);
    setSource(cr, color);
    cairo_fill(cr);
    const double hx = sx + fill + 4;
    roundedRect(cr, hx - 1, cy - bh, 4, bh * 2, 2);
    setSource(cr, color);
    cairo_fill(cr);
    if (hx + 9 < sx + sw) {
        roundedRect(cr, hx + 7, cy - bh / 2, sx + sw - hx - 7, bh, bh / 2);
        setSource(cr, app.color("secondary_container"), 0.75);
        cairo_fill(cr);
    }
    if (volMark(app) > 0) {
        const double mx = sx + 4 + (sw - 8) * volMark(app);
        roundedRect(cr, mx - 1, cy - bh * 0.75, 2, bh * 1.5, 1);
        setSource(cr, app.color("on_surface_variant"), 0.7);
        cairo_fill(cr);
    }
}

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
    const bool ghost = (w == GwNotes && g_gn.ghostDraw) || (w == GwFps && g_gf.ghostDraw);  // (pinned, the overlay closed: only the box)
    // the header: an M3 shape badge with the icon, a title and a line under it, and the close button; the header drags the card
    const double headRight = w == GwNotes || w == GwFps ? 112 : 64;
    if (!ghost) hits.push_back({{r.x, r.y, r.w - headRight, kGameCardPad + kGameBadge + 8}, GhHead, w});
    const double hx = r.x + 16, hy = r.y + kGameCardPad;
    if (!ghost) {
        shapePath(cr, kM3IconShape, hx, hy, kGameBadge);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        drawIcon(cr, kGameIcons[w], hx + (kGameBadge - 24) / 2, hy + (kGameBadge - 24) / 2, 24, app.color("on_primary_container"));
        const char* sub = w == GwResources ? "Live" : w == GwVolume ? (G.mixTab == 1 ? "Input" : app.muted ? "Muted" : "Output") : w == GwNotes ? (g_gn.dirty ? "Saving..." : "Saved") : w == GwFps ? "Live" : (app.recording ? "Recording" : "Ready");
        const double tx = hx + kGameBadge + 14, tw = r.w - (w == GwNotes || w == GwFps ? 180 : 120);
        clkText(cr, L, i18n::tr(kGameNames[w]), 16, true, tx, hy + 14, ink, 1, tw);
        clkText(cr, L, i18n::tr(sub), 12.5, false, tx, hy + 33, app.recording && w == GwRecorder ? app.color("error") : dim, 1, tw);
        const Rect x{r.x + r.w - 16 - 40, hy + 2, 40, 40};
        hits.push_back({x, GhClose, w});
        cairo_arc(cr, x.x + 20, x.y + 20, 20, 0, 2 * M_PI);
        setSource(cr, app.color("surface_container_highest"), G.hover == GhClose * 10 + w ? 1 : 0.7);
        cairo_fill(cr);
        drawIcon(cr, "close-circle", x.x + 10, x.y + 10, 20, dim);
        if (w == GwNotes || w == GwFps) {  // pinned: it stays on the screen, translucent, when the overlay closes
            const bool pinned = w == GwNotes ? G.notesPinned : G.fpsPinned;
            const Rect pin{x.x - 8 - 40, x.y, 40, 40};
            hits.push_back({pin, GhPin, w});
            cairo_arc(cr, pin.x + 20, pin.y + 20, 20, 0, 2 * M_PI);
            setSource(cr, app.color(pinned ? "primary" : "surface_container_highest"), pinned || G.hover == GhPin * 10 + w ? 1 : 0.7);
            cairo_fill(cr);
            drawIcon(cr, "pin", pin.x + 10, pin.y + 10, 20, app.color(pinned ? "on_primary" : "on_surface_variant"));
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
        struct Row { const char* name; const char* icon; double v; std::string txt; };
        std::vector<Row> rows = {{"CPU", "cpu", R.cpu, std::to_string(static_cast<int>(std::lround(R.cpu * 100))) + "%"}};
        if (R.gpu.present) rows.push_back({"GPU", "display", std::max(0.0, R.gpu.usage), gpuinfo::text(R.gpu)});  // (between the processor and the memory)
        rows.push_back({"Memory", "server-square", R.ram, ram});
        rows.push_back({"Disk", "database", R.disk, std::to_string(static_cast<int>(std::lround(R.disk * 100))) + "%"});
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
    } else if (w == GwFps) {
        drawGameFps(app, cr, L, bx, bw, y);
    } else if (w == GwVolume) {
        AudioMenu& A = app.amenu;
        const bool input = G.mixTab == 1;
        {  // the tabs: output and input
            roundedRect(cr, bx, y, bw, kMixTabH, kMixTabH / 2);
            setSource(cr, app.color("surface_container_high"));
            cairo_fill(cr);
            const double tw = (bw - 4) / 2;
            for (int t = 0; t < 2; t++) {
                const Rect b{bx + 2 + t * tw, y + 2, tw, kMixTabH - 4};
                hits.push_back({b, GhMixTab, t});
                const bool on = G.mixTab == t, hot = G.hover == GhMixTab * 10 + t;
                if (on || hot) {
                    roundedRect(cr, b.x, b.y, b.w, b.h, b.h / 2);
                    setSource(cr, app.color(on ? "secondary_container" : "surface_container_highest"), on ? 1 : 0.7);
                    cairo_fill(cr);
                }
                const Color fg = app.color(on ? "on_secondary_container" : "on_surface_variant");
                const std::string label = i18n::tr(t == 0 ? "Output" : "Input");
                const int lw = clkTextWidth(L, label, 14, true);
                const double x0 = b.x + (b.w - 24 - lw) / 2;
                drawIcon(cr, t == 0 ? "volume-loud" : "microphone", x0, b.y + (b.h - 18) / 2, 18, fg);
                clkText(cr, L, label, 14, true, x0 + 24, b.y + b.h / 2.0, fg, 1);
            }
        }
        y += kMixTabH + kMixGap;
        {  // the volume of the tab: the default output, or the default input
            const bool muted = input ? A.inMuted : app.muted;
            const int vol = input ? A.inVol : app.volume;
            const int muteId = input ? GhMicMute : GhVolMute, trackId = input ? GhMicTrack : GhVolTrack;
            tile(kMixTile);
            const Rect mute{bx + 14, y + 20, 44, 44};
            hits.push_back({mute, muteId, 0});
            cairo_arc(cr, mute.x + 22, mute.y + 22, 22, 0, 2 * M_PI);
            setSource(cr, app.color(muted ? "error_container" : "secondary_container"));
            cairo_fill(cr);
            if (G.hover == muteId * 10) { cairo_arc(cr, mute.x + 22, mute.y + 22, 22, 0, 2 * M_PI); setSource(cr, app.color(muted ? "on_error_container" : "on_secondary_container"), 0.1); cairo_fill(cr); }
            drawIcon(cr, input ? "microphone" : muted ? "volume-cross" : "volume-loud", mute.x + 11, mute.y + 11, 22, app.color(muted ? "on_error_container" : "on_secondary_container"));
            const std::string pct = std::to_string(vol) + "%";
            const int pw = clkTextWidth(L, pct, 17, true);
            const double sx = mute.x + 44 + 14, sw = bx + bw - 14 - pw - 14 - sx;
            hits.push_back({{sx - 8, y + 20, sw + 16, 44}, trackId, 0});
            gameSlider(app, cr, sx, y + 42, sw, volFrac(app, vol), volLoud(vol) && !muted, false, 16);
            clkText(cr, L, pct, 17, true, bx + bw - 14 - pw, y + 42, ink, 1);
        }
        y += kMixTile + kMixGap;
        {  // the device in use, and the others to switch to
            std::vector<int> devs = gameMixDevices(app);
            std::string title = i18n::tr(A.loaded ? "No device" : "Loading…");
            for (int i : devs) if (A.devs[static_cast<size_t>(i)].isDefault) title = A.devs[static_cast<size_t>(i)].title;
            if (devs.size() > kMixMaxDevices) devs.resize(kMixMaxDevices);  // (a card with a dozen outputs would not fit the screen)
            const double h = kMixDevH + (G.mixDevOpen ? static_cast<double>(devs.size()) * kMixDevRow + 6 : 0);
            tile(h);
            hits.push_back({{bx, y, bw, kMixDevH}, GhMixDev, 0});
            if (G.hover == GhMixDev * 10) { roundedRect(cr, bx, y, bw, kMixDevH, 20); setSource(cr, ink, 0.06); cairo_fill(cr); }
            cairo_arc(cr, bx + 12 + 16, y + kMixDevH / 2, 16, 0, 2 * M_PI);
            setSource(cr, app.color("tertiary_container"));
            cairo_fill(cr);
            drawIcon(cr, input ? "microphone" : "volume-loud", bx + 12 + 7, y + kMixDevH / 2 - 9, 18, app.color("on_tertiary_container"));
            clkText(cr, L, title, 14, true, bx + 12 + 32 + 12, y + kMixDevH / 2.0, ink, 1, bw - 12 - 32 - 12 - 44);
            drawIcon(cr, G.mixDevOpen ? "alt-arrow-up" : "alt-arrow-down", bx + bw - 14 - 20, y + (kMixDevH - 20) / 2, 20, dim);
            if (G.mixDevOpen) {
                for (size_t j = 0; j < devs.size(); j++) {
                    const AudioDevice& d = A.devs[static_cast<size_t>(devs[j])];
                    const Rect row{bx + 6, y + kMixDevH + static_cast<double>(j) * kMixDevRow, bw - 12, kMixDevRow};
                    hits.push_back({row, GhMixPick, devs[j]});
                    if (static_cast<int>(j) == G.mixDevHover) { roundedRect(cr, row.x, row.y, row.w, row.h - 2, 14); setSource(cr, ink, 0.07); cairo_fill(cr); }
                    const double cy = row.y + row.h / 2;
                    cairo_new_sub_path(cr);  // (the text before it left a current point: without this a line joins it to the circle)
                    cairo_arc(cr, row.x + 18, cy, 8, 0, 2 * M_PI);
                    setSource(cr, app.color(d.isDefault ? "primary" : "outline"));
                    cairo_set_line_width(cr, 2);
                    cairo_stroke(cr);
                    if (d.isDefault) { cairo_new_sub_path(cr); cairo_arc(cr, row.x + 18, cy, 4, 0, 2 * M_PI); setSource(cr, app.color("primary")); cairo_fill(cr); }
                    clkText(cr, L, d.title, 13.5, d.isDefault, row.x + 38, cy, d.isDefault ? ink : dim, 1, row.w - 50);
                }
            }
            y += h;
        }
        if (!input) {  // the apps playing sound, each with its own volume
            y += kMixGap;
            const size_t shown = gameMixAppsShown(app);
            if (shown == 0) {
                tile(kMixNoteH);
                const std::string note = i18n::tr(A.loaded ? "No apps playing sound" : "Loading…");
                clkText(cr, L, note, 13.5, false, bx + 18, y + kMixNoteH / 2.0, dim, 1, bw - 36);
                y += kMixNoteH;
            }
            for (size_t i = 0; i < shown; i++) {
                const AudioApp& a = A.apps[i];
                tile(kMixAppH);
                const Rect badge{bx + 12, y + 9, 32, 32};
                hits.push_back({badge, GhMixAppMute, static_cast<int>(i)});
                cairo_arc(cr, badge.x + 16, badge.y + 16, 16, 0, 2 * M_PI);
                setSource(cr, app.color(a.muted ? "error_container" : "secondary_container"));
                cairo_fill(cr);
                if (G.hover == GhMixAppMute * 10 + std::min<int>(static_cast<int>(i), 9)) { cairo_arc(cr, badge.x + 16, badge.y + 16, 16, 0, 2 * M_PI); setSource(cr, app.color("on_secondary_container"), 0.12); cairo_fill(cr); }
                if (a.muted) {
                    drawIcon(cr, "volume-cross", badge.x + 7, badge.y + 7, 18, app.color("on_error_container"));
                } else {
                    const unsigned char lead = a.name.empty() ? 0 : static_cast<unsigned char>(a.name[0]);
                    const size_t lead_len = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;  // (one letter, whatever the script)
                    std::string letter = a.name.empty() ? "?" : a.name.substr(0, lead_len);
                    for (char& c : letter) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    const int lw = clkTextWidth(L, letter, 15, true);
                    clkText(cr, L, letter, 15, true, badge.x + (32 - lw) / 2.0, badge.y + 16, app.color("on_secondary_container"), 1);
                }
                const std::string pct = std::to_string(a.vol) + "%";
                const int pw = clkTextWidth(L, pct, 13, true);
                clkText(cr, L, a.name, 14, true, badge.x + 32 + 10, y + 25, ink, 1, bw - 12 - 32 - 10 - 14 - pw - 8);
                clkText(cr, L, pct, 13, true, bx + bw - 14 - pw, y + 25, dim, 1);
                const double sx = bx + 14, sw = bw - 28, cy = y + 48;
                hits.push_back({{sx - 8, cy - 15, sw + 16, 30}, GhMixApp, static_cast<int>(i)});
                gameSlider(app, cr, sx, cy, sw, volFrac(app, a.vol), volLoud(a.vol) && !a.muted, a.muted, 10);
                y += kMixAppH + (i + 1 < shown ? kMixAppGap : 0);
            }
            if (A.apps.size() > shown) clkText(cr, L, "+" + std::to_string(A.apps.size() - shown) + " " + i18n::tr("more"), 12.5, false, bx + 14, y + kMixAppGap + kMixMoreH / 2.0, dim, 1);
        }
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
    G.volH = gameVolCardH(app);
    for (int w = 0; w < GwCrosshair; w++)  // (cards grow with their content: none may run off the screen)
        if (G.open[w] && w != GwNotes) G.wy[w] = std::clamp(G.wy[w], 0.0, std::max(0.0, H - gameCardH(G, w)));
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
            if (w == GwFps && !kGameFpsReady) continue;
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
        const double sc = G.scale.factor();  // (damage is in buffer pixels)
        const int px0 = static_cast<int>(std::floor(x0 * sc)), py0 = static_cast<int>(std::floor(y0 * sc));
        const int px1 = static_cast<int>(std::ceil(x1 * sc)), py1 = static_cast<int>(std::ceil(y1 * sc));
        wl_surface_damage_buffer(G.surface, px0, py0, std::max(1, px1 - px0), std::max(1, py1 - py0));
    } else {
        wl_surface_damage_buffer(G.surface, 0, 0, G.bufW, G.bufH);
    }
    if (G.scale.viewport) wp_viewport_set_destination(G.scale.viewport, W, H);
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
    const int S = kGameCross;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, S, S, G.crossScale.factor(), "end4pC-cross")) return;
    cairo_surface_t* surf = hf.surface;
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
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kGameBuffer, nullptr);
    hidpi::present(G.crossSurface, buffer, hf, G.crossScale);
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
    hidpi::detach(G.crossScale);
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
    attachScale(app, G.crossScale, G.crossSurface, [&app] { drawCrosshair(app); });
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
    hidpi::detach(G.scale);
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
    G.mixDrag = -1;
    G.mixDevOpen = false;
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
        fpsPlace(*app);
        fpsProbeSync(*app);
        updateResources(*app);  // (so the card of the resources has its GPU row from the first frame)
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
    closeFpsGhost(app);
    const Bar* bar = mainBar(app);
    G.hover = 0;
    G.closing = false;
    G.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, G.scale, G.surface, [&app] { app.game.needFull = true; drawGame(app); });
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
    G.mixDrag = -1;
    G.mixDevOpen = false;
    drawGame(app);
}
void toggleGame(App& app) { if (app.game.surface && !app.game.closing) retractGame(app); else openGame(app); }

void tickGame(App& app, std::chrono::steady_clock::time_point now) {
    GameOverlay& G = app.game;
    if (!G.surface || !G.configured) return;
    if (G.closing && now - G.t0 >= std::chrono::milliseconds(static_cast<int>(kGameCloseMs))) { closeGameNow(app); return; }
    bool redraw = G.animating;
    gameNotesTick(app, now, redraw);
    if (!G.closing && G.open[GwVolume] && G.mixDrag == -1 && now >= G.nextAudio) {  // the apps and devices of the volume card
        G.nextAudio = now + std::chrono::seconds(2);
        startAudioDevicesJob(app);
    }
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
    const int v = std::min(volFromFrac(app, f), app.cfg.volumeMax);
    if (v == app.volume) return;
    app.volume = v;
    const auto now = std::chrono::steady_clock::now();
    if (now - app.game.lastVolSet < std::chrono::milliseconds(40)) return;
    app.game.lastVolSet = now;
    const std::string pct = std::to_string(v) + "%";
    runDetached({"wpctl", "set-volume", "-l", volLimit(app).c_str(), "@DEFAULT_AUDIO_SINK@", pct.c_str()});
}

// The slider of an app (or of the microphone) follows the pointer; the number goes to pactl at most every 40 ms while dragging.
void gameMixSet(App& app, double x, const GameHit& track, bool force) {
    GameOverlay& G = app.game;
    AudioMenu& A = app.amenu;
    const double f = std::clamp((x - (track.r.x + 8)) / (track.r.w - 16), 0.0, 1.0);
    const int v = std::min(volFromFrac(app, f), app.cfg.volumeMax);
    const bool mic = G.mixDrag == -2;
    int* target = mic ? &A.inVol : (G.mixDrag >= 0 && G.mixDrag < static_cast<int>(A.apps.size()) ? &A.apps[static_cast<size_t>(G.mixDrag)].vol : nullptr);
    if (!target) return;
    const bool changed = *target != v;
    *target = v;
    const auto now = std::chrono::steady_clock::now();
    if (!force && (!changed || now - G.lastMixSet < std::chrono::milliseconds(40))) return;
    G.lastMixSet = now;
    const std::string pct = std::to_string(v) + "%";
    if (mic) runDetached({"pactl", "set-source-volume", "@DEFAULT_SOURCE@", pct.c_str()});
    else { const std::string idx = std::to_string(A.apps[static_cast<size_t>(G.mixDrag)].index); runDetached({"pactl", "set-sink-input-volume", idx.c_str(), pct.c_str()}); }
}

void gameMixPick(App& app, int device) {
    GameOverlay& G = app.game;
    AudioMenu& A = app.amenu;
    G.mixDevOpen = false;
    if (device < 0 || device >= static_cast<int>(A.devs.size())) return;
    const AudioDevice chosen = A.devs[static_cast<size_t>(device)];
    if (chosen.isDefault) return;
    for (auto& d : A.devs)
        if (d.sink == chosen.sink && d.action == "set") d.isDefault = d.target == chosen.target && d.extra == chosen.extra;
    amRunScript({"set", chosen.sink ? "sink" : "source", chosen.target, chosen.extra});
    G.nextAudio = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);  // (read it again once the change went through)
}

void gameMixAppMute(App& app, int i) {
    AudioMenu& A = app.amenu;
    if (i < 0 || i >= static_cast<int>(A.apps.size())) return;
    AudioApp& a = A.apps[static_cast<size_t>(i)];
    a.muted = !a.muted;
    const std::string idx = std::to_string(a.index);
    runDetached({"pactl", "set-sink-input-mute", idx.c_str(), a.muted ? "1" : "0"});
}

// The audio job finished: the apps and devices on the card are new.
void gameAudioLoaded(App& app) {
    GameOverlay& G = app.game;
    // the hit rectangles of the mixer refer to rows of the old lists: they go until the new frame is drawn (a click in between must not hit another app)
    G.hits.erase(std::remove_if(G.hits.begin(), G.hits.end(), [](const GameHit& h) { return h.id == GhMixPick || h.id == GhMixApp || h.id == GhMixAppMute; }), G.hits.end());
    if (G.surface && !G.closing && G.open[GwVolume] && G.mixDrag == -1) drawGame(app);
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
    if (G.mixDrag != -1) {
        for (const GameHit& h : G.hits)
            if (h.id == (G.mixDrag == -2 ? GhMicTrack : GhMixApp) && (G.mixDrag == -2 || h.arg == G.mixDrag)) { gameMixSet(app, app.pointerX, h, false); break; }
        drawGame(app);
        return;
    }
    GameHit under;
    const int over = gameHitAt(G, app.pointerX, app.pointerY, &under);
    int deviceHover = -1;  // the row of the device list under the pointer
    if (under.id == GhMixPick) { const auto devs = gameMixDevices(app); for (size_t j = 0; j < devs.size(); j++) if (devs[j] == under.arg) deviceHover = static_cast<int>(j); }
    if (over != G.hover || deviceHover != G.mixDevHover) { G.hover = over; G.mixDevHover = deviceHover; drawGame(app); updateCursor(app); }
}

void gameButton(App& app, uint32_t button, bool pressed) {
    GameOverlay& G = app.game;
    if (button != 0x110) return;
    if (!pressed) {
        const bool was = G.dragW >= 0 || G.volDrag || G.rsMode != 0 || g_gn.selDrag || G.mixDrag != -1;
        if (G.mixDrag != -1) {  // the last value goes out whatever the pace of the drag was
            for (const GameHit& h : G.hits)
                if (h.id == (G.mixDrag == -2 ? GhMicTrack : GhMixApp) && (G.mixDrag == -2 || h.arg == G.mixDrag)) { gameMixSet(app, app.pointerX, h, true); break; }
            G.mixDrag = -1;
            G.nextAudio = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        }
        if (G.rsMode != 0 || G.dragW == GwNotes) { G.rsMode = 0; gnSaveState(app); }
        if (G.dragW == GwFps) fpsSaveState(app);
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
                if (h.arg == GwFps) fpsSaveState(app);
            }
            break;
        case GhClose: G.open[h.arg] = false; if (h.arg == GwNotes) gnSaveState(app); if (h.arg == GwFps) fpsSaveState(app); break;
        case GhNoteText: gnClick(app, false); break;
        case GhNoteCopy:
            if (h.arg >= 0 && h.arg < static_cast<int>(g_gn.copies.size())) copyToClipboard(g_gn.copies[h.arg]);
            break;
        case GhPin:
            if (h.arg == GwFps) { G.fpsPinned = !G.fpsPinned; fpsSaveState(app); }
            else { G.notesPinned = !G.notesPinned; gnSaveState(app); }
            break;
        case GhResize: G.rsMode = h.arg; G.rsX = app.pointerX; G.rsY = app.pointerY; G.rsW = G.nw; G.rsH = G.nh; break;
        case GhHead: G.dragW = h.arg; G.grabX = app.pointerX - G.wx[h.arg]; G.grabY = app.pointerY - G.wy[h.arg]; break;
        case GhVolTrack: G.volDrag = true; gameSetVolume(app, app.pointerX, h); break;
        case GhVolMute: app.muted = !app.muted; runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}); break;
        case GhMicMute: app.amenu.inMuted = !app.amenu.inMuted; runDetached({"pactl", "set-source-mute", "@DEFAULT_SOURCE@", "toggle"}); break;
        case GhMixTab: G.mixTab = h.arg; G.mixDevOpen = false; G.nextAudio = {}; break;
        case GhMixDev: G.mixDevOpen = !G.mixDevOpen; G.mixDevHover = -1; break;
        case GhMixPick: gameMixPick(app, h.arg); break;
        case GhMixAppMute: gameMixAppMute(app, h.arg); break;
        case GhMixApp: G.mixDrag = h.arg; gameMixSet(app, app.pointerX, h, true); break;
        case GhMicTrack: G.mixDrag = -2; gameMixSet(app, app.pointerX, h, true); break;
        case GhRecord:
            if (app.recording) stopRecording(app); else { retractGame(app); startRecording(app); }
            break;
        case GhShot: retractGame(app); openCapture(app); break;
        default: break;
    }
    fpsProbeSync(app);  // (the counter runs while its card is open)
    drawGame(app);
}
