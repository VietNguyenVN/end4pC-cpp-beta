// Compositor blur behind the shell's translucent surfaces (ext-background-effect-v1). The blur covers what a surface paints as its
// background and nothing else: the surface paints that shape into a coverage mask, with the same code that paints it on screen, and
// the opaque runs of the mask become the rectangles of the wl_region. So the rounded corners, the island pills, the hug curves and the
// notch flares are blurred exactly, while the transparent margins, the shadows and the surface's empty room are not (niri's layer rule
// blurred the whole surface rectangle).
// Included by main.cpp after 02_layout_data.inc (wmKind) and before 04_theme_config.inc (setSource honours the shape-only flag below).

inline bool gShapeOnlyPaint = false;

struct ShapeOnlyScope {
    ShapeOnlyScope() { gShapeOnlyPaint = true; }
    ~ShapeOnlyScope() { gShapeOnlyPaint = false; }
    ShapeOnlyScope(const ShapeOnlyScope&) = delete;
    ShapeOnlyScope& operator=(const ShapeOnlyScope&) = delete;
};

constexpr unsigned char kBlurCoverage = 128;
constexpr size_t kBlurMaxRects = 256;

bool blurAvailable(const App& app) {
    return app.bgEffectManager != nullptr && app.blurCapable && wmKind() != Wm::Hyprland;
}

std::vector<std::pair<int, int>> blurRowRuns(const unsigned char* row, int width) {
    std::vector<std::pair<int, int>> runs;
    int x = 0;
    while (x < width) {
        while (x < width && row[x] < kBlurCoverage) x++;
        const int start = x;
        while (x < width && row[x] >= kBlurCoverage) x++;
        if (x > start) runs.emplace_back(start, x);
    }
    return runs;
}

std::vector<BlurRect> blurRectsOf(cairo_surface_t* mask, int width, int height) {
    cairo_surface_flush(mask);
    const unsigned char* px = cairo_image_surface_get_data(mask);
    const int stride = cairo_image_surface_get_stride(mask);
    struct OpenRun { int x0, x1, y0; };
    std::vector<OpenRun> open;
    std::vector<BlurRect> done;
    for (int y = 0; y <= height; y++) {
        const auto runs = y < height ? blurRowRuns(px + y * stride, width) : std::vector<std::pair<int, int>>{};
        std::vector<OpenRun> next;
        for (const auto& [x0, x1] : runs) {
            const auto same = std::find_if(open.begin(), open.end(), [&](const OpenRun& o) { return o.x0 == x0 && o.x1 == x1; });
            if (same != open.end()) {
                next.push_back(*same);
                open.erase(same);
            } else {
                next.push_back({x0, x1, y});
            }
        }
        for (const OpenRun& o : open) done.push_back({o.x0, o.y0, o.x1 - o.x0, y - o.y0});
        open = std::move(next);
    }
    if (done.size() <= kBlurMaxRects) return done;
    int left = width, top = height, right = 0, bottom = 0;
    for (const BlurRect& r : done) {
        left = std::min(left, r.x);
        top = std::min(top, r.y);
        right = std::max(right, r.x + r.w);
        bottom = std::max(bottom, r.y + r.h);
    }
    return {BlurRect{left, top, right - left, bottom - top}};
}

void sendBlurRegion(App& app, wl_surface* surface, BlurSurface& blur, const std::vector<BlurRect>& rects) {
    if (rects.empty()) {
        if (blur.object) ext_background_effect_surface_v1_set_blur_region(blur.object, nullptr);
    } else {
        if (!blur.object) blur.object = ext_background_effect_manager_v1_get_background_effect(app.bgEffectManager, surface);
        if (!blur.object) return;
        wl_region* region = wl_compositor_create_region(app.compositor);
        for (const BlurRect& r : rects) wl_region_add(region, r.x, r.y, r.w, r.h);
        ext_background_effect_surface_v1_set_blur_region(blur.object, region);
        wl_region_destroy(region);
    }
    blur.sent = rects;
}

void updateBlurRegion(App& app, wl_surface* surface, BlurSurface& blur, int w, int h, const std::function<void(cairo_t*)>& paintShape) {
    if (!blurAvailable(app) || surface == nullptr || w <= 0 || h <= 0) return;
    cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    cairo_t* cr = mkcr(mask);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    {
        ShapeOnlyScope shapeOnly;
        paintShape(cr);
    }
    cairo_destroy(cr);
    const std::vector<BlurRect> rects = blurRectsOf(mask, w, h);
    cairo_surface_destroy(mask);
    if (rects == blur.sent) return;
    sendBlurRegion(app, surface, blur, rects);
}

// Destroys the blur object of a surface. Call it before the surface's wl_surface is destroyed (the protocol forbids the other order).
void releaseBlurSurface(BlurSurface& blur) {
    if (blur.object) ext_background_effect_surface_v1_destroy(blur.object);
    blur.object = nullptr;
    blur.sent.clear();
}
