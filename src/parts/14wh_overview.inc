// The overview (SUPER + O): a strip on one edge of the screen (top, bottom, left or right) with every workspace, live, and a + to add one. The
// windows of the workspace in use are the real ones: the gap of that edge grows so they make room for the strip (and go back on close), so
// they keep being alive and usable. Click a workspace to go there. The live thumbnails are captured with ext-image-copy-capture (the
// toplevel id Hyprland lists is hyprctl's stableId); with "Live windows" off, or when a window cannot be captured, a box with the app icon
// takes its place. Hyprland only (niri has an overview of its own).
constexpr double kOvPad = 14, kOvThumbH = 104, kOvThumbGap = 14, kOvLabelH = 22, kOvPlusLen = 64, kOvScrollStep = 80, kOvWindowGap = 5;
constexpr int kOvInMs = 240, kOvOutMs = 170, kOvLiveMs = 50, kOvBackMs = 600, kOvDrawMs = 30, kOvMaxCaptures = 40;

void ovMoveTo(App& app, const std::string& address, int ws);
bool ovVertical(const App& app) { return app.ov.edge >= 2; }
std::string ovGapsFile() { return stateDir() + "/overview-gaps.txt"; }

std::vector<int> ovIds(const App& app) {
    int top = std::max({1, app.cfg.workspaces, app.workspace});
    for (const auto& w : app.ov.wins) top = std::max(top, w->ws);
    top += app.ov.extra;
    std::vector<int> ids;
    for (int i = 1; i <= top; i++) ids.push_back(i);
    return ids;
}

double ovThumbW(const App& app) { return std::round(kOvThumbH * app.ov.monW / std::max(1.0, app.ov.monH)); }
// One workspace takes this much of the strip's length (a thumbnail, its number and the gap).
double ovCell(const App& app) { return ovVertical(app) ? kOvThumbH + kOvLabelH + kOvThumbGap : ovThumbW(app) + kOvThumbGap; }
double ovStripThickness(const App& app) { return ovVertical(app) ? ovThumbW(app) + 2 * kOvPad : kOvPad + kOvThumbH + kOvLabelH + kOvPad; }
// The visible part of the strip along its length (the + button is outside it).
double ovViewLen(const App& app) {
    const double total = ovVertical(app) ? app.ov.height : app.ov.width;
    return std::max(60.0, total - 2 * kOvPad - kOvPlusLen - kOvThumbGap);
}
double ovMaxScroll(const App& app) { return std::max(0.0, ovIds(app).size() * ovCell(app) - kOvThumbGap - ovViewLen(app)); }
Rect ovThumbRect(const App& app, size_t i) {
    const double along = kOvPad + i * ovCell(app) - app.ov.stripScroll;
    return ovVertical(app) ? Rect{kOvPad, along, ovThumbW(app), kOvThumbH} : Rect{along, kOvPad, ovThumbW(app), kOvThumbH};
}
Rect ovPlusRect(const App& app) {
    const double total = ovVertical(app) ? app.ov.height : app.ov.width, along = total - kOvPad - kOvPlusLen;
    return ovVertical(app) ? Rect{kOvPad, along, ovThumbW(app), kOvPlusLen} : Rect{along, kOvPad, kOvPlusLen, kOvThumbH};
}
// The miniature of a window inside the thumbnail of its workspace.
Rect ovMiniRect(const App& app, const OvWin& w, const Rect& t) {
    const OverviewState& O = app.ov;
    return {t.x + w.x / O.monW * t.w, t.y + w.y / O.monH * t.h, w.w / O.monW * t.w, w.h / O.monH * t.h};
}
// The window (an index of wins) whose miniature is under the point, the last drawn one first; -1 if none.
int ovMiniAt(const App& app, double x, double y) {
    const std::vector<int> ids = ovIds(app);
    for (size_t i = 0; i < ids.size(); i++) {
        const Rect t = ovThumbRect(app, i);
        if (!t.contains(x, y)) continue;
        for (int k = static_cast<int>(app.ov.wins.size()) - 1; k >= 0; k--) {
            const OvWin& w = *app.ov.wins[k];
            if (w.ws == ids[i] && ovMiniRect(app, w, t).contains(x, y)) return k;
        }
    }
    return -1;
}
// The workspace whose thumbnail is at the point (its id), or 0.
int ovThumbAt(const App& app, double x, double y) {
    const std::vector<int> ids = ovIds(app);
    for (size_t i = 0; i < ids.size(); i++) {
        const Rect r = ovThumbRect(app, i);
        const double a = ovVertical(app) ? r.y : r.x;
        if (a + (ovVertical(app) ? r.h : r.w) < kOvPad || a > kOvPad + ovViewLen(app)) continue;  // (scrolled out of the strip)
        if (r.contains(x, y)) return ids[i];
    }
    return 0;
}

// ---- live capture ----
// The picture of a window, small, kept when its buffer goes (the window changed its size, or the overview closes).
void ovKeepThumb(OvWin& w) {
    if (!w.hasFrame || !w.surf) return;
    const double iw = cairo_image_surface_get_width(w.surf), ih = cairo_image_surface_get_height(w.surf);
    if (iw < 1 || ih < 1) return;
    const int tw = 320, th = std::max(1, static_cast<int>(std::lround(tw * ih / iw)));
    cairo_surface_t* t = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
    cairo_t* c = cairo_create(t);
    cairo_scale(c, tw / iw, th / ih);
    cairo_set_source_surface(c, w.surf, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_GOOD);
    cairo_paint(c);
    cairo_destroy(c);
    if (w.last) cairo_surface_destroy(w.last);
    w.last = t;
}
void ovFreeBuffer(OvWin& w) {
    ovKeepThumb(w);
    if (w.surf) cairo_surface_destroy(w.surf);
    if (w.buf) wl_buffer_destroy(w.buf);
    if (w.pool) wl_shm_pool_destroy(w.pool);
    if (w.map) munmap(w.map, w.mapSize);
    w.surf = nullptr; w.buf = nullptr; w.pool = nullptr; w.map = nullptr; w.mapSize = 0;
    w.hasFrame = false;
}
void ovAllocBuffer(OvWin& w) {
    ovFreeBuffer(w);
    if (!w.app->shm || w.bw <= 0 || w.bh <= 0 || w.bw > 16384 || w.bh > 16384 || !w.fmt) { w.dead = true; return; }
    const int stride = w.bw * 4;
    const size_t size = static_cast<size_t>(stride) * w.bh;
    const int fd = memfd_create("end4pC-overview", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); w.dead = true; return; }
    w.map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (w.map == MAP_FAILED) { w.map = nullptr; close(fd); w.dead = true; return; }
    w.mapSize = size;
    w.pool = wl_shm_create_pool(w.app->shm, fd, size);
    close(fd);
    w.buf = wl_shm_pool_create_buffer(w.pool, 0, w.bw, w.bh, stride, w.fmt);
    w.surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(w.map), w.fmt == WL_SHM_FORMAT_ARGB8888 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, w.bw, w.bh, stride);
}

