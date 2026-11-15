
constexpr int kCalPrev = 1, kCalNext = 2, kCalToday = 3, kCalDay = 100;

int calTodayKey() {
    const time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

void calGoToMonth(CalW& X, int year, int month) {
    X.slide = (year * 12 + month) >= (X.year * 12 + X.month) ? 1 : -1;
    X.year = year;
    X.month = month;
    X.slideStart = std::chrono::steady_clock::now();
}

void calShift(App& app, int delta) {
    CalW& X = app.calW;
    int m = X.month + delta, y = X.year;
    while (m < 0) { m += 12; y--; }
    while (m > 11) { m -= 12; y++; }
    calGoToMonth(X, y, m);
    deskDraw(app, X);
}

void calPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    CalW& X = static_cast<CalW&>(d);
    static const char* kMonths[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    static const char* kDays[7] = {"S", "M", "T", "W", "T", "F", "S"};
    roundedRect(cr, 0, 0, W, H, 32);
    setSource(cr, app.color("surface_container"), 0.94);
    cairo_fill(cr);

    const double pad = std::clamp(W * 0.06, 16.0, 24.0);
    const int today = calTodayKey();
    const int weekStart = app.cfg.calendarMonday ? 1 : 0;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const std::string& s, double px, bool bold, double x, double y, const Color& c, double alpha, double boxW = 0, int align = 0) {
        PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, s.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, c, alpha);
        cairo_move_to(cr, align == 1 ? x + (boxW - tw) / 2 : x, y - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        return tw;
    };

    const double hy = pad + 26;
    text(std::to_string(X.year), 13, false, pad + 2, pad + 6, app.color("on_surface_variant"), 0.9);
    const int mw = text(kMonths[X.month], 26, true, pad, hy + 4, app.color("on_surface"), 1.0);
    const double bs = 38, bx2 = W - pad - bs, bx1 = bx2 - 8 - bs, by = hy - bs / 2;
    auto roundBtn = [&](int id, double bx, const char* icon) {
        d.hits.push_back({id, Rect{bx, by, bs, bs}});
        cairo_new_path(cr);
        cairo_arc(cr, bx + bs / 2, by + bs / 2, bs / 2, 0, 2 * M_PI);
        setSource(cr, app.color(d.hover == id ? "surface_container_highest" : "surface_container_high"), 0.95);
        cairo_fill(cr);
        drawIcon(cr, icon, bx + (bs - 20) / 2, by + (bs - 20) / 2, 20, app.color("on_surface"), false);
    };
    roundBtn(kCalPrev, bx1, "alt-arrow-left");
    roundBtn(kCalNext, bx2, "alt-arrow-right");
    const bool onCurrent = X.year * 100 + X.month == (today / 10000) * 100 + ((today / 100) % 100 - 1);
    const double pillX = bx1 - 10 - 62;
    if (!onCurrent && pillX > pad + mw + 10) {
        const double pw = 62, ph = 30, px = pillX, py = hy - ph / 2;
        d.hits.push_back({kCalToday, Rect{px, py, pw, ph}});
        roundedRect(cr, px, py, pw, ph, ph / 2);
        setSource(cr, app.color(d.hover == kCalToday ? "primary" : "primary_container"));
        cairo_fill(cr);
        text("Today", 13, true, px, hy, app.color(d.hover == kCalToday ? "on_primary" : "on_primary_container"), 1.0, pw, 1);
    }

    const double gx = pad, gw = W - 2 * pad, cellW = gw / 7.0;
    const double wy = hy + 42;
    for (int c = 0; c < 7; c++) text(kDays[(weekStart + c) % 7], 12, true, gx + c * cellW, wy, app.color("on_surface_variant"), 0.8, cellW, 1);

    const double gTop = wy + 18, gBottom = H - pad + 4, cellH = (gBottom - gTop) / 6.0;
    const double t = X.slide ? std::min(1.0, std::chrono::duration<double>(std::chrono::steady_clock::now() - X.slideStart).count() / 0.22) : 1.0;
    const double ease = 1 - std::pow(1 - t, 3);
    const double offX = X.slide * (1 - ease) * 26, fade = 0.25 + 0.75 * ease;
    cairo_save(cr);
    cairo_rectangle(cr, 0, gTop - 4, W, gBottom - gTop + 8);
    cairo_clip(cr);
    std::tm first{};
    first.tm_year = X.year - 1900;
    first.tm_mon = X.month;
    first.tm_mday = 1;
    first.tm_hour = 12;
    std::mktime(&first);
    const int offset = (first.tm_wday - weekStart + 7) % 7;
    const double r = std::min(cellW, cellH) / 2 - 3;
    for (int i = 0; i < 42; i++) {
        std::tm dt = first;
        dt.tm_mday = 1 + i - offset;
        std::mktime(&dt);
        const int key = (dt.tm_year + 1900) * 10000 + (dt.tm_mon + 1) * 100 + dt.tm_mday;
        const bool inMonth = dt.tm_mon == X.month, isToday = key == today;
        const bool sel = X.selY == dt.tm_year + 1900 && X.selM == dt.tm_mon && X.selD == dt.tm_mday;
        const double cx = gx + (i % 7) * cellW + cellW / 2 + offX, cy = gTop + (i / 7) * cellH + cellH / 2;
        const bool hot = d.hover == kCalDay + i;
        d.hits.push_back({kCalDay + i, Rect{gx + (i % 7) * cellW, gTop + (i / 7) * cellH, cellW, cellH}});
        if (isToday) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            setSource(cr, app.color("primary"), fade);
            cairo_fill(cr);
        } else if (hot) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            setSource(cr, app.color("on_surface"), 0.1 * fade);
            cairo_fill(cr);
        }
        if (sel && !isToday) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r - 1, 0, 2 * M_PI);
            setSource(cr, app.color("primary"), fade);
            cairo_set_line_width(cr, 2);
            cairo_stroke(cr);
        }
        const bool weekend = dt.tm_wday == 0 || dt.tm_wday == 6;
        const Color ink = isToday ? app.color("on_primary") : weekend && inMonth ? app.color("tertiary") : app.color("on_surface");
        text(std::to_string(dt.tm_mday), std::clamp(r * 0.9, 12.0, 18.0), isToday || sel, cx - cellW / 2, cy, ink, (inMonth ? 1.0 : 0.3) * fade, cellW, 1);
    }
    cairo_restore(cr);
    g_object_unref(layout);
}

