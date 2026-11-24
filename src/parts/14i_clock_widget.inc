
void clockCookiePath(cairo_t* cr, int sides, double x, double y, double size) {
    static const int ready[] = {4, 6, 7, 9, 12};
    for (int r : ready) if (r == sides) { shapePath(cr, "Cookie" + std::to_string(sides) + "Sided", x, y, size); return; }
    const int n = std::max(3, sides) * 2;
    std::vector<std::pair<double, double>> v(n);
    for (int i = 0; i < n; i++) {
        const double a = i * M_PI / sides + M_PI / 6, rad = i % 2 == 0 ? 1.0 : 0.8;
        v[i] = {std::cos(a) * rad, std::sin(a) * rad};
    }
    double mnx = 1e9, mxx = -1e9, mny = 1e9, mxy = -1e9;
    for (auto& p : v) { mnx = std::min(mnx, p.first); mxx = std::max(mxx, p.first); mny = std::min(mny, p.second); mxy = std::max(mxy, p.second); }
    const double sc = size / std::max(mxx - mnx, mxy - mny), ox = x + (size - (mxx - mnx) * sc) / 2 - mnx * sc, oy = y + (size - (mxy - mny) * sc) / 2 - mny * sc;
    auto P = [&](int i) { return std::pair<double, double>{ox + v[(i + n) % n].first * sc, oy + v[(i + n) % n].second * sc}; };
    const double round = (sides < 17 ? 1.5 : 1.1) / std::max(sides, 1) * sc;
    cairo_new_sub_path(cr);
    for (int i = 0; i < n; i++) {
        const auto a = P(i - 1), b = P(i), c = P(i + 1);
        double d1x = a.first - b.first, d1y = a.second - b.second, d2x = c.first - b.first, d2y = c.second - b.second;
        const double l1 = std::hypot(d1x, d1y), l2 = std::hypot(d2x, d2y);
        d1x /= l1; d1y /= l1; d2x /= l2; d2y /= l2;
        const double ang = std::acos(std::clamp(d1x * d2x + d1y * d2y, -1.0, 1.0));
        const double t = std::min({round / std::tan(ang / 2), l1 / 2, l2 / 2}), rad = t * std::tan(ang / 2);
        const double sx = b.first + d1x * t, sy = b.second + d1y * t, ex = b.first + d2x * t, ey = b.second + d2y * t;
        if (i == 0) cairo_move_to(cr, sx, sy); else cairo_line_to(cr, sx, sy);
        double bx = d1x + d2x, by = d1y + d2y;
        const double bl = std::hypot(bx, by);
        bx /= bl; by /= bl;
        const double cx = b.first + bx * (t / std::cos(ang / 2)) * std::cos(ang / 2) * 0 + bx * (rad / std::sin(ang / 2));
        const double cy = b.second + by * (rad / std::sin(ang / 2));
        const double a1 = std::atan2(sy - cy, sx - cx), a2 = std::atan2(ey - cy, ex - cx);
        const double cross = d1x * d2y - d1y * d2x;
        if (cross > 0) cairo_arc_negative(cr, cx, cy, rad, a1, a2); else cairo_arc(cr, cx, cy, rad, a1, a2);
    }
    cairo_close_path(cr);
}

void clockNatural(const App& app, double& w, double& h) {
    const std::string& st = app.cfg.clockStyle;
    if (st == "pixel") {
        if (app.cfg.clockPixel == "vertical") { w = 276; h = 264; } else { w = 420; h = 150; }
    } else if (st == "daily") {
        const std::string& v = app.cfg.clockDaily;
        if (v == "morning") { w = 320; h = 290; }
        else if (v == "script") { w = 360; h = 262; }
        else if (v == "bold") { w = 300; h = 250; }
        else { w = 320; h = 430; }
    } else if (st == "digital") { w = 420; h = 200; }
    else { w = 250; h = 250; }
}
std::string clockFaceKey(const App& app) {
    return app.cfg.clockStyle + ":" + (app.cfg.clockStyle == "daily" ? app.cfg.clockDaily : app.cfg.clockStyle == "pixel" ? app.cfg.clockPixel : "");
}

