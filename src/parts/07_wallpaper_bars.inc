

int g_wpWakeFd = -1;
int g_wpWakeWrite = -1;
cairo_surface_t* collageRenderSurface(const std::string& wallpaper, int w, int h, double scale, double r, double g, double b, const CollageSpec& spec);
void paintWallpaperCover(cairo_t* cr, const std::string& path, int w, int h);
void pruneCollageCache(const App& app, const std::string& oldPath, const CollageSpec& oldCollage);
void collageSync(App& app);

// w x h logical; the picture has w x h times `scale` pixels and that device scale, so it paints at w x h (src/hidpi.h).
cairo_surface_t* renderWallpaperSurface(const std::string& path, int w, int h, double scale, double r, double g, double b, const CwpParams& cwp, const CollageSpec& collage) {
    if (collage.on) return collageRenderSurface(path, w, h, scale, r, g, b, collage);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hidpi::bufferExtent(w, scale), hidpi::bufferExtent(h, scale));
    cairo_surface_set_device_scale(surf, scale, scale);
    cairo_t* cr = mkcr(surf);
    if (cwp.on) {
        cwpPaint(cr, w, h, path, cwp);
        cairo_destroy(cr);
        return surf;
    }
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    paintWallpaperCover(cr, path, w, h);
    cairo_destroy(cr);
    return surf;
}

double easeInOutCubic(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }

void paintWallpaperFrame(Bar& bar, App& app, cairo_surface_t* from, cairo_surface_t* to, const std::string& kind, double e) {
    const int w = bar.wpW, h = bar.wpH;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, w, h, bar.wpScale.factor(), "end4pC-wallpaper-frame")) return;
    cairo_t* cr = mkcr(hf.surface);
    const double W = w, H = h, feather = 180;
    if (e >= 1.0) {
        cairo_set_source_surface(cr, to, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_surface(cr, from, kind == "slide" ? -e * W : 0, 0);
        cairo_paint(cr);
        if (kind == "slide") {
            cairo_set_source_surface(cr, to, W - e * W, 0);
            cairo_paint(cr);
        } else if (kind == "blinds") {
            constexpr int kSlats = 12;
            const double sw = W / kSlats;
            for (int i = 0; i < kSlats; i++) cairo_rectangle(cr, i * sw, 0, sw * e, H);
            cairo_clip(cr);
            cairo_set_source_surface(cr, to, 0, 0);
            cairo_paint(cr);
        } else if (kind == "diamond") {
            const double cx = W / 2, cy = H / 2, k = 2 * e;
            cairo_move_to(cr, cx - k * cx, cy);
            cairo_line_to(cr, cx, cy - k * cy);
            cairo_line_to(cr, cx + k * cx, cy);
            cairo_line_to(cr, cx, cy + k * cy);
            cairo_close_path(cr);
            cairo_clip(cr);
            cairo_set_source_surface(cr, to, 0, 0);
            cairo_paint(cr);
        } else if (kind == "split") {
            cairo_rectangle(cr, W / 2 - e * W / 2, 0, e * W, H);
            cairo_clip(cr);
            cairo_set_source_surface(cr, to, 0, 0);
            cairo_paint(cr);
        } else if (kind == "wipe") {
            const double x = e * (W + feather) - feather;
            if (x > 0) {
                cairo_save(cr);
                cairo_rectangle(cr, 0, 0, std::min(x, W), H);
                cairo_clip(cr);
                cairo_set_source_surface(cr, to, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            const double x0 = std::max(x, 0.0), x1 = std::min(x + feather, W);
            if (x1 > x0) {
                cairo_save(cr);
                cairo_rectangle(cr, x0, 0, x1 - x0, H);
                cairo_clip(cr);
                cairo_pattern_t* grad = cairo_pattern_create_linear(x, 0, x + feather, 0);
                cairo_pattern_add_color_stop_rgba(grad, 0, 0, 0, 0, 1);
                cairo_pattern_add_color_stop_rgba(grad, 1, 0, 0, 0, 0);
                cairo_pattern_set_extend(grad, CAIRO_EXTEND_PAD);
                cairo_set_source_surface(cr, to, 0, 0);
                cairo_mask(cr, grad);
                cairo_pattern_destroy(grad);
                cairo_restore(cr);
            }
        } else if (kind == "circle") {
            const double cx = W / 2, cy = H / 2, maxR = std::hypot(cx, cy) + feather;
            const double R = e * maxR, r0 = std::max(0.0, R - feather);
            if (r0 > 0) {
                cairo_save(cr);
                cairo_arc(cr, cx, cy, r0, 0, 2 * M_PI);
                cairo_clip(cr);
                cairo_set_source_surface(cr, to, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            cairo_save(cr);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, R, 0, 2 * M_PI);
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, r0, 0, 2 * M_PI);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            cairo_clip(cr);
            cairo_pattern_t* grad = cairo_pattern_create_radial(cx, cy, r0, cx, cy, R);
            cairo_pattern_add_color_stop_rgba(grad, 0, 0, 0, 0, 1);
            cairo_pattern_add_color_stop_rgba(grad, 1, 0, 0, 0, 0);
            cairo_pattern_set_extend(grad, CAIRO_EXTEND_PAD);
            cairo_set_source_surface(cr, to, 0, 0);
            cairo_mask(cr, grad);
            cairo_pattern_destroy(grad);
            cairo_restore(cr);
        } else {
            cairo_set_source_surface(cr, to, 0, 0);
            cairo_paint_with_alpha(cr, e);
        }
    }
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, &bar);
    hidpi::present(bar.wpSurface, buffer, hf, bar.wpScale);
}

void cancelWallpaperAnim(Bar& bar) {
    bar.wpJob.reset();
    if (bar.animFrom) { cairo_surface_destroy(bar.animFrom); bar.animFrom = nullptr; }
    if (bar.animTo) { cairo_surface_destroy(bar.animTo); bar.animTo = nullptr; }
}

void collectWallpaperJobs(App& app) {
    if (g_collageDecoded.exchange(false) && app.cfg.collage)
        for (auto& b : app.bars) if (!b->animTo && !b->wpJob) drawWallpaper(*b, app);
    for (auto& b : app.bars) {
        Bar& bar = *b;
        if (!bar.wpJob || bar.wpJob->pending.load() != 0) continue;
        std::shared_ptr<WpJob> job = std::move(bar.wpJob);
        bar.wpJob.reset();
        if (job->serial != app.wpSerial || !job->from || !job->to) {
            if (job->serial == app.wpSerial) drawWallpaper(bar, app);
            continue;
        }
        cancelWallpaperAnim(bar);
        bar.animFrom = job->from;
        bar.animTo = job->to;
        job->from = job->to = nullptr;
        bar.animKind = job->kind;
        bar.animMs = job->ms;
        bar.animStart = std::chrono::steady_clock::now();
    }
}

bool stepWallpaperAnim(App& app, Bar& bar, std::chrono::steady_clock::time_point now) {
    if (!bar.animTo || !bar.animFrom) return false;
    const double t = std::chrono::duration<double, std::milli>(now - bar.animStart).count() / bar.animMs;
    if (t >= 1.0) {
        paintWallpaperFrame(bar, app, bar.animFrom, bar.animTo, bar.animKind, 1.0);
        cancelWallpaperAnim(bar);
        malloc_trim(0);
        return false;
    }
    paintWallpaperFrame(bar, app, bar.animFrom, bar.animTo, bar.animKind, easeInOutCubic(std::max(0.0, t)));
    return true;
}

std::string wallpaperTransitionKind(const App& app) {
    if (app.cfg.wallpaperTransition != "random") return app.cfg.wallpaperTransition;
    const size_t n = sizeof(kWallpaperTransitions) / sizeof(kWallpaperTransitions[0]);
    return kWallpaperTransitions[1 + std::rand() % (n - 1)];
}

void transitionWallpaper(App& app, const std::string& oldPath, CollageSpec oldCollage, const std::string& kind) {
    const CollageSpec nowCollage = collageNow(app);
    pruneCollageCache(app, oldPath, oldCollage);
    app.collageShown = nowCollage;
    if (kind == "none" || oldPath.empty() || (oldPath == app.wallpaper && oldCollage == nowCollage) || g_wpWakeWrite < 0) {
        for (auto& b : app.bars) cancelWallpaperAnim(*b);
        redrawWallpapers(app);
        return;
    }
    const uint64_t serial = ++app.wpSerial;
    const Color bg = app.color("surface");
    for (auto& b : app.bars) {
        Bar& bar = *b;
        if (!bar.wpSurface || bar.wpW == 0 || bar.wpH == 0) continue;
        cancelWallpaperAnim(bar);
        auto job = std::make_shared<WpJob>();
        job->fromPath = oldPath;
        job->toPath = app.wallpaper;
        job->fromCollage = withBarInsets(oldCollage, bar, app);
        job->toCollage = withBarInsets(nowCollage, bar, app);
        job->w = bar.wpW;
        job->h = bar.wpH;
        job->scale = bar.wpScale.factor();
        job->cwp = cwpParams(app);
        job->bgR = bg.r; job->bgG = bg.g; job->bgB = bg.b;
        job->serial = serial;
        job->kind = kind;
        job->ms = app.cfg.wallpaperTransitionMs;
        bar.wpJob = job;
        for (int which = 0; which < 2; which++)
            std::thread([job, which] {
                const bool isOld = which == 0;
                cairo_surface_t* surf = renderWallpaperSurface(isOld ? job->fromPath : job->toPath, job->w, job->h, job->scale, job->bgR, job->bgG, job->bgB,
                                                               job->cwp, isOld ? job->fromCollage : job->toCollage);
                (isOld ? job->from : job->to) = surf;
                job->pending--;
                (void)!write(g_wpWakeWrite, "x", 1);
            }).detach();
    }
}

void changeWallpaper(App& app, const std::string& path) {
    const std::string old = app.wallpaper;
    const CollageSpec oldCollage = app.collageShown;
    app.wallpaper = path;
    transitionWallpaper(app, old, oldCollage, wallpaperTransitionKind(app));
}

void onConfigure(void* userData, zwlr_layer_surface_v1* layerSurface, uint32_t serial, uint32_t width, uint32_t height) {
    auto* bar = static_cast<Bar*>(userData);
    if (sideBar(*bar->app)) {
        bar->width = height;
        if (width > 0) bar->height = width - bar->extra;
    } else {
        bar->width = width;
        if (height > 0) bar->height = height - bar->extra;
    }
    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
    bar->configured = true;
    draw(*bar);
}

void destroyStrip(Strip& st);

// Gives a bar-related surface its fractional scale (src/hidpi.h). A new scale from the compositor redraws them all from the main loop.
void attachBarScale(App& app, hidpi::SurfaceScale& s, wl_surface* surface) {
    hidpi::attach(s, surface, app.viewporter, app.fscaleMgr, [&app] { app.scaleChanged = true; });
}

void destroyBar(Bar& bar) {
    for (auto& st : bar.strips) destroyStrip(st);
    for (auto& st : bar.rounds) destroyStrip(st);
    for (auto& st : bar.corners) destroyStrip(st);
    if (bar.app->hoverBar == &bar) bar.app->hoverBar = nullptr;
    if (bar.wpLayer) zwlr_layer_surface_v1_destroy(bar.wpLayer);
    hidpi::detach(bar.wpScale);
    if (bar.wpSurface) wl_surface_destroy(bar.wpSurface);
    if (bar.bdLayer) zwlr_layer_surface_v1_destroy(bar.bdLayer);
    if (bar.bdSurface) wl_surface_destroy(bar.bdSurface);
    if (bar.bdPic) cairo_surface_destroy(bar.bdPic);
    if (bar.layerSurface) zwlr_layer_surface_v1_destroy(bar.layerSurface);
    releaseBlurSurface(bar.blur);
    hidpi::detach(bar.scale);
    if (bar.surface) wl_surface_destroy(bar.surface);
    if (bar.shLayer) zwlr_layer_surface_v1_destroy(bar.shLayer);
    hidpi::detach(bar.shScale);
    if (bar.shSurface) wl_surface_destroy(bar.shSurface);
    if (bar.output) wl_output_destroy(bar.output);
}

// ---- The Overview's backdrop (niri) ----
// niri shows a flat colour behind the zoomed-out workspaces. A background layer surface with the rule `place-within-backdrop`
// (niri/end4pC.kdl) is drawn there instead: the wallpaper as the shell painted it (so the collage, the centred wallpaper and
// the solid colour come along), shrunk to 1/kBackdropScale and blurred (about the strength of a 48 px FastBlur). The blurred
// picture is kept small (a few hundred KB); each time it is shown it is stretched to the screen's size into a short-lived buffer
// (niri does not draw a backdrop surface whose buffer scale differs from 1).
constexpr int kBackdropScale = 8, kBackdropBlurRadius = 4, kBackdropBlurPasses = 3;

void backdropBoxBlur(cairo_surface_t* s, int radius, int passes) {
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s), stride = cairo_image_surface_get_stride(s);
    auto* px = cairo_image_surface_get_data(s);
    std::vector<uint8_t> line(static_cast<size_t>(std::max(w, h)) * 4);
    const int win = radius * 2 + 1;
    const auto run = [&](int n, auto at) {
        for (int i = 0; i < n; i++) std::memcpy(&line[i * 4], at(i), 4);
        for (int c = 0; c < 4; c++) {
            int sum = 0;
            for (int k = -radius; k <= radius; k++) sum += line[std::clamp(k, 0, n - 1) * 4 + c];
            for (int i = 0; i < n; i++) {
                at(i)[c] = static_cast<uint8_t>(sum / win);
                sum += line[std::min(n - 1, i + radius + 1) * 4 + c] - line[std::max(0, i - radius) * 4 + c];
            }
        }
    };
    for (int pass = 0; pass < passes; pass++) {
        for (int y = 0; y < h; y++) run(w, [&](int i) { return px + y * stride + i * 4; });
        for (int x = 0; x < w; x++) run(h, [&](int i) { return px + i * stride + x * 4; });
    }
    cairo_surface_mark_dirty(s);
}