void ovSesSize(void* d, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h) {
    auto* x = static_cast<OvWin*>(d);
    x->bw = static_cast<int>(w);
    x->bh = static_cast<int>(h);
}
void ovSesShm(void* d, ext_image_copy_capture_session_v1*, uint32_t f) {
    auto* x = static_cast<OvWin*>(d);
    if (f == WL_SHM_FORMAT_ARGB8888 || (f == WL_SHM_FORMAT_XRGB8888 && !x->fmt)) x->fmt = f;
}
void ovSesDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
void ovSesDmabuf(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}
void ovSesDone(void* d, ext_image_copy_capture_session_v1*) {
    auto* x = static_cast<OvWin*>(d);
    if (x->inFlight) { x->realloc = true; return; }
    ovAllocBuffer(*x);
    x->sized = !x->dead;
    x->next = std::chrono::steady_clock::now();
}
void ovSesStopped(void* d, ext_image_copy_capture_session_v1*) { static_cast<OvWin*>(d)->dead = true; }
const ext_image_copy_capture_session_v1_listener kOvSession = {ovSesSize, ovSesShm, ovSesDevice, ovSesDmabuf, ovSesDone, ovSesStopped};

void ovFrameEnd(OvWin& w) {
    w.inFlight = false;
    if (w.frame) { ext_image_copy_capture_frame_v1_destroy(w.frame); w.frame = nullptr; }
    if (w.realloc) { w.realloc = false; ovAllocBuffer(w); w.sized = !w.dead; }
}
void ovFrameTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}
void ovFrameDamage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {}
void ovFrameTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {}
void ovFrameReady(void* d, ext_image_copy_capture_frame_v1*) {
    auto* x = static_cast<OvWin*>(d);
    const bool keep = !x->realloc;
    ovFrameEnd(*x);
    if (keep) x->hasFrame = true;
    x->frames++;
    x->fails = 0;
    x->next = std::chrono::steady_clock::now() + std::chrono::milliseconds(x->ws == x->ov->sel ? kOvLiveMs : kOvBackMs);
    x->ov->dirty = true;
}
void ovFrameFailed(void* d, ext_image_copy_capture_frame_v1*, uint32_t reason) {
    auto* x = static_cast<OvWin*>(d);
    x->lastReason = static_cast<int>(reason);
    ovFrameEnd(*x);
    // (the window changed its size: the session sends the new constraints, and the frame is asked again)
    const bool resized = reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS;
    if (!resized && ++x->fails >= 6) x->dead = true;
    x->next = std::chrono::steady_clock::now() + std::chrono::milliseconds(resized ? 120 : 500);
}
const ext_image_copy_capture_frame_v1_listener kOvFrame = {ovFrameTransform, ovFrameDamage, ovFrameTime, ovFrameReady, ovFrameFailed};

void ovRequestFrame(OvWin& w) {
    if (!w.ses || !w.buf || w.inFlight || w.dead) return;
    w.frame = ext_image_copy_capture_session_v1_create_frame(w.ses);
    ext_image_copy_capture_frame_v1_add_listener(w.frame, &kOvFrame, &w);
    ext_image_copy_capture_frame_v1_attach_buffer(w.frame, w.buf);
    ext_image_copy_capture_frame_v1_damage_buffer(w.frame, 0, 0, w.bw, w.bh);
    ext_image_copy_capture_frame_v1_capture(w.frame);
    w.inFlight = true;
}

void ovCaptureStart(App& app, OvWin& w) {
    if (!app.ftSrcMgr || !app.copyCapMgr || w.handleId.empty()) { w.dead = true; return; }
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    for (const FtHandle& f : app.ftHandles)
        if (f.id == w.handleId) handle = f.h;
    if (!handle) { w.dead = true; return; }
    ext_image_capture_source_v1* src = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(app.ftSrcMgr, handle);
    w.ses = ext_image_copy_capture_manager_v1_create_session(app.copyCapMgr, src, 0);
    ext_image_capture_source_v1_destroy(src);
    ext_image_copy_capture_session_v1_add_listener(w.ses, &kOvSession, &w);
}

void ovCaptureStop(OvWin& w) {
    if (w.frame) ext_image_copy_capture_frame_v1_destroy(w.frame);
    if (w.ses) ext_image_copy_capture_session_v1_destroy(w.ses);
    w.frame = nullptr; w.ses = nullptr;
    ovFreeBuffer(w);
}

// ---- state ----
std::vector<int> ovParseInts(const std::string& s) {
    std::vector<int> v;
    std::istringstream in(s);
    for (std::string f; in >> f;) v.push_back(std::atoi(f.c_str()));
    return v;
}

// Esc closes the overview wherever the keyboard is: a compositor key that exists only while the overview is open and still reaches the
// focused app (non_consuming).
void ovEscapeKey(bool on) {
    char exe[512];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    const std::string cmd = on ? "hl.bind(\"Escape\", hl.dsp.exec_cmd(\"" + std::string(exe) + " 'overview close'\"), {non_consuming = true})" : "hl.unbind(\"Escape\")";
    captureArgv({"hyprctl", "eval", cmd});
}

