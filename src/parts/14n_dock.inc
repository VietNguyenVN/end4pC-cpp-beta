// Dock: a floating pill of app icons on one edge of the screen (bottom by default; left, right or top), centred, with a macOS-like magnification.
// Holds the geometry, the items (pinned apps and running windows from hyprctl) and the surface's life. Drawing: 14n_dock_draw.inc; pointer and timers: 14n_dock_input.inc.
// Included by main.cpp inside its anonymous namespace, in order; parts that come first are visible here.

void dockRedraw(App& app);
void dockAim(App& app);
void dockRebuild(App& app);
void dockFitWidth(App& app);
void dockPaintGrid(cairo_t* cr, App& app);
bool dockGridTick(App& app, std::chrono::steady_clock::time_point now);

constexpr int kDockSepW = 14, kDockPadX = 12, kDockPadTop = 8, kDockDotRoom = 12, kDockTopRoom = 150, kDockGridTopRoom = 490, kDockStrip = 2;
constexpr double kDockSigma = 1.15;
constexpr double kDockStiffness = 900, kDockDamping = 54;
constexpr int kDockSideRoom = 260;
constexpr double kDockStepMax = 1.0 / 30;
constexpr double kDockCtlSize = 28, kDockCtlGap = 4;
constexpr int kDockTrackMax = 100;
constexpr double kDockHideDelay = 0.6, kDockRefreshSeconds = 3.0;
constexpr int kDockMenuDepthV = 240, kDockGridDepthV = 540;

enum DockEdge { kDockBottom = 0, kDockLeft = 1, kDockRight = 2, kDockTop = 3 };
int dockEdgeOf(const std::string& s) { return s == "left" ? kDockLeft : s == "right" ? kDockRight : s == "top" ? kDockTop : kDockBottom; }
bool dockVertical(const DockState& D) { return D.pos == kDockLeft || D.pos == kDockRight; }
int dockLW(const DockState& D) { return dockVertical(D) ? D.h : D.w; }
int dockLH(const DockState& D) { return dockVertical(D) ? D.w : D.h; }
// How far the pill travels to hide: just enough to be off the edge of the screen (the surface is much taller than the pill, so the whole
// height made the slide a pop: only its last stretch was on screen).
double dockSlideDist(const DockState& D) { return std::max(1.0, dockLH(D) - D.pill.y + 24); }
cairo_matrix_t dockFrame(const DockState& D) {
    cairo_matrix_t m;
    switch (D.pos) {
        case kDockTop: cairo_matrix_init(&m, 1, 0, 0, -1, 0, D.h); break;
        case kDockLeft: cairo_matrix_init(&m, 0, 1, -1, 0, D.w, 0); break;
        case kDockRight: cairo_matrix_init(&m, 0, 1, 1, 0, 0, 0); break;
        default: cairo_matrix_init_identity(&m); break;
    }
    return m;
}
struct DockPt { double x = 0, y = 0; };
DockPt dockToPhys(const DockState& D, double lx, double ly) {
    const cairo_matrix_t m = dockFrame(D);
    DockPt p{lx, ly};
    cairo_matrix_transform_point(&m, &p.x, &p.y);
    return p;
}
DockPt dockToLogical(const DockState& D, double px, double py) {
    cairo_matrix_t m = dockFrame(D);
    DockPt p{px, py};
    if (cairo_matrix_invert(&m) == CAIRO_STATUS_SUCCESS) cairo_matrix_transform_point(&m, &p.x, &p.y);
    return p;
}
Rect dockRectToPhys(const DockState& D, const Rect& r) {
    const DockPt a = dockToPhys(D, r.x, r.y), b = dockToPhys(D, r.x + r.w, r.y + r.h);
    return Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}
