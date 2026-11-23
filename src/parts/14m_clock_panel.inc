
constexpr double kClkW = 388, kClkPad = 12, kClkHeroH = 188, kClkTabsH = 38, kClkBodyH = 290, kClkGap = 8, kClkRadius = 28, kClkRoom = 40;
constexpr double kClkH = kClkPad + kClkHeroH + kClkGap + kClkTabsH + kClkGap + kClkBodyH + kClkPad;
inline double clkCardH(const ClockPanel& P) { return P.cardH > 0 ? P.cardH : kClkH; }
constexpr int kClkSurfW = static_cast<int>(kClkW + 2 * kClkRoom), kClkSurfH = static_cast<int>(kClkH + 2 * kClkRoom);
constexpr int kClkMaxZones = 4, kClkMaxResults = 5;

enum ClkId { ClkTab = 1, ClkPrev = 10, ClkNext = 11, ClkToday = 12, ClkTimerGo = 20, ClkTimerReset = 21, ClkFab = 40, ClkBack = 41, ClkProfile = 45, ClkWxReload = 48, ClkTasks = 49, ClkStyle = 400, ClkDivSlider = 440, ClkLayout = 300, ClkLayoutRemove = 320, ClkLayoutPick = 340, ClkRemove = 50, ClkRow = 60, ClkZone = 200, ClkPreset = 30, ClkDay = 100 };
constexpr int kClkPresets[4] = {15, 25, 45, 60};

std::tm clkLocalTm(time_t t = std::time(nullptr)) {
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

int clkDayKey(const std::tm& tm) { return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday; }

bool clkZoneTm(const std::string& zone, std::tm& out) {
    if (zone.empty() || zone.find("..") != std::string::npos || !std::filesystem::exists("/usr/share/zoneinfo/" + zone)) return false;
    const char* old = std::getenv("TZ");
    const std::string keep = old ? old : "";
    setenv("TZ", zone.c_str(), 1);
    tzset();
    out = clkLocalTm();
    if (old) setenv("TZ", keep.c_str(), 1); else unsetenv("TZ");
    tzset();
    return true;
}

std::vector<std::string> clkZones(const Config& cfg) {
    std::vector<std::string> zones;
    std::stringstream ss(cfg.worldClocks);
    for (std::string z; std::getline(ss, z, ',') && static_cast<int>(zones.size()) < kClkMaxZones;) {
        const size_t a = z.find_first_not_of(" \t"), b = z.find_last_not_of(" \t");
        if (a != std::string::npos) zones.push_back(z.substr(a, b - a + 1));
    }
    return zones;
}

std::string clkCityName(std::string zone) {
    zone = zone.substr(zone.rfind('/') == std::string::npos ? 0 : zone.rfind('/') + 1);
    std::replace(zone.begin(), zone.end(), '_', ' ');
    return zone;
}

std::string clkFirstGlyph(const std::string& s) {  // the first character (UTF-8), for the weekday letters
    if (s.empty()) return "";
    size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n++;
    return s.substr(0, n);
}

std::string clkFormat(const char* fmt, const std::tm& tm) {
    char b[96];
    const size_t n = i18n::strftime(b, sizeof b, fmt, &tm);
    return std::string(b, n);
}

bool clkLongBreak(const ClockTimer& T) { return T.isBreak && T.cycle + 1 == T.cycles; }
int clkLap(const ClockTimer& T) { return clkLongBreak(T) ? T.longBreak : T.isBreak ? T.shortBreak : T.focus; }

double clkLeft(const ClockTimer& T, std::chrono::steady_clock::time_point now) {
    return T.running ? std::max(0.0, std::chrono::duration<double>(T.end - now).count()) : T.left;
}

void clkToggle(ClockTimer& T) {
    const auto now = std::chrono::steady_clock::now();
    if (T.running) { T.left = clkLeft(T, now); T.running = false; return; }
    T.running = true;
    T.end = now + std::chrono::milliseconds(static_cast<long>(T.left * 1000));
}

void clkReset(ClockTimer& T) {
    T.running = false;
    T.isBreak = false;
    T.cycle = 0;
    T.left = T.focus;
}

void clkSetFocus(ClockTimer& T, int minutes) {
    if (T.running) return;
    T.focus = minutes * 60;
    if (!T.isBreak) T.left = T.focus;
}

void clkAdvance(ClockTimer& T, std::chrono::steady_clock::time_point now) {
    T.isBreak = !T.isBreak;
    if (!T.isBreak) T.cycle = (T.cycle + 1) % T.cycles;
    T.left = clkLap(T);
    T.end = now + std::chrono::milliseconds(static_cast<long>(T.left * 1000));
    const char* phase = clkLongBreak(T) ? "Long break" : T.isBreak ? "Break" : "Focus";
    const std::string body = std::string(i18n::tr(phase)) + " · " + std::to_string(static_cast<int>(T.left) / 60) + " min";
    runDetached({"notify-send", "-a", "end4pC", "-i", "alarm-symbolic", "Pomodoro", body.c_str()});
}

struct ClkZoneInfo { std::string tz, name, country, hay; };

std::string clkLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::vector<ClkZoneInfo>& clkZoneDb() {
    static std::vector<ClkZoneInfo> db;
    static bool loaded = false;
    if (loaded) return db;
    loaded = true;
    std::map<std::string, std::string> countries;
    std::ifstream iso("/usr/share/zoneinfo/iso3166.tab");
    for (std::string line; std::getline(iso, line);) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) countries[line.substr(0, tab)] = line.substr(tab + 1);
    }
    std::ifstream in("/usr/share/zoneinfo/zone.tab");
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string cc, coords, tz;
        std::getline(ss, cc, '\t'); std::getline(ss, coords, '\t'); std::getline(ss, tz, '\t');
        if (tz.empty()) continue;
        ClkZoneInfo z{tz, clkCityName(tz), countries.count(cc) ? countries[cc] : cc, ""};
        z.hay = clkLower(z.name + " " + z.country + " " + tz);
        db.push_back(std::move(z));
    }
    return db;
}

std::vector<const ClkZoneInfo*> clkMatches(const std::string& query, const std::vector<std::string>& have) {
    std::vector<const ClkZoneInfo*> starts, inside;
    const std::string q = clkLower(query);
    if (q.empty()) return starts;
    for (const ClkZoneInfo& z : clkZoneDb()) {
        if (std::find(have.begin(), have.end(), z.tz) != have.end()) continue;
        const size_t at = z.hay.find(q);
        if (at == std::string::npos) continue;
        (clkLower(z.name).rfind(q, 0) == 0 ? starts : inside).push_back(&z);
    }
    starts.insert(starts.end(), inside.begin(), inside.end());
    if (static_cast<int>(starts.size()) > kClkMaxResults) starts.resize(kClkMaxResults);
    return starts;
}

void clkSaveZones(App& app, const std::vector<std::string>& zones) {
    std::string joined;
    for (const std::string& z : zones) joined += (joined.empty() ? "" : ",") + z;
    if (setConfig(app, "worldClocks", joined.empty() ? "-" : joined).empty()) applyConfig(app);
}

int clkText(cairo_t* cr, PangoLayout* layout, const std::string& s, double px, bool bold, double x, double cy, const Color& c, double alpha = 1.0, double boxW = 0, int align = 0, bool light = false) {
    PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : light ? "Sans Light" : "Sans");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, s.c_str(), static_cast<int>(s.size()));
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, c, alpha);
    cairo_move_to(cr, align == 1 ? x + (boxW - tw) / 2 : align == 2 ? x + boxW - tw : x, cy - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    return tw;
}
int clkTextWidth(PangoLayout* layout, const std::string& s, double px, bool bold) {
    PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, s.c_str(), static_cast<int>(s.size()));
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    return tw;
}

struct ClkCtx {
    App& app;
    ClockPanel& P;
    cairo_t* cr;
    PangoLayout* layout;

    int text(const std::string& s, double px, bool bold, double x, double cy, const Color& c, double alpha = 1.0, double boxW = 0, int align = 0) {
        return clkText(cr, layout, s, px, bold, x, cy, c, alpha, boxW, align);
    }
    int textRaw(const std::string& s, double px, bool bold) { return clkTextWidth(layout, s, px, bold); }
    int literal(const std::string& s, double px, bool bold, double x, double cy, const Color& c, double alpha = 1.0, double boxW = 0, int align = 0, bool light = false) {
        return clkText(cr, layout, s, px, bold, x, cy, c, alpha, boxW, align, light);
    }
    void hit(int id, double x, double y, double w, double h) { P.hits.push_back({id, Rect{x, y, w, h}}); }
    bool hot(int id) const { return P.hover == id; }
    void circleButton(int id, double x, double y, double d, const char* icon, bool filled = false) {
        hit(id, x, y, d, d);
        cairo_new_path(cr);
        cairo_arc(cr, x + d / 2, y + d / 2, d / 2, 0, 2 * M_PI);
        setSource(cr, app.color(hot(id) ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        drawIcon(cr, icon, x + (d - 20) / 2, y + (d - 20) / 2, 20, app.color("on_surface"), filled);
    }
};

std::string clkWeekChip(const std::tm& tm) {
    char b[8];
    std::strftime(b, sizeof b, "%V", &tm);
    return std::string(i18n::tr("Week")) + " " + std::to_string(std::atoi(b));
}

void clkHero(ClkCtx& X, double x, double y, const std::tm& tm) {
    cairo_t* cr = X.cr;
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, X.app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 150, y - 26, 230);
    setSource(cr, X.app.color("primary"), 0.2);
    cairo_fill(cr);
    cairo_restore(cr);

    const Color ink = X.app.color("on_primary_container");
    X.literal(clockTimeString(X.app, tm), 62, true, x + 18, y + 54, ink);
    X.literal(clkFormat("%A, %e %B", tm), 15, false, x + 20, y + 108, ink, 0.88);
    double cx = x + 18;
    for (const std::string& label : {clkWeekChip(tm), std::string(i18n::tr("Day")) + " " + std::to_string(tm.tm_yday + 1)}) {
        const int tw = X.textRaw(label, 12, true);
        roundedRect(cr, cx, y + h - 46, tw + 20, 28, 10);
        setSource(cr, ink, 0.14);
        cairo_fill(cr);
        X.literal(label, 12, true, cx + 10, y + h - 32, ink, 0.95);
        cx += tw + 28;
    }
}

void clkTabs(ClkCtx& X, double x, double y) {
    cairo_t* cr = X.cr;
    static const char* kClockNames[4] = {"Calendar", "World", "Pomodoro", ""};
    static const char* kWeatherNames[4] = {"Today", "Week", "", ""};
    static const char* kAiNames[4] = {"Claude Code", "Codex", "", ""};
    static const char* kResNames[4] = {"CPU", "Memory", "Disk", "Network"};
    const char* const* kNames = X.P.kind == 1 ? kWeatherNames : X.P.kind == 2 ? kAiNames : X.P.kind == 3 ? kResNames : kClockNames;
    const int count = X.P.kind == 0 ? 3 : X.P.kind == 3 ? 4 : 2;
    const double w = kClkW - 2 * kClkPad, seg = (w - 8) / count;
    roundedRect(cr, x, y, w, kClkTabsH, kClkTabsH / 2);
    setSource(cr, X.app.color("surface_container_high"));
    cairo_fill(cr);
    roundedRect(cr, x + 4 + X.P.ta.drawPos(count, seg) * seg, y + 4, seg, kClkTabsH - 8, (kClkTabsH - 8) / 2.0);
    setSource(cr, X.app.color("primary"));
    cairo_fill(cr);
    for (int i = 0; i < count; i++) {
        const double sx = x + 4 + i * seg;
        X.hit(ClkTab + i, sx, y + 4, seg, kClkTabsH - 8);
        const bool on = std::fabs(X.P.ta.pos - i) < 0.5;
        if (!on && X.hot(ClkTab + i)) {
            roundedRect(cr, sx, y + 4, seg, kClkTabsH - 8, (kClkTabsH - 8) / 2.0);
            setSource(cr, X.app.color("on_surface"), 0.08);
            cairo_fill(cr);
        }
        X.text(kNames[i], 13, true, sx, y + kClkTabsH / 2.0, on ? X.app.color("on_primary") : X.app.color("on_surface_variant"), 1.0, seg, 1);
    }
}

void clkCalendar(ClkCtx& X, double x, double y, double w, const std::tm& nowTm) {
    ClockPanel& P = X.P;
    cairo_t* cr = X.cr;
    const int today = clkDayKey(nowTm);
    std::tm first{};
    first.tm_year = P.year - 1900; first.tm_mon = P.month; first.tm_mday = 1; first.tm_hour = 12;
    std::mktime(&first);
    const std::string month = clkFormat("%B", first);
    X.literal(month.empty() ? "" : month, 19, true, x + 4, y + 14, X.app.color("on_surface"));
    X.literal(std::to_string(P.year), 12, false, x + 4, y + 33, X.app.color("on_surface_variant"), 0.9);
    const double bs = 34, bx2 = x + w - bs, bx1 = bx2 - 6 - bs;
    X.circleButton(ClkNext, bx2, y, bs, "alt-arrow-right");
    X.circleButton(ClkPrev, bx1, y, bs, "alt-arrow-left");
    if (P.year * 12 + P.month != (nowTm.tm_year + 1900) * 12 + nowTm.tm_mon) {
        const double pw = 58, ph = 28, px = bx1 - 8 - pw, py = y + (bs - ph) / 2;
        X.hit(ClkToday, px, py, pw, ph);
        roundedRect(cr, px, py, pw, ph, ph / 2);
        setSource(cr, X.app.color(X.hot(ClkToday) ? "primary" : "secondary_container"));
        cairo_fill(cr);
        X.text("Today", 12, true, px, py + ph / 2, X.app.color(X.hot(ClkToday) ? "on_primary" : "on_secondary_container"), 1.0, pw, 1);
    }
    const int weekStart = X.app.cfg.calendarMonday ? 1 : 0;
    const double wkW = 24, gx = x + wkW, cellW = (w - wkW) / 7.0, dowY = y + bs + 14, gTop = dowY + 14, cellH = (kClkBodyH - (gTop - y) - 2) / 6.0;
    for (int c = 0; c < 7; c++) {
        std::tm d{};
        d.tm_year = 123; d.tm_mon = 0; d.tm_mday = 1 + (weekStart + c) % 7; d.tm_hour = 12;
        std::mktime(&d);
        X.literal(clkFirstGlyph(clkFormat("%a", d)), 12, true, gx + c * cellW, dowY, X.app.color("on_surface_variant"), 0.85, cellW, 1);
    }
    const int offset = (first.tm_wday - weekStart + 7) % 7;
    const double r = std::min(cellW, cellH) / 2 - 2;
    for (int i = 0; i < 42; i++) {
        std::tm dt = first;
        dt.tm_mday = 1 + i - offset;
        std::mktime(&dt);
        const int key = clkDayKey(dt);
        const bool inMonth = dt.tm_mon == P.month, isToday = key == today;
        const bool sel = P.selY == dt.tm_year + 1900 && P.selM == dt.tm_mon && P.selD == dt.tm_mday;
        const double cx = gx + (i % 7) * cellW + cellW / 2, cy = gTop + (i / 7) * cellH + cellH / 2;
        if (i % 7 == 0) {
            std::tm th = dt;
            th.tm_mday += (4 - dt.tm_wday + 7) % 7;
            std::mktime(&th);
            char wk[8];
            std::strftime(wk, sizeof wk, "%V", &th);
            X.literal(std::to_string(std::atoi(wk)), 10, false, x, cy, X.app.color("on_surface_variant"), 0.55, wkW - 4, 1);
        }
        X.hit(ClkDay + i, gx + (i % 7) * cellW, gTop + (i / 7) * cellH, cellW, cellH);
        if (isToday || X.hot(ClkDay + i)) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
            if (isToday) setSource(cr, X.app.color("primary")); else setSource(cr, X.app.color("on_surface"), 0.1);
            cairo_fill(cr);
        }
        if (sel && !isToday) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, r - 1, 0, 2 * M_PI);
            setSource(cr, X.app.color("primary"));
            cairo_set_line_width(cr, 2);
            cairo_stroke(cr);
        }
        const bool weekend = dt.tm_wday == 0 || dt.tm_wday == 6;
        const Color ink = isToday ? X.app.color("on_primary") : weekend && inMonth ? X.app.color("tertiary") : X.app.color("on_surface");
        X.literal(std::to_string(dt.tm_mday), 14, isToday || sel, cx - cellW / 2, cy, ink, inMonth ? 1.0 : 0.3, cellW, 1);
    }
}

