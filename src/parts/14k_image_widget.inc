
void imageWLoad(ImageW& X, const std::string& path) {
    if (X.img) { cairo_surface_destroy(X.img); X.img = nullptr; }
    X.imgPath = path;
    if (path.empty()) return;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(expandHome(path).c_str(), 1400, 1400, TRUE, nullptr);
    if (!pb) return;
    X.img = surfaceFromPixbuf(pb);
    g_object_unref(pb);
}

void imageWEditButton(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    if (!d.editing) return;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, i18n::tr("Change image"), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    constexpr double kIcon = 18, kGap = 8, kPad = 16, kH = 36;
    const double bw = kPad + kIcon + kGap + tw + kPad, bx = (W - bw) / 2.0, by = H - kH - 18;
    const bool hot = d.hover == kDeskTap;
    roundedRect(cr, bx, by, bw, kH, kH / 2);
    setSource(cr, app.color(hot ? "secondary_container" : "surface_container_highest"), hot ? 1.0 : 0.92);
    cairo_fill(cr);
    const Color ink = app.color(hot ? "on_secondary_container" : "on_surface");
    drawIcon(cr, "gallery", bx + kPad, by + (kH - kIcon) / 2.0, kIcon, ink, true);
    setSource(cr, ink);
    cairo_move_to(cr, bx + kPad + kIcon + kGap, by + (kH - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    d.hits.push_back({kDeskTap, Rect{bx, by, bw, kH}});
}

void imageWPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    ImageW& X = static_cast<ImageW&>(d);
    if (X.imgPath != app.cfg.customImagePath) imageWLoad(X, app.cfg.customImagePath);
    cairo_save(cr);
    roundedRect(cr, 0, 0, W, H, d.radius);
    ClipGroup cut;
    beginClipGroup(cr, cut);
    setSource(cr, app.color("primary_container"));
    cairo_paint(cr);
    if (X.img) {
        cairo_save(cr);
        const double iw = cairo_image_surface_get_width(X.img), ih = cairo_image_surface_get_height(X.img), sc = std::max(W / iw, H / ih);
        cairo_translate(cr, (W - iw * sc) / 2, (H - ih * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.img, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), d.drag ? CAIRO_FILTER_FAST : CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        endClipGroup(cr, cut);
        cairo_restore(cr);
        imageWEditButton(app, d, cr, W, H);
        return;
    }
    const int isz = static_cast<int>(std::clamp(std::min(W, H) / 3.0, 28.0, 90.0));
    drawIcon(cr, "gallery", (W - isz) / 2.0, H / 2.0 - isz / 2.0 - 8, isz, app.color("on_primary_container"), true);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, 14 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, "Click to choose", -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_primary_container"), 0.8);
    cairo_move_to(cr, (W - tw) / 2.0, H / 2.0 + isz / 2.0 - 2);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    endClipGroup(cr, cut);
    cairo_restore(cr);
    imageWEditButton(app, d, cr, W, H);
}

void imageWPick() {
    char exe[512] = {};
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    const std::string cmd = "p=$(zenity --file-selection --title='Choose an image' --file-filter='Images | *.png *.jpg *.jpeg *.webp *.avif *.bmp *.gif *.tiff *.PNG *.JPG *.JPEG *.WEBP') && [ -n \"$p\" ] && '" + std::string(exe, n) + "' \"set customImagePath $p\"";
    runDetached({"bash", "-c", cmd.c_str()});
}

void imageWTap(App& app, DeskWidget& d) {
    ImageW& X = static_cast<ImageW&>(d);
    const auto now = std::chrono::steady_clock::now();
    const bool dbl = now - X.lastClick < std::chrono::milliseconds(400);
    X.lastClick = now;
    if (d.editing || app.cfg.customImagePath.empty() || dbl) imageWPick();
}

void imageWInit(ImageW& X) {
    X.name = "image";
    X.minW = 80; X.minH = 80;
    X.maxW = 1200; X.maxH = 900;
    X.w = 300; X.h = 220;
    X.radius = 24;
    X.paint = imageWPaint;
    X.tap = imageWTap;
}