std::string clockTimeString(const App& app, const std::tm& tm) {
    char b[16];
    if (app.cfg.clock24) std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else std::snprintf(b, sizeof b, "%d:%02d", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min);
    return b;
}
std::string clockGreeting(const std::tm& tm) {
    return tm.tm_hour < 6 ? "Good night" : tm.tm_hour < 12 ? "Good morning" : tm.tm_hour < 19 ? "Good afternoon" : "Good evening";
}
std::string clockWeatherLine(const WeatherData& w) {
    if (!w.ok) return "";
    std::string label, ic;
    weatherDescribe(w.code, w.day, label, ic);
    const std::string deg = w.unit == "fahrenheit" ? "°F" : "°C";
    return "Currently " + std::to_string(std::lround(w.temp)) + deg + " with " + lowerCopy(label) + ". Feels like " + std::to_string(std::lround(w.feels)) +
           "°, high " + std::to_string(std::lround(w.hi)) + "°, low " + std::to_string(std::lround(w.lo)) + "°.";
}
std::string clockStrftime(const char* fmt, const std::tm& tm) {
    char b[64];
    i18n::strftime(b, sizeof b, fmt, &tm);
    return b;
}

void clockOwnShadow(const App& app, cairo_t* cr, const std::string& key, double w, double h, double scale, const std::function<void(cairo_t*, double)>& outline) {
    if (!app.cfg.widgetShadow) return;
    static cairo_surface_t* mask = nullptr;
    static std::string maskKey;
    if (!mask || maskKey != key) {
        if (mask) cairo_surface_destroy(mask);
        mask = makeShadowMaskOf(w, h, outline);
        maskKey = key;
    }
    setSource(cr, app.color("shadow"), 0.5);
    cairo_mask_surface(cr, mask, -kLaunShadow, -kLaunShadow + 3 / scale);
}

