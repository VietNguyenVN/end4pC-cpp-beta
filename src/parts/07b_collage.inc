
using CollageImage = std::shared_ptr<cairo_surface_t>;

struct CollageCacheEntry {
    std::string path;
    CollageImage image;
};
std::mutex g_collageMutex;
std::map<std::string, CollageCacheEntry> g_collageCache;
std::set<std::string> g_collageQueued;

constexpr int kCollageBackdropWidth = 192;
constexpr double kCollageBlurPerPercent = 0.18;
constexpr double kCollageDarken = 0.22;

cairo_surface_t* decodeCollageImage(const std::string& path, int w, int h) {
    int iw = 0, ih = 0;
    if (path.empty() || !gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(w) / iw, double(h) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(w, int(iw * scale + 0.5)), std::max(h, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* img = surfaceFromPixbuf(pb);
    g_object_unref(pb);
    return img;
}

CollageImage ownCollageImage(cairo_surface_t* surf) {
    return surf ? CollageImage(surf, cairo_surface_destroy) : CollageImage();
}

std::string collageKey(const std::string& path, int w, int h) {
    return path + '\n' + std::to_string(w) + 'x' + std::to_string(h);
}

void queueCollageDecode(const std::string& path, int w, int h, const std::string& key) {
    if (!g_collageQueued.insert(key).second) return;
    std::thread([path, w, h, key] {
        CollageImage image = ownCollageImage(decodeCollageImage(path, w, h));
        {
            std::lock_guard<std::mutex> lock(g_collageMutex);
            g_collageCache[key] = {path, image};
            g_collageQueued.erase(key);
        }
        g_collageDecoded = true;
        if (g_wpWakeWrite >= 0) (void)!write(g_wpWakeWrite, "x", 1);
    }).detach();
}

struct CollageLookup {
    bool pending = false;
    CollageImage image;
};

CollageLookup lookupCollageImage(const std::string& path, int w, int h, bool blocking) {
    if (path.empty()) return {};
    const std::string key = collageKey(path, w, h);
    {
        std::lock_guard<std::mutex> lock(g_collageMutex);
        const auto hit = g_collageCache.find(key);
        if (hit != g_collageCache.end()) return {false, hit->second.image};
        if (!blocking) {
            queueCollageDecode(path, w, h, key);
            return {true, nullptr};
        }
    }
    CollageImage image = ownCollageImage(decodeCollageImage(path, w, h));
    std::lock_guard<std::mutex> lock(g_collageMutex);
    g_collageCache[key] = {path, image};
    return {false, image};
}

void pruneCollageCache(const App& app, const std::string& oldPath, const CollageSpec& oldCollage) {
    std::set<std::string> keep{app.wallpaper, oldPath};
    for (const std::string& p : collageImagePaths(oldCollage)) keep.insert(p);
    for (const std::string& p : collageImagePaths(collageNow(app))) keep.insert(p);
    std::lock_guard<std::mutex> lock(g_collageMutex);
    std::erase_if(g_collageCache, [&](const auto& entry) { return !keep.count(entry.second.path); });
}

void paintCollageCover(cairo_t* cr, cairo_surface_t* img, double x, double y, double w, double h) {
    const double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
    const double k = std::max(w / iw, h / ih);
    cairo_save(cr);
    cairo_translate(cr, x + (w - iw * k) / 2, y + (h - ih * k) / 2);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

void collageBoxPass(unsigned char* src, unsigned char* dst, int len, int lines, int lineStride, int pixStride, int radius) {
    const int window = 2 * radius + 1;
    for (int line = 0; line < lines; line++) {
        const unsigned char* in = src + static_cast<size_t>(line) * lineStride;
        unsigned char* out = dst + static_cast<size_t>(line) * lineStride;
        for (int ch = 0; ch < 4; ch++) {
            int sum = 0;
            for (int i = -radius; i <= radius; i++) sum += in[std::clamp(i, 0, len - 1) * pixStride + ch];
            for (int i = 0; i < len; i++) {
                out[i * pixStride + ch] = static_cast<unsigned char>(sum / window);
                sum += in[std::min(len - 1, i + radius + 1) * pixStride + ch] - in[std::max(0, i - radius) * pixStride + ch];
            }
        }
    }
}

void collageBlurSurface(cairo_surface_t* surf, int radius) {
    cairo_surface_flush(surf);
    const int w = cairo_image_surface_get_width(surf), h = cairo_image_surface_get_height(surf), stride = cairo_image_surface_get_stride(surf);
    unsigned char* px = cairo_image_surface_get_data(surf);
    std::vector<unsigned char> tmp(static_cast<size_t>(stride) * h);
    for (int pass = 0; pass < 3; pass++) {
        collageBoxPass(px, tmp.data(), w, h, stride, 4, radius);
        collageBoxPass(tmp.data(), px, h, w, 4, stride, radius);
    }
    cairo_surface_mark_dirty(surf);
}

void paintCollageBackdrop(cairo_t* cr, int w, int h, cairo_surface_t* img, int blur) {
    const int radius = static_cast<int>(std::lround(blur * kCollageBlurPerPercent));
    const int bw = radius > 0 ? kCollageBackdropWidth : std::min(w, 1280);
    const int bh = std::max(1, static_cast<int>(std::lround(double(bw) * h / std::max(1, w))));
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bw, bh);
    cairo_t* sc = mkcr(small);
    paintCollageCover(sc, img, 0, 0, bw, bh);
    cairo_destroy(sc);
    if (radius > 0) collageBlurSurface(small, radius);
    cairo_save(cr);
    cairo_scale(cr, double(w) / bw, double(h) / bh);
    cairo_set_source_surface(cr, small, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(small);
    cairo_set_source_rgba(cr, 0, 0, 0, kCollageDarken);
    cairo_paint(cr);
}

void collageRoundRectPath(cairo_t* cr, const CollageRect& r, double rad) {
    if (rad <= 0) {
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        return;
    }
    const double pi = M_PI;
    cairo_new_sub_path(cr);
    cairo_arc(cr, r.x + r.w - rad, r.y + rad, rad, -pi / 2, 0);
    cairo_arc(cr, r.x + r.w - rad, r.y + r.h - rad, rad, 0, pi / 2);
    cairo_arc(cr, r.x + rad, r.y + r.h - rad, rad, pi / 2, pi);
    cairo_arc(cr, r.x + rad, r.y + rad, rad, pi, 3 * pi / 2);
    cairo_close_path(cr);
}

void paintCollageTile(cairo_t* cr, const CollageRect& r, double radius, cairo_surface_t* img) {
    cairo_save(cr);
    collageRoundRectPath(cr, r, std::min({radius, r.w / 2, r.h / 2}));
    cairo_clip(cr);
    paintCollageCover(cr, img, r.x, r.y, r.w, r.h);
    cairo_restore(cr);
}

bool paintCollage(cairo_t* cr, int w, int h, const std::string& wallpaper, const CollageSpec& spec, double r, double g, double b, bool blocking) {
    const std::vector<CollageTile> tiles = collageTiles(spec, w, h);
    bool complete = true;
    const double ds = hidpi::scaleOf(cr);  // (the pictures are decoded at the canvas's real pixels; collageReady asks for the same size)
    const int pw = hidpi::bufferExtent(w, ds), ph = hidpi::bufferExtent(h, ds);
    auto fetch = [&](const std::string& path) {
        const CollageLookup got = lookupCollageImage(path, pw, ph, blocking);
        complete = complete && !got.pending;
        return got.image;
    };
    const CollageImage wall = fetch(wallpaper);
    auto pictureOf = [&](const std::string& path) {
        const CollageImage img = fetch(path);
        return img ? img : wall;
    };
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    const CollageImage primary = pictureOf(collagePrimaryImage(spec));
    if (primary) paintCollageBackdrop(cr, w, h, primary.get(), spec.blur);
    for (const CollageTile& tile : tiles) {
        const CollageImage img = pictureOf(tile.img);
        if (img) paintCollageTile(cr, tile.rect, spec.radius, img.get());
    }
    return complete;
}

cairo_surface_t* collageRenderSurface(const std::string& wallpaper, int w, int h, double scale, double r, double g, double b, const CollageSpec& spec) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hidpi::bufferExtent(w, scale), hidpi::bufferExtent(h, scale));
    cairo_surface_set_device_scale(surf, scale, scale);
    cairo_t* cr = mkcr(surf);
    paintCollage(cr, w, h, wallpaper, spec, r, g, b, true);
    cairo_destroy(cr);
    return surf;
}

bool collageReady(const Bar& bar, const App& app) {
    const CollageSpec spec = collageNow(app);
    std::vector<std::string> paths = collageImagePaths(spec);
    paths.push_back(app.wallpaper);
    paths.push_back(collagePrimaryImage(spec));
    bool ready = true;
    const double ds = std::max(1.0, bar.wpScale.factor());  // (the size paintCollage asks for)
    const int pw = hidpi::bufferExtent(static_cast<int>(bar.wpW), ds), ph = hidpi::bufferExtent(static_cast<int>(bar.wpH), ds);
    for (const std::string& path : paths)
        if (lookupCollageImage(path, pw, ph, false).pending) ready = false;
    return ready;
}

void collagePaintNow(cairo_t* cr, Bar& bar, App& app) {
    const Color bg = app.color("surface");
    paintCollage(cr, bar.wpW, bar.wpH, app.wallpaper, withBarInsets(collageNow(app), bar, app), bg.r, bg.g, bg.b, false);
}

void collageSync(App& app) {
    const CollageSpec now = collageNow(app);
    if (now == app.collageShown) return;
    transitionWallpaper(app, app.wallpaper, app.collageShown, wallpaperTransitionKind(app));
}

std::string collageReport(const App& app) {
    return "collage=" + std::to_string(app.cfg.collage) + " collageGap=" + std::to_string(app.cfg.collageGap) +
           " collageMargin=" + std::to_string(app.cfg.collageMargin) + " collageRadius=" + std::to_string(app.cfg.collageRadius) + " collageBlur=" + std::to_string(app.cfg.collageBlur) +
           " collagePrimary=" + std::to_string(app.cfg.collagePrimary) + "\ncollageTree=" + app.cfg.collageTree;
}

std::string collageCommand(App& app, const std::string& arg) {
    if (arg.empty()) return collageReport(app);
    if (arg != "on" && arg != "off" && arg != "toggle") return "usage: collage [on|off|toggle]";
    const int want = arg == "on" ? 1 : arg == "off" ? 0 : (app.cfg.collage ? 0 : 1);
    const std::string err = setConfig(app, "collage", std::to_string(want));
    if (!err.empty()) return err;
    applyConfig(app);
    return app.cfg.collage ? "on" : "off";
}