// The gaps go back to what they were (also at the start of the shell: it may have ended with the overview open).
void ovSetGaps(const int g[4]) {
    const std::string cmd = "hl.config({ general = { gaps_out = { top = " + std::to_string(g[0]) + ", right = " + std::to_string(g[1]) + ", bottom = " + std::to_string(g[2]) + ", left = " + std::to_string(g[3]) + " } } })";
    captureArgv({"hyprctl", "eval", cmd});
}
// The room for the strip is reserved by a tiny invisible layer surface on that screen only (the gaps_out of Hyprland are global: every screen moved).
void onResConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    App& app = *static_cast<App*>(d);
    OverviewState& O = app.ov;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!O.resSurface || O.resBuf) return;
    const int fd = memfd_create("end4pC-ov-res", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, 4) != 0) { if (fd >= 0) close(fd); return; }
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, 4);
    O.resBuf = wl_shm_pool_create_buffer(pool, 0, 1, 1, 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_attach(O.resSurface, O.resBuf, 0, 0);
    wl_surface_damage_buffer(O.resSurface, 0, 0, 1, 1);
    wl_surface_commit(O.resSurface);
}
void onResClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kResListener = {onResConfigure, onResClosed};

void ovDropReserve(OverviewState& O) {
    if (O.resLayer) zwlr_layer_surface_v1_destroy(O.resLayer);
    if (O.resSurface) wl_surface_destroy(O.resSurface);
    if (O.resBuf) wl_buffer_destroy(O.resBuf);
    O.resLayer = nullptr; O.resSurface = nullptr; O.resBuf = nullptr;
}
void ovRestoreGaps(App& app) {
    OverviewState& O = app.ov;
    ovDropReserve(O);
    ovEscapeKey(false);
    O.gapsSet = false;
    O.gapsPending = false;
    unlink(ovGapsFile().c_str());
}
void ovRecoverGaps() {
    const std::vector<int> g = ovParseInts(ccFileRead(ovGapsFile()));
    if (g.size() != 4) return;
    const int a[4] = {g[0], g[1], g[2], g[3]};
    ovSetGaps(a);
    ovEscapeKey(false);
    unlink(ovGapsFile().c_str());
}

// One line of the windows' table: address, workspace, position, size, class, title, stable id.
std::unique_ptr<OvWin> ovParseClient(const std::string& line, double mx, double my) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
    if (f.size() < 9) return nullptr;
    auto w = std::make_unique<OvWin>();
    w->address = f[0];
    w->ws = std::atoi(f[1].c_str());
    w->x = std::atof(f[2].c_str()) - mx;
    w->y = std::atof(f[3].c_str()) - my;
    w->w = std::max(1.0, std::atof(f[4].c_str()));
    w->h = std::max(1.0, std::atof(f[5].c_str()));
    w->cls = f[6];
    w->title = f[7];
    w->handleId = f[8];
    w->fixed = f.size() > 9 && f[9] == "true";
    return w;
}

// A monitor name goes into a command: only the characters a connector name has.
std::string ovSafeName(const std::string& n) {
    std::string s;
    for (char c : n) if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':') s += c;
    return s;
}

// The windows of every screen, each one placed in the space of the strip's screen (same proportions inside its own screen); the ones of the
// other screens count as fixed (the room for the strip does not move them).
std::string ovClientsJq(const std::string& m, const std::string& mx, const std::string& my, const std::string& mw, const std::string& mh) {
    return "hyprctl clients -j | jq -r --argjson ms \"$(hyprctl monitors -j)\" --argjson m " + m + " --argjson MX " + mx + " --argjson MY " + my + " --argjson MW " + mw + " --argjson MH " + mh +
           " '.[]|select(.mapped and (.hidden|not) and .workspace.id>0) | . as $c | ($ms[]|select(.id==$c.monitor)) as $cm | "
           "($cm.width/$cm.scale) as $cw | ($cm.height/$cm.scale) as $ch | "
           "[.address,.workspace.id,(if .monitor==$m then .at[0] else $MX+(.at[0]-$cm.x)*$MW/$cw end),(if .monitor==$m then .at[1] else $MY+(.at[1]-$cm.y)*$MH/$ch end),"
           "(if .monitor==$m then .size[0] else .size[0]*$MW/$cw end),(if .monitor==$m then .size[1] else .size[1]*$MH/$ch end),.class,.title,.stableId,(.floating or (.fullscreen>0) or .monitor!=$m)]|@tsv'";
}

void ovLoad(App& app) {
    OverviewState& O = app.ov;
    O.wins.clear();
    const std::string tsv = captureArgv({"bash", "-c",
        "mon=$(hyprctl monitors -j | jq -r --arg n \"" + ovSafeName(O.monName) + "\" '.[]|select(.name==$n or ($n==\"\" and .focused))|[.id,.x,.y,(.width/.scale),(.height/.scale),.reserved[0],.reserved[1],.reserved[2],.reserved[3]]|@tsv'); echo \"$mon\"; "
        "hyprctl getoption general:gaps_out -j | jq -r '.css'; "
        "read -r mid mx my mw mh _ <<< \"$(echo \"$mon\" | tr '\\t' ' ')\"; " + ovClientsJq("\"${mid:-0}\"", "\"${mx:-0}\"", "\"${my:-0}\"", "\"${mw:-1}\"", "\"${mh:-1}\"")});
    std::istringstream in(tsv);
    std::string first, gaps;
    std::getline(in, first);
    std::getline(in, gaps);
    double mx = 0, my = 0;
    {
        std::stringstream ss(first);
        std::vector<double> m;
        for (std::string part; std::getline(ss, part, '\t');) m.push_back(std::atof(part.c_str()));
        if (m.size() >= 9) {
            O.monId = static_cast<int>(m[0]); mx = m[1]; my = m[2]; O.monX = m[1]; O.monY = m[2]; O.monW = std::max(1.0, m[3]); O.monH = std::max(1.0, m[4]);
            for (int i = 0; i < 4; i++) O.resv[i] = m[5 + i];
            const double edgeRes[4] = {m[6], m[8], m[5], m[7]};  // reserved: left, top, right, bottom
            O.edgeReserved = edgeRes[O.edge];
        }
    }
    const std::vector<int> g = ovParseInts(gaps);
    if (g.size() == 4) for (int i = 0; i < 4; i++) O.gapOrig[i] = g[i];
    for (std::string line; std::getline(in, line);) {
        if (auto w = ovParseClient(line, mx, my)) {
            w->ov = &O;
            w->app = &app;
            if (const auto c = app.ovCache.find(w->address); c != app.ovCache.end()) w->last = cairo_surface_reference(c->second);  // (the last picture shows at once)
            O.wins.push_back(std::move(w));
        }
    }
}

