
constexpr int kUcLock = 1, kUcSettings = 2, kUcPower = 3, kUcPick = 4, kUcReset = 5;

cairo_surface_t* userLoadPicture(const std::string& path, int size, bool square) {
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), size, size, !square, nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* img = surfaceFromPixbuf(pb);
    g_object_unref(pb);
    return img;
}

std::string userHeroSource(const App& app, const UserW& X) {
    if (!X.customImage.empty() && access(expandHome(X.customImage).c_str(), R_OK) == 0) return expandHome(X.customImage);
    return app.wallpaper;
}

void userRefresh(App& app, UserW& X) {
    X.name = shownName(app);  // (the display name of Settings > Profile, or the user name)
    {
        char h[128] = {};
        gethostname(h, sizeof h - 1);
        X.host = h;
    }
    if (const std::string src = userHeroSource(app, X); X.heroPath != src) {
        X.heroPath = src;
        if (X.hero) { cairo_surface_destroy(X.hero); X.hero = nullptr; }
        if (!X.heroPath.empty()) X.hero = userLoadPicture(X.heroPath, 900, false);
    }
    std::string pic;
    {
        std::ifstream f(homeDir() + "/.config/illogical-impulse/config.json");
        std::stringstream ss;
        ss << f.rdbuf();
        readStringKey(ss.str(), "avatarPicture", pic);
    }
    std::string chosen;
    for (const std::string& c : {app.cfg.avatarPicture, pic, homeDir() + "/.face", homeDir() + "/.face.icon"})
        if (!c.empty() && access(c.c_str(), R_OK) == 0) { chosen = c; break; }
    if (chosen != X.facePath) {
        X.facePath = chosen;
        if (X.face) { cairo_surface_destroy(X.face); X.face = nullptr; }
        if (!chosen.empty()) X.face = userLoadPicture(chosen, 256, false);
    }
}