void drawBackdrop(Bar& bar, App& app) {
    if (!bar.bdSurface || !bar.bdPic || bar.bdW == 0 || bar.bdH == 0) return;
    const int w = static_cast<int>(bar.bdW), h = static_cast<int>(bar.bdH), stride = w * 4, size = stride * h;
    const int fd = memfd_create("end4pC-backdrop", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, w, h, stride);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, double(w) / cairo_image_surface_get_width(bar.bdPic), double(h) / cairo_image_surface_get_height(bar.bdPic));
    cairo_set_source_surface(cr, bar.bdPic, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(bar.bdSurface, buffer, 0, 0);
    wl_surface_damage_buffer(bar.bdSurface, 0, 0, w, h);
    wl_surface_commit(bar.bdSurface);
}

void backdropSetPicture(Bar& bar, cairo_surface_t* full) {
    const int fw = cairo_image_surface_get_width(full), fh = cairo_image_surface_get_height(full);
    const int w = (fw + kBackdropScale - 1) / kBackdropScale, h = (fh + kBackdropScale - 1) / kBackdropScale;
    double ds = 1, dsy = 1;  // (`full` may carry a device scale, src/hidpi.h: the shrink is counted in its pixels)
    cairo_surface_get_device_scale(full, &ds, &dsy);
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(small);
    cairo_scale(cr, double(w) * ds / fw, double(h) * dsy / fh);
    cairo_set_source_surface(cr, full, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    backdropBoxBlur(small, kBackdropBlurRadius, kBackdropBlurPasses);
    if (bar.bdPic) cairo_surface_destroy(bar.bdPic);
    bar.bdPic = small;
    drawBackdrop(bar, *bar.app);
}

void onBackdropConfigure(void* userData, zwlr_layer_surface_v1* layerSurface, uint32_t serial, uint32_t width, uint32_t height) {
    auto* bar = static_cast<Bar*>(userData);
    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
    bar->bdW = width;
    bar->bdH = height;
    drawBackdrop(*bar, *bar->app);
}
void onBackdropClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kBackdropListener = {onBackdropConfigure, onBackdropClosed};

void onWpConfigure(void* userData, zwlr_layer_surface_v1* layerSurface, uint32_t serial, uint32_t width, uint32_t height) {
    auto* bar = static_cast<Bar*>(userData);
    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
    if (width == bar->wpW && height == bar->wpH) return;
    bar->wpW = width;
    bar->wpH = height;
    drawWallpaper(*bar, *bar->app);
}
void onWpClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kWpListener = {onWpConfigure, onWpClosed};

void onClosed(void* userData, zwlr_layer_surface_v1*) {
    auto* bar = static_cast<Bar*>(userData);
    App& app = *bar->app;
    destroyBar(*bar);
    std::erase_if(app.bars, [&](const auto& b) { return b.get() == bar; });
}

const zwlr_layer_surface_v1_listener kLayerSurfaceListener = {onConfigure, onClosed};

void drawStrip(Strip& st, App& app) {
    if (!st.surface || st.w == 0 || st.h == 0) return;
    const int stride = st.w * 4, size = stride * st.h;
    int fd = memfd_create("end4pC-frame", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    std::memset(data, 0, size);
    uint32_t* px = static_cast<uint32_t*>(data);
    const Color fc = frameColorOf(app);
    const uint32_t opaque = 0xFF000000u | (static_cast<uint32_t>(std::lround(fc.r * 255)) << 16) | (static_cast<uint32_t>(std::lround(fc.g * 255)) << 8) | static_cast<uint32_t>(std::lround(fc.b * 255));
    for (size_t i = 0; i < static_cast<size_t>(st.w) * st.h; i++) px[i] = opaque;
    munmap(data, size);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, st.w, st.h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, st.bar);
    wl_surface_attach(st.surface, buffer, 0, 0);
    wl_surface_damage_buffer(st.surface, 0, 0, st.w, st.h);
    wl_surface_commit(st.surface);
}

void onStripConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* st = static_cast<Strip*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    st->w = w;
    st->h = h;
    drawStrip(*st, *st->bar->app);
}
void onCornerRelease(void*, wl_buffer* buffer) { wl_buffer_destroy(buffer); }
const wl_buffer_listener kCornerBufferListener = {onCornerRelease};
void paintHotCorner(cairo_t* cr, const App& app, int w, int h) {
    const Color c = app.color("primary");
    roundedRect(cr, 0.75, 0.75, w - 1.5, h - 1.5, std::min(8.0, std::min(w, h) / 3.0));
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.3);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}
// One hot corner: an invisible region of the configured size, or a translucent one in the color of the theme (to see where it is).
void drawCorner(Strip& st) {
    const App& app = *st.bar->app;
    const int w = app.cfg.hotCornerWidth, h = app.cfg.hotCornerHeight, stride = w * 4, size = stride * h;
    const int fd = memfd_create("end4pC-corner", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }  // a fresh memfd is zero-filled: transparent
    if (app.cfg.cornerHighlighted(st.corner, app.outputs.size() > 1 ? st.bar->connector : "")) {
        void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data != MAP_FAILED) {
            cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, w, h, stride);
            cairo_t* cr = mkcr(surf);
            paintHotCorner(cr, app, w, h);
            cairo_destroy(cr);
            cairo_surface_destroy(surf);
            munmap(data, size);
        }
    }
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &kCornerBufferListener, nullptr);
    wl_surface_attach(st.surface, buffer, 0, 0);
    wl_surface_damage_buffer(st.surface, 0, 0, w, h);
    wl_surface_commit(st.surface);
}
void onCornerConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* st = static_cast<Strip*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    st->w = st->bar->app->cfg.hotCornerWidth;
    drawCorner(*st);
}
// The size or the look of the corners changed: every corner is resized and drawn again.
void applyCorners(App& app, bool force = false) {
    static std::string last;
    std::string now = std::to_string(app.cfg.hotCornerWidth) + "x" + std::to_string(app.cfg.hotCornerHeight) + ":" + std::to_string(app.cfg.hotCorners) + ":" + std::to_string(app.cfg.hotCornerVisible);
    for (int corner = 0; corner < 4; ++corner)
        now += ":" + std::string(app.cfg.cornerAction(corner));
    now += app.outputs.size() > 1 ? ":multi:" + app.cfg.hotCornerActions : ":single";
    if (now == last && !force) return;
    last = now;
    app.ov.dirty = true;
    for (auto& bar : app.bars)
        for (Strip& st : bar->corners) {
            if (!st.layer || !st.surface) continue;
            zwlr_layer_surface_v1_set_size(st.layer, app.cfg.hotCornerWidth, app.cfg.hotCornerHeight);
            if (st.w > 0) drawCorner(st);
            else wl_surface_commit(st.surface);
        }
}
void onCornerClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kCornerListener = {onCornerConfigure, onCornerClosed};

void onStripClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kStripListener = {onStripConfigure, onStripClosed};

void destroyStrip(Strip& st) {
    if (st.layer) zwlr_layer_surface_v1_destroy(st.layer);
    hidpi::detach(st.scale);
    if (st.surface) wl_surface_destroy(st.surface);
    st = Strip{};
}

// (Re)builds the frame and the bar, in that order. Hyprland stacks layer surfaces by creation order, and each
// one's exclusive zone shrinks the area the next gets; so frame strips first, then the bar inside them.
//   top (full width)  ->  bottom (full width)  ->  left, right (between them)  ->  bar (inside everything)
// The fake rounding pieces: a square with a quarter circle cut from its inner corner, in opaque black, drawn once.
void drawRound(Strip* st) {
    const int W = st->w, H = st->h;
    if (!st->surface || W <= 0 || H <= 0) return;
    hidpi::Frame frame;
    if (!hidpi::begin(frame, W, H, st->scale.factor(), "end4pC-round")) return;
    cairo_t* cr = mkcr(frame.surface);
    const double cx = (st->corner == 0 || st->corner == 2) ? W : 0, cy = (st->corner == 0 || st->corner == 1) ? H : 0;
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_arc(cr, cx, cy, W, 0, 2 * M_PI);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    const Color fc = frameColorOf(*st->bar->app);
    cairo_set_source_rgba(cr, fc.r, fc.g, fc.b, 1);
    cairo_fill(cr);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(frame, st->bar->app->shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kCornerBufferListener, nullptr);
    hidpi::present(st->surface, buffer, frame, st->scale);
}
void onRoundConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* st = static_cast<Strip*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    st->w = static_cast<int>(w);
    st->h = static_cast<int>(h);
    drawRound(st);
}
const zwlr_layer_surface_v1_listener kRoundListener = {onRoundConfigure, onCornerClosed};