// While the overview is open the gap of the strip's edge is bigger, and Hyprland moves every tiled window of every workspace with it. The strip
// shows the workspaces as they normally are: a window goes from the area it has now to the area it has without the overview.
struct OvArea { double x, y, w, h; };
OvArea ovAreaFor(const OverviewState& O, const int g[4]) {  // g: top, right, bottom, left
    return {O.resv[0] + g[3], O.resv[1] + g[0], O.monW - O.resv[0] - O.resv[2] - g[3] - g[1], O.monH - O.resv[1] - O.resv[3] - g[0] - g[2]};
}
void ovToOriginal(const OverviewState& O, OvWin& w) {
    if (!O.gapsSet || w.fixed) return;
    const OvArea a = ovAreaFor(O, O.gapNow), b = ovAreaFor(O, O.gapOrig);
    if (a.w < 1 || a.h < 1) return;
    w.x = b.x + (w.x - a.x) * b.w / a.w;
    w.y = b.y + (w.y - a.y) * b.h / a.h;
    w.w = w.w * b.w / a.w;
    w.h = w.h * b.h / a.h;
}

// The windows again, while the overview is open: moved, resized, opened or closed ones show in the strip as they are now.
void ovRefresh(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface || O.carrying) return;
    const std::string tsv = captureArgv({"bash", "-c",
        ovClientsJq(std::to_string(O.monId), std::to_string(O.monX), std::to_string(O.monY), std::to_string(O.monW), std::to_string(O.monH))});
    std::vector<std::unique_ptr<OvWin>> now;
    std::istringstream in(tsv);
    for (std::string line; std::getline(in, line);)
        if (auto w = ovParseClient(line, O.monX, O.monY)) { ovToOriginal(O, *w); now.push_back(std::move(w)); }
    if (now.empty() && !O.wins.empty()) return;  // (the question failed: keep what is shown)
    bool changed = false;
    for (auto it = O.wins.begin(); it != O.wins.end();) {  // closed
        const bool still = std::any_of(now.begin(), now.end(), [&](const auto& n) { return n->address == (*it)->address; });
        if (still) { ++it; continue; }
        ovCaptureStop(**it);
        it = O.wins.erase(it);
        O.pressWin = -1;  // (the indexes moved)
        changed = true;
    }
    for (auto& n : now) {
        const auto it = std::find_if(O.wins.begin(), O.wins.end(), [&](const auto& w) { return w->address == n->address; });
        if (it == O.wins.end()) {  // opened
            n->ov = &O;
            n->app = &app;
            O.wins.push_back(std::move(n));
            if (app.cfg.overviewLive && static_cast<int>(O.wins.size()) <= kOvMaxCaptures) ovCaptureStart(app, *O.wins.back());
            changed = true;
            continue;
        }
        OvWin& w = **it;
        if (w.ws != n->ws || std::abs(w.x - n->x) > 0.5 || std::abs(w.y - n->y) > 0.5 || std::abs(w.w - n->w) > 0.5 || std::abs(w.h - n->h) > 0.5) changed = true;
        w.ws = n->ws; w.x = n->x; w.y = n->y; w.w = n->w; w.h = n->h; w.cls = n->cls; w.title = n->title;
    }
    if (changed) O.dirty = true;
}
void ovSoon(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface) return;
    O.reloadAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(180);
    O.reloadAgain = true;
}

void ovSlotRelease(void* d, wl_buffer*) { static_cast<OverviewState::Slot*>(d)->busy = false; }
const wl_buffer_listener kOvSlotListener = {ovSlotRelease};

void ovFreeSlots(OverviewState& O) {
    for (auto& s : O.slot) {
        if (s.buf) wl_buffer_destroy(s.buf);
        if (s.map) munmap(s.map, O.slotSize);
        s = {};
    }
    O.slotSize = 0;
}

void destroyOverview(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface) return;
    ovRestoreGaps(app);
    const bool withLauncher = O.withLauncher;
    O.withLauncher = false;
    for (auto& w : O.wins) {
        ovCaptureStop(*w);  // (the picture of each window is kept small: it shows at once the next time)
        if (!w->last) continue;
        cairo_surface_t*& slot = app.ovCache[w->address];
        if (slot) cairo_surface_destroy(slot);
        slot = cairo_surface_reference(w->last);
    }
    O.wins.clear();
    ovFreeSlots(O);
    if (O.layer) zwlr_layer_surface_v1_destroy(O.layer);
    hidpi::detach(O.scale);
    wl_surface_destroy(O.surface);
    O.surface = nullptr; O.layer = nullptr;
    O.configured = false; O.dirty = false; O.hoverPlus = false; O.closing = false;
    O.hoverWs = 0;
    app.onOv = false;
    if (withLauncher) closeLauncher(app);  // (it came with the overview)
    else launcherApplyMargin(app);         // (the launcher goes back up)
}

// Closing slides the strip out; the windows come back with the gaps at the same time. The strip is taken down when it is out.
void closeOverview(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface || O.closing) return;
    if (!O.configured) { destroyOverview(app); return; }
    O.closing = true;
    O.closeT0 = std::chrono::steady_clock::now();
    ovRestoreGaps(app);
    if (O.withLauncher) { O.withLauncher = false; closeLauncher(app); }
    else launcherApplyMargin(app);
    O.dirty = true;
}
// How much of the strip is out: 1 at rest, 0 when it is off the edge.
double ovVisible(const OverviewState& O, std::chrono::steady_clock::time_point now) {
    const auto ms = [&](std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(now - a).count(); };
    if (O.closing) { const double t = std::clamp(ms(O.closeT0) / kOvOutMs, 0.0, 1.0); return 1 - t * t * t; }
    const double t = std::clamp(ms(O.openT0) / kOvInMs, 0.0, 1.0);
    return 1 - std::pow(1 - t, 3);
}