void clockPixelPaint(App& app, cairo_t* cr, int W, int H, const std::tm& tm) {
    double nw = 0, nh = 0;
    clockNatural(app, nw, nh);
    const bool vertical = app.cfg.clockPixel == "vertical";
    const double s = std::min(W / nw, H / nh);
    cairo_save(cr);
    cairo_translate(cr, (W - nw * s) / 2, (H - nh * s) / 2);
    cairo_scale(cr, s, s);
    const double tileW = vertical ? nw * 0.66 : nw * 0.327, tileH = vertical ? nh * 0.66 : nh * 0.9;
    const double glyph = vertical ? nh * 0.66 : nh * 0.85, fringe = vertical ? nw * 0.026 : nh * 0.03;
    const double gx[4] = {0, vertical ? nw * 0.30 : nw * 0.163, vertical ? 0.0 : nw * 0.50, vertical ? nw * 0.30 : nw * 0.653};
    const double gy[4] = {vertical ? nh * -0.04 : nh * 0.05, vertical ? nh * -0.04 : nh * 0.05, vertical ? nh * 0.42 : nh * 0.05, vertical ? nh * 0.42 : nh * 0.05};
    char hm[8];
    std::snprintf(hm, sizeof hm, "%02d%02d", app.cfg.clock24 ? tm.tm_hour : (tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12), tm.tm_min);
    auto makeFont = [&](PangoLayout* lay) {
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, kDisplayFamily);
        pango_font_description_set_weight(fd, static_cast<PangoWeight>(1000));
        pango_font_description_set_variations(fd, "wght=1000");
        pango_font_description_set_absolute_size(fd, glyph * PANGO_SCALE);
        pango_layout_set_font_description(lay, fd);
        pango_font_description_free(fd);
    };
    PangoLayout* layout = pango_cairo_create_layout(cr);
    makeFont(layout);
    auto digitPath = [&](cairo_t* c2, PangoLayout* lay, int i, double ox, double oy) {
        char one[2] = {hm[i], 0};
        pango_layout_set_text(lay, one, -1);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(lay, &lw, &lh);
        cairo_move_to(c2, ox + gx[i] + (tileW - lw) / 2.0, oy + gy[i] + (tileH - lh) / 2.0);
        pango_cairo_layout_path(c2, lay);
    };
    const std::string key = std::string("pixel") + (vertical ? "v" : "h") + hm + std::to_string(static_cast<int>(nw));
    clockOwnShadow(app, cr, key, nw, nh, s, [&](cairo_t* c2, double m) {
        PangoLayout* lay2 = pango_cairo_create_layout(c2);
        makeFont(lay2);
        for (int i = 0; i < 4; i++) digitPath(c2, lay2, i, m, m);
        g_object_unref(lay2);
    });
    const Color soft = app.color("primary_container"), bold = app.color("primary");
    cairo_push_group(cr);
    for (int i = 0; i < 4; i++) {
        if (i > 0) {
            digitPath(cr, layout, i, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
            cairo_set_line_width(cr, fringe * 2);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke_preserve(cr);
            cairo_fill(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        }
        digitPath(cr, layout, i, 0, 0);
        setSource(cr, i == 0 || i == 3 ? soft : bold);
        cairo_fill(cr);
    }
    if (!vertical) {
        const double dot = nh * 0.2, gap = nh * 0.04;
        const double colonX = gx[1] + tileW + (gx[2] - (gx[1] + tileW)) / 2 - nw * 0.03;
        const double top = gy[0] + tileH / 2 - (2 * dot + gap) / 2;
        setSource(cr, bold);
        for (int k = 0; k < 2; k++) {
            cairo_new_path(cr);
            cairo_arc(cr, colonX + dot / 2, top + dot / 2 + k * (dot + gap), dot / 2, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
    g_object_unref(layout);
    cairo_restore(cr);
}

void clockDailyPaint(App& app, ClockW& X, cairo_t* cr, int W, int H, const std::tm& tm) {
    double nw = 0, nh = 0;
    clockNatural(app, nw, nh);
    const double s = std::min(W / nw, H / nh);
    cairo_save(cr);
    cairo_translate(cr, (W - nw * s) / 2, (H - nh * s) / 2);
    cairo_scale(cr, s, s);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    const std::string variant = app.cfg.clockDaily;
    const Color ink = app.color("on_surface");
    std::string wlabel, wicon = "cloud";
    if (X.wx.ok) weatherDescribe(X.wx.code, X.wx.day, wlabel, wicon);
    const std::string wline = clockWeatherLine(X.wx), timeStr = clockTimeString(app, tm);
    const std::string weekday = clockStrftime("%A", tm), dayNum = clockStrftime("%d", tm), month = clockStrftime("%B", tm);
    bool shadowPass = false;
    auto font = [&](const char* family, double px, int weight, bool italic, double wrap) {
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, family);
        pango_font_description_set_weight(fd, static_cast<PangoWeight>(weight));
        if (italic) pango_font_description_set_style(fd, PANGO_STYLE_ITALIC);
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_width(layout, wrap > 0 ? static_cast<int>(wrap * PANGO_SCALE) : -1);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    };
    auto put = [&](const std::string& str, const char* family, double px, int weight, double x, double y, const Color& c, double a, bool italic, double wrap, double* outW) {
        font(family, px, weight, italic, wrap);
        pango_layout_set_text(layout, str.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (outW) *outW = tw;
        if (shadowPass) {
            static const double off[9][2] = {{0, 2}, {-2, 0}, {2, 0}, {0, 3}, {-2, 2}, {2, 2}, {-1, -1}, {1, -1}, {0, 4}};
            setSource(cr, app.color("shadow"), 0.2 * a);
            for (const auto& o : off) {
                cairo_move_to(cr, x + o[0], y + o[1]);
                pango_cairo_show_layout(cr, layout);
            }
            return static_cast<double>(th);
        }
        setSource(cr, c, a);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);
        return static_cast<double>(th);
    };
    auto meas = [&](const std::string& str, const char* family, double px, int weight, bool italic, double wrap) {
        font(family, px, weight, italic, wrap);
        pango_layout_set_text(layout, str.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        return std::pair<double, double>(tw, th);
    };
    const char* F = kDisplayFamily;
    auto content = [&](bool sp) {
        shadowPass = sp;
        if (variant == "morning") {
            double y = 0;
            y += put(clockGreeting(tm), "cursive", 56, 300, 0, y, ink, 1, true, 0, nullptr) + 10;
            std::string today = "Today, it's " + weekday + " " + std::to_string(tm.tm_mday) + " of " + month + (wlabel.empty() ? "" : " with " + lowerCopy(wlabel));
            for (char& c : today) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            y += put(today, F, 26, 700, 0, y, ink, 1, false, 300, nullptr) + 10;
            if (!sp) { setSource(cr, ink, 0.6); cairo_rectangle(cr, 0, y, 90, 2); cairo_fill(cr); }
            y += 12;
            if (app.batPresent) y += put("Battery " + std::to_string(app.batPct) + "% left", F, 13, 400, 0, y, ink, 1, false, 0, nullptr) + 10;
            if (!sp) {
                shapePath(cr, "Cookie9Sided", 0, y, 28);
                setSource(cr, app.color("primary_container"));
                cairo_fill(cr);
                drawIcon(cr, "schedule", 6, y + 6, 16, app.color("on_primary_container"), true);
            }
            std::string spaced;
            for (char c : timeStr) { spaced += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); spaced += ' '; }
            const auto [tw, th] = meas(spaced, F, 18, 600, false, 0);
            (void)tw;
            put(spaced, F, 18, 600, 38, y + (28 - th) / 2, ink, 1, false, 0, nullptr);
        } else if (variant == "script") {
            if (!sp) {
                shapePath(cr, "Cookie9Sided", 0, 0, 40);
                setSource(cr, app.color("secondary_container"));
                cairo_fill(cr);
                drawIcon(cr, "schedule", 9, 9, 22, app.color("on_secondary_container"), true);
            }
            const auto [tw, th] = meas(timeStr, F, 28, 600, false, 0);
            (void)tw;
            put(timeStr, F, 28, 600, 52, (40 - th) / 2, ink, 1, false, 0, nullptr);
            double y = 52;
            y += put(weekday + ".", "cursive", 84, 400, 0, y, app.color("primary"), 1, true, 0, nullptr) + 12;
            const std::string date = clockStrftime("%b. %d, %Y", tm);
            const auto [dw, dh] = meas(date, F, 14, 400, false, 0);
            if (!sp) {
                roundedRect(cr, 0, y, dw + 28, 32, 10);
                setSource(cr, app.color("secondary_container"));
                cairo_fill(cr);
                put(date, F, 14, 400, 14, y + (32 - dh) / 2, app.color("on_secondary_container"), 1, false, 0, nullptr);
            }
            y += 44;
            if (!wline.empty()) put(wline, F, 15, 400, 0, y, ink, 1, false, 300, nullptr);
        } else if (variant == "bold") {
            if (sp) return;
            roundedRect(cr, 0, 0, nw, nh, 32);
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
            const Color on = app.color("on_primary");
            const std::string tempLine = X.wx.ok ? std::to_string(std::lround(X.wx.temp)) + "° " + wlabel : "";
            double blockH = meas(dayNum, F, 14, 700, false, 0).second;
            blockH += meas(clockStrftime("%b", tm) + ".", F, 96, 900, false, 0).second - 22;
            blockH += meas("Today's " + weekday, F, 18, 700, false, 0).second + 4;
            if (X.wx.ok) blockH += meas(tempLine, F, 14, 400, false, 0).second + 8;
            blockH += meas(timeStr, F, 14, 400, false, 0).second;
            double y = std::max(0.0, (nh - blockH) / 2.0);
            y += put(dayNum, F, 14, 700, 24, y, on, 1, false, 0, nullptr);
            y += put(clockStrftime("%b", tm) + ".", F, 96, 900, 24, y - 12, on, 1, false, 0, nullptr) - 22;
            y += put("Today's " + weekday, F, 18, 700, 24, y, on, 1, false, 0, nullptr) + 4;
            if (X.wx.ok) y += put(std::to_string(std::lround(X.wx.temp)) + "° " + wlabel, F, 14, 400, 24, y, on, 0.8, false, 0, nullptr) + 8;
            double tw = 0;
            put(timeStr, F, 14, 400, 24, y, on, 0.8, false, 0, &tw);
            setSource(cr, on);
            cairo_rectangle(cr, 24 + tw + 12, y + 9, 40, 2);
            cairo_fill(cr);
        } else {
            if (!sp) {
                shapePath(cr, "Cookie12Sided", 0, 0, 130);
                setSource(cr, app.color("secondary_container"));
                cairo_fill(cr);
                drawIcon(cr, wicon, 33, 33, 64, app.color("on_secondary_container"), true);
            }
            double y = 144;
            double ddw = 0;
            put(dayNum, F, 64, 500, 0, y, ink, 1, false, 0, &ddw);
            put(clockStrftime("%a", tm), F, 20, 400, ddw + 10, y + 14, ink, 1, false, 0, nullptr);
            put(month, F, 20, 400, ddw + 10, y + 38, ink, 1, false, 0, nullptr);
            y += 90;
            y += put(clockGreeting(tm), F, 38, 300, 0, y, ink, 1, false, 0, nullptr) + 14;
            if (X.wx.ok) {
                y += put(wlabel, F, 18, 500, 0, y, ink, 1, false, 0, nullptr) + 4;
                put(wline, F, 15, 400, 0, y, ink, 1, false, 320, nullptr);
            }
            const double ry = nh - 52;
            const auto [tw, th] = meas(timeStr, F, 20, 600, false, 0);
            if (!sp) {
                shapePath(cr, "Cookie9Sided", 0, ry, 52);
                setSource(cr, app.color("primary_container"));
                cairo_fill(cr);
                drawIcon(cr, "schedule", 15, ry + 15, 22, app.color("on_primary_container"), true);
                roundedRect(cr, 62, ry, tw + 56, 52, 26);
                setSource(cr, app.color("secondary_container"));
                cairo_fill(cr);
                put(timeStr, F, 20, 600, 62 + 28, ry + (52 - th) / 2, app.color("on_secondary_container"), 1, false, 0, nullptr);
            }
        }
    };
    if (variant == "bold") {
        clockOwnShadow(app, cr, "dailybold", nw, nh, s, [&](cairo_t* c2, double m) { roundedRect(c2, m, m, nw, nh, 32); });
    } else {
        content(true);
    }
    content(false);
    g_object_unref(layout);
    cairo_restore(cr);
}

void clockPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    ClockW& X = static_cast<ClockW&>(d);
    const time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const std::string& s, const char* family, double px, int weight, double cx, double cy, const Color& c, double alpha, bool centred = true, double* outW = nullptr) {
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, family);
        pango_font_description_set_weight(fd, static_cast<PangoWeight>(weight));
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, s.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (outW) *outW = tw;
        setSource(cr, c, alpha);
        cairo_move_to(cr, centred ? cx - tw / 2.0 : cx, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
    };

    if (app.cfg.clockStyle == "daily") { g_object_unref(layout); clockDailyPaint(app, X, cr, W, H, tm); return; }
    if (app.cfg.clockStyle == "pixel") { g_object_unref(layout); clockPixelPaint(app, cr, W, H, tm); return; }
    if (app.cfg.clockStyle == "digital") {
        char hm[16];
        if (app.cfg.clock24) std::snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
        else std::snprintf(hm, sizeof hm, "%d:%02d", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min);
        char dateBuf[64];
        i18n::strftime(dateBuf, sizeof dateBuf, "%a, %d %b", &tm);
        std::string date = dateBuf;
        for (char& ch : date) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const double pad = W * 0.03, px = std::min(H * 0.78, (W - 2 * pad) / 2.9);
        const Color tint = mixColor(app.color("primary"), app.color("on_surface"), 0.55);
        const double timeY = H * 0.46;
        text(hm, kDisplayFamily, px, 900, pad, timeY, tint, 0.42, false);
        const double datePx = std::clamp(px * 0.27, 14.0, 44.0);
        double dateW = 0;
        text(date, kDisplayFamily, datePx, 800, 0, -1000, tint, 0.0, false, &dateW);
        text(date, kDisplayFamily, datePx, 800, W - pad - dateW, timeY + px * 0.40, tint, 0.85, false);
        g_object_unref(layout);
        return;
    }

    const double S = std::min(W, H), s = S / 230.0;
    cairo_save(cr);
    cairo_translate(cr, (W - S) / 2, (H - S) / 2);
    cairo_scale(cr, s, s);
    if (app.cfg.widgetShadow) {
        static cairo_surface_t* shadow = nullptr;
        static int shadowSize = 0, shadowSides = 0;
        if (!shadow || shadowSize != static_cast<int>(S) || shadowSides != app.cfg.cookieSides) {
            if (shadow) cairo_surface_destroy(shadow);
            const int sides = app.cfg.cookieSides;
            shadow = makeShadowMaskOf(230, 230, [sides](cairo_t* c2, double m) { clockCookiePath(c2, sides, m, m, 230); });
            shadowSize = static_cast<int>(S);
            shadowSides = app.cfg.cookieSides;
        }
        setSource(cr, app.color("shadow"), 0.5);
        cairo_mask_surface(cr, shadow, -kLaunShadow, -kLaunShadow + 3 / s);
    }
    const Color bg = app.color("primary_container"), info = mixColor(app.color("primary"), bg, 0.55);
    const Color onBg = mixColor(app.color("secondary"), bg, 0.15);
    clockCookiePath(cr, app.cfg.cookieSides, 0, 0, 230);
    setSource(cr, bg);
    cairo_fill(cr);
    const double cx = 115, cy = 115;

    {
        const std::string& dial = app.cfg.cookieDial;
        setSource(cr, onBg);
        if (dial == "full") {
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            for (int i = 0; i < 60; i++) {
                const bool hour = i % 5 == 0;
                const double a = i * M_PI / 30 - M_PI / 2, r1 = 115 - 22, r0 = r1 - (hour ? 18 : 7);
                cairo_set_line_width(cr, hour ? 4 : 2);
                cairo_move_to(cr, cx + std::cos(a) * r0, cy + std::sin(a) * r0);
                cairo_line_to(cr, cx + std::cos(a) * r1, cy + std::sin(a) * r1);
                cairo_stroke(cr);
            }
        } else if (dial == "dots") {
            for (int i = 0; i < 12; i++) {
                const double a = i * M_PI / 6 - M_PI / 2;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx + std::cos(a) * 87, cy + std::sin(a) * 87, 6, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        } else if (dial == "numbers") {
            for (int i = 1; i <= 4; i++) {
                const double a = i * M_PI / 2 - M_PI / 2;
                text(std::to_string(i * 3), kDisplayFamily, 80, 900, cx + std::cos(a) * 65, cy + std::sin(a) * 65, onBg, 1.0);
            }
        }
    }

    if (app.cfg.cookieTime) {
        char hh[8], mm[8];
        if (app.cfg.clock24) std::snprintf(hh, sizeof hh, "%02d", tm.tm_hour);
        else std::snprintf(hh, sizeof hh, "%02d", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12);
        std::snprintf(mm, sizeof mm, "%02d", tm.tm_min);
        const bool ampm = !app.cfg.clock24;
        const double y0 = cy - (ampm ? 36 : 25);
        text(hh, kDisplayFamily, 68, 700, cx, y0, info, 1.0);
        text(mm, kDisplayFamily, 68, 700, cx, y0 + 52, info, 1.0);
        if (ampm) text(tm.tm_hour < 12 ? "AM" : "PM", kDisplayFamily, 26, 700, cx, y0 + 96, info, 1.0);
    }

    auto bar = [&](double deg, double start, double length, double w, double r, const Color& c, bool hollow) {
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, (deg - 90) * M_PI / 180);
        if (hollow) { roundedRect(cr, start + 2, -w / 2 + 2, length - 4, w - 4, std::max(0.0, r - 2)); cairo_set_line_width(cr, 4); setSource(cr, c); cairo_stroke(cr); }
        else { roundedRect(cr, start, -w / 2, length, w, r); setSource(cr, c); cairo_fill(cr); }
        cairo_restore(cr);
    };
    const std::string& hourStyle = app.cfg.cookieHour;
    const std::string& minStyle = app.cfg.cookieMinute;
    const Color cMin = app.color("tertiary"), cHour = app.color("primary"), cSec = app.color("secondary");
    auto hourHand = [&]() {
        if (hourStyle == "classic") bar(X.hourAngle, -25, 72, 8, 2, cHour, false);
        else bar(X.hourAngle, -10, 72, 20, 10, cHour, hourStyle == "hollow");
    };
    if (hourStyle == "hollow") hourHand();
    if (minStyle != "hide") {
        const double w = minStyle == "bold" ? 20 : minStyle == "medium" ? 12 : 5;
        const bool classic = minStyle == "classic";
        bar(X.minuteAngle, classic ? -w / 2 - 15 : -w / 2, 95, w, classic ? 2 : w / 2, cMin, false);
    }
    if (hourStyle != "hollow" && hourStyle != "hide") hourHand();
    if (app.cfg.cookieSecond != "hide") {
        const std::string& st = app.cfg.cookieSecond;
        const double a = (tm.tm_sec * 6 - 90) * M_PI / 180;
        if (st == "line" || st == "classic") {
            cairo_set_line_width(cr, 2);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            setSource(cr, cSec);
            cairo_move_to(cr, cx + std::cos(a) * 10, cy + std::sin(a) * 10);
            cairo_line_to(cr, cx + std::cos(a) * 105, cy + std::sin(a) * 105);
            cairo_stroke(cr);
        }
        if (st == "dot" || st == "classic") {
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx + std::cos(a) * 75, cy + std::sin(a) * 75, st == "dot" ? 10 : 7, 0, 2 * M_PI);
            setSource(cr, cSec);
            cairo_fill(cr);
        }
    }
    if (minStyle != "bold") {
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, 3, 0, 2 * M_PI);
        setSource(cr, minStyle == "medium" ? bg : cMin);
        cairo_fill(cr);
    }

    if (app.cfg.cookieDate == "bubble") {
        const double sz = 64;
        shapePath(cr, "Pentagon", 0, 0, sz);
        setSource(cr, app.color("tertiary_container"));
        cairo_fill(cr);
        text(std::to_string(tm.tm_mday), kDisplayFamily, 30, 900, sz / 2, sz / 2, app.color("on_tertiary_container"), 1.0);
        shapePath(cr, "Pill", 230 - sz, 230 - sz, sz);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        char mo[8];
        std::snprintf(mo, sizeof mo, "%02d", tm.tm_mon + 1);
        text(mo, kDisplayFamily, 30, 900, 230 - sz / 2, 230 - sz / 2, app.color("on_secondary_container"), 1.0);
    } else if (app.cfg.cookieDate == "rect") {
        roundedRect(cr, 230 - 10 - 45, 115 - 15, 45, 30, 8);  // (10 from the right edge: the end-4 clock slides it from 40 to 10 as it fades in)
        setSource(cr, mixColor(info, app.color("secondary_container"), 0.5));
        cairo_fill(cr);
        char dd[8];
        std::snprintf(dd, sizeof dd, "%02d", tm.tm_mday);
        text(dd, kDisplayFamily, 20, 900, 230 - 10 - 22.5, 115, app.color("secondary"), 1.0);
    }
    cairo_restore(cr);
    g_object_unref(layout);
}