void clkGlyph(cairo_t* cr, double cx, double cy, double r, bool cross, const Color& c) {
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    if (cross) cairo_rotate(cr, M_PI / 4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 2.2);
    setSource(cr, c);
    cairo_move_to(cr, -r, 0); cairo_line_to(cr, r, 0);
    cairo_move_to(cr, 0, -r); cairo_line_to(cr, 0, r);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void clkAddZone(ClkCtx& X, double x, double y, double w) {
    ClockPanel& P = X.P;
    cairo_t* cr = X.cr;
    X.circleButton(ClkBack, x, y, 44, "alt-arrow-left");
    const double fx = x + 52, fw = w - 52, fh = 44;
    roundedRect(cr, fx, y, fw, fh, fh / 2);
    setSource(cr, X.app.color("surface_container_high"));
    cairo_fill(cr);
    drawIcon(cr, "magnifer", fx + 14, y + (fh - 20) / 2, 20, X.app.color("on_surface_variant"), false);
    if (P.query.empty()) X.text("City or country", 14, false, fx + 44, y + fh / 2, X.app.color("on_surface_variant"), 0.8);
    else {
        const int tw = X.literal(P.query, 15, false, fx + 44, y + fh / 2, X.app.color("on_surface"));
        cairo_set_line_width(cr, 1.6);
        setSource(cr, X.app.color("primary"));
        cairo_move_to(cr, fx + 45 + tw, y + 11);
        cairo_line_to(cr, fx + 45 + tw, y + fh - 11);
        cairo_stroke(cr);
    }
    const std::vector<std::string> have = clkZones(X.app.cfg);
    const auto found = clkMatches(P.query, have);
    P.pick = std::clamp(P.pick, 0, std::max(0, static_cast<int>(found.size()) - 1));
    const double rowH = 48, gap = 4, top = y + fh + 12;
    for (size_t i = 0; i < found.size(); i++) {
        const double ry = top + i * (rowH + gap);
        const bool on = static_cast<int>(i) == P.pick || X.hot(ClkZone + static_cast<int>(i));
        X.hit(ClkZone + static_cast<int>(i), x, ry, w, rowH);
        roundedRect(cr, x, ry, w, rowH, 16);
        setSource(cr, X.app.color(on ? "secondary_container" : "surface_container_high"));
        cairo_fill(cr);
        const Color ink = X.app.color(on ? "on_secondary_container" : "on_surface");
        X.literal(found[i]->name, 15, true, x + 16, ry + 18, ink);
        X.literal(found[i]->country, 12, false, x + 16, ry + 36, ink, 0.7);
        std::tm tm{};
        if (clkZoneTm(found[i]->tz, tm)) X.literal(clockTimeString(X.app, tm), 16, true, x, ry + rowH / 2, ink, 0.9, w - 16, 2);
    }
    if (P.query.empty()) X.text("Type a city or a country", 13, false, x, top + 50, X.app.color("on_surface_variant"), 0.8, w, 1);
    else if (found.empty()) X.text("No zone found", 13, false, x, top + 50, X.app.color("on_surface_variant"), 0.8, w, 1);
}

void clkWorld(ClkCtx& X, double x, double y, double w, const std::tm& nowTm) {
    cairo_t* cr = X.cr;
    if (X.P.adding) { clkAddZone(X, x, y, w); return; }
    struct Row { std::string name; std::tm tm; };
    std::vector<Row> rows;
    for (const std::string& z : clkZones(X.app.cfg)) {
        std::tm tm{};
        if (clkZoneTm(z, tm)) rows.push_back({clkCityName(z), tm});
    }
    const bool canAdd = static_cast<int>(rows.size()) < kClkMaxZones;
    const double fab = 52, fx = x + w - fab, fy = y + kClkBodyH - fab;
    if (canAdd) X.hit(ClkFab, fx, fy, fab, fab);
    if (rows.empty()) {
        roundedRect(cr, x, y, w, kClkBodyH, 24);
        setSource(cr, X.app.color("surface_container_high"));
        cairo_fill(cr);
        drawIcon(cr, "global", x + (w - 48) / 2, y + kClkBodyH / 2 - 50, 48, X.app.color("on_surface_variant"), false);
        X.text("Press + to add a city", 14, false, x, y + kClkBodyH / 2 + 24, X.app.color("on_surface_variant"), 0.9, w, 1);
    }
    const double gap = 6, n = std::max<size_t>(rows.size(), 1);
    const double rowH = (kClkBodyH - gap * (n - 1)) / n;
    const double timePx = std::clamp(rowH * 0.36, 22.0, 40.0), namePx = std::clamp(rowH * 0.22, 15.0, 20.0), subPx = std::clamp(rowH * 0.15, 11.0, 13.0);
    for (size_t i = 0; i < rows.size(); i++) {
        Row& row = rows[i];
        const double ry = y + i * (rowH + gap);
        const int idx = static_cast<int>(i);
        const bool over = X.hot(ClkRow + idx) || X.hot(ClkRemove + idx), last = i + 1 == rows.size();
        const std::string clock = clockTimeString(X.app, row.tm);
        const double rightPad = last && canAdd ? 16 + fab + 6 : 20;
        const double crossX = x + w - rightPad - X.textRaw(clock, timePx, true) - 30;
        X.hit(ClkRemove + idx, crossX - 17, ry + rowH / 2 - 17, 34, 34);
        X.hit(ClkRow + idx, x, ry, w, rowH);
        roundedRect(cr, x, ry, w, rowH, 22);
        setSource(cr, X.app.color(over ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        const Color ink = X.app.color("on_surface");
        const int dayDiff = clkDayKey(row.tm) == clkDayKey(nowTm) ? 0 : clkDayKey(row.tm) > clkDayKey(nowTm) ? 1 : -1;
        const long minutes = (row.tm.tm_gmtoff - nowTm.tm_gmtoff) / 60;
        const std::string rel = minutes == 0 ? std::string(i18n::tr("Same time")) : (minutes > 0 ? "+" : "-") + std::to_string(std::abs(minutes) / 60) + (std::abs(minutes) % 60 ? ":" + std::to_string(std::abs(minutes) % 60) : "") + " h";
        const std::string day = i18n::tr(dayDiff > 0 ? "Tomorrow" : dayDiff < 0 ? "Yesterday" : "Today");
        X.literal(row.name, namePx, true, x + 20, ry + rowH / 2 - subPx * 0.9, ink);
        X.literal(day + " · " + rel, subPx, false, x + 20, ry + rowH / 2 + namePx * 0.8, ink, 0.7);
        X.literal(clock, timePx, true, x, ry + rowH / 2, ink, 1.0, w - rightPad, 2);
        if (over) {
            const double cx = crossX, cy = ry + rowH / 2;
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, 14, 0, 2 * M_PI);
            setSource(cr, ink, X.hot(ClkRemove + idx) ? 0.18 : 0.1);
            cairo_fill(cr);
            clkGlyph(cr, cx, cy, 5, true, ink);
        }
    }
    if (canAdd) {
        roundedRect(cr, fx, fy, fab, fab, 16);
        setSource(cr, X.app.color(X.hot(ClkFab) ? "primary" : "primary_container"));
        cairo_fill(cr);
        clkGlyph(cr, fx + fab / 2, fy + fab / 2, 8, false, X.app.color(X.hot(ClkFab) ? "on_primary" : "on_primary_container"));
    }
}

void clkPomodoro(ClkCtx& X, double x, double y, double w) {
    cairo_t* cr = X.cr;
    ClockTimer& T = X.app.timer;
    const double left = clkLeft(T, std::chrono::steady_clock::now()), lap = clkLap(T), frac = lap > 0 ? std::clamp(left / lap, 0.0, 1.0) : 0;
    const bool brk = T.isBreak;
    const char* accent = brk ? "tertiary" : "primary";
    const double cx = x + w / 2, cy = y + 96, rad = 84;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 12);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
    setSource(cr, X.app.color("surface_container_highest"));
    cairo_stroke(cr);
    if (frac > 0.001) {
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, rad, -M_PI / 2, -M_PI / 2 + 2 * M_PI * frac);
        setSource(cr, X.app.color(accent));
        cairo_stroke(cr);
    }
    const int secs = static_cast<int>(std::ceil(left));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", secs / 60, secs % 60);
    X.literal(buf, 42, true, cx - rad, cy - 8, X.app.color("on_surface"), 1.0, 2 * rad, 1);
    X.text(clkLongBreak(T) ? "Long break" : brk ? "Break" : "Focus", 14, true, cx - rad, cy + 30, X.app.color(accent), 1.0, 2 * rad, 1);
    const double bd = 34, bx = cx + rad * 0.72 - bd / 2, by = cy + rad * 0.72 - bd / 2;
    cairo_new_path(cr);
    cairo_arc(cr, bx + bd / 2, by + bd / 2, bd / 2, 0, 2 * M_PI);
    setSource(cr, X.app.color("secondary_container"));
    cairo_fill(cr);
    X.literal(std::to_string(T.cycle + 1), 14, true, bx, by + bd / 2, X.app.color("on_secondary_container"), 1.0, bd, 1);

    const double chipW = 66, chipH = 30, chipGap = 8, chipsX = x + (w - (4 * chipW + 3 * chipGap)) / 2, chipY = y + 198;
    for (int i = 0; i < 4; i++) {
        const double px = chipsX + i * (chipW + chipGap);
        const bool on = T.focus == kClkPresets[i] * 60, locked = T.running;
        if (!locked) X.hit(ClkPreset + i, px, chipY, chipW, chipH);
        roundedRect(cr, px, chipY, chipW, chipH, chipH / 2);
        setSource(cr, X.app.color(on ? "secondary_container" : "surface_container_high"), locked && !on ? 0.5 : 1.0);
        cairo_fill(cr);
        if (X.hot(ClkPreset + i)) { roundedRect(cr, px, chipY, chipW, chipH, chipH / 2); setSource(cr, X.app.color("on_surface"), 0.08); cairo_fill(cr); }
        X.literal(std::to_string(kClkPresets[i]) + " min", 13, true, px, chipY + chipH / 2, X.app.color(on ? "on_secondary_container" : "on_surface"), locked && !on ? 0.45 : 1.0, chipW, 1);
    }
    const double byy = y + 238, bh = 46, goW = 140, rsW = 100, gap = 10, bx0 = x + (w - (goW + gap + rsW)) / 2;
    const bool fresh = !T.running && !T.isBreak && T.cycle == 0 && T.left >= T.focus - 0.5;
    X.hit(ClkTimerGo, bx0, byy, goW, bh);
    roundedRect(cr, bx0, byy, goW, bh, T.running ? 16 : bh / 2);
    const Color goBg = X.app.color(T.running ? (X.hot(ClkTimerGo) ? "surface_container_highest" : "secondary_container") : (X.hot(ClkTimerGo) ? "primary_container" : "primary"));
    setSource(cr, goBg);
    cairo_fill(cr);
    const Color onGo = X.app.color(T.running ? "on_secondary_container" : (X.hot(ClkTimerGo) ? "on_primary_container" : "on_primary"));
    X.text(T.running ? "Pause" : fresh ? "Start" : "Resume", 15, true, bx0, byy + bh / 2, onGo, 1.0, goW, 1);
    const bool canReset = T.running || !fresh;
    const double rx = bx0 + goW + gap;
    if (canReset) X.hit(ClkTimerReset, rx, byy, rsW, bh);
    roundedRect(cr, rx, byy, rsW, bh, bh / 2);
    setSource(cr, X.app.color(X.hot(ClkTimerReset) ? "surface_container_highest" : "error_container"), canReset ? 1.0 : 0.4);
    cairo_fill(cr);
    X.text("Reset", 15, true, rx, byy + bh / 2, X.app.color("on_error_container"), canReset ? 1.0 : 0.45, rsW, 1);
}

