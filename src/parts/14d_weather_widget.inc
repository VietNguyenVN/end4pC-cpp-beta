
void weatherDescribe(int c, bool day, std::string& label, std::string& ic) {
    if (c == 0) { label = "Clear sky"; ic = day ? "sun-2" : "moon"; }
    else if (c == 1 || c == 2) { label = c == 1 ? "Mostly clear" : "Partly cloudy"; ic = day ? "cloud-sun" : "cloudy-moon"; }
    else if (c == 3) { label = "Overcast"; ic = "cloud"; }
    else if (c == 45 || c == 48) { label = "Fog"; ic = "fog"; }
    else if (c >= 51 && c <= 57) { label = "Drizzle"; ic = "cloud-rain"; }
    else if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) { label = "Rain"; ic = "cloud-rain"; }
    else if ((c >= 71 && c <= 77) || c == 85 || c == 86) { label = "Snow"; ic = "cloud-snowfall"; }
    else if (c >= 95) { label = "Thunderstorm"; ic = "cloud-bolt"; }
    else { label = "Cloudy"; ic = "cloud"; }
}

void weatherSkyHex(const WeatherData& w, Color& top, Color& bottom);

Color weatherThemed(const Color& hue, int like) {
    auto rgb = [](int hex) { return Color{((hex >> 16) & 255) / 255.0, ((hex >> 8) & 255) / 255.0, (hex & 255) / 255.0}; };
    auto lightness = [](const Color& c) { return (std::max({c.r, c.g, c.b}) + std::min({c.r, c.g, c.b})) / 2.0; };
    const double mx = std::max({hue.r, hue.g, hue.b}), mn = std::min({hue.r, hue.g, hue.b}), d = mx - mn;
    double h = 0;
    if (d > 1e-6) {
        if (mx == hue.r) h = std::fmod((hue.g - hue.b) / d, 6.0);
        else if (mx == hue.g) h = (hue.b - hue.r) / d + 2;
        else h = (hue.r - hue.g) / d + 4;
        if (h < 0) h += 6;
    }
    const double lh = (mx + mn) / 2.0;
    const double sat = d < 1e-6 ? 0.0 : std::clamp(d / (1 - std::fabs(2 * lh - 1) + 1e-9), 0.32, 0.72);
    const double l = lightness(rgb(like)), c = (1 - std::fabs(2 * l - 1)) * sat, x = c * (1 - std::fabs(std::fmod(h, 2.0) - 1)), m = l - c / 2;
    double r = 0, g = 0, b = 0;
    const int sector = static_cast<int>(h) % 6;
    if (sector == 0) { r = c; g = x; } else if (sector == 1) { r = x; g = c; } else if (sector == 2) { g = c; b = x; }
    else if (sector == 3) { g = x; b = c; } else if (sector == 4) { r = x; b = c; } else { r = c; b = x; }
    return Color{r + m, g + m, b + m};
}