void userEditButtons(App& app, UserW& X, cairo_t* cr, int W) {
    if (!X.editing) return;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    constexpr double kIcon = 18, kGap = 8, kPadX = 14, kH = 34, kBetween = 8;
    const double pad = std::clamp(W * 0.045, 14.0, 24.0), y = pad - 4;
    double x = pad;
    auto pill = [&](int id, const char* label, const char* icon) {
        pango_layout_set_text(layout, i18n::tr(label), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        const double bw = kPadX + kIcon + kGap + tw + kPadX;
        const bool hot = X.hover == id;
        roundedRect(cr, x, y, bw, kH, kH / 2);
        setSource(cr, app.color(hot ? "secondary_container" : "surface_container_highest"), hot ? 1.0 : 0.92);
        cairo_fill(cr);
        const Color ink = app.color(hot ? "on_secondary_container" : "on_surface");
        drawIcon(cr, icon, x + kPadX, y + (kH - kIcon) / 2.0, kIcon, ink, true);
        setSource(cr, ink);
        cairo_move_to(cr, x + kPadX + kIcon + kGap, y + (kH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        X.hits.push_back({id, Rect{x, y, bw, kH}});
        x += bw + kBetween;
    };
    pill(kUcPick, "Change image", "gallery");
    if (!X.customImage.empty()) pill(kUcReset, "Reset", "restart");
    g_object_unref(layout);
}

void userPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    UserW& X = static_cast<UserW&>(d);
    userRefresh(app, X);
    const double R = 30, heroH = H * 0.58;
    cairo_save(cr);
    roundedRect(cr, 0, 0, W, H, R);
    ClipGroup cut;  // The card is drawn into a group and the curve cuts it once, so no fringe of the layers underneath shows.
    beginClipGroup(cr, cut);
    setSource(cr, app.color("surface_container"));
    cairo_paint(cr);
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, W, heroH);
    cairo_clip(cr);
    if (X.hero) {
        const double iw = cairo_image_surface_get_width(X.hero), ih = cairo_image_surface_get_height(X.hero), sc = std::max(W / iw, heroH / ih);
        cairo_translate(cr, (W - iw * sc) / 2, (heroH - ih * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.hero, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        setSource(cr, app.color("primary_container"));
        cairo_paint(cr);
    }
    cairo_restore(cr);
    {
        const Color c = app.color("surface_container");
        cairo_pattern_t* fade = cairo_pattern_create_linear(0, heroH * 0.45, 0, heroH);
        cairo_pattern_add_color_stop_rgba(fade, 0, c.r, c.g, c.b, 0);
        cairo_pattern_add_color_stop_rgba(fade, 1, c.r, c.g, c.b, 1);
        cairo_set_source(cr, fade);
        cairo_rectangle(cr, 0, heroH * 0.45, W, heroH * 0.55 + 1);
        cairo_fill(cr);
        cairo_pattern_destroy(fade);
    }
    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const std::string& s, double px, bool bold, double x, double y, const Color& c, double alpha, double maxW) {
        PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, static_cast<int>(maxW * PANGO_SCALE));
        pango_layout_set_text(layout, s.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, c, alpha);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);
        return th;
    };
    const double pad = std::clamp(W * 0.045, 14.0, 24.0), av = std::clamp(H * 0.30, 56.0, 130.0);
    const double ay = heroH - av * 0.62;

    cairo_new_path(cr);
    cairo_arc(cr, pad + av / 2, ay + av / 2, av / 2 + 4, 0, 2 * M_PI);
    setSource(cr, app.color("surface_container"));
    cairo_fill(cr);
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, pad + av / 2, ay + av / 2, av / 2, 0, 2 * M_PI);
    cairo_clip(cr);
    if (X.face) {
        const double iw = cairo_image_surface_get_width(X.face), ih = cairo_image_surface_get_height(X.face), sc = std::max(av / iw, av / ih);
        cairo_translate(cr, pad + (av - iw * sc) / 2, ay + (av - ih * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.face, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        setSource(cr, app.color("primary_container"));
        cairo_paint(cr);
        const double glyph = av * 0.56;
        drawIcon(cr, "user-circle", pad + (av - glyph) / 2, ay + (av - glyph) / 2, glyph, app.color("on_primary_container"), true);
    }
    cairo_restore(cr);

    {
        const double bs = 36, bx = W - pad - bs, by = pad - 4;
        d.hits.push_back({kUcSettings, Rect{bx, by, bs, bs}});
        cairo_new_path(cr);
        cairo_arc(cr, bx + bs / 2, by + bs / 2, bs / 2, 0, 2 * M_PI);
        setSource(cr, app.color("surface_container"), d.hover == kUcSettings ? 0.95 : 0.75);
        cairo_fill(cr);
        drawIcon(cr, "settings", bx + 8, by + 8, 20, app.color("on_surface"), false);
    }

    const double ty = ay + av + 6;
    const double nameW = W - 2 * pad;
    text(X.name + "@" + X.host, std::clamp(H * 0.075, 17.0, 26.0), true, pad + 2, ty, app.color("on_surface"), 1.0, nameW);

    {
        const double bh = std::clamp(H * 0.14, 34.0, 44.0), by = H - pad - bh + 6;
        const double pw = bh, lw = std::clamp(W * 0.26, 96.0, 140.0);
        const double px = W - pad - pw, lx = px - 8 - lw;
        d.hits.push_back({kUcPower, Rect{px, by, pw, bh}});
        cairo_new_path(cr);
        cairo_arc(cr, px + pw / 2, by + bh / 2, bh / 2 - 0.5, 0, 2 * M_PI);
        setSource(cr, app.color(d.hover == kUcPower ? "surface_container_highest" : "surface_container_high"));
        cairo_fill_preserve(cr);
        setSource(cr, app.color(d.hover == kUcPower ? "on_surface" : "outline"), d.hover == kUcPower ? 0.5 : 0.7);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        drawIcon(cr, "power", px + (pw - 20) / 2, by + (bh - 20) / 2, 20, app.color("on_surface"), false);
        d.hits.push_back({kUcLock, Rect{lx, by, lw, bh}});
        roundedRect(cr, lx, by, lw, bh, bh / 2);
        setSource(cr, app.color(d.hover == kUcLock ? "primary_fixed_dim" : "primary"));
        cairo_fill(cr);
        {
            PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
            pango_font_description_set_absolute_size(fd, 14 * PANGO_SCALE);
            pango_layout_set_font_description(layout, fd);
            pango_font_description_free(fd);
            pango_layout_set_width(layout, -1);
            pango_layout_set_text(layout, "Lock", -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            const double group = 18 + 7 + tw, gx = lx + (lw - group) / 2;
            drawIcon(cr, "lock", gx, by + (bh - 18) / 2, 18, app.color("on_primary"), true);
            text("Lock", 14, true, gx + 25, by + (bh - th) / 2.0, app.color("on_primary"), 1.0, tw + 4);
        }
    }
    {
        const double bh = std::clamp(H * 0.14, 34.0, 44.0), by = H - pad - bh + 6;
        const double pw = bh, lw = std::clamp(W * 0.26, 96.0, 140.0), lx = W - pad - pw - 8 - lw;
        const long up = X.upSeconds, vals[3] = {up / 86400, (up % 86400) / 3600, (up % 3600) / 60};
        const char* labels[3] = {"days", "hours", "min"};
        const double numPx = std::clamp(H * 0.06, 14.0, 19.0), lblPx = std::clamp(H * 0.04, 10.0, 13.0), top = by + (bh - (numPx + lblPx + 6)) / 2;
        // As in the Quickshell card: every column is as wide as its own text, and a 1 px divider sits 10 px from each column.
        constexpr double kGapToDivider = 10;
        auto textWidth = [&](const std::string& str, double px, bool bold) {
            PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
            pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
            pango_layout_set_font_description(layout, fd);
            pango_font_description_free(fd);
            pango_layout_set_width(layout, -1);
            pango_layout_set_text(layout, str.c_str(), -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            return static_cast<double>(tw);
        };
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        double cx = pad + 2;
        for (int k2 = 0; k2 < 3; k2++) {
            const std::string num = std::to_string(vals[k2]);
            const double colW = std::max(textWidth(num, numPx, true), textWidth(labels[k2], lblPx, false)) + 2;
            text(num, numPx, true, cx, top, app.color("on_surface"), 1.0, colW);
            text(labels[k2], lblPx, false, cx, top + numPx + 4, app.color("on_surface_variant"), 1.0, colW);
            if (k2 < 2) {
                setSource(cr, app.color("on_surface"), 0.2);
                cairo_rectangle(cr, cx + colW + kGapToDivider - 0.5, by + bh / 2 - 14, 1, 28);
                cairo_fill(cr);
            }
            cx += colW + 2 * kGapToDivider + 1;
        }
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    }
    g_object_unref(layout);
    endClipGroup(cr, cut);
    cairo_restore(cr);
    userEditButtons(app, X, cr, W);
}

void userPickImage() {
    char exe[512] = {};
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    const std::string cmd = "p=$(zenity --file-selection --title='Choose an image' --file-filter='Images | *.png *.jpg *.jpeg *.webp *.avif *.bmp *.gif *.tiff *.PNG *.JPG *.JPEG *.WEBP') && [ -n \"$p\" ] && '" + std::string(exe, n) + "' \"usercard image $p\"";
    runDetached({"bash", "-c", cmd.c_str()});
}

void drawIslandCard(App& app);
void userSetImage(App& app, const std::string& path) {
    UserW& X = app.userW;
    if (X.customImage == path) return;
    X.customImage = path;
    deskSave(X);
    if (X.open()) deskDraw(app, X);
    if (app.ic.open()) drawIslandCard(app);
}

void userClick(App& app, DeskWidget&, int id) {
    if (id == kUcPick) { userPickImage(); return; }
    if (id == kUcReset) { userSetImage(app, ""); return; }
    if (id == kUcLock) startLock();
    else if (id == kUcSettings) toggleSettings();
    else if (id == kUcPower) openPowerMenu(app);
}

int userCursor(const DeskWidget&, int id) { return id >= 1 && id <= 5 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : -1; }

void userWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    UserW& X = app.userW;
    if (!X.open() || !X.configured) return;
    if (now >= X.nextRead) {
        std::ifstream f("/proc/uptime");
        double secs = 0;
        f >> secs;
        const long old = X.upSeconds / 60;
        X.upSeconds = static_cast<long>(secs);
        X.nextRead = now + std::chrono::seconds(20);
        if (X.upSeconds / 60 != old || X.heroPath != userHeroSource(app, X)) deskDraw(app, X);
    }
    timeoutMs = timeoutMs < 0 ? 20000 : std::min(timeoutMs, 20000);
}

void userSaveExtra(const DeskWidget& d, std::ostream& out) {
    const std::string& path = static_cast<const UserW&>(d).customImage;
    if (!path.empty()) out << "image\t" << path << "\n";
}
void userLoadLine(DeskWidget& d, const std::string& line) {
    if (line.rfind("image\t", 0) == 0) static_cast<UserW&>(d).customImage = line.substr(6);
}

void userWInit(UserW& X) {
    X.DeskWidget::name = "usercard";
    X.minW = 300; X.maxW = 640;
    X.minH = 240; X.maxH = 560;
    X.w = 420; X.h = 300;
    X.radius = 30;
    X.paint = userPaint;
    X.click = userClick;
    X.DeskWidget::cursor = userCursor;
    X.saveExtra = userSaveExtra;
    X.loadLine = userLoadLine;
}