std::string clkDeg(double v) { return std::to_string(static_cast<int>(std::lround(v))) + "°"; }

std::string clkMinutesText(const App& app, int minutes) {
    std::tm tm{};
    tm.tm_hour = minutes / 60;
    tm.tm_min = minutes % 60;
    return clockTimeString(app, tm);
}

void clkWxReloadButton(ClkCtx& X, double x, double y, const Color& ink) {
    cairo_t* cr = X.cr;
    const double w = kClkW - 2 * kClkPad, d = 36, bx = x + w - 16 - d, by = y + kClkHeroH - 14 - d;
    X.hit(ClkWxReload, bx, by, d, d);
    cairo_new_path(cr);
    cairo_arc(cr, bx + d / 2, by + d / 2, d / 2, 0, 2 * M_PI);
    setSource(cr, ink, X.hot(ClkWxReload) ? 0.26 : 0.14);
    cairo_fill(cr);
    cairo_save(cr);
    cairo_translate(cr, bx + d / 2, by + d / 2);
    if (X.P.wxRefreshing) cairo_rotate(cr, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 6.0);
    drawIcon(cr, "refresh", -10, -10, 20, ink, false);
    cairo_restore(cr);
}

void clkWxHero(ClkCtx& X, double x, double y) {
    cairo_t* cr = X.cr;
    const WeatherData& wx = X.P.wx;
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, X.app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 196, y - 34, 236);
    setSource(cr, X.app.color("primary"), 0.2);
    cairo_fill(cr);
    const Color ink = X.app.color("on_primary_container");
    if (!wx.ok) {
        cairo_restore(cr);
        X.text("Loading…", 15, false, x + 20, y + h / 2, ink, 0.8);
        clkWxReloadButton(X, x, y, ink);
        return;
    }
    std::string label, icon;
    weatherDescribe(wx.code, wx.day, label, icon);
    drawIcon(cr, icon, x + w - 78 - 40, y + 84 - 40, 80, ink, true);
    cairo_restore(cr);
    cairo_push_group(cr);
    drawIcon(cr, "gps", x + 20, y + 26 - 9, 18, ink, false);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.92);
    X.literal(wx.city.empty() ? std::string(i18n::tr("Weather")) : wx.city, 15, true, x + 44, y + 26, ink, 0.92);
    {
        const int numW = X.literal(std::to_string(static_cast<int>(std::lround(wx.temp))), 76, true, x + 16, y + 80, ink, 1.0);
        X.literal("°", 76 * 0.52, true, x + 16 + numW + 2, y + 63, ink, 1.0);
    }
    const std::string chip = i18n::tr(label.c_str());
    const int tw = X.textRaw(chip, 13, true);
    roundedRect(cr, x + 20, y + 126, tw + 24, 28, 14);
    setSource(cr, ink, 0.14);
    cairo_fill(cr);
    X.literal(chip, 13, true, x + 32, y + 140, ink, 0.95);
    X.literal("H " + clkDeg(wx.hi) + "  ·  L " + clkDeg(wx.lo), 13, false, x + 22, y + 172, ink, 0.75);
    clkWxReloadButton(X, x, y, ink);
}

void clkWxCard(ClkCtx& X, double x, double y, double w, double h) {
    roundedRect(X.cr, x, y, w, h, 22);
    setSource(X.cr, X.app.color("surface_container_high"));
    cairo_fill(X.cr);
}

void clkWxToday(ClkCtx& X, double x, double y, double w, const std::tm& nowTm) {
    cairo_t* cr = X.cr;
    const WeatherData& wx = X.P.wx;
    if (!wx.ok) { X.text("Loading…", 14, false, x, y + kClkBodyH / 2, X.app.color("on_surface_variant"), 0.8, w, 1); return; }
    const double gap = 8, hourlyH = 120, tilesH = 78, sunH = kClkBodyH - hourlyH - tilesH - 2 * gap;
    clkWxCard(X, x, y, w, hourlyH);
    const double colW = w / 5;
    for (int i = 0; i < 5; i++) {
        const HourW& hr = wx.hourly[i];
        std::string label, icon;
        weatherDescribe(hr.code, hr.day, label, icon);
        const double cx = x + i * colW;
        X.text(weatherHourLabel(X.app, hr.hour).c_str(), 12, false, cx, y + 22, X.app.color("on_surface_variant"), 0.9, colW, 1);
        drawIcon(cr, icon, cx + (colW - 30) / 2, y + 40, 30, X.app.color(i == 0 ? "primary" : "on_surface"), true);
        X.literal(clkDeg(hr.temp), 17, true, cx, y + 94, X.app.color("on_surface"), 1.0, colW, 1);
    }
    const double ty = y + hourlyH + gap, tw = (w - 2 * gap) / 3;
    struct Tile { const char* icon; std::string value; const char* label; };
    const Tile tiles[3] = {{"waterdrop", wx.humidity >= 0 ? std::to_string(wx.humidity) + "%" : "–", "Humidity"},
                           {"wind", std::to_string(static_cast<int>(std::lround(wx.wind))) + " km/h", "Wind"},
                           {"thermometer", clkDeg(wx.feels), "Feels like"}};
    for (int i = 0; i < 3; i++) {
        const double tx = x + i * (tw + gap);
        clkWxCard(X, tx, ty, tw, tilesH);
        drawIcon(cr, tiles[i].icon, tx + (tw - 22) / 2, ty + 9, 22, X.app.color("primary"), false);
        X.literal(tiles[i].value, 16, true, tx, ty + 46, X.app.color("on_surface"), 1.0, tw, 1);
        X.text(tiles[i].label, 11, false, tx, ty + 65, X.app.color("on_surface_variant"), 0.9, tw, 1);
    }
    const double sy = ty + tilesH + gap;
    clkWxCard(X, x, sy, w, sunH);
    if (wx.sunrise < 0 || wx.sunset <= wx.sunrise) return;
    const double x0 = x + 28, x1 = x + w - 28, base = sy + sunH - 24, peak = 26;
    const auto at = [&](double t, double& px, double& py) { px = x0 + t * (x1 - x0); py = base - std::sin(M_PI * t) * peak; };
    const double nowMin = nowTm.tm_hour * 60 + nowTm.tm_min, frac = std::clamp((nowMin - wx.sunrise) / double(wx.sunset - wx.sunrise), 0.0, 1.0);
    const bool daytime = nowMin >= wx.sunrise && nowMin <= wx.sunset;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 2.5);
    const double dashes[2] = {0.1, 7};
    cairo_set_dash(cr, dashes, 2, 0);
    setSource(cr, X.app.color("on_surface_variant"), 0.55);
    for (int pass = 0; pass < 2; pass++) {
        const double upTo = pass == 0 ? 1.0 : (daytime ? frac : 0.0);
        if (pass == 1) { cairo_set_dash(cr, nullptr, 0, 0); cairo_set_line_width(cr, 3); setSource(cr, X.app.color("primary")); }
        if (upTo <= 0) continue;
        double px, py;
        at(0, px, py);
        cairo_new_path(cr);
        cairo_move_to(cr, px, py);
        for (int i = 1; i <= 40; i++) { at(upTo * i / 40.0, px, py); cairo_line_to(cr, px, py); }
        cairo_stroke(cr);
    }
    cairo_set_dash(cr, nullptr, 0, 0);
    if (daytime) {
        double px, py;
        at(frac, px, py);
        cairo_new_path(cr);
        cairo_arc(cr, px, py, 8, 0, 2 * M_PI);
        setSource(cr, X.app.color("tertiary"));
        cairo_fill(cr);
    }
    drawIcon(cr, "sunrise", x + 16, sy + sunH - 30, 18, X.app.color("on_surface_variant"), false);
    X.literal(clkMinutesText(X.app, wx.sunrise), 13, true, x + 38, sy + sunH - 20, X.app.color("on_surface"), 0.9);
    drawIcon(cr, "sunset", x + w - 34, sy + sunH - 30, 18, X.app.color("on_surface_variant"), false);
    X.literal(clkMinutesText(X.app, wx.sunset), 13, true, x, sy + sunH - 20, X.app.color("on_surface"), 0.9, w - 40, 2);
}

void clkWxWeek(ClkCtx& X, double x, double y, double w) {
    cairo_t* cr = X.cr;
    const WeatherData& wx = X.P.wx;
    if (!wx.ok || wx.nDays == 0) { X.text("Loading…", 14, false, x, y + kClkBodyH / 2, X.app.color("on_surface_variant"), 0.8, w, 1); return; }
    const int n = wx.nDays;
    const double gap = 5, rowH = (kClkBodyH - (n - 1) * gap) / n;
    double lo = wx.days[0].lo, hi = wx.days[0].hi;
    for (int i = 0; i < n; i++) { lo = std::min(lo, wx.days[i].lo); hi = std::max(hi, wx.days[i].hi); }
    const double span = std::max(1.0, hi - lo), barX = x + 196, barW = w - 196 - 66;
    for (int i = 0; i < n; i++) {
        const DayW& d = wx.days[i];
        const double ry = y + i * (rowH + gap), cy = ry + rowH / 2;
        clkWxCard(X, x, ry, w, rowH);
        std::tm tm{};
        tm.tm_year = d.year - 1900; tm.tm_mon = d.mon; tm.tm_mday = d.mday; tm.tm_hour = 12;
        std::mktime(&tm);
        std::string label, icon;
        weatherDescribe(d.code, true, label, icon);
        const Color ink = X.app.color("on_surface");
        if (i == 0) X.text("Today", 14, true, x + 16, cy, X.app.color("primary"));
        else { std::string name = clkFormat("%A", tm); if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))); X.literal(name, 14, false, x + 16, cy, ink); }
        drawIcon(cr, icon, x + 118, cy - 11, 22, ink, true);
        X.literal(clkDeg(d.lo), 14, false, x + 150, cy, X.app.color("on_surface_variant"), 0.9, 36, 2);
        roundedRect(cr, barX, cy - 3, barW, 6, 3);
        setSource(cr, ink, 0.12);
        cairo_fill(cr);
        const double a = (d.lo - lo) / span, b = (d.hi - lo) / span;
        roundedRect(cr, barX + a * barW, cy - 3, std::max(6.0, (b - a) * barW), 6, 3);
        setSource(cr, X.app.color(i == 0 ? "primary" : "secondary"));
        cairo_fill(cr);
        X.literal(clkDeg(d.hi), 14, true, barX + barW + 10, cy, ink);
    }
}

std::string clkDuration(long long seconds) {
    if (seconds < 0) seconds = 0;
    const long long h = seconds / 3600, m = (seconds % 3600) / 60;
    return h > 0 ? std::to_string(h) + " h " + std::to_string(m) + " min" : std::to_string(std::max(1LL, m)) + " min";
}

int clkAiKindOfTab(const App& app, int tab) {
    const std::vector<int> shown = aiShown(app);
    for (int k : shown) if (k == tab) return k;
    return shown.empty() ? 0 : shown[0];
}
int clkAiKind(const App& app, const ClockPanel& P) { return clkAiKindOfTab(app, P.tab); }

void clkCodexSummary(ClkCtx& X, double x, double y) {
    cairo_t* cr = X.cr;
    const auto& primary = X.app.barAi[1];
    const auto& weekly = X.app.codexWeekly;
    const double w = kClkW - 2 * kClkPad, track = (w - 52) / 2;
    const Color ink = X.app.color("on_primary_container");
    roundedRect(cr, x, y, w, kClkHeroH, 24);
    setSource(cr, X.app.color("primary_container"));
    cairo_fill(cr);
    X.literal(std::string("Codex · ") + i18n::tr("Account limit"), 14, true, x + 20, y + 26, ink);
    drawIcon(cr, "openai", x + w - 44, y + 14, 24, ink, false);

    const char* mode = X.app.cfg.aiCodexPercent == "available" ? "left" : "used";
    auto window = [&](const App::BarAi& usage, const char* label, double left) {
        const double fraction = codexFraction(X.app.cfg, usage);
        std::string text = std::string(i18n::tr(label)) + " ";
        text += usage.seen ? aiPercentText(fraction) + " " + i18n::tr(mode) : "—";
        X.literal(text, 16, true, left, y + 74, ink);
        roundedRect(cr, left, y + 96, track, 8, 4);
        setSource(cr, ink, 0.18);
        cairo_fill(cr);
        if (fraction > 0) {
            roundedRect(cr, left, y + 96, track * fraction, 8, 4);
            setSource(cr, usage.pct >= 0.9 ? X.app.color("error") : ink);
            cairo_fill(cr);
        }
        const long long reset = usage.resetsAt - static_cast<long long>(std::time(nullptr));
        if (usage.seen && usage.pct >= 0.7)
            X.text(usage.pct >= 0.9 ? "Near limit" : "High", 13, true, left, y + 124, ink);
        if (usage.seen && reset > 0)
            X.literal(std::string(i18n::tr("resets in")) + " " + clkDuration(reset), 12, false, left, y + 149, ink);
    };
    window(primary, "5h", x + 20);
    window(weekly, "Weekly", x + 32 + track);
}

void clkAiHero(ClkCtx& X, double x, double y, int kind) {
    if (kind == 1) { clkCodexSummary(X, x, y); return; }
    cairo_t* cr = X.cr;
    const App::BarAi& a = X.app.barAi[0];
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    const double used = std::clamp(a.pct, 0.0, 1.0);
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, X.app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 196, y - 34, 236);
    setSource(cr, X.app.color("primary"), 0.2);
    cairo_fill(cr);
    const Color ink = X.app.color("on_primary_container");
    drawIcon(cr, "claude", x + w - 78 - 32, y + 84 - 32, 64, ink, false);
    cairo_restore(cr);
    const std::string title = std::string("Claude Code · ") + i18n::tr("5 h window");
    X.literal(title, 14, true, x + 20, y + 26, ink, 0.92);
    const int tw = X.literal(std::to_string(static_cast<int>(std::lround(used * 100))), 76, false, x + 16, y + 80, ink, 1.0, 0, 0, true);
    X.literal("%", 30, false, x + 20 + tw, y + 98, ink, 0.85, 0, 0, true);
    const char* state = used >= 0.9 ? "Near limit" : used >= 0.7 ? "High" : "Normal";
    const std::string chip = i18n::tr(state);
    const int cw = X.textRaw(chip, 13, true);
    roundedRect(cr, x + 20, y + 126, cw + 24, 28, 14);
    setSource(cr, used >= 0.9 ? X.app.color("error") : ink, used >= 0.9 ? 0.3 : 0.14);
    cairo_fill(cr);
    X.literal(chip, 13, true, x + 32, y + 140, ink, 0.95);
    const long long left = a.resetsAt - static_cast<long long>(std::time(nullptr));
    std::string info = std::to_string(a.sessions) + " " + i18n::tr(a.sessions == 1 ? "session" : "sessions");
    if (a.resetsAt > 0 && left > 0) info += " · " + std::string(i18n::tr("resets in")) + " " + clkDuration(left);
    X.literal(info, 13, false, x + 22, y + 172, ink, 0.78);
}