void calClick(App& app, DeskWidget& d, int id) {
    CalW& X = static_cast<CalW&>(d);
    if (id == kCalPrev) calShift(app, -1);
    else if (id == kCalNext) calShift(app, 1);
    else if (id == kCalToday) {
        const int today = calTodayKey();
        calGoToMonth(X, today / 10000, (today / 100) % 100 - 1);
        deskDraw(app, X);
    } else if (id >= kCalDay && id < kCalDay + 42) {
        std::tm first{};
        first.tm_year = X.year - 1900;
        first.tm_mon = X.month;
        first.tm_mday = 1;
        first.tm_hour = 12;
        std::mktime(&first);
        const int offset = (first.tm_wday - (app.cfg.calendarMonday ? 1 : 0) + 7) % 7;
        std::tm dt = first;
        dt.tm_mday = 1 + (id - kCalDay) - offset;
        std::mktime(&dt);
        const bool same = X.selY == dt.tm_year + 1900 && X.selM == dt.tm_mon && X.selD == dt.tm_mday;
        if (same) X.selY = -1;
        else { X.selY = dt.tm_year + 1900; X.selM = dt.tm_mon; X.selD = dt.tm_mday; }
        if (dt.tm_mon != X.month) calGoToMonth(X, dt.tm_year + 1900, dt.tm_mon);
        deskDraw(app, X);
    }
}

void calScroll(App& app, DeskWidget&, int dir) { calShift(app, dir); }
int calCursor(const DeskWidget&, int id) { return id >= 1 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : -1; }

void calWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    CalW& X = app.calW;
    if (!X.open() || !X.configured) return;
    auto want = [&](int ms) { timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms); };
    if (X.slide) {
        deskDraw(app, X);
        if (std::chrono::duration<double>(now - X.slideStart).count() >= 0.22) { X.slide = 0; deskDraw(app, X); }
        else want(16);
    }
    const int key = calTodayKey();
    if (key != X.dayKey) {
        X.dayKey = key;
        deskDraw(app, X);
    }
    want(20000);
}

void calWToggled(App& app, bool open) {
    CalW& X = app.calW;
    if (!open) return;
    const int today = calTodayKey();
    X.year = today / 10000;
    X.month = (today / 100) % 100 - 1;
    X.slide = 0;
    X.dayKey = today;
    (void)app;
}

void calWInit(CalW& X) {
    X.name = "calendar";
    X.minW = 280; X.maxW = 560;
    X.minH = 300; X.maxH = 560;
    X.w = 360; X.h = 380;
    X.radius = 32;
    X.blurCard = true;
    X.paint = calPaint;
    X.click = calClick;
    X.DeskWidget::scroll = calScroll;
    X.DeskWidget::cursor = calCursor;
    X.onToggle = calWToggled;
}