DockPt dockPointer(const App& app) { return dockToLogical(app.dock, app.pointerX, app.pointerY); }
DockPt dockSlideOffset(const DockState& D) {
    const cairo_matrix_t m = dockFrame(D);
    DockPt d{0, D.slide * dockSlideDist(D)};
    cairo_matrix_transform_distance(&m, &d.x, &d.y);
    return d;
}
DockPt dockInward(const DockState& D) {
    const cairo_matrix_t m = dockFrame(D);
    DockPt d{0, -1};
    cairo_matrix_transform_distance(&m, &d.x, &d.y);
    return d;
}
Rect dockBeside(const DockState& D, DockPt a, double w, double h, double margin) {
    const DockPt n = dockInward(D);
    double x = n.x == 0 ? a.x - w / 2 : (n.x > 0 ? a.x : a.x - w), y = n.y == 0 ? a.y - h / 2 : (n.y > 0 ? a.y : a.y - h);
    x = std::clamp(x, margin, std::max(margin, D.w - w - margin));
    y = std::clamp(y, margin, std::max(margin, D.h - h - margin));
    return Rect{x, y, w, h};
}
template <class Paint>
void dockUpright(cairo_t* cr, const DockState& D, double cx, double cy, Paint paint) {
    if (D.pos == kDockBottom) { paint(); return; }
    cairo_matrix_t m;
    cairo_get_matrix(cr, &m);
    double dx = cx, dy = cy;
    cairo_matrix_transform_point(&m, &dx, &dy);
    cairo_save(cr);
    cairo_identity_matrix(cr);
    cairo_translate(cr, dx - cx, dy - cy);
    paint();
    cairo_restore(cr);
}

// Auto-hide has three modes: 0 off, 1 always hides, 2 smart: the dock shows while the workspace in view has no windows and hides when one is open.
// (In modes 1 and 2 the dock never takes room from the windows: it is on top, and it is only ever visible over an empty workspace or under the pointer.)
bool dockWantsHidden(const App& app) {
    if (app.cfg.dockAutoHide == 1) return true;
    return app.cfg.dockAutoHide == 2 && app.occupied.count(app.workspace) > 0;
}

int dockPeakPx(const App& app) {
    return app.cfg.dockMagnify ? (app.cfg.dockIconSize * app.cfg.dockMagnification + 99) / 100 : app.cfg.dockIconSize;
}
int dockBottomGap(const App& app) {
    if (!app.cfg.dockAutoHide) return app.cfg.dockHug ? 0 : app.dock.gapOut;
    return app.cfg.frame + (app.cfg.dockHug ? 0 : app.dock.gapOut) + (app.dock.barHere && app.cfg.barPosition == app.cfg.dockPosition ? barSpan(app) : 0);
}
int dockPopPx(const App& app) {
    return dockPeakPx(app) - app.cfg.dockIconSize;
}
int dockBaseH(const App& app) {
    return app.cfg.dockIconSize + kDockPadTop + kDockDotRoom;
}
int dockSurfH(const App& app) {
    const bool vertical = app.cfg.dockPosition == "left" || app.cfg.dockPosition == "right";
    const int room = vertical ? (app.cfg.dockLauncher ? kDockGridDepthV : kDockMenuDepthV) : (app.cfg.dockLauncher ? kDockGridTopRoom : kDockTopRoom);
    return dockBottomGap(app) + dockPeakPx(app) + kDockPadTop + kDockDotRoom + room;
}
void dockAskSize(const App& app, int lw, int lh) {
    const DockState& D = app.dock;
    (void)app;
    if (D.layer) zwlr_layer_surface_v1_set_size(D.layer, dockVertical(D) ? lh : lw, dockVertical(D) ? 0 : lh);
}
int dockExclusive(const App& app) {
    return app.cfg.dockAutoHide ? -1 : dockBottomGap(app) + dockBaseH(app);
}

Bar* dockScreenBar(App& app) {
    if (!app.cfg.dock || !app.layerShell || !app.compositor || !app.shm) return nullptr;
    if (!app.cfg.dockScreen.empty() && mainBar(app) && mainBar(app)->surface)
        for (auto& b : app.bars) if (b->output && b->connector == app.cfg.dockScreen) return b.get();
    Bar* bar = mainBar(app);
    return bar && bar->surface ? bar : nullptr;
}

void closeDock(App& app) {
    DockState& D = app.dock;
    if (!D.open()) return;
    if (D.layer) zwlr_layer_surface_v1_destroy(D.layer);
    releaseBlurSurface(D.blur);
    hidpi::detach(D.scale);
    wl_surface_destroy(D.surface);
    app.dock = DockState{};
    app.onDock = false;
}

void onDockConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    DockState& D = app->dock;
    const bool first = !D.configured;
    D.configured = true;
    D.w = static_cast<int>(w);
    D.h = static_cast<int>(h);
    if (first) {
        dockRebuild(*app);
        dockAim(*app);
    }
    dockRedraw(*app);
}
void onDockClosed(void* d, zwlr_layer_surface_v1*) { closeDock(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kDockListener = {onDockConfigure, onDockClosed};

void openDock(App& app, Bar& bar) {
    DockState& D = app.dock;
    D.gapOut = hyprOptionInt("general:gaps_out", 5);
    D.barHere = bar.surface != nullptr;
    D.layerGen = app.layerGen;
    D.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, D.scale, D.surface, [&app] { dockRedraw(app); });
    D.output = bar.output;
    D.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, D.surface, bar.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-dock");
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    D.pos = dockEdgeOf(app.cfg.dockPosition);
    zwlr_layer_surface_v1_set_anchor(D.layer, D.pos == kDockLeft ? (kL | kT | kB) : D.pos == kDockRight ? (kR | kT | kB) : D.pos == kDockTop ? kT : kB);
    zwlr_layer_surface_v1_set_keyboard_interactivity(D.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_add_listener(D.layer, &kDockListener, &app);
    D.reqH = dockSurfH(app);
    D.reqW = 2 * kDockSideRoom + 300;
    dockAskSize(app, D.reqW, D.reqH);
    zwlr_layer_surface_v1_set_exclusive_zone(D.layer, dockExclusive(app));
    D.slide = D.slideTarget = dockWantsHidden(app) ? 1 : 0;
    wl_surface_commit(D.surface);
}

void dockSync(App& app) {
    Bar* bar = dockScreenBar(app);
    if (!bar) { closeDock(app); return; }
    if (app.dock.open() && (app.dock.output != bar->output || app.dock.layerGen != app.layerGen)) closeDock(app);
    if (!app.dock.open()) openDock(app, *bar);
}

void dockApply(App& app) {
    if (app.dock.open() && app.dock.pos != dockEdgeOf(app.cfg.dockPosition)) closeDock(app);
    dockSync(app);
    DockState& D = app.dock;
    if (!D.open() || !D.configured) return;
    if (Bar* b = dockScreenBar(app)) D.barHere = b->surface != nullptr;
    D.gapOut = hyprOptionInt("general:gaps_out", 5);
    if (!app.cfg.dockAutoHide) { D.slide = D.slideTarget = 0; D.hideArmed = false; }
    else if (app.cfg.dockAutoHide == 2) { D.slide = D.slideTarget = dockWantsHidden(app) ? 1 : 0; D.hideArmed = false; }
    const int h = dockSurfH(app);
    if (h != D.reqH) {
        D.reqH = h;
        dockAskSize(app, D.reqW, h);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(D.layer, dockExclusive(app));
    app.dock.icons.clear();
    dockRebuild(app);
    dockRedraw(app);
}

// One window of a running app, as hyprctl lists it.
struct DockWindow {
    std::string address, cls;
    bool focused = false;
};

std::vector<DockWindow> dockWindows() {
    const std::string list = wmWindowsTsv();
    std::vector<DockWindow> out;
    std::istringstream in(list);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
        if (f.size() < 3 || f[0].empty() || f[1].empty()) continue;
        out.push_back(DockWindow{f[0], f[1], f[2] == "0"});
    }
    return out;
}

std::string dockLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const AppEntry* dockEntryFor(const App& app, const std::string& key) {
    if (key.empty()) return nullptr;
    const std::string want = dockLower(key);
    for (const AppEntry& e : app.apps) if (e.id == key) return &e;
    for (const AppEntry& e : app.apps) if (dockLower(e.id) == want) return &e;
    for (const AppEntry& e : app.apps) {
        const std::string id = dockLower(e.id);
        if (id.size() > want.size() && id.ends_with("." + want)) return &e;
    }
    for (const AppEntry& e : app.apps) if (!e.wmClass.empty() && dockLower(e.wmClass) == want) return &e;
    return nullptr;
}

bool dockOwns(const DockItem& it, const AppEntry* e, const std::string& cls) {
    const std::string c = dockLower(cls);
    if (dockLower(it.key) == c) return true;
    if (!e) return false;
    const std::string id = dockLower(e->id);
    return id == c || (id.size() > c.size() && id.ends_with("." + c)) || (!e->wmClass.empty() && dockLower(e->wmClass) == c);
}

void dockFillEntry(DockItem& it, const AppEntry* e) {
    it.name = e ? e->name : it.key;
    it.entryId = e ? e->id : "";
    it.exec = e ? e->exec : "";
    it.terminal = e && e->terminal;
    it.iconName = e && !e->icon.empty() ? e->icon : it.key;
}

void dockAddWindow(DockItem& it, const DockWindow& w) {
    it.addresses.push_back(w.address);
    it.running = true;
    it.focused = it.focused || w.focused;
}

double dockMediaExtra(const App& app) {
    if (app.dock.pos == kDockLeft || app.dock.pos == kDockRight) return 0;
    if (app.musicStatus.empty() || app.musicTitle.empty()) return 0;
    return 10 + std::min(app.musicTextW, kDockTrackMax) + 8 + 2 * kDockCtlSize + kDockCtlGap;
}

void dockPinnedAndLoose(App& app, const std::vector<DockWindow>& wins, std::vector<DockItem>& pinned, std::vector<DockItem>& loose) {
    std::vector<bool> used(wins.size(), false);
    for (const std::string& key : splitList(app.cfg.dockPinned)) {
        DockItem it;
        it.key = key;
        it.pinned = true;
        const AppEntry* e = dockEntryFor(app, key);
        dockFillEntry(it, e);
        for (size_t i = 0; i < wins.size(); i++)
            if (!used[i] && dockOwns(it, e, wins[i].cls)) { used[i] = true; dockAddWindow(it, wins[i]); }
        pinned.push_back(std::move(it));
    }
    for (size_t i = 0; i < wins.size(); i++) {
        if (used[i]) continue;
        auto same = std::find_if(loose.begin(), loose.end(), [&](const DockItem& x) { return x.key == wins[i].cls; });
        if (same == loose.end()) {
            DockItem it;
            it.key = wins[i].cls;
            dockFillEntry(it, dockEntryFor(app, it.key));
            loose.push_back(std::move(it));
            same = std::prev(loose.end());
        }
        dockAddWindow(*same, wins[i]);
    }
}

std::vector<DockItem> dockBuildItems(App& app, const std::vector<DockWindow>& wins) {
    std::vector<DockItem> pinned, loose;
    dockPinnedAndLoose(app, wins, pinned, loose);
    std::vector<DockItem> items;
    DockItem music;
    music.key = "special:media";
    music.special = 2;
    music.extra = dockMediaExtra(app);
    const bool media = app.cfg.dockMedia != 0, group = !pinned.empty() || media;
    if (app.cfg.dockPin) {
        DockItem pin;
        pin.key = "special:pin";
        pin.special = 1;
        pin.name = app.cfg.dockAutoHide ? "Keep the Dock visible" : "Hide the Dock automatically";
        items.push_back(std::move(pin));
        if (group || !loose.empty()) { DockItem sep; sep.separator = true; items.push_back(sep); }
    }
    for (DockItem& it : pinned) items.push_back(std::move(it));
    if (media) items.push_back(std::move(music));
    if (group && !loose.empty()) {
        DockItem sep;
        sep.separator = true;
        items.push_back(sep);
    }
    for (DockItem& it : loose) items.push_back(std::move(it));
    if (app.cfg.dockLauncher) {
        if (!items.empty()) { DockItem sep; sep.separator = true; items.push_back(sep); }
        DockItem launcher;
        launcher.key = "special:launcher";
        launcher.special = 3;
        launcher.name = "Apps";
        items.push_back(std::move(launcher));
    }
    return items;
}

std::shared_ptr<cairo_surface_t> dockIcon(App& app, const std::string& name) {
    if (name.empty()) return nullptr;
    const int px = dockPeakPx(app);
    const std::string key = name + "@" + std::to_string(px);
    if (auto hit = app.dock.icons.find(key); hit != app.dock.icons.end()) return hit->second;
    std::shared_ptr<cairo_surface_t> surf;
    const std::string path = lookupIcon(name, px);
    if (!path.empty()) {
        if (cairo_surface_t* raw = loadIconScaled(path, px)) surf = std::shared_ptr<cairo_surface_t>(raw, cairo_surface_destroy);
    }
    app.dock.icons[key] = surf;
    return surf;
}

void dockRebuild(App& app) {
    DockState& D = app.dock;
    if (D.dragging) return;
    ensureApps(app);
    std::vector<DockItem> items = dockBuildItems(app, dockWindows());
    for (DockItem& it : items) {
        if (it.separator) continue;
        for (const DockItem& old : D.items) {
            if (old.separator || old.key != it.key) continue;
            it.scale = old.scale;
            it.vel = old.vel;
            it.target = old.target;
            it.shift = old.shift;
            it.shiftVel = old.shiftVel;
            break;
        }
        it.icon = dockIcon(app, it.iconName);
    }
    D.items = std::move(items);
    if (D.hover >= static_cast<int>(D.items.size())) D.hover = -1;
    dockFitWidth(app);
    D.nextRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long>(kDockRefreshSeconds * 1000));
}

double dockRestWidth(const App& app) {
    double w = 2 * kDockPadX - app.cfg.dockSpacing;
    for (const DockItem& it : app.dock.items) w += (it.separator ? kDockSepW : app.cfg.dockIconSize + it.extra) + app.cfg.dockSpacing;
    return app.dock.items.empty() ? 2 * kDockPadX + app.cfg.dockIconSize : w;
}

void dockFitWidth(App& app) {
    DockState& D = app.dock;
    if (!D.layer) return;
    const int want = static_cast<int>(std::ceil(dockRestWidth(app))) + 2 * kDockSideRoom;
    if (want == D.reqW) return;
    D.reqW = want;
    dockAskSize(app, D.reqW, D.reqH);
    wl_surface_commit(D.surface);
}

void dockLayout(App& app) {
    DockState& D = app.dock;
    const double px = app.cfg.dockIconSize;
    const double rest = dockRestWidth(app), avail = rest - 2 * kDockPadX;
    double widths = 0;
    int count = 0;
    for (const DockItem& it : D.items) { widths += it.separator ? kDockSepW : px * it.scale + it.extra; count++; }
    const double gaps = count > 1 ? count - 1 : 1;
    double gap = std::max(0.0, (avail - widths) / gaps), squeeze = 1;
    if (widths > avail) { gap = 0; squeeze = avail / widths; }
    D.squeeze = squeeze;
    const double bottom = dockLH(D) - dockBottomGap(app), top = bottom - dockBaseH(app);
    D.pill = Rect{(dockLW(D) - rest) / 2, top, rest, bottom - top};
    double x = D.pill.x + kDockPadX, restX = D.pill.x + kDockPadX;
    for (DockItem& it : D.items) {
        const double w = (it.separator ? kDockSepW : px * it.scale + it.extra) * squeeze;
        const double baseW = it.separator ? kDockSepW : px + it.extra;
        it.slot = Rect{x + it.shift, top, w + app.cfg.dockSpacing, bottom - top};
        it.baseCx = restX + (it.separator ? baseW : px) / 2;
        x += w + gap;
        restX += baseW + app.cfg.dockSpacing;
    }
}

bool dockSpring(double& x, double& v, double target, double dt) {
    v += (kDockStiffness * (target - x) - kDockDamping * v) * dt;
    x += v * dt;
    return std::abs(target - x) > 0.0005 || std::abs(v) > 0.002;
}

// The slide of the autohide: critically damped (no bounce) and never past either end.
// Small steps: one big step from rest (dt = 1/30 with this stiffness) went straight past the target, which was the pop and the bounce.
constexpr double kDockSlideStiffness = 520;
bool dockSlideSpring(double& x, double& v, double target, double dt) {
    const int n = std::max(1, static_cast<int>(std::ceil(dt / 0.004)));
    const double h = dt / n, c = 2 * std::sqrt(kDockSlideStiffness);
    for (int i = 0; i < n; i++) {
        v += (kDockSlideStiffness * (target - x) - c * v) * h;
        x += v * h;
        if (x < 0 || x > 1) { x = std::clamp(x, 0.0, 1.0); v = 0; }
    }
    return std::abs(target - x) > 0.0005 || std::abs(v) > 0.002;
}

void dockStep(App& app, std::chrono::steady_clock::time_point now) {
    DockState& D = app.dock;
    if (!D.open()) return;
    const double dt = std::clamp(std::chrono::duration<double>(now - D.lastStep).count(), 0.0, kDockStepMax);
    D.lastStep = now;
    bool moving = false;
    for (DockItem& it : D.items) {
        if (it.separator) continue;
        moving = dockSpring(it.scale, it.vel, it.target, dt) || moving;
        if (it.shift != 0 || it.shiftVel != 0) {
            if (!dockSpring(it.shift, it.shiftVel, 0, dt)) it.shift = it.shiftVel = 0;
            moving = true;
        }
    }
    moving = dockSlideSpring(D.slide, D.slideVel, D.slideTarget, dt) || moving;
    moving = dockGridTick(app, now) || moving;
    D.moving = moving;
    dockRedraw(app);
}