void clkAiRing(ClkCtx& X, double cx, double cy, double r, double pct, const char* accent) {
    cairo_t* cr = X.cr;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 10);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    setSource(cr, X.app.color("surface_container_highest"));
    cairo_stroke(cr);
    if (pct > 0.004) {
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, -M_PI / 2, -M_PI / 2 + 2 * M_PI * pct);
        setSource(cr, X.app.color(accent));
        cairo_stroke(cr);
    }
    X.literal(std::to_string(static_cast<int>(std::lround(pct * 100))) + "%", 20, true, cx - r, cy, X.app.color("on_surface"), 1.0, 2 * r, 1);
}

void clkAiTile(ClkCtx& X, double x, double y, double w, double h, const char* label) {
    roundedRect(X.cr, x, y, w, h, 22);
    setSource(X.cr, X.app.color("surface_container_high"));
    cairo_fill(X.cr);
    X.text(label, 12, false, x + 16, y + 22, X.app.color("on_surface_variant"), 0.95);
}

void clkClaudeBody(ClkCtx& X, double x, double y, double w, double h) {
    cairo_t* cr = X.cr;
    const App::BarAi& a = X.app.barAi[0];
    const double gap = 8, tw = (w - gap) / 2, pct = std::clamp(a.pct, 0.0, 1.0);
    const char* accent = pct >= 0.9 ? "error" : "primary";
    const double th = (h - gap) / 2;
    clkAiTile(X, x, y, tw, th, "Window");
    clkAiRing(X, x + tw / 2, y + th / 2 + 10, std::min(th / 2 - 26, 46.0), pct, accent);
    clkAiTile(X, x + tw + gap, y, tw, th, "Resets in");
    const long long left = a.resetsAt - static_cast<long long>(std::time(nullptr));
    char clock[24];
    if (a.resetsAt > 0 && left > 0) std::snprintf(clock, sizeof clock, "%lld:%02lld:%02lld", left / 3600, (left % 3600) / 60, left % 60); else std::snprintf(clock, sizeof clock, "–");
    X.literal(clock, 30, false, x + tw + gap + 16, y + th / 2 + 8, X.app.color("on_surface"));
    if (left > 0 && a.resetsAt > 0) X.literal(clkDuration(left) + " " + i18n::tr("left"), 12, false, x + tw + gap + 16, y + th - 22, X.app.color("on_surface_variant"), 0.9);
    clkAiTile(X, x, y + th + gap, tw, th, "Open sessions");
    X.literal(std::to_string(a.sessions), 34, false, x + 16, y + th + gap + th / 2 + 6, X.app.color("on_surface"));
    for (int i = 0; i < 5; i++) {
        cairo_new_path(cr);
        cairo_arc(cr, x + 20 + i * 16, y + th + gap + th - 20, 5, 0, 2 * M_PI);
        setSource(cr, X.app.color(i < a.sessions ? "primary" : "surface_container_highest"));
        cairo_fill(cr);
    }
    clkAiTile(X, x + tw + gap, y + th + gap, tw, th, "Tokens used");
    const double tokens = a.pct * std::max(1, X.app.cfg.aiTokenLimit) * 1000.0;
    char used[24], limit[24];
    if (tokens >= 1e6) std::snprintf(used, sizeof used, "%.1f M", tokens / 1e6); else std::snprintf(used, sizeof used, "%.0fk", tokens / 1e3);
    const double lim = std::max(1, X.app.cfg.aiTokenLimit) * 1000.0;
    if (lim >= 1e6) std::snprintf(limit, sizeof limit, "%.1f M", lim / 1e6); else std::snprintf(limit, sizeof limit, "%.0fk", lim / 1e3);
    X.literal(used, 30, false, x + tw + gap + 16, y + th + gap + th / 2 + 6, X.app.color("on_surface"));
    X.literal(std::string(i18n::tr("of")) + " " + limit, 12, false, x + tw + gap + 16, y + th + gap + th - 22, X.app.color("on_surface_variant"), 0.9);
}

// The mounted disks of this machine: one per device (a btrfs with subvolumes counts once), local file systems only, no virtual ones.
std::vector<ClockPanel::Volume> clkReadVolumes() {
    static const char* const kTypes[] = {"ext4", "ext3", "ext2", "btrfs", "xfs", "f2fs", "ntfs", "ntfs3", "fuseblk", "vfat", "exfat", "zfs", "jfs", "reiserfs", "bcachefs"};
    std::map<std::string, std::string> byDevice;  // device -> shortest mount point
    std::ifstream f("/proc/mounts");
    for (std::string line; std::getline(f, line);) {
        std::istringstream ss(line);
        std::string dev, mp, type;
        ss >> dev >> mp >> type;
        bool ok = false;
        for (const char* t : kTypes) if (type == t) ok = true;
        if (!ok || dev.rfind("/dev/", 0) != 0) continue;
        if (mp.rfind("/boot", 0) == 0 || mp.rfind("/var/lib", 0) == 0 || mp.rfind("/snap", 0) == 0 || mp.rfind("/run/user", 0) == 0) continue;
        size_t at = 0;
        while ((at = mp.find("\\040", at)) != std::string::npos) mp.replace(at, 4, " ");  // (a space in a name is written like this)
        auto it = byDevice.find(dev);
        if (it == byDevice.end() || mp.size() < it->second.size()) byDevice[dev] = mp;
    }
    std::vector<ClockPanel::Volume> out;
    for (const auto& [dev, mp] : byDevice) {
        struct statvfs sv {};
        if (statvfs(mp.c_str(), &sv) != 0 || sv.f_blocks == 0) continue;
        ClockPanel::Volume v;
        v.totalGb = sv.f_blocks * double(sv.f_frsize) / 1073741824.0;
        v.usedGb = (sv.f_blocks - sv.f_bfree) * double(sv.f_frsize) / 1073741824.0;
        const bool removable = mp.rfind("/run/media/", 0) == 0 || mp.rfind("/media/", 0) == 0 || mp.rfind("/mnt/", 0) == 0;
        v.name = removable ? mp.substr(mp.rfind('/') + 1) : mp;
        if (v.name.empty()) v.name = mp;
        out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](const ClockPanel::Volume& a, const ClockPanel::Volume& b) { return a.name == "/" ? b.name != "/" : (b.name == "/" ? false : a.name < b.name); });
    return out;
}

void clkSampleResources(App& app) {
    ClockPanel& P = app.clockP;
    updateResources(app);
    {
        unsigned long long rx = 0, tx = 0;
        readNetBytes(rx, tx);
        const auto now = std::chrono::steady_clock::now();
        if (P.netAt != std::chrono::steady_clock::time_point{}) {
            const double dt = std::chrono::duration<double>(now - P.netAt).count();
            if (dt > 0.05) {
                P.netDown = rx >= P.netRx ? (rx - P.netRx) / dt : 0;
                P.netUp = tx >= P.netTx ? (tx - P.netTx) / dt : 0;
            }
        }
        P.netRx = rx;
        P.netTx = tx;
        P.netAt = now;
    }
    {
        std::ifstream f("/proc/stat");
        std::string line;
        std::vector<double> load;
        size_t n = 0;
        while (std::getline(f, line)) {
            if (line.compare(0, 3, "cpu") != 0 || line.size() < 4 || !std::isdigit(static_cast<unsigned char>(line[3]))) continue;
            std::istringstream ss(line);
            std::string name;
            unsigned long long u = 0, ni = 0, sy = 0, idle = 0, iow = 0, irq = 0, sirq = 0, steal = 0;
            ss >> name >> u >> ni >> sy >> idle >> iow >> irq >> sirq >> steal;
            const unsigned long long total = u + ni + sy + idle + iow + irq + sirq + steal, idl = idle + iow;
            if (P.coreTotal.size() <= n) { P.coreTotal.push_back(0); P.coreIdle.push_back(0); }
            load.push_back(P.coreTotal[n] && total > P.coreTotal[n] ? 1.0 - double(idl - P.coreIdle[n]) / double(total - P.coreTotal[n]) : 0.0);
            P.coreIdle[n] = idl;
            P.coreTotal[n] = total;
            n++;
        }
        P.cores = load;
    }
    {
        std::ifstream f("/proc/uptime");
        f >> P.uptimeS;
    }
    {
        int count = 0;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator("/proc", ec)) {
            const std::string name = e.path().filename().string();
            if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); })) count++;
        }
        P.procs = count;
    }
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        double memAvail = 0, swapTotal = 0, swapFree = 0;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string k;
            double v = 0;
            ss >> k >> v;
            if (k == "MemAvailable:") memAvail = v;
            else if (k == "SwapTotal:") swapTotal = v;
            else if (k == "SwapFree:") swapFree = v;
        }
        P.ramFreeGb = memAvail / 1048576.0;
        P.swapTotalGb = swapTotal / 1048576.0;
        P.swapUsedGb = (swapTotal - swapFree) / 1048576.0;
    }
    P.volumes = clkReadVolumes();
    P.netPeak = std::max({1e6, P.netPeak * 0.995, P.netDown, P.netUp});
}

void clkRate(double bytes, std::string& value, std::string& unit) {
    static const char* kUnits[4] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int u = 0;
    while (bytes >= 1000 && u < 3) { bytes /= 1000; u++; }
    char b[16];
    std::snprintf(b, sizeof b, u >= 2 && bytes < 100 ? "%.1f" : "%.0f", bytes);
    value = b;
    unit = kUnits[u];
}

std::string clkGb(double gb) {
    char b[24];
    std::snprintf(b, sizeof b, gb >= 100 ? "%.0f" : "%.1f", gb);
    return b;
}

void clkResHero(ClkCtx& X, double x, double y, int tab) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const ClockPanel& P = X.P;
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    double pct = 0;
    std::string title, number, unit, info;
    const char* icon = "cpu";
    bool network = false;
    if (tab == 0) {
        pct = app.res.cpu;
        title = "CPU"; number = std::to_string(static_cast<int>(std::lround(std::clamp(pct, 0.0, 1.0) * 100))); unit = "%";
        info = (app.res.tempC >= 0 ? std::to_string(static_cast<int>(std::lround(app.res.tempC))) + " °C · " : std::string()) + std::to_string(std::max<size_t>(1, P.cores.size())) + " " + i18n::tr("cores");
    } else if (tab == 1) {
        pct = app.res.ram; icon = "server-square";
        title = i18n::tr("Memory"); number = std::to_string(static_cast<int>(std::lround(std::clamp(pct, 0.0, 1.0) * 100))); unit = "%";
        info = clkGb(app.res.ramUsedGb) + " " + i18n::tr("of") + " " + clkGb(app.res.ramTotalGb) + " GB";
    } else if (tab == 2) {
        pct = app.res.disk; icon = "database";
        title = i18n::tr("Disk"); number = std::to_string(static_cast<int>(std::lround(std::clamp(pct, 0.0, 1.0) * 100))); unit = "%";
        info = clkGb(app.res.diskUsedGb) + " " + i18n::tr("of") + " " + clkGb(app.res.diskTotalGb) + " GB";
    } else {
        network = true; icon = "wi-fi-router";
        std::string upValue, upUnit;
        clkRate(P.netDown, number, unit);
        clkRate(P.netUp, upValue, upUnit);
        title = i18n::tr("Network");
        info = std::string(i18n::tr("Upload")) + " " + upValue + " " + upUnit;
    }
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 196, y - 34, 236);
    setSource(cr, app.color("primary"), 0.2);
    cairo_fill(cr);
    const Color ink = app.color("on_primary_container");
    drawIcon(cr, icon, x + w - 78 - 32, y + 84 - 32, 64, ink, false);
    cairo_restore(cr);
    {  // opens the Task Manager (a window of its own)
        const double d = 36, bx = x + w - 16 - d, by = y + h - 14 - d;
        X.hit(ClkTasks, bx, by, d, d);
        cairo_new_path(cr);
        cairo_arc(cr, bx + d / 2, by + d / 2, d / 2, 0, 2 * M_PI);
        setSource(cr, ink, X.hot(ClkTasks) ? 0.26 : 0.14);
        cairo_fill(cr);
        drawIcon(cr, "arrow-right-up", bx + d / 2 - 10, by + d / 2 - 10, 20, ink, false);
    }
    X.literal(title, 14, true, x + 20, y + 26, ink, 0.92);
    const int tw = X.literal(number, 76, false, x + 16, y + 80, ink, 1.0, 0, 0, true);
    X.literal(unit, unit == "%" ? 30 : 20, false, x + 22 + tw, y + 98, ink, 0.85, 0, 0, true);
    const bool critical = !network && pct >= 0.9;
    const char* state = network ? (P.netDown + P.netUp > 20e3 ? "Active" : "Idle") : critical ? "Critical" : pct >= 0.7 ? "High" : "Normal";
    const std::string chip = i18n::tr(state);
    const int cw = X.textRaw(chip, 13, true);
    roundedRect(cr, x + 20, y + 126, cw + 24, 28, 14);
    setSource(cr, critical ? app.color("error") : ink, critical ? 0.3 : 0.14);
    cairo_fill(cr);
    X.literal(chip, 13, true, x + 32, y + 140, ink, 0.95);
    X.literal(info, 13, false, x + 22, y + 172, ink, 0.78);
}