void redrawFrames(App& app) {
    for (auto& b : app.bars) {
        for (auto& st : b->strips) drawStrip(st, app);
        for (auto& st : b->rounds) if (st.surface) drawRound(&st);
    }
}

int hyprOptionInt(const char* option, int fallback) {
    if (wmKind() != Wm::Hyprland) return fallback;
    const std::string cmd = std::string("hyprctl getoption ") + option + " -j 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return fallback;
    char buf[512] = {};
    const size_t n = fread(buf, 1, sizeof buf - 1, f);
    pclose(f);
    const std::string out(buf, n);
    for (const char* key : {"\"int\":", "\"css\": \""}) {
        const size_t k = out.find(key);
        if (k != std::string::npos) return std::atoi(out.c_str() + k + std::strlen(key));
    }
    return fallback;
}

bool nameInList(const std::string& list, const std::string& name) {
    size_t from = 0;
    while (from <= list.size()) {
        size_t end = list.find(',', from);
        if (end == std::string::npos) end = list.size();
        if (list.compare(from, end - from, name) == 0) return true;
        from = end + 1;
    }
    return false;
}
bool barWanted(const App& app, const Bar& bar) {
    if (app.cfg.barScreens.empty() || bar.connector.empty()) return true;
    if (nameInList(app.cfg.barScreens, bar.connector)) return true;
    for (const auto& o : app.outputs)
        if (!o->connector.empty() && nameInList(app.cfg.barScreens, o->connector)) return false;
    return true;
}
Bar* mainBar(App& app) {
    for (auto& b : app.bars) if (b->surface) return b.get();
    return app.bars.empty() ? nullptr : app.bars.front().get();
}
Bar* widgetBar(App& app) {
    if (!app.cfg.widgetScreen.empty())
        for (auto& b : app.bars) if (b->connector == app.cfg.widgetScreen) return b.get();
    return app.bars.empty() ? nullptr : app.bars.front().get();
}

void onShadowConfigure(void* userData, zwlr_layer_surface_v1* layerSurface, uint32_t serial, uint32_t width, uint32_t height) {
    auto* bar = static_cast<Bar*>(userData);
    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
    bar->shW = width;
    bar->shH = height;
    bar->shSig.clear();
    refreshBarShadow(*bar);
}
void onShadowClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kShadowListener = {onShadowConfigure, onShadowClosed};