void clockTargets(ClockW& X, const std::tm& tm) {
    X.hourTarget = 360.0 / 12 * (tm.tm_hour % 12 + tm.tm_min / 60.0);
    X.minuteTarget = 360.0 / 60 * tm.tm_min;
}

bool clockGlide(double& shown, double target, double dt) {
    double diff = std::fmod(target - shown + 720.0, 360.0);
    if (diff < 0.05 || diff > 359.5) { shown = target; return false; }
    shown = std::fmod(shown + diff * (1 - std::exp(-dt * 9)) + 360.0, 360.0);
    return true;
}

void clockWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    ClockW& X = app.deskClock;
    if (!X.open() || !X.configured) return;
    auto want = [&](int ms) { timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms); };
    bool redraw = false;
    if (X.styleShown != clockFaceKey(app)) {
        const bool first = X.styleShown.empty();
        X.styleShown = clockFaceKey(app);
        X.resizable = true;
        double fw = 0, fh = 0;
        clockNatural(app, fw, fh);
        const bool fixedShape = app.cfg.clockStyle == "daily" || app.cfg.clockStyle == "pixel";
        X.keepAspect = fixedShape ? fw / fh : 0;
        X.minW = fixedShape ? static_cast<int>(fw * 0.5) : 180;
        X.maxW = fixedShape ? static_cast<int>(fw * 2.2) : 640;
        X.minH = fixedShape ? static_cast<int>(fh * 0.5) : 120;
        X.maxH = fixedShape ? static_cast<int>(fh * 2.2) : 640;
        weatherSvcWant(app, app.cfg.clockStyle == "daily" || app.weather.open() || inBarLayout(app, "weather"));
        if (!first) {
            const int nw = static_cast<int>(fw), nh = static_cast<int>(fh);
            X.w = nw; X.h = nh;
            if (deskFit(app, X)) {
                zwlr_layer_surface_v1_set_margin(X.layer, X.mt - kDeskRoom, 0, 0, X.ml - kDeskRoom);
                deskSave(X);
            }
            deskRequestSize(app, X, nw, nh);
        }
        redraw = true;
    }
    if (app.cfg.clockStyle == "daily") {
        uint64_t v = 0;
        WeatherData fresh;
        const bool ok = weatherSvcGet(fresh, &v);
        if (v != X.wxSeen) { X.wxSeen = v; if (ok) X.wx = fresh; redraw = true; }
    }
    const time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    if (app.cfg.clockStyle == "cookie" && app.cfg.cookieSecond != "hide" && tm.tm_sec != X.lastSecond) { X.lastSecond = tm.tm_sec; redraw = true; }
    if (tm.tm_min != X.lastMinute) {
        const bool firstTime = X.lastMinute < 0;
        X.lastMinute = tm.tm_min;
        clockTargets(X, tm);
        if (firstTime) { X.hourAngle = X.hourTarget; X.minuteAngle = X.minuteTarget; }
        redraw = true;
    }
    const double dt = X.last == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(now - X.last).count());
    X.last = now;
    bool moving = clockGlide(X.hourAngle, X.hourTarget, dt);
    moving = clockGlide(X.minuteAngle, X.minuteTarget, dt) || moving;
    if (moving) { redraw = true; want(16); }
    want(app.cfg.clockStyle == "cookie" && app.cfg.cookieSecond != "hide" ? 200 : 1000);
    if (redraw) deskDraw(app, X);
}

void clockWToggled(App& app, bool open) {
    ClockW& X = app.deskClock;
    weatherSvcWant(app, (open && app.cfg.clockStyle == "daily") || app.weather.open() || inBarLayout(app, "weather"));
    if (!open) return;
    X.lastMinute = -1;
    X.wxSeen = 0;
    X.styleShown = clockFaceKey(app);
    double fw = 0, fh = 0;
    clockNatural(app, fw, fh);
    const bool fixedShape = app.cfg.clockStyle == "daily" || app.cfg.clockStyle == "pixel";
    X.keepAspect = fixedShape ? fw / fh : 0;
    X.minW = fixedShape ? static_cast<int>(fw * 0.5) : 180;
    X.maxW = fixedShape ? static_cast<int>(fw * 2.2) : 640;
    X.minH = fixedShape ? static_cast<int>(fh * 0.5) : 120;
    X.maxH = fixedShape ? static_cast<int>(fh * 2.2) : 640;
}

void clockWInit(ClockW& X) {
    X.name = "clock";
    X.minW = 180; X.maxW = 640;
    X.minH = 120; X.maxH = 640;
    X.w = 250; X.h = 250;
    X.noShadow = true;
    X.paint = clockPaint;
    X.onToggle = clockWToggled;
}