void clkResTile(ClkCtx& X, double x, double y, double w, double h, const char* label, const std::string& value, const std::string& unit) {
    clkAiTile(X, x, y, w, h, label);
    const int tw = X.literal(value, 38, false, x + 16, y + h - 34, X.app.color("on_surface"), 1.0, 0, 0, true);
    if (!unit.empty()) X.literal(unit, 15, false, x + 20 + tw, y + h - 30, X.app.color("on_surface_variant"), 0.9);
}

void clkResBar(ClkCtx& X, double x, double y, double w, double h, const char* label, double fraction, const char* accent) {
    clkAiTile(X, x, y, w, h, label);
    const double bx = x + 16, bw = w - 32, by = y + h - 40;
    roundedRect(X.cr, bx, by, bw, 16, 8);
    setSource(X.cr, X.app.color("surface_container_highest"));
    cairo_fill(X.cr);
    roundedRect(X.cr, bx, by, std::max(16.0, bw * std::clamp(fraction, 0.0, 1.0)), 16, 8);
    setSource(X.cr, X.app.color(accent));
    cairo_fill(X.cr);
}

void clkResBody(ClkCtx& X, double x, double y, double w, double h, int tab) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const ClockPanel& P = X.P;
    const double gap = 8, tw = (w - gap) / 2, th = (h - gap) / 2;
    const double x2 = x + tw + gap, y2 = y + th + gap;
    if (tab == 0) {
        clkResTile(X, x, y, tw, th, "Temperature", app.res.tempC >= 0 ? std::to_string(static_cast<int>(std::lround(app.res.tempC))) : "–", app.res.tempC >= 0 ? "°C" : "");
        clkAiTile(X, x2, y, tw, th, "Cores");
        const size_t n = std::max<size_t>(1, P.cores.size());
        const double areaX = x2 + 16, areaW = tw - 32, areaH = th - 52, bw = (areaW - (n - 1) * 4.0) / n;
        for (size_t i = 0; i < P.cores.size(); i++) {
            const double bh = std::max(5.0, areaH * std::clamp(P.cores[i], 0.0, 1.0));
            roundedRect(cr, areaX + i * (bw + 4), y + th - 16 - bh, bw, bh, std::min(bw / 2, 5.0));
            setSource(cr, app.color(P.cores[i] >= 0.9 ? "error" : "primary"));
            cairo_fill(cr);
        }
        const long long up = static_cast<long long>(P.uptimeS), days = up / 86400, hours = up % 86400 / 3600, mins = up % 3600 / 60;
        clkResTile(X, x, y2, tw, th, "Uptime", days > 0 ? std::to_string(days) + "d " + std::to_string(hours) + "h" : std::to_string(hours) + "h " + std::to_string(mins) + "m", "");
        clkResTile(X, x2, y2, tw, th, "Processes", std::to_string(P.procs), "");
    } else if (tab == 1) {
        clkResTile(X, x, y, tw, th, "Used", clkGb(app.res.ramUsedGb), "GB");
        clkResTile(X, x2, y, tw, th, "Free", clkGb(P.ramFreeGb), "GB");
        clkAiTile(X, x, y2, tw, th, "Swap");
        clkAiRing(X, x + tw / 2, y2 + th / 2 + 10, std::min(th / 2 - 26, 46.0), app.res.swap, app.res.swap >= 0.85 ? "error" : "tertiary");
        clkResTile(X, x2, y2, tw, th, "Swap used", clkGb(P.swapUsedGb), "GB");
    } else if (tab == 2 && P.volumes.size() > 1) {  // several disks: one row each, with how full it is
        const size_t n = std::min<size_t>(P.volumes.size(), 4);
        const double rowH = (h - gap * (n - 1)) / n;
        for (size_t i = 0; i < n; i++) {
            const ClockPanel::Volume& v = P.volumes[i];
            const double ry = y + i * (rowH + gap), fraction = v.totalGb > 0 ? std::clamp(v.usedGb / v.totalGb, 0.0, 1.0) : 0;
            roundedRect(cr, x, ry, w, rowH, 22);
            setSource(cr, app.color("surface_container_high"));
            cairo_fill(cr);
            X.literal(v.name, 15, true, x + 16, ry + 24, app.color("on_surface"), 1.0);
            const std::string amount = clkGb(v.usedGb) + " / " + clkGb(v.totalGb) + " GB";
            const int aw = X.textRaw(amount, 13, false);
            X.literal(amount, 13, false, x + w - 16 - aw, ry + 24, app.color("on_surface_variant"), 0.95);
            const double bx = x + 16, bw = w - 32, by = ry + rowH - 16 - 14;
            roundedRect(cr, bx, by, bw, 14, 7);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
            roundedRect(cr, bx, by, std::max(14.0, bw * fraction), 14, 7);
            setSource(cr, app.color(fraction >= 0.9 ? "error" : "primary"));
            cairo_fill(cr);
        }
    } else if (tab == 2) {
        clkResTile(X, x, y, tw, th, "Used", clkGb(app.res.diskUsedGb), "GB");
        clkResTile(X, x2, y, tw, th, "Free", clkGb(std::max(0.0, app.res.diskTotalGb - app.res.diskUsedGb)), "GB");
        clkResTile(X, x, y2, tw, th, "Total", clkGb(app.res.diskTotalGb), "GB");
        clkAiTile(X, x2, y2, tw, th, "Occupied");
        clkAiRing(X, x2 + tw / 2, y2 + th / 2 + 10, std::min(th / 2 - 26, 46.0), app.res.disk, app.res.disk >= 0.9 ? "error" : "primary");
    } else {
        std::string value, unit;
        clkRate(P.netDown, value, unit);
        clkResTile(X, x, y, tw, th, "Download", value, unit);
        clkRate(P.netUp, value, unit);
        clkResTile(X, x2, y, tw, th, "Upload", value, unit);
        clkResBar(X, x, y2, tw, th, "Download", P.netDown / P.netPeak, "primary");
        clkResBar(X, x2, y2, tw, th, "Upload", P.netUp / P.netPeak, "tertiary");
    }
}

std::string clkReadFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string v;
    std::getline(in, v);
    return v;
}

void clkSampleBattery(App& app) {
    ClockPanel& P = app.clockP;
    P.batWatts = P.batHours = P.batHealth = -1;
    P.batCycles = -1;
    P.profile = readPerfProfile();
    P.lastBatPct = app.batPct;
    P.lastBatChg = app.batCharging;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/power_supply", ec)) {
        if (clkReadFile(e.path() / "type") != "Battery" || clkReadFile(e.path() / "scope") == "Device") continue;
        const auto num = [&](const char* f) { const std::string v = clkReadFile(e.path() / f); return v.empty() ? -1.0 : std::atof(v.c_str()); };
        double now = num("energy_now"), full = num("energy_full"), design = num("energy_full_design"), rate = num("power_now");
        const double volt = num("voltage_now");
        if (now < 0 && volt > 0) {
            now = num("charge_now") * volt / 1e6; full = num("charge_full") * volt / 1e6; design = num("charge_full_design") * volt / 1e6;
            rate = num("current_now") * volt / 1e6;
        }
        if (rate >= 0) P.batWatts = rate / 1e6;
        if (full > 0 && design > 0) P.batHealth = std::clamp(full / design, 0.0, 1.0);
        if (rate > 0 && now >= 0) P.batHours = (app.batCharging ? std::max(0.0, full - now) : now) / rate;
        P.batCycles = static_cast<int>(num("cycle_count"));
        break;
    }
}

void clkSetProfile(App& app, const char* profile) {
    const std::string cmd = std::string("busctl --system set-property net.hadess.PowerProfiles /net/hadess/PowerProfiles net.hadess.PowerProfiles ActiveProfile s ") + profile + " 2>/dev/null";
    std::system(cmd.c_str());
    app.perfProfile = readPerfProfile();
    app.clockP.profile = app.perfProfile;
}

void clkBatHero(ClkCtx& X, double x, double y) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const ClockPanel& P = X.P;
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    const int pct = app.batPct;
    const bool low = !app.batCharging && pct <= app.cfg.batteryLow, full = app.batCharging && pct >= 100;
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 196, y - 34, 236);
    setSource(cr, app.color("primary"), 0.2);
    cairo_fill(cr);
    const Color ink = app.color("on_primary_container");
    const char* icon = app.batCharging ? "battery-charge" : pct >= 70 ? "battery-full" : pct >= 35 ? "battery-half" : "battery-low";
    drawIcon(cr, icon, x + w - 78 - 32, y + 84 - 32, 64, ink, false);
    cairo_restore(cr);
    X.text("Battery", 14, true, x + 20, y + 26, ink, 0.92);
    const int tw = X.literal(std::to_string(pct), 76, false, x + 16, y + 80, ink, 1.0, 0, 0, true);
    X.literal("%", 30, false, x + 22 + tw, y + 98, ink, 0.85, 0, 0, true);
    const char* state = full ? "Full" : app.batCharging ? "Charging" : low ? "Low" : "Discharging";
    const std::string chip = i18n::tr(state);
    const int cw = X.textRaw(chip, 13, true);
    roundedRect(cr, x + 20, y + 126, cw + 24, 28, 14);
    setSource(cr, low ? app.color("error") : ink, low ? 0.3 : 0.14);
    cairo_fill(cr);
    X.literal(chip, 13, true, x + 32, y + 140, ink, 0.95);
    std::string info;
    if (P.batHours > 0 && !full) {
        const long long mins = static_cast<long long>(P.batHours * 60 + 0.5);
        info = (mins >= 60 ? std::to_string(mins / 60) + " h " + std::to_string(mins % 60) + " min " : std::to_string(mins) + " min ") + i18n::tr(app.batCharging ? "to full" : "left");
    }
    if (P.batWatts >= 0) { char b[24]; std::snprintf(b, sizeof b, P.batWatts < 10 ? "%.1f W" : "%.0f W", P.batWatts); info += (info.empty() ? "" : " · ") + std::string(b); }
    if (!info.empty()) X.literal(info, 13, false, x + 22, y + 172, ink, 0.78);
}

void clkBatBody(ClkCtx& X, double x, double y, double w, double h) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const ClockPanel& P = X.P;
    const double gap = 8;
    struct Profile { const char* id; const char* icon; const char* label; };
    static const Profile kProfiles[3] = {{"power-saver", "leaf", "Power saver"}, {"balanced", "battery-half", "Balanced"}, {"performance", "bolt", "Performance"}};
    const bool haveProfiles = !P.profile.empty();
    double top = y;
    if (haveProfiles) {
        const double ch = 132;
        clkAiTile(X, x, y, w, ch, "Power profile");
        const double bw = (w - 24 - 2 * 6) / 3, by = y + 36, bh = ch - 36 - 12;
        for (int i = 0; i < 3; i++) {
            const double bx = x + 12 + i * (bw + 6);
            const bool on = P.profile == kProfiles[i].id;
            X.hit(ClkProfile + i, bx, by, bw, bh);
            roundedRect(cr, bx, by, bw, bh, 18);
            setSource(cr, X.app.color(on ? "primary" : X.hot(ClkProfile + i) ? "surface_container_highest" : "surface_container"));
            cairo_fill(cr);
            const Color ink = X.app.color(on ? "on_primary" : "on_surface");
            const double top = by + (bh - 50) / 2;
            drawIcon(cr, kProfiles[i].icon, bx + (bw - 28) / 2, top, 28, ink, false);
            X.text(kProfiles[i].label, 12, true, bx, top + 28 + 8 + 7, ink, 1.0, bw, 1);
        }
        top = y + ch + gap;
    }
    const double th = (y + h - top - gap) / 2, tw = (w - gap) / 2, x2 = x + tw + gap, y2 = top + th + gap;
    std::string left = "–", unit;
    if (P.batHours > 0) { const long long mins = static_cast<long long>(P.batHours * 60 + 0.5); char b[16]; std::snprintf(b, sizeof b, "%lld:%02lld", mins / 60, mins % 60); left = b; unit = "h"; }
    char watts[16], health[16];
    if (P.batWatts >= 0) std::snprintf(watts, sizeof watts, P.batWatts < 10 ? "%.1f" : "%.0f", P.batWatts); else std::snprintf(watts, sizeof watts, "–");
    if (P.batHealth >= 0) std::snprintf(health, sizeof health, "%.0f", P.batHealth * 100); else std::snprintf(health, sizeof health, "–");
    clkResTile(X, x, top, tw, th, "Remaining", left, unit);
    clkResTile(X, x2, top, tw, th, "Power", watts, P.batWatts >= 0 ? "W" : "");
    clkResTile(X, x, y2, tw, th, "Health", health, P.batHealth >= 0 ? "%" : "");
    clkResTile(X, x2, y2, tw, th, "Cycles", P.batCycles >= 0 ? std::to_string(P.batCycles) : "–", "");
    if (P.batHealth >= 0) {
        roundedRect(cr, x + 16, y2 + th - 14, tw - 32, 5, 2.5);
        setSource(cr, X.app.color("surface_container_highest"));
        cairo_fill(cr);
        roundedRect(cr, x + 16, y2 + th - 14, std::max(5.0, (tw - 32) * P.batHealth), 5, 2.5);
        setSource(cr, X.app.color("primary"));
        cairo_fill(cr);
    }
}

void clkKbdHero(ClkCtx& X, double x, double y) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const double w = kClkW - 2 * kClkPad, h = kClkHeroH;
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 196, y - 34, 236);
    setSource(cr, app.color("primary"), 0.2);
    cairo_fill(cr);
    const Color ink = app.color("on_primary_container");
    drawIcon(cr, "keyboard", x + w - 78 - 32, y + 84 - 32, 64, ink, false);
    cairo_restore(cr);
    X.text("Keyboard layout", 14, true, x + 20, y + 26, ink, 0.92);
    X.literal(kbdCodeText(app), 76, false, x + 16, y + 80, ink, 1.0, 0, 0, true);
    if (!app.kbd.list.empty()) {
        const std::string name = app.kbd.list[std::clamp(app.kbd.active, 0, static_cast<int>(app.kbd.list.size()) - 1)].name;
        const int cw = std::min(static_cast<int>(w) - 40, X.textRaw(name, 13, true) + 24);
        roundedRect(cr, x + 20, y + 126, cw, 28, 14);
        setSource(cr, ink, 0.14);
        cairo_fill(cr);
        X.literal(name, 13, true, x + 32, y + 140, ink, 0.95, cw - 24);
        const std::string count = std::string(i18n::tr("Layouts")) + ": " + std::to_string(app.kbd.list.size());
        X.literal(count, 13, false, x + 22, y + 172, ink, 0.78);
    }
}