// ---- drawing ----
void ovScaledPaint(cairo_t* cr, cairo_surface_t* img, const Rect& r, double radius, cairo_filter_t filter) {
    const double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
    cairo_save(cr);
    roundedRect(cr, r.x, r.y, r.w, r.h, radius);
    cairo_clip(cr);
    cairo_translate(cr, r.x, r.y);
    cairo_scale(cr, r.w / iw, r.h / ih);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), filter);
    cairo_paint(cr);
    cairo_restore(cr);
}

void ovDrawThumb(App& app, cairo_t* cr, PangoLayout* L, int id, const Rect& t, bool selected, bool hot) {
    const OverviewState& O = app.ov;
    roundedRect(cr, t.x, t.y, t.w, t.h, 12);
    setSource(cr, app.color("surface_container"), 0.72);
    cairo_fill(cr);
    cairo_save(cr);
    roundedRect(cr, t.x, t.y, t.w, t.h, 12);
    cairo_clip(cr);
    for (size_t k = 0; k < O.wins.size(); k++) {
        const OvWin* wp = O.wins[k].get();
        if (wp->ws != id) continue;
        const Rect r = ovMiniRect(app, *wp, t);
        if (O.carrying && static_cast<int>(k) == O.pressWin) cairo_push_group(cr);
        if (app.cfg.overviewLive && ovImage(*wp)) ovScaledPaint(cr, ovImage(*wp), r, 3, CAIRO_FILTER_GOOD);
        else {
            roundedRect(cr, r.x, r.y, r.w, r.h, 3);
            setSource(cr, app.color("surface_container_highest"), 0.95);
            cairo_fill(cr);
            if (cairo_surface_t* icon = launcherAppIcon(app, lowerCopy(wp->cls)); icon && r.w > 22 && r.h > 22) {
                const double box = std::min(22.0, std::min(r.w, r.h) - 6);
                const double cx = app.cfg.overviewIcon == "left" ? r.x + 4 + box / 2 : app.cfg.overviewIcon == "right" ? r.x + r.w - 4 - box / 2 : r.x + r.w / 2;
                drawWsAppIcon(cr, icon, cx, r.y + r.h / 2, box);
            }
        }
        if (O.carrying && static_cast<int>(k) == O.pressWin) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, 0.3); }  // (the one being carried stays faint)
    }
    cairo_restore(cr);
    if (selected || hot) {
        roundedRect(cr, t.x - 2, t.y - 2, t.w + 4, t.h + 4, 14);
        setSource(cr, app.color("primary"), selected ? 1.0 : 0.6);
        cairo_set_line_width(cr, 3);
        cairo_stroke(cr);
    }
    clkText(cr, L, std::to_string(id), 13, selected, t.x, t.y + t.h + kOvLabelH / 2 + 1, app.color("on_surface"), selected ? 1.0 : 0.75, t.w, 1);
}

constexpr int kOverviewEdgeCorners[4][2] = {{0, 1}, {2, 3}, {0, 2}, {1, 3}};

Rect ovCornerRect(const App& app, int corner) {
    const auto& O = app.ov;
    const double w = app.cfg.hotCornerWidth, h = app.cfg.hotCornerHeight;
    return {corner % 2 ? O.width - w : 0, corner >= 2 ? O.height - h : 0, w, h};
}

std::string_view ovCornerScreen(const App& app) {
    return app.outputs.size() > 1 ? std::string_view(app.ov.monName) : std::string_view{};
}

int ovCornerAt(const App& app, double x, double y) {
    if (!app.cfg.hotCorners || app.ov.monName.empty()) return -1;
    for (int corner : kOverviewEdgeCorners[app.ov.edge])
        if (ovCornerRect(app, corner).contains(x, y) && app.cfg.cornerAction(corner, ovCornerScreen(app)) != "none")
            return corner;
    return -1;
}