void createBarShadow(Bar& bar, bool side, bool atBottom) {
    App& app = *bar.app;
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                       L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const int gap = app.cfg.frame + ((app.cfg.barStyle == "float" || app.cfg.barStyle == "island") ? kBarGap : 0);
    bar.shSurface = wl_compositor_create_surface(app.compositor);
    attachBarScale(app, bar.shScale, bar.shSurface);
    app.layerGen++;
    bar.shLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, bar.shSurface, bar.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-shadow-bar");
    zwlr_layer_surface_v1_set_anchor(bar.shLayer, side ? (T | B | (atBottom ? R : L)) : ((atBottom ? B : T) | L | R));
    if (side) {
        zwlr_layer_surface_v1_set_size(bar.shLayer, kSideBarW + bar.extra, 0);
        zwlr_layer_surface_v1_set_margin(bar.shLayer, gap, atBottom ? gap : 0, gap, atBottom ? 0 : gap);
    } else {
        zwlr_layer_surface_v1_set_size(bar.shLayer, 0, kBarHeight + bar.extra);
        zwlr_layer_surface_v1_set_margin(bar.shLayer, atBottom ? 0 : gap, gap, atBottom ? gap : 0, gap);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(bar.shLayer, -1);
    wl_region* none = wl_compositor_create_region(app.compositor);
    wl_surface_set_input_region(bar.shSurface, none);
    wl_region_destroy(none);
    zwlr_layer_surface_v1_add_listener(bar.shLayer, &kShadowListener, &bar);
    wl_surface_commit(bar.shSurface);
}

void applyFrame(Bar& bar) {
    App& app = *bar.app;
    const int f = app.cfg.frame;
    constexpr uint32_t T = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, B = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                       L = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, R = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    struct Edge { uint32_t anchor; bool horizontal; };
    const Edge edges[4] = {{T | L | R, true}, {B | L | R, true}, {L | T | B, false}, {R | T | B, false}};

    if (bar.layerSurface) { zwlr_layer_surface_v1_destroy(bar.layerSurface); bar.layerSurface = nullptr; }
    releaseBlurSurface(bar.blur);
    hidpi::detach(bar.scale);
    if (bar.surface) { wl_surface_destroy(bar.surface); bar.surface = nullptr; }
    if (bar.shLayer) { zwlr_layer_surface_v1_destroy(bar.shLayer); bar.shLayer = nullptr; }
    hidpi::detach(bar.shScale);
    if (bar.shSurface) { wl_surface_destroy(bar.shSurface); bar.shSurface = nullptr; }
    bar.shW = bar.shH = 0;
    bar.shSig.clear();
    bar.configured = false;
    bar.buffer = nullptr;
    bar.regionSegs.clear();
    for (auto& st : bar.strips) destroyStrip(st);
    for (auto& st : bar.rounds) destroyStrip(st);
    for (auto& st : bar.corners) destroyStrip(st);

    for (int i = 0; i < 4 && f > 0; i++) {
        Strip& st = bar.strips[i];
        st.bar = &bar;
        st.surface = wl_compositor_create_surface(app.compositor);
        app.layerGen++;
        st.layer = zwlr_layer_shell_v1_get_layer_surface(
            app.layerShell, st.surface, bar.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-frame");
        zwlr_layer_surface_v1_add_listener(st.layer, &kStripListener, &st);
        zwlr_layer_surface_v1_set_anchor(st.layer, edges[i].anchor);
        zwlr_layer_surface_v1_set_size(st.layer, edges[i].horizontal ? 0 : f, edges[i].horizontal ? f : 0);
        zwlr_layer_surface_v1_set_exclusive_zone(st.layer, f);
        wl_surface_commit(st.surface);
    }

    bar.screensApplied = app.cfg.barScreens;
    if (barWanted(app, bar)) {
    const bool side = sideBar(app), atBottom = barAtEnd(app);
    if (app.cfg.barStyle == "hug" || app.cfg.barStyle == "split") {
        app.hugRadius = hyprOptionInt("decoration:rounding", 22);
        app.hugGap = hyprOptionInt("general:gaps_out", 5);
        bar.extra = app.hugRadius + app.hugGap;
    } else {
        bar.extra = 0;
    }
    if (app.cfg.barShadow && bar.extra < kBarShadowRoom) bar.extra = kBarShadowRoom;
    bar.contentY = atBottom ? static_cast<int>(bar.extra) : 0;
    if (app.cfg.barShadow && app.cfg.barBackground) createBarShadow(bar, side, atBottom);
    bar.surface = wl_compositor_create_surface(app.compositor);
    attachBarScale(app, bar.scale, bar.surface);
    app.layerGen++;
    bar.layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        app.layerShell, bar.surface, bar.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-bar");
    zwlr_layer_surface_v1_set_anchor(bar.layerSurface, side ? (T | B | (atBottom ? R : L)) : ((atBottom ? B : T) | L | R));
    if (side) zwlr_layer_surface_v1_set_size(bar.layerSurface, kSideBarW + bar.extra, 0);
    else zwlr_layer_surface_v1_set_size(bar.layerSurface, 0, kBarHeight + bar.extra);
    const std::string& style = app.cfg.barStyle;
    if (style == "float" || style == "island") {
        if (side) zwlr_layer_surface_v1_set_margin(bar.layerSurface, kBarGap, atBottom ? kBarGap : 0, kBarGap, atBottom ? 0 : kBarGap);
        else zwlr_layer_surface_v1_set_margin(bar.layerSurface, atBottom ? 0 : kBarGap, kBarGap, atBottom ? kBarGap : 0, kBarGap);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(bar.layerSurface, barThickness(app));
    if (bar.extra > 0) {
        wl_region* region = wl_compositor_create_region(app.compositor);
        if (side) wl_region_add(region, bar.contentY, 0, kSideBarW, 100000);
        else wl_region_add(region, 0, bar.contentY, 100000, kBarHeight);
        wl_surface_set_input_region(bar.surface, region);
        wl_region_destroy(region);
    }
    zwlr_layer_surface_v1_add_listener(bar.layerSurface, &kLayerSurfaceListener, &bar);
    wl_surface_commit(bar.surface);
    } else {
        bar.extra = 0;
        bar.contentY = 0;
    }

    bar.roundApplied = std::to_string(app.cfg.fakeRounding) + ":" + std::to_string(app.cfg.fakeRoundingSize) + ":" + std::to_string(f);
    if (app.cfg.fakeRounding > 0) {
        const int radius = app.cfg.fakeRoundingSize > 0 ? app.cfg.fakeRoundingSize : hyprOptionInt("decoration:rounding", 22);
        const uint32_t cornerAnchors[4] = {T | L, T | R, B | L, B | R};
        for (int i = 0; i < 4 && radius > 0; i++) {
            Strip& st = bar.rounds[i];
            st.bar = &bar;
            st.corner = i;
            st.surface = wl_compositor_create_surface(app.compositor);
            attachBarScale(app, st.scale, st.surface);
            st.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, st.surface, bar.output,
                app.cfg.fakeRounding == 1 ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-rounding");
            zwlr_layer_surface_v1_add_listener(st.layer, &kRoundListener, &st);
            zwlr_layer_surface_v1_set_anchor(st.layer, cornerAnchors[i]);
            zwlr_layer_surface_v1_set_size(st.layer, radius, radius);
            zwlr_layer_surface_v1_set_exclusive_zone(st.layer, -1);
            zwlr_layer_surface_v1_set_margin(st.layer, (i < 2) ? f : 0, (i % 2 == 1) ? f : 0, (i >= 2) ? f : 0, (i % 2 == 0) ? f : 0);
            wl_region* none = wl_compositor_create_region(app.compositor);
            wl_surface_set_input_region(st.surface, none);
            wl_region_destroy(none);
            wl_surface_commit(st.surface);
        }
    }

    const uint32_t cornerAnchor[4] = {T | L, T | R, B | L, B | R};
    for (int i = 0; i < 4; i++) {
        Strip& st = bar.corners[i];
        st.bar = &bar;
        st.corner = i;
        st.surface = wl_compositor_create_surface(app.compositor);
        st.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, st.surface, bar.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-corner");
        zwlr_layer_surface_v1_add_listener(st.layer, &kCornerListener, &st);
        zwlr_layer_surface_v1_set_anchor(st.layer, cornerAnchor[i]);
        zwlr_layer_surface_v1_set_size(st.layer, app.cfg.hotCornerWidth, app.cfg.hotCornerHeight);
        zwlr_layer_surface_v1_set_exclusive_zone(st.layer, -1);
        wl_surface_commit(st.surface);
    }
}

double cwpEase(double t) {  // easing curve cubic-bezier(0.38, 1.21, 0.22, 1.0): overshoots slightly before settling
    const double x1 = 0.38, y1 = 1.21, x2 = 0.22, y2 = 1.0;
    double lo = 0, hi = 1, s = t;
    for (int i = 0; i < 24; i++) {
        s = (lo + hi) / 2;
        const double x = 3 * (1 - s) * (1 - s) * s * x1 + 3 * (1 - s) * s * s * x2 + s * s * s;
        (x < t ? lo : hi) = s;
    }
    return 3 * (1 - s) * (1 - s) * s * y1 + 3 * (1 - s) * s * s * y2 + s * s * s;
}
void cwpFree(App& app) {
    for (auto& b : app.bars) if (b->cwpFull) { cairo_surface_destroy(b->cwpFull); b->cwpFull = nullptr; }
}
void cwpStart(App& app, bool entering) {
    const Color bg = app.color("surface");
    cwpFree(app);
    for (auto& b : app.bars)
        if (b->wpSurface && b->wpW > 0 && b->wpH > 0) b->cwpFull = renderWallpaperSurface(app.wallpaper, b->wpW, b->wpH, b->wpScale.factor(), bg.r, bg.g, bg.b, CwpParams{}, CollageSpec{});
    app.cwpA.active = true;
    app.cwpA.from = app.cwpA.p = entering ? 1 : 0;
    app.cwpA.to = entering ? 0 : 1;
    app.cwpA.ms = entering ? 650 : 800;
    app.cwpA.start = std::chrono::steady_clock::now();
}
bool cwpStep(App& app, std::chrono::steady_clock::time_point now) {
    if (!app.cwpA.active) return false;
    const double t = std::clamp(std::chrono::duration<double, std::milli>(now - app.cwpA.start).count() / app.cwpA.ms, 0.0, 1.0);
    app.cwpA.p = app.cwpA.from + (app.cwpA.to - app.cwpA.from) * cwpEase(t);
    if (t >= 1.0) {
        app.cwpA.active = false;
        cwpFree(app);
    }
    redrawWallpapers(app);
    return app.cwpA.active;
}

void placeNotifSurface(App& app);
void applyConfig(App& app) {
    videoPauseSync(app, true);  // (the pause settings may have changed)
    applyCorners(app);
    app.dtp.dirty = true;
    {
        static std::string lastFont = "\x01";
        const std::string font = app.cfg.fontFamily + "|" + std::to_string(app.cfg.fontWeightText) + "|" + std::to_string(app.cfg.fontWeightTitle);
        applyShellFont(app.cfg.fontFamily, app.cfg.fontWeightText, app.cfg.fontWeightTitle);
        if (font != lastFont) {  // the bar clock keeps its width until its text changes: measure it again with the new font
            const bool first = lastFont == "\x01";
            lastFont = font;
            if (!first) { app.clockTime.clear(); updateClock(app); }
        }
    }
    measureMusic(app);
    {
        static int lastTemplates = -1;
        const int now = app.cfg.templateKitty | (app.cfg.templateGtk << 1) | (app.cfg.templateKde << 2);
        if (lastTemplates != now) {
            if (lastTemplates >= 0 && (now & ~lastTemplates)) {
                const std::string script = assetsDir() + "/../scripts/templates.py";
                runDetached({"python3", "-I", script.c_str()});
            }
            lastTemplates = now;
        }
    }
    refreshWsApps(app);
    collageSync(app);
    applyUiBackground(app);
    {
        static std::string lastUiBackground;
        if (app.cfg.uiBackground != lastUiBackground) {
            const bool first = lastUiBackground.empty();
            lastUiBackground = app.cfg.uiBackground;
            if (!first) deskRedrawAll(app);
        }
    }
    {
        static int lastWeatherTheme = -1;
        if (app.cfg.weatherTheme != lastWeatherTheme) {
            const bool first = lastWeatherTheme < 0;
            lastWeatherTheme = app.cfg.weatherTheme;
            if (!first) deskRedrawAll(app);
        }
    }
    {
        static std::string lastIcons = "?";
        const std::string now = std::to_string(app.cfg.materialIcons) + app.cfg.iconStyle;
        if (now != lastIcons) {
            const bool first = lastIcons == "?";
            lastIcons = now;
            if (!first) deskRedrawAll(app);
        }
    }
    dockApply(app);
    weatherSvcConfigure(app);
    placeNotifSurface(app);
    static std::string lastQuick = "?";
    if (app.cfg.quickActions != lastQuick) {
        lastQuick = app.cfg.quickActions;
        if (quickIsShown(app, kQaMic)) app.micMuted = readMicMuted();
        if (quickIsShown(app, kQaPerf)) app.perfProfile = readPerfProfile();
    }
    {
        static std::string lastCwp;
        if (!app.cwpA.active && app.cwpDrawn >= 0 && app.cwpDrawn != app.cfg.centeredWallpaper) cwpStart(app, app.cfg.centeredWallpaper != 0);
        const std::string now = std::to_string(app.cfg.centeredWallpaper) + app.cfg.centeredWallpaperShape + std::to_string(app.cfg.centeredWallpaperSize) + app.cfg.centeredWallpaperColor;
        if (now != lastCwp) {
            lastCwp = now;
            redrawWallpapers(app);
        }
    }
    {
        static std::string lastFrameColor;
        if (app.cfg.frameColor != lastFrameColor) { lastFrameColor = app.cfg.frameColor; redrawFrames(app); }
        static std::string lastLang = "\x01";
        if (app.cfg.language != lastLang) {
            const bool first = lastLang == "\x01";
            lastLang = app.cfg.language;
            i18n::load(app.cfg.language, assetsDir(), configDir() + "/i18n");
            if (!first) { app.clockTime.clear(); updateClock(app); drawAll(app); }
        }
        static std::string lastBarClock;
        if (widgetStyle(app, "clock") != lastBarClock) { lastBarClock = widgetStyle(app, "clock"); app.clockTime.clear(); updateClock(app); }
    }
    static std::string lastAvatar;
    if (app.cfg.avatarPicture != lastAvatar) {
        lastAvatar = app.cfg.avatarPicture;
        if (app.avatar) { cairo_surface_destroy(app.avatar); app.avatar = nullptr; }
        app.avatarTried = false;
    }
    if (app.cfg.preferredPlayer != app.watchedPlayer) {
        if (app.musicPid > 0) kill(app.musicPid, SIGTERM);
        if (app.musicFd >= 0) close(app.musicFd);
        app.musicFd = startMusicWatcher(app.cfg.preferredPlayer, &app.musicPid);
        app.watchedPlayer = app.cfg.preferredPlayer;
        parseMusicLine(app, "");
    }
    {
        static std::string lastOsdPos;
        if (app.cfg.osdPosition != lastOsdPos) { lastOsdPos = app.cfg.osdPosition; closeOsd(app); }
    }
    barModulesSync(app);
    app.sel.modeTarget = app.cfg.colorMode == "dark" ? 1 : 0;
    drawSelector(app);
    for (auto& bar : app.bars) {
        const std::string roundKey = std::to_string(app.cfg.fakeRounding) + ":" + std::to_string(app.cfg.fakeRoundingSize) + ":" + std::to_string(app.cfg.frame);
        if (bar->roundApplied != roundKey || bar->frameApplied != app.cfg.frame || bar->positionApplied != app.cfg.barPosition || bar->styleApplied != app.cfg.barStyle + (app.cfg.barShadow && app.cfg.barBackground ? "+shadow" : "") || bar->screensApplied != app.cfg.barScreens) {
            applyFrame(*bar);
            if (app.cfg.collage) drawWallpaper(*bar, app);
            bar->frameApplied = app.cfg.frame;
            bar->positionApplied = app.cfg.barPosition;
            bar->styleApplied = app.cfg.barStyle + (app.cfg.barShadow && app.cfg.barBackground ? "+shadow" : "");
        }
    }
    drawAll(app);
}

void createBar(App& app, wl_output* output, uint32_t name) {
    auto bar = std::make_unique<Bar>();
    bar->app = &app;
    bar->output = output;
    bar->outputName = name;
    for (const auto& o : app.outputs) if (o->name == name) bar->connector = o->connector;
    bar->wpSurface = wl_compositor_create_surface(app.compositor);
    hidpi::attach(bar->wpScale, bar->wpSurface, app.viewporter, app.fscaleMgr, [&app] { app.wallpaperScaleChanged = true; });
    bar->wpLayer = zwlr_layer_shell_v1_get_layer_surface(
        app.layerShell, bar->wpSurface, output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "end4pC-wallpaper");
    zwlr_layer_surface_v1_set_anchor(bar->wpLayer,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(bar->wpLayer, -1);
    zwlr_layer_surface_v1_add_listener(bar->wpLayer, &kWpListener, bar.get());
    wl_surface_commit(bar->wpSurface);
    if (wmKind() == Wm::Niri) {
        bar->bdSurface = wl_compositor_create_surface(app.compositor);
        bar->bdLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, bar->bdSurface, output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "end4pC-wallpaper-backdrop");
        zwlr_layer_surface_v1_set_anchor(bar->bdLayer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(bar->bdLayer, -1);
        zwlr_layer_surface_v1_add_listener(bar->bdLayer, &kBackdropListener, bar.get());
        wl_surface_commit(bar->bdSurface);
    }
    app.bars.push_back(std::move(bar));
    applyFrame(*app.bars.back());
    app.bars.back()->frameApplied = app.cfg.frame;
    app.bars.back()->positionApplied = app.cfg.barPosition;
    app.bars.back()->styleApplied = app.cfg.barStyle + (app.cfg.barShadow && app.cfg.barBackground ? "+shadow" : "");
    if (app.ready) applyCorners(app);
    deskVizReopen(app);
}