constexpr double kKbdRowH = 56, kKbdRowGap = 6, kKbdAddH = 48;

constexpr int kKbdPickFit = 5;

// The looks the popup of a widget offers: with the group styles the workspaces only have Dots and M3.
std::vector<WidgetStyleOption> styleChoices(const App& app, const std::string& mod) {
    std::vector<WidgetStyleOption> out = widgetStyleChoices(mod, sideBar(app));
    if (groupSeparated(app.cfg)) return out;  // (Separated: every look of the widget)
    if (mod == "notifications" && groupActive(app.cfg))
        out.erase(std::remove_if(out.begin(), out.end(), [](const WidgetStyleOption& o) { return std::string(o.id) != "dot" && std::string(o.id) != "count"; }), out.end());
    if (mod == "media" && groupActive(app.cfg))
        out.erase(std::remove_if(out.begin(), out.end(), [](const WidgetStyleOption& o) { return std::string(o.id) != "progress" && std::string(o.id) != "plain"; }), out.end());
    if (mod == "visualizer" && groupActive(app.cfg))
        out.erase(std::remove_if(out.begin(), out.end(), [](const WidgetStyleOption& o) { return std::string(o.id) != "dots" && std::string(o.id) != "reflect"; }), out.end());
    if (mod == "resources" && groupActive(app.cfg))
        out.erase(std::remove_if(out.begin(), out.end(), [](const WidgetStyleOption& o) { return std::string(o.id) == "bars"; }), out.end());
    if (mod == "workspaces" && groupActive(app.cfg))
        out.erase(std::remove_if(out.begin(), out.end(), [](const WidgetStyleOption& o) { return std::string(o.id) != "gnome" && std::string(o.id) != "m3"; }), out.end());
    return out;
}
constexpr double kStyleHeadH = 64, kStyleChipH = 48, kStyleChipGap = 8;
constexpr double kDivSliderH = 58;
bool clkIsDividerPick(const ClockPanel& P) { return P.styleMod.rfind("divider#", 0) == 0; }
int clkDividerOf(const ClockPanel& P) { return std::atoi(P.styleMod.c_str() + 8); }
double clkStyleH(const App& app, const ClockPanel& P) {
    if (clkIsDividerPick(P)) return kClkPad + kStyleHeadH + kClkGap + 3 * (kStyleChipH + kStyleChipGap) + kDivSliderH + kClkPad - kStyleChipGap;
    const int n = static_cast<int>(styleChoices(app, P.styleMod).size()), rows = (n + 1) / 2;
    return kClkPad + kStyleHeadH + kClkGap + rows * (kStyleChipH + kStyleChipGap) - kStyleChipGap + kClkPad;
}

double clkWantedH(const App& app, const ClockPanel& P) {
    if (P.kind == 6) return clkStyleH(app, P);
    if (P.kind == 2 && clkAiKind(app, P) == 1)
        return 2 * kClkPad + kClkHeroH + (aiShown(app).size() > 1 ? kClkGap + kClkTabsH : 0);
    if (P.kind != 5) return 0;
    if (P.adding) {
        const int rows = std::clamp(static_cast<int>(kbdMatches(app, P.query).size()), 1, kKbdPickFit);
        return kClkPad + kClkHeroH + kClkGap + 44 + 12 + rows * 52 - 4 + kClkPad;
    }
    const int rows = std::min(static_cast<int>(app.kbd.list.size()), kKbdMaxLayouts);
    const bool canAdd = rows < kKbdMaxLayouts;
    return kClkPad + kClkHeroH + kClkGap + rows * (kKbdRowH + kKbdRowGap) - (canAdd ? 0 : kKbdRowGap) + (canAdd ? kKbdAddH : 0) + kClkPad;
}

void clkKbdPick(ClkCtx& X, double x, double y, double w) {
    ClockPanel& P = X.P;
    cairo_t* cr = X.cr;
    X.circleButton(ClkBack, x, y, 44, "alt-arrow-left");
    const double fx = x + 52, fw = w - 52, fh = 44;
    roundedRect(cr, fx, y, fw, fh, fh / 2);
    setSource(cr, X.app.color("surface_container_high"));
    cairo_fill(cr);
    drawIcon(cr, "magnifer", fx + 14, y + (fh - 20) / 2, 20, X.app.color("on_surface_variant"), false);
    if (P.query.empty()) X.text("Language or country", 14, false, fx + 44, y + fh / 2, X.app.color("on_surface_variant"), 0.8);
    else {
        const int tw = X.literal(P.query, 15, false, fx + 44, y + fh / 2, X.app.color("on_surface"));
        cairo_set_line_width(cr, 1.6);
        setSource(cr, X.app.color("primary"));
        cairo_move_to(cr, fx + 45 + tw, y + 11);
        cairo_line_to(cr, fx + 45 + tw, y + fh - 11);
        cairo_stroke(cr);
    }
    const auto found = kbdMatches(X.app, P.query);
    P.pick = std::clamp(P.pick, 0, std::max(0, static_cast<int>(found.size()) - 1));
    const double rowH = 48, gap = 4, top = y + fh + 12;
    for (int i = 0; i < std::min<int>(kKbdPickFit, static_cast<int>(found.size())); i++) {
        const double ry = top + i * (rowH + gap);
        const bool on = i == P.pick || X.hot(ClkLayoutPick + i);
        X.hit(ClkLayoutPick + i, x, ry, w, rowH);
        roundedRect(cr, x, ry, w, rowH, 16);
        setSource(cr, X.app.color(on ? "secondary_container" : "surface_container_high"));
        cairo_fill(cr);
        const Color ink = X.app.color(on ? "on_secondary_container" : "on_surface");
        std::string code = found[i]->code;
        for (char& ch : code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        X.literal(code, 14, true, x + 14, ry + rowH / 2, ink, 0.8, 44, 1);
        X.literal(found[i]->name, 15, false, x + 14 + 44 + 8, ry + rowH / 2, ink, 1.0, w - 14 - 44 - 8 - 12);
    }
    if (found.empty()) X.text("No layout found", 13, false, x, top + 50, X.app.color("on_surface_variant"), 0.8, w, 1);
}

void clkKbdBody(ClkCtx& X, double x, double y, double w, double h) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    if (X.P.adding) { clkKbdPick(X, x, y, w); return; }
    (void)h;
    const int total = static_cast<int>(app.kbd.list.size());
    const int shown = std::min(total, kKbdMaxLayouts);
    for (int i = 0; i < shown; i++) {
        const double ry = y + i * (kKbdRowH + kKbdRowGap);
        const bool on = i == app.kbd.active, over = X.hot(ClkLayout + i) || X.hot(ClkLayoutRemove + i), canRemove = total > 1;
        const double crossX = x + w - 14 - 12, crossY = ry + kKbdRowH / 2;
        if (canRemove) X.hit(ClkLayoutRemove + i, crossX - 17, crossY - 17, 34, 34);
        X.hit(ClkLayout + i, x, ry, w, kKbdRowH);
        roundedRect(cr, x, ry, w, kKbdRowH, 20);
        setSource(cr, app.color(on ? "primary" : over ? "surface_container_highest" : "surface_container"));
        cairo_fill(cr);
        const Color ink = app.color(on ? "on_primary" : "on_surface");
        std::string code = app.kbd.list[i].code;
        for (char& ch : code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + 14 + 20, ry + kKbdRowH / 2, 20, 0, 2 * M_PI);
        setSource(cr, on ? ink : app.color("secondary_container"), on ? 0.2 : 1.0);
        cairo_fill(cr);
        X.literal(code, code.size() > 3 ? 11 : 14, true, x + 14, ry + kKbdRowH / 2, on ? ink : app.color("on_secondary_container"), 1.0, 40, 1);
        X.literal(app.kbd.list[i].name, 14, on, x + 14 + 40 + 12, ry + kKbdRowH / 2, ink, 1.0, w - 14 - 40 - 12 - 52);
        if (over && canRemove) {
            cairo_new_path(cr);
            cairo_arc(cr, crossX, crossY, 14, 0, 2 * M_PI);
            setSource(cr, ink, X.hot(ClkLayoutRemove + i) ? 0.22 : 0.12);
            cairo_fill(cr);
            clkGlyph(cr, crossX, crossY, 5, true, ink);
        } else if (on) drawIcon(cr, "check-circle", x + w - 14 - 24, ry + (kKbdRowH - 24) / 2, 24, ink, true);
    }
    if (shown < kKbdMaxLayouts) {
        const double ay = y + shown * (kKbdRowH + kKbdRowGap);
        X.hit(ClkFab, x, ay, w, kKbdAddH);
        roundedRect(cr, x, ay, w, kKbdAddH, 20);
        const bool hot = X.hot(ClkFab);
        setSource(cr, app.color(hot ? "primary" : "primary_container"));
        cairo_fill(cr);
        const Color ink = app.color(hot ? "on_primary" : "on_primary_container");
        clkGlyph(cr, x + 14 + 20, ay + kKbdAddH / 2, 7, false, ink);
        X.text("Add a layout", 14, true, x + 14 + 40 + 12, ay + kKbdAddH / 2, ink);
    }
}