void drawOverview(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface || !O.configured || O.width <= 0 || O.height <= 0) return;
    const int W = O.width, H = O.height;  // logical; the buffers are scaled (src/hidpi.h)
    const double sc = O.scale.factor();
    const int PW = hidpi::bufferExtent(W, sc), PH = hidpi::bufferExtent(H, sc), stride = PW * 4;
    const size_t size = static_cast<size_t>(stride) * PH;
    if (O.slotSize != size) ovFreeSlots(O);
    OverviewState::Slot* slot = nullptr;
    for (auto& s : O.slot) if (!s.busy) { slot = &s; break; }
    if (!slot) { O.dirty = true; return; }  // (both are still held by the compositor: next turn)
    if (!slot->buf) {
        const int fd = memfd_create("end4pC-overview-frame", 0);
        if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
        slot->map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
        slot->buf = wl_shm_pool_create_buffer(pool, 0, PW, PH, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        wl_buffer_add_listener(slot->buf, &kOvSlotListener, slot);
        O.slotSize = size;
    }
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(slot->map), CAIRO_FORMAT_ARGB32, PW, PH, stride);
    cairo_surface_set_device_scale(surf, sc, sc);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    cairo_save(cr);
    {  // the strip slides in from its edge and out to it
        const double off = (1 - ovVisible(O, std::chrono::steady_clock::now())) * O.thick;
        cairo_translate(cr, O.edge == 2 ? -off : O.edge == 3 ? off : 0, O.edge == 0 ? -off : O.edge == 1 ? off : 0);
    }

    // the band: flat, dark and translucent (the compositor blurs what is behind it)
    cairo_rectangle(cr, 0, 0, W, H);
    setSource(cr, Color{0.04, 0.04, 0.05}, 0.5);  // (translucent: the compositor blurs what is behind it)
    cairo_fill(cr);

    const std::vector<int> ids = ovIds(app);
    cairo_save(cr);
    if (ovVertical(app)) cairo_rectangle(cr, 0, kOvPad - 6, W, ovViewLen(app) + 12);
    else cairo_rectangle(cr, kOvPad - 6, 0, ovViewLen(app) + 12, H);
    cairo_clip(cr);
    for (size_t i = 0; i < ids.size(); i++) ovDrawThumb(app, cr, L, ids[i], ovThumbRect(app, i), ids[i] == O.sel, ids[i] == O.hoverWs);
    cairo_restore(cr);
    {
        const Rect p = ovPlusRect(app);
        roundedRect(cr, p.x, p.y, p.w, p.h, 12);
        setSource(cr, app.color(O.hoverPlus ? "primary_container" : "surface_container"), 0.92);
        cairo_fill(cr);
        drawIcon(cr, "add-circle", p.x + (p.w - 28) / 2, p.y + (p.h - 28) / 2, 28, app.color(O.hoverPlus ? "on_primary_container" : "on_surface"));
    }
    if (O.carrying && O.pressWin >= 0 && O.pressWin < static_cast<int>(O.wins.size())) {  // the window in the hand
        const OvWin& w = *O.wins[O.pressWin];
        const double cw = 150, ch = std::clamp(cw * w.h / w.w, 60.0, 110.0);
        const Rect r{app.pointerX - cw / 2, app.pointerY - ch / 2, cw, ch};
        roundedRect(cr, r.x + 1, r.y + 4, r.w, r.h, 8);
        setSource(cr, Color{0, 0, 0}, 0.3);
        cairo_fill(cr);
        if (app.cfg.overviewLive && ovImage(w)) ovScaledPaint(cr, ovImage(w), r, 8, CAIRO_FILTER_GOOD);
        else {
            roundedRect(cr, r.x, r.y, r.w, r.h, 8);
            setSource(cr, app.color("surface_container_highest"), 0.95);
            cairo_fill(cr);
            if (cairo_surface_t* icon = launcherAppIcon(app, lowerCopy(w.cls))) drawWsAppIcon(cr, icon, r.x + r.w / 2, r.y + r.h / 2, 36);
        }
        roundedRect(cr, r.x, r.y, r.w, r.h, 8);
        setSource(cr, app.color("primary"), 0.9);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    if (app.cfg.hotCorners && !O.monName.empty()) {
        for (int corner : kOverviewEdgeCorners[O.edge]) {
            if (!app.cfg.cornerHighlighted(corner, ovCornerScreen(app))) continue;
            const Rect r = ovCornerRect(app, corner);
            cairo_save(cr);
            cairo_translate(cr, r.x, r.y);
            paintHotCorner(cr, app, app.cfg.hotCornerWidth, app.cfg.hotCornerHeight);
            cairo_restore(cr);
        }
    }
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_surface_attach(O.surface, slot->buf, 0, 0);
    wl_surface_damage_buffer(O.surface, 0, 0, PW, PH);
    if (O.scale.viewport) wp_viewport_set_destination(O.scale.viewport, W, H);
    wl_surface_commit(O.surface);
    slot->busy = true;
    O.dirty = false;
}

void ovEnsureVisible(App& app) {
    OverviewState& O = app.ov;
    const std::vector<int> ids = ovIds(app);
    const auto it = std::find(ids.begin(), ids.end(), O.sel);
    if (it == ids.end()) return;
    const double left = (it - ids.begin()) * ovCell(app), right = left + (ovVertical(app) ? kOvThumbH + kOvLabelH : ovThumbW(app));
    if (left < O.stripScroll) O.stripScroll = left;
    if (right > O.stripScroll + ovViewLen(app)) O.stripScroll = right - ovViewLen(app);
    O.stripScroll = std::clamp(O.stripScroll, 0.0, ovMaxScroll(app));
}

void onOvConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    OverviewState& O = app->ov;
    O.width = static_cast<int>(w);
    O.height = static_cast<int>(h);
    O.configured = true;
    ovEnsureVisible(*app);
    drawOverview(*app);
}
void onOvClosed(void* d, zwlr_layer_surface_v1*) { destroyOverview(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kOvListener = {onOvConfigure, onOvClosed};

// Make room: the gap of the strip's edge grows until the windows start below it. This resizes the windows (Hyprland animates it for about half a
// second), and a capture of a window that is resizing fails: so the gaps wait until the first frames are in, 150 ms at most.
void ovApplyGaps(App& app) {
    OverviewState& O = app.ov;
    O.gapsPending = false;
    if (O.gapsSet) return;
    const int slotIdx[4] = {0, 2, 3, 1};  // top, bottom, left, right -> index in gapOrig (top, right, bottom, left)
    const int want = static_cast<int>(O.thick + kOvWindowGap - O.edgeReserved);
    const int extra = std::max(0, want - O.gapOrig[slotIdx[O.edge]]);
    for (int i = 0; i < 4; i++) O.gapNow[i] = O.gapOrig[i];
    O.gapNow[slotIdx[O.edge]] += extra;
    O.resZone = extra;
    O.gapsSet = true;
    if (extra <= 0) return;
    O.resSurface = wl_compositor_create_surface(app.compositor);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(O.resSurface, none);
    wl_region_destroy(none);
    wl_output* out = nullptr;
    for (auto& b : app.bars) if (b->connector == O.monName) { out = b->output; break; }
    O.resLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, O.resSurface, out, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "end4pC-overview-room");
    const uint32_t anchors[4] = {ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT};
    zwlr_layer_surface_v1_set_anchor(O.resLayer, anchors[O.edge]);
    zwlr_layer_surface_v1_set_size(O.resLayer, 1, 1);
    zwlr_layer_surface_v1_set_exclusive_zone(O.resLayer, extra);
    zwlr_layer_surface_v1_add_listener(O.resLayer, &kResListener, &app);
    wl_surface_commit(O.resSurface);
}

void openOverview(App& app, const std::string& output) {
    OverviewState& O = app.ov;
    if (O.surface) { closeOverview(app); return; }
    if (wmKind() != Wm::Hyprland || !app.compositor || !app.layerShell || !app.shm) return;
    const std::string pos = app.cfg.overviewPosition;
    O.edge = pos == "bottom" ? 1 : pos == "left" ? 2 : pos == "right" ? 3 : 0;
    // The screen of the overview is the one in focus (where the pointer is), and so is the list of windows it shows.
    const std::string focused = output.empty() ? focusedOutputName() : output;
    Bar* bar = nullptr;
    for (auto& b : app.bars)
        if (!focused.empty() && b->connector == focused) { bar = b.get(); break; }
    if (!bar) bar = mainBar(app);  // (no bar on that screen: the one of the main bar)
    O.monName = bar ? bar->connector : "";
    ovLoad(app);
    for (auto it = app.ovCache.begin(); it != app.ovCache.end();) {  // (pictures of windows that are gone go)
        const bool there = std::any_of(O.wins.begin(), O.wins.end(), [&](const auto& w) { return w->address == it->first; });
        if (there) { ++it; continue; }
        cairo_surface_destroy(it->second);
        it = app.ovCache.erase(it);
    }
    O.sel = std::max(1, app.workspace);
    O.extra = 0;
    O.stripScroll = 0;
    O.hoverWs = 0;
    O.hoverPlus = false;
    O.thick = std::ceil(ovStripThickness(app));
    if (app.cfg.overviewLive)
        for (size_t i = 0; i < O.wins.size() && static_cast<int>(i) < kOvMaxCaptures; i++) ovCaptureStart(app, *O.wins[i]);
    O.pressedCorner = -1;
    O.openT0 = std::chrono::steady_clock::now();
    O.closing = false;
    O.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, O.scale, O.surface, [&app] { app.ov.dirty = true; drawOverview(app); });
    O.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, O.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-overview");
    const uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, Lf = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const uint32_t anchors[4] = {T | Lf | R, B | Lf | R, T | B | Lf, T | B | R};
    zwlr_layer_surface_v1_set_anchor(O.layer, anchors[O.edge]);
    const uint32_t th = static_cast<uint32_t>(O.thick);
    zwlr_layer_surface_v1_set_size(O.layer, ovVertical(app) ? th : 0, ovVertical(app) ? 0 : th);
    zwlr_layer_surface_v1_set_exclusive_zone(O.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(O.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);  // (an exclusive one would stop the mouse binds that drag a window)
    zwlr_layer_surface_v1_add_listener(O.layer, &kOvListener, &app);
    wl_surface_commit(O.surface);
    // the windows make room for the strip, but only once their first frames are in (see ovApplyGaps)
    ovEscapeKey(true);
    O.gapsPending = true;
    O.gapsAt = O.openT0 + std::chrono::milliseconds(150);
    if (!app.cfg.overviewLive || O.wins.empty()) ovApplyGaps(app);
    O.nextRefresh = O.openT0 + std::chrono::seconds(2);
    O.switchedAt = std::chrono::steady_clock::now();
    if (app.launcher.open()) launcherApplyMargin(app);  // (a launcher that was already open moves down)
    if (app.cfg.overviewLauncher && !app.launcher.open()) { O.withLauncher = true; openLauncher(app); }
}

// ---- actions and input ----
// Goes to a workspace: the real windows below change by themselves.
void ovGoTo(App& app, int id) {
    OverviewState& O = app.ov;
    const int top = static_cast<int>(ovIds(app).size());
    id = std::clamp(id, 1, top);
    O.sel = id;
    O.switchedAt = std::chrono::steady_clock::now();
    focusWorkspace(id);
    ovEnsureVisible(app);
    O.dirty = true;
}

// Where a carried window would land: a thumbnail, + (-1), or the workspace in use when the pointer is out of the strip (over the real windows).
int ovCarryTarget(const App& app, bool& plus) {
    const OverviewState& O = app.ov;
    plus = false;
    const double x = app.pointerX, y = app.pointerY;
    if (x < 0 || y < 0 || x >= O.width || y >= O.height) return O.sel;
    if (ovPlusRect(app).contains(x, y)) { plus = true; return 0; }
    return ovThumbAt(app, x, y);
}

void ovMotion(App& app) {
    OverviewState& O = app.ov;
    if (O.closing) return;
    if (O.pressWin >= 0 && !O.carrying && std::hypot(app.pointerX - O.pressX, app.pointerY - O.pressY) > 6) O.carrying = true;
    if (O.carrying) {
        bool plus = false;
        const int ws = ovCarryTarget(app, plus);
        O.hoverWs = ws; O.hoverPlus = plus; O.dirty = true;
        return;
    }
    const int ws = ovThumbAt(app, app.pointerX, app.pointerY);
    const bool plus = ovPlusRect(app).contains(app.pointerX, app.pointerY);
    if (ws != O.hoverWs || plus != O.hoverPlus) { O.hoverWs = ws; O.hoverPlus = plus; O.dirty = true; }
}

void ovButton(App& app, uint32_t button, bool pressed) {
    OverviewState& O = app.ov;
    if (O.closing) return;
    if (button != 0x110) return;
    if (pressed) { O.pressWin = ovMiniAt(app, app.pointerX, app.pointerY); O.pressX = app.pointerX; O.pressY = app.pointerY; O.carrying = false; return; }
    if (O.carrying) {  // a window of a thumbnail is dropped
        bool plus = false;
        const int ws = ovCarryTarget(app, plus);
        const int win = O.pressWin;
        O.carrying = false; O.pressWin = -1; O.hoverWs = 0; O.hoverPlus = false; O.dirty = true;
        if (win < 0 || win >= static_cast<int>(O.wins.size())) return;
        const std::string addr = O.wins[win]->address;
        int target = ws;
        if (plus) { O.extra++; target = static_cast<int>(ovIds(app).size()); }
        if (target >= 1 && target != O.wins[win]->ws) ovMoveTo(app, addr, target);
        return;
    }
    O.pressWin = -1;
    const double x = app.pointerX, y = app.pointerY;
    if (ovPlusRect(app).contains(x, y)) { app.ov.extra++; ovGoTo(app, static_cast<int>(ovIds(app).size())); return; }
    if (const int ws = ovThumbAt(app, x, y); ws > 0) ovGoTo(app, ws);
}

void ovScroll(App& app, int dir) { ovGoTo(app, app.ov.sel + dir); }

void ovKey(App& app, uint32_t key) {
    if (key == kKeyEsc || key == kKeyEnter || key == kKeyKpEnter) { closeOverview(app); return; }
    if (key == kKeyLeft || key == kKeyUp) ovGoTo(app, app.ov.sel - 1);
    else if (key == kKeyRight || key == kKeyDown) ovGoTo(app, app.ov.sel + 1);
}

void ovWorkspaceChanged(App& app) {
    OverviewState& O = app.ov;
    if (!O.surface || app.workspace == O.sel) return;
    O.sel = app.workspace;
    ovEnsureVisible(app);
    O.dirty = true;
}

// While a window is dragged with the mouse bind, Hyprland keeps the pointer away from every client: no event reaches the strip. So the cursor is
// read from the compositor. It is over the strip for a few reads in a row with no pointer event: a window is on its way to a workspace. When the
// drag ends the pointer comes back to the strip (an enter event): that is the drop.
std::pair<double, double> ovLocal(const App& app, double gx, double gy) {
    const OverviewState& O = app.ov;
    const double ox = O.edge == 3 ? O.monW - O.thick : 0, oy = O.edge == 1 ? O.monH - O.thick : 0;
    return {gx - O.monX - ox, gy - O.monY - oy};
}
void ovPollDrag(App& app) {
    OverviewState& O = app.ov;
    const std::string r = hyprRequest("cursorpos");
    double gx = 0, gy = 0;
    if (std::sscanf(r.c_str(), "%lf, %lf", &gx, &gy) != 2) return;
    const auto [lx, ly] = ovLocal(app, gx, gy);
    const bool inside = lx >= 0 && ly >= 0 && lx < O.width && ly < O.height;
    if (!inside) {
        O.dragSeen = 0;
        if (O.dragArmed) { O.dragArmed = false; O.hoverWs = 0; O.hoverPlus = false; O.dirty = true; }
        return;
    }
    O.dragX = lx; O.dragY = ly;
    if (++O.dragSeen < 3) return;
    if (!O.dragArmed) {
        const std::string aw = hyprRequest("j/activewindow");
        const size_t p = aw.find("\"address\": \"");
        O.dragWin = p == std::string::npos ? "" : aw.substr(p + 12, aw.find('"', p + 12) - p - 12);
        if (O.dragWin.empty()) return;
        O.dragArmed = true;
    }
    const int ws = ovThumbAt(app, lx, ly);
    const bool plus = ovPlusRect(app).contains(lx, ly);
    if (ws != O.hoverWs || plus != O.hoverPlus) { O.hoverWs = ws; O.hoverPlus = plus; O.dirty = true; }
}
void ovMoveTo(App& app, const std::string& address, int ws) {
    const std::string r = hyprRequest("dispatch hl.dsp.window.move({ workspace = " + std::to_string(ws) + ", follow = false, window = \"address:" + address + "\" })");
    if (r.rfind("ok", 0) != 0) hyprRequest("dispatch movetoworkspacesilent " + std::to_string(ws) + ",address:" + address);
    for (auto& w : app.ov.wins) if (w->address == address) w->ws = ws;
    app.ov.dirty = true;
    ovSoon(app);
}
void ovDrop(App& app) {
    OverviewState& O = app.ov;
    if (!O.dragArmed) return;
    O.dragArmed = false;
    O.dragSeen = 0;
    int ws = ovThumbAt(app, O.dragX, O.dragY);
    if (ovPlusRect(app).contains(O.dragX, O.dragY)) { O.extra++; ws = static_cast<int>(ovIds(app).size()); }
    O.hoverWs = 0; O.hoverPlus = false; O.dirty = true;
    if (ws < 1 || O.dragWin.empty()) return;
    ovMoveTo(app, O.dragWin, ws);
}
void ovPointerEntered(App& app) { ovDrop(app); }

void ovTick(App& app, std::chrono::steady_clock::time_point now) {
    OverviewState& O = app.ov;
    if (!O.surface) return;
    if (O.gapsPending && !O.closing) {
        const bool framed = std::all_of(O.wins.begin(), O.wins.end(), [](const auto& w) { return w->dead || !w->ses || w->hasFrame; });
        if (framed || now >= O.gapsAt) ovApplyGaps(app);
    }
    const bool sliding = O.closing || now - O.openT0 < std::chrono::milliseconds(kOvInMs + 40);
    if (O.closing && now - O.closeT0 >= std::chrono::milliseconds(kOvOutMs)) { destroyOverview(app); return; }
    if (sliding) O.dirty = true;
    if (O.reloadAt != std::chrono::steady_clock::time_point{} && now >= O.reloadAt) {
        ovRefresh(app);
        if (O.reloadAgain) { O.reloadAgain = false; O.reloadAt = now + std::chrono::milliseconds(450); } else O.reloadAt = {};
    } else if (now >= O.nextRefresh) { O.nextRefresh = now + std::chrono::seconds(2); ovRefresh(app); }
    if (!app.onOv && now >= O.nextPoll) { ovPollDrag(app); O.nextPoll = now + std::chrono::milliseconds(50); }
    if (app.cfg.overviewLive)
        for (auto& w : O.wins)
            if (!w->dead && w->sized && !w->inFlight && now >= w->next) ovRequestFrame(*w);
    if (O.dirty && O.configured && now >= O.nextDraw) { drawOverview(app); O.nextDraw = now + std::chrono::milliseconds(sliding ? 12 : kOvDrawMs); }
}
std::chrono::steady_clock::time_point ovWake(const App& app) {
    return app.ov.surface ? std::chrono::steady_clock::now() + std::chrono::milliseconds(16) : std::chrono::steady_clock::time_point::max();
}