void weatherSky(const App& app, const WeatherData& w, Color& top, Color& bottom) {
    weatherSkyHex(w, top, bottom);
    if (!app.cfg.weatherTheme) return;
    const int c = w.code;
    const bool wet = (c >= 51 && c <= 67) || (c >= 80 && c <= 86) || (c >= 71 && c <= 77) || c >= 95, grey = c == 3 || c == 45 || c == 48;
    const Color hue = !w.day || (!wet && !grey) ? app.color("primary") : wet ? app.color("tertiary") : app.color("secondary");
    int likeTop = 0, likeBottom = 0;
    if (!w.day) { likeTop = 0x22346a; likeBottom = 0x111b3d; }
    else if (c >= 95) { likeTop = 0x4a4a78; likeBottom = 0x2a2848; }
    else if (wet) { likeTop = 0x56709a; likeBottom = 0x34496e; }
    else if (grey) { likeTop = 0x7a93bd; likeBottom = 0x4f6891; }
    else { likeTop = 0x4f8cf5; likeBottom = 0x2e5fd2; }
    top = weatherThemed(hue, likeTop);
    bottom = weatherThemed(hue, likeBottom);
}
void weatherSkyHex(const WeatherData& w, Color& top, Color& bottom) {
    auto rgb = [](int hex) { return Color{((hex >> 16) & 255) / 255.0, ((hex >> 8) & 255) / 255.0, (hex & 255) / 255.0}; };
    const int c = w.code;
    if (!w.day) { top = rgb(0x22346a); bottom = rgb(0x111b3d); }
    else if (c >= 95) { top = rgb(0x4a4a78); bottom = rgb(0x2a2848); }
    else if ((c >= 51 && c <= 67) || (c >= 80 && c <= 86) || (c >= 71 && c <= 77)) { top = rgb(0x56709a); bottom = rgb(0x34496e); }
    else if (c == 3 || c == 45 || c == 48) { top = rgb(0x7a93bd); bottom = rgb(0x4f6891); }
    else { top = rgb(0x4f8cf5); bottom = rgb(0x2e5fd2); }
}

std::string weatherSourceLine() {
    std::string t = i18n::tr("Source: Open-Meteo");
    size_t p = t.find(':'), len = 1;
    if (const size_t q = t.find("\xEF\xBC\x9A"); q != std::string::npos && q < p) { p = q; len = 3; }
    if (p == std::string::npos) return t;
    size_t end = p + len;
    while (end < t.size() && t[end] == ' ') end++;
    while (p > 0 && t[p - 1] == ' ') p--;
    return t.substr(0, p) + " \xC2\xB7 " + t.substr(end);
}

std::string weatherHourLabel(const App& app, int hour) {
    if (hour < 0) return "Now";
    char b[16];
    if (app.cfg.clock24) std::snprintf(b, sizeof b, "%02d:00", hour);
    else std::snprintf(b, sizeof b, "%d %s", hour % 12 == 0 ? 12 : hour % 12, hour < 12 ? "AM" : "PM");
    return b;
}

void weatherPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    WeatherW& X = static_cast<WeatherW&>(d);
    const WeatherData& w = X.data;
    Color top, bottom;
    weatherSky(app, w, top, bottom);
    roundedRect(cr, 0, 0, W, H, 28);
    cairo_pattern_t* grad = cairo_pattern_create_linear(0, 0, 0, H);
    cairo_pattern_add_color_stop_rgb(grad, 0, top.r, top.g, top.b);
    cairo_pattern_add_color_stop_rgb(grad, 1, bottom.r, bottom.g, bottom.b);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const char* font, const std::string& s, double x, double y, double alpha, bool rightAlign = false, bool center = false, double maxW = 0) {
        PangoFontDescription* fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, s.c_str(), -1);
        if (maxW > 0) {
            pango_layout_set_width(layout, static_cast<int>(maxW * PANGO_SCALE));
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        }
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, 1, 1, 1, alpha);
        cairo_move_to(cr, rightAlign ? x - tw : center ? x - tw / 2.0 : x, y);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        return th;
    };
    const double pad = 22;
    if (!w.ok) {
        text("Sans Bold 14", "Loading the weather…", pad, H / 2.0 - 12, 0.95);
        g_object_unref(layout);
        return;
    }
    std::string label, ic;
    weatherDescribe(w.code, w.day, label, ic);
    auto deg = [](double v) { return std::to_string(static_cast<int>(std::lround(v))) + "°"; };

    constexpr double kPinSize = 14, kPinGap = 4;
    cairo_save(cr);
    cairo_push_group(cr);
    drawIcon(cr, "map-point", pad - 1, 21, static_cast<int>(kPinSize), Color{1, 1, 1});
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.92);
    cairo_restore(cr);
    text("Sans 10", w.city.empty() ? "Weather" : w.city, pad + kPinSize + kPinGap, 20, 0.92, false, false, W - 2 * pad - kPinSize - kPinGap - 120);
    constexpr double kShapeBox = 40, kShapeTop = 46, kCondIcon = 30;
    cairo_save(cr);
    shapePath(cr, kM3IconShape, pad, kShapeTop, kShapeBox);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    drawIcon(cr, ic, pad + (kShapeBox - kCondIcon) / 2.0, kShapeTop + (kShapeBox - kCondIcon) / 2.0, static_cast<int>(kCondIcon), Color{1, 1, 1}, true);
    text("Sans Bold 14", label, pad + kShapeBox + 10, kShapeTop + kShapeBox / 2.0 - 11, 1.0);
    text("Sans 11", "Feels like " + deg(w.feels), pad, 94, 0.95);
    text("Sans 11", "H " + deg(w.hi) + " · L " + deg(w.lo), pad, 118, 0.8);
    {
        const std::string num = std::to_string(static_cast<int>(std::lround(w.temp)));
        PangoRectangle ink{};
        PangoFontDescription* fd = pango_font_description_from_string("Sans Bold 54");
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, num.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        pango_layout_get_pixel_extents(layout, &ink, nullptr);
        const double digitsTop = 10 + ink.y;
        fd = pango_font_description_from_string("Sans Bold 28");
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, "\xC2\xB0", -1);
        int uw = 0, uh = 0;
        pango_layout_get_pixel_size(layout, &uw, &uh);
        PangoRectangle uink{};
        pango_layout_get_pixel_extents(layout, &uink, nullptr);
        const double x0 = W - pad - (tw + 2 + uw);
        cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
        cairo_move_to(cr, x0 + tw + 2, digitsTop - uink.y);
        pango_cairo_show_layout(cr, layout);
        fd = pango_font_description_from_string("Sans Bold 54");
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, num.c_str(), -1);
        cairo_move_to(cr, x0, 10);
        pango_cairo_show_layout(cr, layout);
    }

    const double colW = 62, x0 = W - pad - 3 * colW;
    for (int i = 0; i < 3; i++) {
        const HourW& h = w.hourly[i + 1];
        std::string hl, hic;
        weatherDescribe(h.code, h.day, hl, hic);
        const double cx = x0 + i * colW + colW / 2;
        text("Sans 10", deg(h.temp), cx, 126, 0.95, false, true);
        drawIcon(cr, hic, cx - 13, 146, 26, Color{1, 1, 1}, true);
        text("Sans 9", weatherHourLabel(app, h.hour), cx, 178, 0.85, false, true);
    }
    text("Sans 8", weatherSourceLine(), pad, H - 28, 0.6);
    g_object_unref(layout);
}

void weatherApply(App& app) {
    WeatherW& X = app.weather;
    WeatherData fresh;
    uint64_t v = 0;
    const bool ok = weatherSvcGet(fresh, &v);
    if (v == X.seen) return;
    X.seen = v;
    if (ok) X.data = fresh;
    if (X.open()) deskDraw(app, X);
}

void weatherToggled(App& app, bool open) {
    weatherSvcWant(app, open || (app.deskClock.open() && app.cfg.clockStyle == "daily") || inBarLayout(app, "weather"));
    if (open) weatherApply(app);
}

void weatherInit(WeatherW& X) {
    X.name = "weather";
    X.resizable = false;
    X.lightHandle = true;
    X.w = 360;
    X.h = 212;
    X.minW = X.maxW = 360;
    X.minH = X.maxH = 212;
    X.paint = weatherPaint;
    X.onToggle = weatherToggled;
}

void deskInit(App& app) {
    todoInit(app.todo);
    weatherInit(app.weather);
    mediaWInit(app.mediaW);
    resWInit(app.resW);
    calWInit(app.calW);
    clockWInit(app.deskClock);
    userWInit(app.userW);
    imageWInit(app.imageW);
    for (DeskWidget* d : deskList(app)) d->app = &app;
}