// The picker of one divider of the bar: its style (line, dot, space, / or \\) and how much air it has around it.
void clkDividerPaint(ClkCtx& X) {
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const int n = clkDividerOf(X.P);
    const std::string current = dividerStyleOf(app.cfg, n);
    const int spacing = dividerSpacingOf(app.cfg, n);
    const double w = kClkW - 2 * kClkPad;
    roundedRect(cr, kClkPad, kClkPad, w, kStyleHeadH, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill(cr);
    const Color ink = app.color("on_primary_container");
    constexpr double kBadge = 46;
    const double bx = kClkPad + 10, by = kClkPad + (kStyleHeadH - kBadge) / 2;
    shapePath(cr, kM3IconShape, bx, by, kBadge);
    setSource(cr, app.color("primary"));
    cairo_fill(cr);
    drawIcon(cr, "palette", bx + (kBadge - 24) / 2, by + (kBadge - 24) / 2, 24, app.color("on_primary"), true);
    X.text("Divider", 17, true, kClkPad + 68, kClkPad + 22, ink);
    X.text("Pick a look for this divider", 12, false, kClkPad + 68, kClkPad + 44, ink, 0.78);
    const double colW = (w - kStyleChipGap) / 2, top = kClkPad + kStyleHeadH + kClkGap;
    static const char* const kIds[5] = {"line", "dot", "space", "slash", "backslash"};
    static const char* const kLabels[5] = {"Line", "Dot", "Space", "/", "\\"};
    for (int i = 0; i < 5; i++) {
        const double cx = kClkPad + (i % 2) * (colW + kStyleChipGap), cy = top + (i / 2) * (kStyleChipH + kStyleChipGap);
        const int id = ClkStyle + i;
        const bool on = current == kIds[i], hot = X.hot(id);
        X.hit(id, cx, cy, colW, kStyleChipH);
        roundedRect(cr, cx, cy, colW, kStyleChipH, kStyleChipH / 2);
        setSource(cr, app.color(on ? "primary" : hot ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        const Color tc = app.color(on ? "on_primary" : "on_surface");
        if (on) drawIcon(cr, "check-circle", cx + 14, cy + (kStyleChipH - 22) / 2, 22, tc, true);
        X.text(kLabels[i], 14, on, cx + (on ? 44 : 20), cy + kStyleChipH / 2, tc);
    }
    const double sy = top + 3 * (kStyleChipH + kStyleChipGap) + 4;
    X.text("Spacing", 14, true, kClkPad + 8, sy + 10, app.color("on_surface"));
    const std::string val = std::to_string(spacing) + " px";
    const double vw = X.textRaw(val, 13, false);
    X.text(val, 13, false, kClkPad + w - 8 - vw, sy + 10, app.color("on_surface_variant"));
    const double tx = kClkPad + 8, tw = w - 16, ty = sy + 30;
    X.hit(ClkDivSlider, tx - 6, ty - 8, tw + 12, 22);
    roundedRect(cr, tx, ty - 2, tw, 4, 2);
    setSource(cr, app.color("surface_container_highest"));
    cairo_fill(cr);
    const double fx = tx + tw * std::clamp(spacing, 0, 24) / 24.0;
    roundedRect(cr, tx, ty - 2, fx - tx, 4, 2);
    setSource(cr, app.color("primary"));
    cairo_fill(cr);
    cairo_arc(cr, fx, ty, X.hot(ClkDivSlider) || X.P.sliding ? 8 : 7, 0, 2 * M_PI);
    setSource(cr, app.color("primary"));
    cairo_fill(cr);
}

void clkStylePaint(ClkCtx& X) {
    if (clkIsDividerPick(X.P)) { clkDividerPaint(X); return; }
    cairo_t* cr = X.cr;
    const App& app = X.app;
    const WidgetStyleSet* set = widgetStyleSet(X.P.styleMod);
    if (!set) return;
    const double w = kClkW - 2 * kClkPad;
    roundedRect(cr, kClkPad, kClkPad, w, kStyleHeadH, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill(cr);
    const Color ink = app.color("on_primary_container");
    constexpr double kBadge = 46;
    const double bx = kClkPad + 10, by = kClkPad + (kStyleHeadH - kBadge) / 2;
    shapePath(cr, kM3IconShape, bx, by, kBadge);
    setSource(cr, app.color("primary"));
    cairo_fill(cr);
    drawIcon(cr, "palette", bx + (kBadge - 24) / 2, by + (kBadge - 24) / 2, 24, app.color("on_primary"), true);
    X.text(set->title, 17, true, kClkPad + 68, kClkPad + 22, ink);
    X.text("Pick a look for this widget", 12, false, kClkPad + 68, kClkPad + 44, ink, 0.78);
    const std::string current = widgetStyle(app, set->mod);
    const double colW = (w - kStyleChipGap) / 2, top = kClkPad + kStyleHeadH + kClkGap;
    const std::vector<WidgetStyleOption> choices = styleChoices(app, set->mod);
    for (size_t i = 0; i < choices.size(); i++) {
        const double cx = kClkPad + (i % 2) * (colW + kStyleChipGap), cy = top + (i / 2) * (kStyleChipH + kStyleChipGap);
        const int id = ClkStyle + static_cast<int>(i);
        const bool on = current == choices[i].id, hot = X.hot(id);
        X.hit(id, cx, cy, colW, kStyleChipH);
        roundedRect(cr, cx, cy, colW, kStyleChipH, kStyleChipH / 2);
        setSource(cr, app.color(on ? "primary" : hot ? "surface_container_highest" : "surface_container_high"));
        cairo_fill(cr);
        const Color tc = app.color(on ? "on_primary" : "on_surface");
        if (on) drawIcon(cr, "check-circle", cx + 14, cy + (kStyleChipH - 22) / 2, 22, tc, true);
        X.text(choices[i].label, 14, on, cx + (on ? 44 : 20), cy + kStyleChipH / 2, tc);
    }
}

void clkPaintCard(ClkCtx& X) {
    ClockPanel& P = X.P;
    cairo_t* cr = X.cr;
    const std::tm tm = clkLocalTm();
    P.lastMin = tm.tm_min;
    roundedRect(cr, 0, 0, kClkW, clkCardH(P), kClkRadius);
    setSource(cr, X.app.color("surface"), X.app.cfg.barOpacity / 100.0);
    cairo_fill(cr);
    P.hits.clear();
    if (P.kind == 6) { clkStylePaint(X); return; }
    if (P.kind == 1) weatherSvcGet(P.wx, &P.wxSeen);
    const int aiKind = P.kind == 2 ? clkAiKind(X.app, P) : 0;
    auto heroFor = [&](int tab) {
        if (P.kind == 3) clkResHero(X, kClkPad, kClkPad, tab);
        else clkAiHero(X, kClkPad, kClkPad, clkAiKindOfTab(X.app, tab));
    };
    if (P.ta.fading && (P.kind == 2 || P.kind == 3)) {
        const size_t hitsBefore = P.hits.size();
        heroFor(P.ta.prevTab);
        P.hits.resize(hitsBefore);
        cairo_push_group(cr);
        heroFor(P.tab);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, P.ta.inAlpha());
    }
    else if (P.kind == 1) clkWxHero(X, kClkPad, kClkPad);
    else if (P.kind == 3) clkResHero(X, kClkPad, kClkPad, P.tab);
    else if (P.kind == 4) clkBatHero(X, kClkPad, kClkPad);
    else if (P.kind == 5) clkKbdHero(X, kClkPad, kClkPad);
    else if (P.kind == 2) clkAiHero(X, kClkPad, kClkPad, aiKind);
    else clkHero(X, kClkPad, kClkPad, tm);
    const double tabsY = kClkPad + kClkHeroH + kClkGap, bodyW = kClkW - 2 * kClkPad;
    const bool noTabs = P.kind == 4 || P.kind == 5 || (P.kind == 2 && aiShown(X.app).size() < 2);
    const double bodyY = noTabs ? tabsY : tabsY + kClkTabsH + kClkGap, bodyH = noTabs ? kClkBodyH + kClkTabsH + kClkGap : kClkBodyH;
    if (!noTabs) clkTabs(X, kClkPad, tabsY);
    if (P.kind == 2 && aiKind == 1) return;
    if (P.kind == 4) { clkBatBody(X, kClkPad, bodyY, bodyW, bodyH); return; }
    if (P.kind == 5) { clkKbdBody(X, kClkPad, bodyY, bodyW, bodyH); return; }
    auto bodyFor = [&](int tab) {
        if (P.kind == 2) {
            if (clkAiKindOfTab(X.app, tab) == 0) clkClaudeBody(X, kClkPad, bodyY, bodyW, bodyH);
            return;
        }
        if (P.kind == 3) { clkResBody(X, kClkPad, bodyY, bodyW, bodyH, tab); return; }
        cairo_save(cr);
        cairo_rectangle(cr, kClkPad - 2, bodyY - 2, bodyW + 4, kClkBodyH + 4);
        cairo_clip(cr);
        if (P.kind == 1) { if (tab == 0) clkWxToday(X, kClkPad, bodyY, bodyW, tm); else clkWxWeek(X, kClkPad, bodyY, bodyW); }
        else if (tab == 0) clkCalendar(X, kClkPad, bodyY, bodyW, tm);
        else if (tab == 1) clkWorld(X, kClkPad, bodyY, bodyW, tm);
        else clkPomodoro(X, kClkPad, bodyY, bodyW);
        cairo_restore(cr);
    };
    if (!P.ta.fading) { bodyFor(P.tab); return; }
    cairo_save(cr);
    cairo_rectangle(cr, kClkPad - 2, bodyY - 2, bodyW + 4, bodyH + 4);
    cairo_clip(cr);
    const size_t hitsBefore = P.hits.size();
    cairo_push_group(cr);
    cairo_translate(cr, P.ta.outDx(), 0);
    bodyFor(P.ta.prevTab);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, P.ta.outAlpha());
    P.hits.resize(hitsBefore);
    cairo_push_group(cr);
    cairo_translate(cr, P.ta.inDx(), 0);
    bodyFor(P.tab);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, P.ta.inAlpha());
    cairo_restore(cr);
}

void drawClockPanel(App& app) {
    ClockPanel& P = app.clockP;
    if (!P.open() || !P.configured) return;
    const double oldHeight = clkCardH(P);
    P.cardH = clkWantedH(app, P);
    if (!sideBar(app) && app.cfg.barPosition == "bottom") {
        P.cardY = static_cast<int>(kClkSurfH - kClkRoom - clkCardH(P));
        if (P.kind == 2) P.growY += clkCardH(P) - oldHeight;
    }
    const int W = kClkSurfW, H = kClkSurfH;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, P.scale.factor(), "end4pC-clock")) return;
    cairo_surface_t* surface = hf.surface;
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double rev = std::max(0.0, P.reveal);
    const double ox = P.cardX + P.growX, oy = P.cardY + P.growY;
    P.originX = P.cardX;
    P.originY = P.cardY;
    if (rev > 0.001) {
        popPaintGrown(cr, rev, ox, oy, P.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8), [&] {
            if (P.shadow && P.shadowForH != static_cast<int>(clkCardH(P))) { cairo_surface_destroy(P.shadow); P.shadow = nullptr; }
            if (!P.shadow) { P.shadow = makeShadowMask(kClkW, clkCardH(P), kClkRadius); P.shadowForH = static_cast<int>(clkCardH(P)); }
            setSource(cr, app.color("shadow"), 0.3);
            cairo_mask_surface(cr, P.shadow, P.cardX - kLaunShadow, P.cardY - kLaunShadow + 2);
            cairo_save(cr);
            cairo_translate(cr, P.cardX, P.cardY);
            PangoLayout* layout = pango_cairo_create_layout(cr);
            ClkCtx X{app, P, cr, layout};
            clkPaintCard(X);
            g_object_unref(layout);
            cairo_restore(cr);
        });
    }

    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, P.surface, P.blur, W, H, [&](cairo_t* m) {
        if (rev <= 0.001) return;
        popPaintGrown(m, rev, ox, oy, 1.0, [&] {
            cairo_translate(m, P.cardX, P.cardY);
            roundedRect(m, 0, 0, kClkW, clkCardH(P), kClkRadius);
            cairo_fill(m);
        });
    });
    hidpi::present(P.surface, buffer, hf, P.scale);
}

void closeClockPanel(App& app) {
    ClockPanel& P = app.clockP;
    if (!P.open()) return;
    if (P.layer) zwlr_layer_surface_v1_destroy(P.layer);
    releaseBlurSurface(P.blur);
    hidpi::detach(P.scale);
    wl_surface_destroy(P.surface);
    if (P.shadow) cairo_surface_destroy(P.shadow);
    const bool hadWeather = P.kind == 1;
    P = ClockPanel{};
    if (hadWeather) barModulesSync(app);
    app.onClockP = false;
    app.repeatKey = 0;
    updateCursor(app);
    malloc_trim(0);
}

void onClockConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->clockP.configured) app->clockP.animStart = std::chrono::steady_clock::now();
    app->clockP.configured = true;
    drawClockPanel(*app);
}
void onClockClosed(void* d, zwlr_layer_surface_v1*) { closeClockPanel(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kClockListener = {onClockConfigure, onClockClosed};

int clkResTab(int resKind) { return resKind == 0 || resKind == 4 ? 1 : resKind == 3 ? 2 : 0; }

void clkSelectTab(ClockPanel& P, int tab) {
    if (tab == P.tab) return;
    if (P.configured) P.ta.select(tab);
    else P.ta.snap(tab);
    P.tab = tab;
}

bool clkIpcTab(App& app, const std::string& a) {
    if (a.rfind("tab ", 0) != 0) return false;
    ClockPanel& P = app.clockP;
    if (P.open() && P.phase < 2) {
        clkSelectTab(P, std::max(0, std::atoi(a.c_str() + 4)));
        P.adding = false;
        P.query.clear();
        drawClockPanel(app);
    }
    return true;
}

double clkWidgetCentre(const App& app, const Bar& bar, int kind, int provider = 0) {
    const double margin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    if (kind == 6) return margin + (sideBar(app) ? app.pointerY : app.pointerX);
    if (sideBar(app)) {
        for (const SideItem& it : sideItems(bar))
            if (it.id == (kind == 1 ? HitWeather : kind == 2 ? kHitAi + provider : kind == 3 ? kHitRes + provider : kind == 4 ? kHitStatus + 3 : kind == 5 ? HitKeyboard : HitClock)) return margin + it.r.y + it.r.h / 2;
        return margin + bar.width / 2.0;
    }
    if (kind == 2) return margin + slotX(bar, "aiusage") + app.aiBarW / 2.0;
    if (kind == 4) return margin + slotX(bar, "battery") + batteryW(app) / 2.0;
    if (kind == 5) return margin + slotX(bar, "keyboard") + kbdBarW(app) / 2.0;
    if (kind == 3) {
        const double one = resOneW(app), val = resValW(app);
        double rx = slotX(bar, "resources");
        for (const ResItem& ri : resItems(app)) {
            if (ri.kind == provider) return margin + rx + (one + val) / 2;
            rx += one + val + 6;
        }
        return margin + slotX(bar, "resources");
    }
    if (kind == 0 && !hasSlot(bar, "clock") && hasSlot(bar, "island")) return margin + slotX(bar, "island") + islandW(app) - kIslandPadIn - kIslandEndInset - app.islandTimeW / 2.0;
    return kind == 1 ? margin + slotX(bar, "weather") + app.weatherBarW / 2.0 : margin + clockX(bar) + app.clockW / 2.0;
}

void openClockPanel(App& app, Bar* bar, int kind = 0, int provider = 0, const std::string& styleMod = "") {
    ClockPanel& P = app.clockP;
    if (P.open() || !app.layerShell) return;
    if (!bar) bar = mainBar(app);
    if (!bar) return;
    closeSelector(app);
    closeLauncher(app);
    closeNotifPanel(app);
    closeMediaPanel(app);
    P = ClockPanel{};
    P.kind = kind;
    P.styleMod = styleMod;
    P.tab = kind == 2 ? provider : kind == 3 ? clkResTab(provider) : 0;
    P.cardH = clkWantedH(app, P);
    P.ta.snap(P.tab);
    const std::tm now = clkLocalTm();
    P.year = now.tm_year + 1900;
    P.month = now.tm_mon;
    const bool vert = sideBar(app), atBottom = app.cfg.barPosition == "bottom", atRight = app.cfg.barPosition == "right";
    const double margin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    const double screenLen = bar->width + 2.0 * margin, centre = clkWidgetCentre(app, *bar, kind, provider);
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    P.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, P.scale, P.surface, [&app] { drawClockPanel(app); });
    P.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, P.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-clock");
    const double inset = app.cfg.frame + 12, room = kClkRoom;
    const double along = kClkW;
    if (vert) {
        const double along2 = clkCardH(P);
        // Leave room for the taller Claude panel when both providers can be selected.
        const double maxHeight = kind == 2 && aiShown(app).size() > 1 ? kClkH : along2;
        const double top = std::clamp(centre - along2 / 2, inset, std::max(inset, screenLen - inset - maxHeight));
        P.anchor = std::clamp(centre - top, 36.0, along2 - 36.0);
        P.cardX = static_cast<int>(room);
        P.cardY = static_cast<int>(room);
        zwlr_layer_surface_v1_set_anchor(P.layer, kT | (atRight ? kR : kL));
        const int side = std::max(0, sideCardEdge(app) - static_cast<int>(room)), topMargin = static_cast<int>(top - room);
        zwlr_layer_surface_v1_set_margin(P.layer, topMargin, atRight ? side : 0, 0, atRight ? 0 : side);
    } else {
        const double left = std::clamp(centre - along / 2, inset, std::max(inset, screenLen - inset - along));
        P.anchor = std::clamp(centre - left, 36.0, along - 36.0);
        const int surfLeft = std::max(0, static_cast<int>(left - room));
        P.cardX = static_cast<int>(left) - surfLeft;
        P.cardY = static_cast<int>(room);
        int edge = app.cfg.frame + kBarHeight + 12;
        if (app.cfg.barStyle == "float" || app.cfg.barStyle == "island") edge += kBarGap;
        const int m = std::max(0, edge - static_cast<int>(room));
        zwlr_layer_surface_v1_set_anchor(P.layer, (atBottom ? kB : kT) | kL);
        zwlr_layer_surface_v1_set_margin(P.layer, atBottom ? 0 : m, 0, atBottom ? m : 0, surfLeft);
    }
    const double widgetOffset = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0) + barThickness(app) / 2.0;
    const double cardOffset = vert ? sideCardEdge(app) : app.cfg.frame + kBarHeight + 12 + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    const double reach = std::max(8.0, cardOffset - widgetOffset);
    if (vert) { P.growX = atRight ? kClkW + reach : -reach; P.growY = P.anchor; }
    else { P.growX = P.anchor; P.growY = atBottom ? clkCardH(P) + reach : -reach; }
    P.phase = 1;
    zwlr_layer_surface_v1_set_size(P.layer, kClkSurfW, kClkSurfH);
    zwlr_layer_surface_v1_set_exclusive_zone(P.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(P.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(P.layer, &kClockListener, &app);
    wl_surface_commit(P.surface);
    if (kind == 1) weatherSvcWant(app, true);
    if (kind == 4) clkSampleBattery(app);
    if (kind == 3) clkSampleResources(app);
}

void retractClockPanel(App& app) {
    ClockPanel& P = app.clockP;
    if (!P.open() || P.phase >= 2) return;
    if (!P.configured) { closeClockPanel(app); return; }
    P.phase = 2;
    P.from = P.reveal;
    P.animStart = std::chrono::steady_clock::now();
    drawClockPanel(app);
}

void toggleStylePicker(App& app, Bar* bar, const std::string& mod) {
    ClockPanel& P = app.clockP;
    if (!widgetStyleSet(mod)) return;
    if (groupActive(app.cfg) && !groupSeparated(app.cfg) && !groupFreeWidget(app.cfg, mod) && mod != "workspaces" && mod != "resources" && mod != "visualizer" && mod != "notifications" && mod != "media") return;  // (in the group styles the cell gives the look; workspaces keep Dots / M3, resources all of theirs)
    if (P.open() && (P.kind != 6 || P.styleMod != mod)) closeClockPanel(app);
    if (!P.open()) { openClockPanel(app, bar, 6, 0, mod); return; }
    if (P.phase >= 2) { P.phase = 1; P.from = P.reveal; P.animStart = std::chrono::steady_clock::now(); }
    else retractClockPanel(app);
}

void toggleDividerPicker(App& app, Bar* bar, int n) {
    ClockPanel& P = app.clockP;
    const std::string mod = "divider#" + std::to_string(n);
    if (P.open() && (P.kind != 6 || P.styleMod != mod)) closeClockPanel(app);
    if (!P.open()) { openClockPanel(app, bar, 6, 0, mod); return; }
    if (P.phase >= 2) { P.phase = 1; P.from = P.reveal; P.animStart = std::chrono::steady_clock::now(); }
    else retractClockPanel(app);
}

void toggleClockPanel(App& app, Bar* bar = nullptr, int kind = 0, int provider = 0) {
    ClockPanel& P = app.clockP;
    if (kind == 2 && aiShown(app).empty()) return;
    if (kind == 3 && !resourcesInLayout(app)) return;
    if (kind == 5 && app.kbd.list.empty()) return;
    if (kind == 4 && !app.batPresent) return;
    if (P.open() && P.kind != kind) closeClockPanel(app);
    if (!P.open()) { openClockPanel(app, bar, kind, provider); return; }
    const int wanted = kind == 2 ? provider : kind == 3 ? clkResTab(provider) : P.tab;
    if ((kind == 2 || kind == 3) && P.phase < 2 && P.tab != wanted) { clkSelectTab(P, wanted); drawClockPanel(app); return; }
    if (P.phase >= 2) {
        P.phase = 1;
        P.from = P.reveal;
        P.animStart = std::chrono::steady_clock::now();
    } else retractClockPanel(app);
}

void clkAddPicked(App& app, int n) {
    ClockPanel& P = app.clockP;
    if (P.kind == 5) {
        const auto found = kbdMatches(app, P.query);
        if (n >= 0 && n < static_cast<int>(found.size())) kbdAdd(app, found[n]->code);
        P.adding = false;
        P.query.clear();
        return;
    }
    std::vector<std::string> zones = clkZones(app.cfg);
    const auto found = clkMatches(P.query, zones);
    if (n < 0 || n >= static_cast<int>(found.size()) || static_cast<int>(zones.size()) >= kClkMaxZones) return;
    zones.push_back(found[n]->tz);
    P.adding = false;
    P.query.clear();
    clkSaveZones(app, zones);
}

bool clockPanelKey(App& app, uint32_t key) {
    ClockPanel& P = app.clockP;
    if (!P.adding || !app.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, key + 8);
    const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool repeat = false;
    if (key == kKeyEsc) P.adding = false;
    else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) clkAddPicked(app, P.pick);
    else if (sym == XKB_KEY_Down) P.pick++;
    else if (sym == XKB_KEY_Up) P.pick = std::max(0, P.pick - 1);
    else if (sym == XKB_KEY_BackSpace) {
        if (ctrl) P.query.clear();
        else if (!P.query.empty()) P.query.erase(prevChar(P.query, P.query.size()));
        P.pick = 0;
        repeat = true;
    } else if (!ctrl && P.query.size() < 40) {
        char buf[16];
        const int n = xkb_state_key_get_utf8(app.xkbState, key + 8, buf, sizeof buf);
        if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f) { P.query.append(buf, n); P.pick = 0; }
    }
    drawClockPanel(app);
    return repeat;
}

int clkHitAt(const ClockPanel& P, double lx, double ly) {
    for (const auto& h : P.hits) if (h.second.contains(lx, ly)) return h.first;
    return 0;
}

void clkShiftMonth(ClockPanel& P, int delta) {
    int m = P.month + delta, y = P.year;
    while (m < 0) { m += 12; y--; }
    while (m > 11) { m -= 12; y++; }
    P.year = y;
    P.month = m;
}

// The spacing slider of the divider picker: the pointer (x in the card) is the value, from 0 to 24 px.
void clkSetDividerSpacing(App& app, ClockPanel& P, double lx) {
    const double w = kClkW - 2 * kClkPad, tx = kClkPad + 8, tw = w - 16;
    const int v = std::clamp(static_cast<int>(std::lround((lx - tx) / tw * 24.0)), 0, 24);
    const int n = clkDividerOf(P);
    const DividerLook own = dividerLookOf(app.cfg.dividerLooks, n);
    if (v == dividerSpacingOf(app.cfg, n)) return;
    if (setConfig(app, "dividerLook", std::to_string(n) + " " + (own.style.empty() ? std::string("-") : own.style) + " " + std::to_string(v)).empty()) { applyConfig(app); drawAll(app); }
}

void clockPanelMotion(App& app) {
    ClockPanel& P = app.clockP;
    if (P.sliding) { clkSetDividerSpacing(app, P, app.pointerX - P.originX); drawClockPanel(app); return; }
    const int h = clkHitAt(P, app.pointerX - P.originX, app.pointerY - P.originY);
    if (h == P.hover) return;
    P.hover = h;
    updateCursor(app);
    drawClockPanel(app);
}

void clockPanelClick(App& app) {
    ClockPanel& P = app.clockP;
    if (P.phase >= 2) return;
    const double lx = app.pointerX - P.originX, ly = app.pointerY - P.originY;
    if (!Rect{0, 0, kClkW, clkCardH(P)}.contains(lx, ly)) { retractClockPanel(app); return; }
    const int id = clkHitAt(P, lx, ly);
    if (id >= ClkTab && id < ClkTab + 4) { clkSelectTab(P, id - ClkTab); P.adding = false; P.query.clear(); }
    else if (id == ClkPrev) clkShiftMonth(P, -1);
    else if (id == ClkNext) clkShiftMonth(P, 1);
    else if (id == ClkToday) { const std::tm n = clkLocalTm(); P.year = n.tm_year + 1900; P.month = n.tm_mon; }
    else if (id == ClkFab) { P.adding = true; P.query.clear(); P.pick = 0; }
    else if (id == ClkBack) P.adding = false;
    else if (id >= ClkRemove && id < ClkRemove + kClkMaxZones) {
        std::vector<std::string> zones = clkZones(app.cfg);
        if (id - ClkRemove < static_cast<int>(zones.size())) zones.erase(zones.begin() + (id - ClkRemove));
        clkSaveZones(app, zones);
    }
    else if (id >= ClkZone && id < ClkZone + kClkMaxResults) clkAddPicked(app, id - ClkZone);
    else if (id == ClkTasks) {
        closeClockPanel(app);
        toggleTasks();
    }
    else if (id == ClkWxReload) {
        weatherSvcRefreshNow();
        WeatherData tmp;
        weatherSvcGet(tmp, &P.wxReqVersion);
        P.wxRefreshing = true;
        P.wxReqAt = std::chrono::steady_clock::now();
    }
    else if (clkIsDividerPick(P) && (id == ClkDivSlider || (id >= ClkStyle && id < ClkStyle + 5))) {
        const int n = clkDividerOf(P);
        const DividerLook own = dividerLookOf(app.cfg.dividerLooks, n);
        static const char* const kIds[5] = {"line", "dot", "space", "slash", "backslash"};
        std::string value;
        if (id == ClkDivSlider) {
            clkSetDividerSpacing(app, P, lx);
            P.sliding = true;
            drawClockPanel(app);
            return;
        }
        value = std::to_string(n) + " " + kIds[id - ClkStyle] + " " + (own.spacing < 0 ? std::string("-") : std::to_string(own.spacing));
        if (setConfig(app, "dividerLook", value).empty()) { applyConfig(app); drawAll(app); }
    }
    else if (id >= ClkStyle && id < ClkStyle + 32) {
        const std::vector<WidgetStyleOption> choices = styleChoices(app, P.styleMod);
        const size_t i = static_cast<size_t>(id - ClkStyle);
        if (i < choices.size() && setConfig(app, groupSeparated(app.cfg) ? "groupLook" : "widgetStyle", P.styleMod + "=" + choices[i].id).empty()) { applyConfig(app); drawAll(app); }
    }
    else if (id >= ClkLayout && id < ClkLayout + 16) { kbdSwitch(app, id - ClkLayout); drawAll(app); }
    else if (id >= ClkLayoutRemove && id < ClkLayoutRemove + 16) { kbdRemove(app, id - ClkLayoutRemove); drawAll(app); }
    else if (id >= ClkLayoutPick && id < ClkLayoutPick + 8) { clkAddPicked(app, id - ClkLayoutPick); drawAll(app); }
    else if (id >= ClkProfile && id < ClkProfile + 3) { static const char* kIds[3] = {"power-saver", "balanced", "performance"}; clkSetProfile(app, kIds[id - ClkProfile]); }
    else if (id == ClkTimerGo) clkToggle(app.timer);
    else if (id == ClkTimerReset) clkReset(app.timer);
    else if (id >= ClkPreset && id < ClkPreset + 4) clkSetFocus(app.timer, kClkPresets[id - ClkPreset]);
    else if (id >= ClkDay && id < ClkDay + 42) {
        std::tm first{};
        first.tm_year = P.year - 1900; first.tm_mon = P.month; first.tm_mday = 1; first.tm_hour = 12;
        std::mktime(&first);
        std::tm dt = first;
        dt.tm_mday = 1 + (id - ClkDay) - (first.tm_wday - (app.cfg.calendarMonday ? 1 : 0) + 7) % 7;
        std::mktime(&dt);
        const bool same = P.selY == dt.tm_year + 1900 && P.selM == dt.tm_mon && P.selD == dt.tm_mday;
        if (same) P.selY = -1; else { P.selY = dt.tm_year + 1900; P.selM = dt.tm_mon; P.selD = dt.tm_mday; }
        if (dt.tm_mon != P.month) { P.year = dt.tm_year + 1900; P.month = dt.tm_mon; }
    }
    drawClockPanel(app);
}

void clockPanelScroll(App& app, int dir) {
    ClockPanel& P = app.clockP;
    if (P.tab != 0) return;
    clkShiftMonth(P, dir);
    drawClockPanel(app);
}

void tickClockPanel(App& app, std::chrono::steady_clock::time_point now) {
    ClockTimer& T = app.timer;
    if (T.running && now >= T.end) clkAdvance(T, now);
    ClockPanel& P = app.clockP;
    if (!P.open() || !P.configured) return;
    bool redraw = false;
    if (P.phase == kPopIn || P.phase == kPopOut) redraw = true;
    if (popStep(P.phase, P.reveal, P.from, P.animStart, P.settleAt, now)) { closeClockPanel(app); return; }

    if (P.kind == 0 && P.tab == 2 && T.running) redraw = true;
    if (clkLocalTm().tm_min != P.lastMin) redraw = true;
    if ((P.kind == 2 || P.kind == 3) && clkLocalTm().tm_sec != P.lastSec) {
        P.lastSec = clkLocalTm().tm_sec;
        if (P.kind == 3) clkSampleResources(app);
        redraw = true;
    }
    if (P.kind == 4 && (app.batPct != P.lastBatPct || app.batCharging != P.lastBatChg || clkLocalTm().tm_min != P.lastMin)) { clkSampleBattery(app); redraw = true; }
    if (P.kind == 1) {
        WeatherData wx;
        uint64_t version = 0;
        weatherSvcGet(wx, &version);
        if (version != P.wxSeen) redraw = true;
        if (P.wxRefreshing) {
            if (version != P.wxReqVersion || std::chrono::steady_clock::now() - P.wxReqAt > std::chrono::seconds(10)) P.wxRefreshing = false;
            redraw = true;
        }
    }
    if (P.ta.moving()) {
        P.ta.stepNow(now);
        redraw = true;
    }
    if (redraw) drawClockPanel(app);
}

std::chrono::steady_clock::time_point clockWake(const App& app) {
    auto when = std::chrono::steady_clock::time_point::max();
    const auto now = std::chrono::steady_clock::now();
    if (app.timer.running) when = std::min(when, app.timer.end);
    if (app.clockP.open()) {
        if (app.clockP.ta.moving()) when = std::min(when, now + std::chrono::milliseconds(16));
        if (app.clockP.phase != kPopOpen) when = std::min(when, popWake(app.clockP.phase, app.clockP.settleAt, now));
        else if (app.clockP.kind == 1 && app.clockP.wxRefreshing) when = std::min(when, now + std::chrono::milliseconds(33));
        else if (app.clockP.kind == 0 && app.clockP.tab == 2 && app.timer.running) when = std::min(when, now + std::chrono::milliseconds(200));
        else if (app.clockP.kind == 2 || app.clockP.kind == 3) when = std::min(when, now + std::chrono::milliseconds(1030 - static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000)));
        else when = std::min(when, now + std::chrono::seconds(60 - clkLocalTm().tm_sec) + std::chrono::milliseconds(30));
    }
    return when;
}
