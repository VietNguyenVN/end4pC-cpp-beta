// Timers, stopwatch and reminders (a page of the Tools bar each). The shell keeps them (so they ring with this window closed): this page asks it with its commands
// (`timer add 5m Tea`...) and draws what it finds in timers.txt, in the state folder.
#include <sys/socket.h>
#include <sys/un.h>

namespace {
struct TpTimer { int id = 0; std::string label; double total = 0, remaining = 0; bool running = false, done = false; long long endAt = 0; };
struct TpReminder { int id = 0; std::string label; int hour = 0, minute = 0, repeat = 0, date = 0; bool enabled = true; long long lastFired = 0; };
struct TpState {
    std::vector<TpTimer> timers;
    std::vector<TpReminder> reminders;
    bool swRunning = false;
    double swAccum = 0;
    long long swStart = 0;
    std::vector<double> laps;
};

long long tpNow() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string tpShell(const std::string& cmd) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return "";
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    const std::string path = runtimeDir() + "/end4pC.sock";
    std::snprintf(a.sun_path, sizeof a.sun_path, "%s", path.c_str());
    std::string reply;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        timeval tv{1, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        (void)!write(fd, cmd.data(), cmd.size());
        shutdown(fd, SHUT_WR);
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) reply.append(buf, n);
    }
    close(fd);
    return reply;
}

TpState tpLoad() {
    TpState T;
    std::ifstream in(stateDir() + "/timers.txt");
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '|');) f.push_back(part);
        if (f.empty()) continue;
        if (f[0] == "T" && f.size() >= 8) {
            TpTimer t;
            t.id = std::atoi(f[1].c_str()); t.label = f[2]; t.total = std::atof(f[3].c_str()); t.remaining = std::atof(f[4].c_str());
            t.running = f[5] == "1"; t.done = f[6] == "1"; t.endAt = std::atoll(f[7].c_str());
            T.timers.push_back(t);
        } else if (f[0] == "R" && f.size() >= 7) {
            TpReminder r;
            r.id = std::atoi(f[1].c_str()); r.label = f[2]; r.hour = std::atoi(f[3].c_str()); r.minute = std::atoi(f[4].c_str());
            r.repeat = std::atoi(f[5].c_str()); r.enabled = f[6] == "1"; r.lastFired = std::atoll(f[7].c_str()); r.date = f.size() > 8 ? std::atoi(f[8].c_str()) : 0;
            T.reminders.push_back(r);
        } else if (f[0] == "S" && f.size() >= 4) {
            T.swRunning = f[1] == "1"; T.swAccum = std::atof(f[2].c_str()); T.swStart = std::atoll(f[3].c_str());
            if (f.size() > 4) { std::stringstream ls(f[4]); for (std::string lap; std::getline(ls, lap, ',');) if (!lap.empty()) T.laps.push_back(std::atof(lap.c_str())); }
        }
    }
    return T;
}

std::string tpClock(double secs, bool tenths = false) {
    const double clamped = std::max(0.0, secs);
    const long s = static_cast<long>(std::floor(clamped + (tenths ? 0 : 0.0001)));
    char b[40];
    if (tenths) std::snprintf(b, sizeof b, "%02ld:%02ld.%ld", s / 60, s % 60, static_cast<long>((clamped - std::floor(clamped)) * 10));
    else if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(b, sizeof b, "%02ld:%02ld", s / 60, s % 60);
    return b;
}

int tpDaysIn(int year, int month) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : d[month - 1];
}
int tpWeekday(int year, int month, int day) {  // 0 Monday ... 6 Sunday
    std::tm t{};
    t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day; t.tm_hour = 12;
    std::mktime(&t);
    return (t.tm_wday + 6) % 7;
}
int tpToday() {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}
std::string tpDateText(int date) {  // "Mon, Nov 17"
    if (!date) return "Today";
    std::tm t{};
    t.tm_year = date / 10000 - 1900; t.tm_mon = date / 100 % 100 - 1; t.tm_mday = date % 100; t.tm_hour = 12;
    std::mktime(&t);
    char b[40];
    std::strftime(b, sizeof b, "%a, %b %d", &t);
    return b;
}
// When a reminder rings next, as the card says it: "in 25 min", "in 3 h 10 min", "Mon, Nov 17".
std::string tpUntil(const TpReminder& r) {
    if (!r.enabled) return "";
    const std::time_t now = std::time(nullptr);
    std::tm n{};
    localtime_r(&now, &n);
    std::tm t = n;
    t.tm_hour = r.hour; t.tm_min = r.minute; t.tm_sec = 0;
    if (r.date) { t.tm_year = r.date / 10000 - 1900; t.tm_mon = r.date / 100 % 100 - 1; t.tm_mday = r.date % 100; }
    std::time_t when = std::mktime(&t);
    if (!r.date) {
        for (int i = 0; i < 8 && when <= now; i++) { t.tm_mday++; when = std::mktime(&t); }
        while (r.repeat == 2 && (t.tm_wday == 0 || t.tm_wday == 6)) { t.tm_mday++; when = std::mktime(&t); }
    }
    if (when <= now) return r.date ? "Past" : "";
    const long mins = static_cast<long>((when - now + 59) / 60);
    char b[48];
    if (mins < 60) std::snprintf(b, sizeof b, "in %ld min", mins);
    else if (mins < 24 * 60) std::snprintf(b, sizeof b, "in %ld h %ld min", mins / 60, mins % 60);
    else { std::strftime(b, sizeof b, "%a, %b %d", &t); }
    return b;
}

struct TimersPage final : ToolPage {
    enum Act { kNone, kStep, kPreset, kField, kStart, kPause, kResume, kReset, kRemove, kClear, kSwStart, kSwPause, kSwLap, kSwReset, kRepeat, kRemToggle, kAddRem, kOpenTime, kOpenDate, kClearDate, kDlgCancel, kDlgOk, kDayPick, kMonthStep, kDial, kTimeSel, kPeriod };
    struct Hit { Rect r; Act act; int a, b; };
    TpState st;
    time_t stamp = 0;
    int tab = 0;
    int h = 0, m = 5, s = 0, remH = 8, remM = 0, remRepeat = 0, remDate = 0;
    int dlg = 0;  // 0 none, 1 date picker, 2 time picker
    int dYear = 0, dMonth = 0, dSel = 0, tH = 8, tM = 0, tMode = 0;
    bool tPm = false, dialDrag = false, h24 = true, mondayFirst = true;
    double dialCx = 0, dialCy = 0;
    Rect dlgRect;
    std::string note;  // "Added · in 1 h 4 min", for a few seconds after a reminder is added
    Clock::time_point noteUntil;
    std::string label;
    bool fieldFocus = false;
    std::vector<Hit> hits;
    Act hotAct = kNone;
    int hotA = -1, hotB = -1;
    double scrollY = 0, contentH = 0;
    Elastic band;
    Rect listRect;
    Clock::time_point blink = Clock::now();

    explicit TimersPage(int which) : tab(which) {}  // 0 timers, 1 stopwatch, 2 reminders: each a page of the Tools bar
    const char* name() const override { return tab == 0 ? "Timers" : tab == 1 ? "Stopwatch" : "Reminders"; }
    const char* icon() const override { return tab == 0 ? "hourglass" : tab == 1 ? "stopwatch" : "alarm"; }
    void init() override {
        reload(true);
        const std::string cfg = readFile(configPath());
        std::string v;
        if (jsonValue(cfg, "clock24", v)) h24 = v != "0";
        if (jsonValue(cfg, "calendarMonday", v)) mondayFirst = v != "0";
    }

    void reload(bool force = false) {
        const time_t t = mtimeOf(stateDir() + "/timers.txt");
        if (!force && t == stamp) return;
        stamp = t;
        st = tpLoad();
    }
    bool anyRunning() const {
        for (const TpTimer& t : st.timers) if (t.running) return true;
        return st.swRunning;
    }
    bool tick() override {
        const time_t t = mtimeOf(stateDir() + "/timers.txt");
        const bool fileChanged = t != stamp;
        reload();
        static long long lastSecond = 0;
        const long long sec = tpNow() / (tab == 1 ? 100 : 1000);
        const bool moved = anyRunning() && sec != lastSecond;
        lastSecond = sec;
        const bool noteGone = !note.empty() && Clock::now() > noteUntil;
        if (noteGone) note.clear();
        const bool caret = fieldFocus && ((Clock::now() - blink).count() / 1000000 / 500) % 2 != static_cast<long>(lastCaret);
        lastCaret = (Clock::now() - blink).count() / 1000000 / 500 % 2;
        return fileChanged || moved || caret || noteGone || band.step();
    }
    long lastCaret = 0;
    bool animating() const override { return band.over != 0; }

    bool addHit(const Rect& r, Act act, int a = 0, int b = 0) { hits.push_back({r, act, a, b}); return hotAct == act && hotA == a && hotB == b; }
    // Times drawn with every digit as wide as the widest one, so a number that changes (a "1" is narrower than a "0") does not wobble.
    static double digitPitch(double px, bool bold) {
        static std::map<std::pair<int, bool>, double> cache;
        const auto key = std::make_pair(static_cast<int>(px * 10), bold);
        if (const auto it = cache.find(key); it != cache.end()) return it->second;
        double best = 0, h = 0;
        for (char d = '0'; d <= '9'; d++) {
            double w = 0;
            measure(std::string(1, d), px, bold, 0, w, h);
            best = std::max(best, w);
        }
        return cache[key] = best;
    }
    static double monoWidth(const std::string& s, double px, bool bold) {
        const double dp = digitPitch(px, bold);
        double w = 0, h = 0;
        for (char ch : s) {
            if (ch >= '0' && ch <= '9') { w += dp; continue; }
            double cw = 0;
            measure(std::string(1, ch), px, bold, 0, cw, h);
            w += cw;
        }
        return w;
    }
    static void monoText(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double alpha = 1) {
        const double dp = digitPitch(px, bold);
        for (char ch : s) {
            double cw = 0, h = 0;
            measure(std::string(1, ch), px, bold, 0, cw, h);
            const bool digit = ch >= '0' && ch <= '9';
            text(cr, std::string(1, ch), x + (digit ? (dp - cw) / 2 : 0), y, px, bold, c, 0, alpha);
            x += digit ? dp : cw;
        }
    }
    static void centeredMono(cairo_t* cr, const std::string& s, const Rect& r, double px, bool bold, const Color& c, double alpha = 1) {
        double w = 0, hh = 0;
        measure("0", px, bold, 0, w, hh);
        monoText(cr, s, r.x + (r.w - monoWidth(s, px, bold)) / 2, r.y + (r.h - hh) / 2, px, bold, c, alpha);
    }
    static void centered(cairo_t* cr, const std::string& str, const Rect& r, double px, bool bold, const Color& c, double alpha = 1) {
        double w = 0, hh = 0;
        measure(str, px, bold, 0, w, hh);
        text(cr, str, r.x + (r.w - w) / 2, r.y + (r.h - hh) / 2, px, bold, c, 0, alpha);
    }
    void button(cairo_t* cr, const Rect& r, const std::string& lab, const char* ic, bool filled, Act act, int a = 0, int b = 0, bool enabled = true) {
        const bool hot = enabled && addHit(r, act, a, b);
        const Color base = g.color(filled ? "primary" : "secondary_container"), fg = g.color(filled ? "on_primary" : "on_secondary_container");
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        src(cr, hot ? mix(fg, base, 0.12) : base, enabled ? 1 : 0.4);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(lab, 15, true, 0, tw, th);
        const int is = 20;
        const double lead = ic ? is + (lab.empty() ? 0 : 8) : 0, x0 = r.x + (r.w - lead - tw) / 2;
        if (ic) ::icon(cr, ic, x0, r.y + (r.h - is) / 2, is, fg);
        if (!lab.empty()) text(cr, lab, x0 + lead, r.y + (r.h - th) / 2, 15, true, fg, 0, enabled ? 1 : 0.5);
    }
    void chip(cairo_t* cr, const Rect& r, const std::string& lab, bool on, Act act, int a = 0) {
        const bool hot = addHit(r, act, a);
        rrect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, r.h / 2 - 0.5);
        if (on) { src(cr, g.color("secondary_container")); cairo_fill(cr); }
        else { src(cr, g.color("outline_variant")); cairo_set_line_width(cr, 1); cairo_stroke(cr); }
        if (hot) { rrect(cr, r.x, r.y, r.w, r.h, r.h / 2); src(cr, g.color("on_surface"), 0.08); cairo_fill(cr); }
        centered(cr, lab, r, 14, false, g.color(on ? "on_secondary_container" : "on_surface_variant"));
    }
    void stepper(cairo_t* cr, const Rect& r, int value, const char* unit, int which, int width2 = 2) {
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        src(cr, g.color("surface_container"));
        cairo_fill(cr);
        char b[8];
        std::snprintf(b, sizeof b, "%0*d", width2, value);
        centered(cr, b, {r.x, r.y + 38, r.w, 44}, 34, true, g.color("on_surface"));
        centered(cr, unit, {r.x, r.y + r.h - 26, r.w, 20}, 12, false, g.color("on_surface_variant"));
        for (int dir = 0; dir < 2; dir++) {
            const Rect br = {r.x + r.w / 2 - 18, dir == 0 ? r.y + 6 : r.y + 86, 36, 28};
            const bool hot = addHit(br, kStep, which, dir == 0 ? 1 : -1);
            if (hot) { rrect(cr, br.x, br.y, br.w, br.h, 14); src(cr, g.color("on_surface"), 0.1); cairo_fill(cr); }
            ::icon(cr, dir == 0 ? "alt-arrow-up" : "alt-arrow-down", br.x + 8, br.y + 4, 20, g.color("on_surface_variant"));
        }
    }
    // A text box that grows to the height it is given: the text wraps, and the caret follows the end of it.
    void textBox(cairo_t* cr, const Rect& r, const char* placeholder) {
        addHit(r, kField);
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        src(cr, g.color("surface_container_highest"));
        cairo_fill_preserve(cr);
        if (fieldFocus) { src(cr, g.color("primary")); cairo_set_line_width(cr, 2); cairo_stroke(cr); } else cairo_new_path(cr);
        const double pad = 18, innerW = r.w - 2 * pad, innerH = r.h - 2 * pad;
        cairo_save(cr);
        cairo_rectangle(cr, r.x + pad - 2, r.y + pad - 2, innerW + 4, innerH + 4);
        cairo_clip(cr);
        PangoLayout* l = pango_cairo_create_layout(cr);
        setFont(l, 16, false);
        pango_layout_set_width(l, static_cast<int>(innerW * PANGO_SCALE));
        pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
        const std::string shown = label.empty() ? std::string(placeholder) : label;
        pango_layout_set_text(l, label.empty() ? i18n::tr(placeholder) : label.c_str(), label.empty() ? -1 : static_cast<int>(label.size()));
        PangoRectangle caret{};
        pango_layout_get_cursor_pos(l, label.empty() ? 0 : static_cast<int>(label.size()), &caret, nullptr);
        const double caretBottom = (caret.y + caret.height) / double(PANGO_SCALE), shift = std::max(0.0, caretBottom - innerH);  // past the bottom: it scrolls
        src(cr, g.color(label.empty() ? "on_surface_variant" : "on_surface"), label.empty() ? 0.7 : 1);
        cairo_move_to(cr, r.x + pad, r.y + pad - shift);
        pango_cairo_show_layout(cr, l);
        if (fieldFocus && (Clock::now() - blink).count() / 1000000 % 1000 < 600) {
            src(cr, g.color("primary"));
            cairo_rectangle(cr, r.x + pad + caret.x / double(PANGO_SCALE), r.y + pad + caret.y / double(PANGO_SCALE) - shift, 2, caret.height / double(PANGO_SCALE));
            cairo_fill(cr);
        }
        g_object_unref(l);
        cairo_restore(cr);
        (void)shown;
    }

    void field(cairo_t* cr, const Rect& r, const char* placeholder) {
        const bool hot = addHit(r, kField);
        rrect(cr, r.x, r.y, r.w, r.h, 14);
        src(cr, g.color("surface_container_highest"));
        cairo_fill_preserve(cr);
        if (fieldFocus) { src(cr, g.color("primary")); cairo_set_line_width(cr, 2); cairo_stroke(cr); } else cairo_new_path(cr);
        (void)hot;
        cairo_save(cr);
        cairo_rectangle(cr, r.x + 12, r.y, r.w - 24, r.h);
        cairo_clip(cr);
        double tw = 0, th = 0;
        measure(label.empty() ? std::string(placeholder) : label, 16, false, 0, tw, th);
        if (label.empty()) text(cr, placeholder, r.x + 16, r.y + (r.h - th) / 2, 16, false, g.color("on_surface_variant"), 0, 0.7);
        else text(cr, label, r.x + 16 - std::max(0.0, tw - (r.w - 40)), r.y + (r.h - th) / 2, 16, false, g.color("on_surface"));
        if (fieldFocus && (Clock::now() - blink).count() / 1000000 % 1000 < 600) {
            src(cr, g.color("primary"));
            cairo_rectangle(cr, r.x + 16 + (label.empty() ? 0 : std::min(tw, r.w - 40)) + 1, r.y + 10, 2, r.h - 20);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        if (tab == 0) drawTimers(cr, r);
        else if (tab == 1) drawStopwatch(cr, r);
        else drawReminders(cr, r);
        if (dlg) {  // a dialog: a scrim over the page, and only the dialog takes clicks
            hits.clear();
            rrect(cr, r.x, r.y, r.w, r.h, 28);
            src(cr, Color{0, 0, 0}, 0.32);
            cairo_fill(cr);
            if (dlg == 1) drawDateDialog(cr, r); else drawTimeDialog(cr, r);
        }
    }

    void textBtn(cairo_t* cr, const Rect& r, const std::string& lab, Act act) {
        const bool hot = addHit(r, act);
        if (hot) { rrect(cr, r.x, r.y, r.w, r.h, r.h / 2); src(cr, g.color("primary"), 0.08); cairo_fill(cr); }
        centered(cr, lab, r, 14, true, g.color("primary"));
    }

    void drawDateDialog(cairo_t* cr, Rect pr) {
        const double W = 360, H = 568, x0 = pr.x + (pr.w - W) / 2, y0 = pr.y + (pr.h - H) / 2;
        dlgRect = {x0, y0, W, H};
        rrect(cr, x0, y0, W, H, 28);
        src(cr, g.color("surface_container_high"));
        cairo_fill(cr);
        const Color var = g.color("on_surface_variant"), onS = g.color("on_surface"), prim = g.color("primary");
        text(cr, "Select date", x0 + 24, y0 + 24, 14, true, var);
        text(cr, tpDateText(dSel ? dSel : tpToday()), x0 + 24, y0 + 56, 32, false, var);
        rrect(cr, x0, y0 + 119, W, 1, 0);
        src(cr, g.color("outline_variant"));
        cairo_fill(cr);
        std::tm tm0{};
        tm0.tm_year = dYear - 1900; tm0.tm_mon = dMonth - 1; tm0.tm_mday = 1; tm0.tm_hour = 12;
        std::mktime(&tm0);
        char ml[40];
        std::strftime(ml, sizeof ml, "%B %Y", &tm0);
        text(cr, ml, x0 + 24, y0 + 120 + (48 - 20) / 2, 14, true, onS);
        iconBtn(cr, {x0 + W - 16 - 88, y0 + 124, 40, 40}, "alt-arrow-left", kMonthStep, -1);
        iconBtn(cr, {x0 + W - 16 - 40, y0 + 124, 40, 40}, "alt-arrow-right", kMonthStep, 1);
        static const char* wd[7] = {"M", "T", "W", "T", "F", "S", "S"};
        for (int c = 0; c < 7; c++) {
            const int di = mondayFirst ? c : (c + 6) % 7;
            centered(cr, wd[di], {x0 + 12 + c * 48, y0 + 172, 48, 32}, 14, false, onS);
        }
        const int first = tpWeekday(dYear, dMonth, 1), lead = mondayFirst ? first : (first + 1) % 7, days = tpDaysIn(dYear, dMonth), today = tpToday();
        for (int d = 1; d <= days; d++) {
            const int idx = lead + d - 1, row = idx / 7, col = idx % 7;
            const Rect cell = {x0 + 12 + col * 48 + 4, y0 + 208 + row * 48 + 4, 40, 40};
            const int stamp = dYear * 10000 + dMonth * 100 + d;
            const bool past = stamp < today, sel = stamp == dSel, isToday = stamp == today;
            bool hot = false;
            if (!past) hot = addHit(cell, kDayPick, stamp);
            if (sel) { rrect(cr, cell.x, cell.y, 40, 40, 20); src(cr, prim); cairo_fill(cr); }
            else if (isToday) { rrect(cr, cell.x + 0.5, cell.y + 0.5, 39, 39, 19.5); src(cr, prim); cairo_set_line_width(cr, 1); cairo_stroke(cr); }
            if (hot && !sel) { rrect(cr, cell.x, cell.y, 40, 40, 20); src(cr, onS, 0.08); cairo_fill(cr); }
            centered(cr, std::to_string(d), cell, 16, false, sel ? g.color("on_primary") : isToday ? prim : onS, past ? 0.38 : 1);
        }
        textBtn(cr, {x0 + W - 24 - 64, y0 + H - 16 - 40, 64, 40}, "OK", kDlgOk);
        textBtn(cr, {x0 + W - 24 - 64 - 8 - 80, y0 + H - 16 - 40, 80, 40}, "Cancel", kDlgCancel);
    }

    void drawTimeDialog(cairo_t* cr, Rect pr) {
        const double W = 328, H = 516, x0 = pr.x + (pr.w - W) / 2, y0 = pr.y + (pr.h - H) / 2;
        dlgRect = {x0, y0, W, H};
        rrect(cr, x0, y0, W, H, 28);
        src(cr, g.color("surface_container_high"));
        cairo_fill(cr);
        const Color var = g.color("on_surface_variant"), onS = g.color("on_surface");
        text(cr, "Select time", x0 + 24, y0 + 24, 12, true, var);
        const double sy = y0 + 56, rowW = h24 ? 96 + 24 + 96 : 96 + 24 + 96 + 12 + 52, sx = x0 + (W - rowW) / 2;
        const int shownH = h24 ? tH : (tH % 12 == 0 ? 12 : tH % 12);
        for (int k = 0; k < 2; k++) {
            const Rect r = {sx + k * (96 + 24), sy, 96, 80};
            const bool sel = tMode == k;
            addHit(r, kTimeSel, k);
            rrect(cr, r.x, r.y, r.w, r.h, 8);
            src(cr, g.color(sel ? "primary_container" : "surface_container_highest"));
            cairo_fill(cr);
            char b[8];
            std::snprintf(b, sizeof b, "%02d", k == 0 ? shownH : tM);
            centered(cr, b, r, 52, false, g.color(sel ? "on_primary_container" : "on_surface"));
        }
        centered(cr, ":", {sx + 96, sy, 24, 80}, 52, false, onS);
        if (!h24) {
            for (int k = 0; k < 2; k++) {
                const Rect r = {sx + 96 + 24 + 96 + 12, sy + k * 40, 52, 40};
                const bool on = tPm == (k == 1);
                addHit(r, kPeriod, k);
                rrect(cr, r.x, r.y, r.w, r.h, 8);
                if (on) { src(cr, g.color("tertiary_container")); cairo_fill(cr); }
                else cairo_new_path(cr);  // (an unfilled path would stay and be filled, over the "AM", with the next one)
                centered(cr, k == 0 ? "AM" : "PM", r, 16, true, g.color(on ? "on_tertiary_container" : "on_surface_variant"));
            }
            rrect(cr, sx + 96 + 24 + 96 + 12 + 0.5, sy + 0.5, 51, 79, 7.5);
            src(cr, g.color("outline"));
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            rrect(cr, sx + 96 + 24 + 96 + 12, sy + 40, 52, 1, 0);
            src(cr, g.color("outline"));
            cairo_fill(cr);
        }
        dialCx = x0 + W / 2;
        dialCy = sy + 80 + 36 + 128;
        const double R = 128;
        cairo_new_sub_path(cr);
        cairo_arc(cr, dialCx, dialCy, R, 0, 2 * M_PI);
        src(cr, g.color("surface_container_highest"));
        cairo_fill(cr);
        addHit({dialCx - R, dialCy - R, 2 * R, 2 * R}, kDial);
        // the labels first, the handle over them (so it hides the label under it), and the value on the handle
        auto label = [&](const std::string& t, double a, double rr) {
            const double lx = dialCx + rr * std::sin(a * M_PI / 180), ly = dialCy - rr * std::cos(a * M_PI / 180);
            centered(cr, t, {lx - 20, ly - 12, 40, 24}, 16, false, onS);
        };
        if (tMode == 0) {
            for (int k = 0; k < 12; k++) {
                label(std::to_string(k == 0 ? 12 : k), k * 30.0, 100);
                if (h24) { char b[4]; std::snprintf(b, sizeof b, "%02d", k == 0 ? 0 : k + 12); label(b, k * 30.0, 66); }
            }
        } else {
            for (int k = 0; k < 12; k++) { char b[4]; std::snprintf(b, sizeof b, "%d", k * 5); label(b, k * 30.0, 100); }
        }
        double ang = 0, rad = 100;
        std::string chosen;
        if (tMode == 0) {
            ang = (tH % 12) * 30.0;
            if (h24 && (tH == 0 || tH > 12)) rad = 66;
            char b[4];
            if (h24 && (tH == 0 || tH > 12)) std::snprintf(b, sizeof b, "%02d", tH); else std::snprintf(b, sizeof b, "%d", h24 ? tH : (tH % 12 == 0 ? 12 : tH % 12));
            chosen = b;
        } else { ang = tM * 6.0; chosen = std::to_string(tM); }
        const double hx = dialCx + rad * std::sin(ang * M_PI / 180), hy = dialCy - rad * std::cos(ang * M_PI / 180);
        cairo_move_to(cr, dialCx, dialCy);
        cairo_line_to(cr, hx, hy);
        cairo_set_line_width(cr, 2);
        src(cr, g.color("primary"));
        cairo_stroke(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, dialCx, dialCy, 4, 0, 2 * M_PI);
        src(cr, g.color("primary"));
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, hx, hy, 24, 0, 2 * M_PI);
        src(cr, g.color("primary"));
        cairo_fill(cr);
        centered(cr, chosen, {hx - 24, hy - 12, 48, 24}, 16, false, g.color("on_primary"));
        textBtn(cr, {x0 + W - 24 - 64, y0 + H - 24 - 40, 64, 40}, "OK", kDlgOk);
        textBtn(cr, {x0 + W - 24 - 64 - 8 - 80, y0 + H - 24 - 40, 80, 40}, "Cancel", kDlgCancel);
    }

    void openTimeDialog() { dlg = 2; tH = remH; tM = remM; tMode = 0; tPm = remH >= 12; }
    void openDateDialog() {
        dlg = 1;
        const int today = tpToday(), base = remDate ? remDate : today;
        dYear = base / 10000; dMonth = base / 100 % 100; dSel = remDate;
    }
    void setDial(double x, double y) {
        const double dx = x - dialCx, dy = y - dialCy;
        double ang = std::atan2(dx, -dy) * 180 / M_PI;
        if (ang < 0) ang += 360;
        if (tMode == 0) {
            const int k = static_cast<int>(std::lround(ang / 30)) % 12;
            const bool inner = h24 && std::hypot(dx, dy) < 83;
            if (h24) tH = inner ? (k == 0 ? 0 : k + 12) : (k == 0 ? 12 : k);
            else tH = (k + (tPm ? 12 : 0)) % 24;
        } else tM = static_cast<int>(std::lround(ang / 6)) % 60;
    }
    void dialogApply() {
        if (dlg == 2) { remH = tH; remM = tM; }
        else if (dlg == 1) { remDate = dSel; if (remDate) remRepeat = 0; }
        dlg = 0;
    }

    void listCard(cairo_t* cr, Rect box, bool done) {
        rrect(cr, box.x, box.y, box.w, box.h, 24);
        src(cr, g.color(done ? "primary_container" : "surface_container"));
        cairo_fill(cr);
    }

    void drawTimers(cairo_t* cr, Rect b) {
        const double lw = 400;
        rrect(cr, b.x, b.y, lw, b.h, 28);
        src(cr, g.color("surface_container_low"));
        cairo_fill(cr);
        text(cr, "New timer", b.x + 24, b.y + 20, 20, true, g.color("on_surface"));
        const double sw = (lw - 48 - 24) / 3, sy = b.y + 64;
        stepper(cr, {b.x + 24, sy, sw, 140}, h, "Hours", 0);
        stepper(cr, {b.x + 24 + sw + 12, sy, sw, 140}, m, "Minutes", 1);
        stepper(cr, {b.x + 24 + 2 * (sw + 12), sy, sw, 140}, s, "Seconds", 2);
        const char* pl[6] = {"1 min", "5 min", "10 min", "15 min", "30 min", "1 h"};
        const int pm[6] = {60, 300, 600, 900, 1800, 3600};
        double px = b.x + 24, py = sy + 156;
        for (int i = 0; i < 6; i++) {
            double tw = 0, th = 0;
            measure(pl[i], 14, false, 0, tw, th);
            const double w = tw + 28;
            if (px + w > b.x + lw - 24) { px = b.x + 24; py += 40; }
            chip(cr, {px, py, w, 32}, pl[i], false, kPreset, pm[i]);
            px += w + 8;
        }
        field(cr, {b.x + 24, py + 56, lw - 48, 52}, "Label (optional)");
        const bool ok = h * 3600 + m * 60 + s > 0;
        button(cr, {b.x + 24, b.y + b.h - 24 - 56, lw - 48, 56}, "Start", "play", true, kStart, 0, 0, ok);
        drawList(cr, {b.x + lw + 16, b.y, b.w - lw - 16, b.h}, 0);
    }

    void drawList(cairo_t* cr, Rect box, int kind) {
        listRect = box;
        cairo_save(cr);
        rrect(cr, box.x, box.y, box.w, box.h, 28);
        cairo_clip(cr);
        rrect(cr, box.x, box.y, box.w, box.h, 28);
        src(cr, g.color("surface_container_low"));
        cairo_fill(cr);
        const double top = box.y + 64;
        double y = top - (scrollY + band.over);
        const char* title = kind == 0 ? "Timers" : "Reminders";
        text(cr, title, box.x + 24, box.y + 20, 20, true, g.color("on_surface"));
        if (kind == 1 && !note.empty()) {
            double tw = 0, th = 0;
            measure(title, 20, true, 0, tw, th);
            text(cr, note, box.x + 24 + tw + 18, box.y + 24, 15, false, g.color("primary"));
        }
        const size_t count = kind == 0 ? st.timers.size() : st.reminders.size();
        if (kind == 0 && std::any_of(st.timers.begin(), st.timers.end(), [](const TpTimer& t) { return t.done; }))
            button(cr, {box.x + box.w - 24 - 120, box.y + 14, 120, 36}, "Clear done", nullptr, false, kClear);
        if (count == 0) {  // an empty list: a Material shape with the tool's icon, what it is and what to do
            const double cx = box.x + box.w / 2, cy = box.y + box.h / 2 - 40;
            shapePath(cr, "Cookie9Sided", cx - 74, cy - 74, 148);
            src(cr, g.color("secondary_container"));
            cairo_fill(cr);
            ::icon(cr, kind == 0 ? "hourglass" : "alarm", cx - 32, cy - 32, 64, g.color("on_secondary_container"), true);
            centered(cr, kind == 0 ? "No timers yet" : "No reminders yet", {box.x, cy + 100, box.w, 34}, 24, true, g.color("on_surface"));
            centered(cr, kind == 0 ? "Pick a time and press Start." : "Choose a time, write what to remember and press Add reminder.", {box.x + 24, cy + 138, box.w - 48, 24}, 15, false, g.color("on_surface_variant"));
        }
        cairo_save(cr);
        cairo_rectangle(cr, box.x, top, box.w, box.h - 64);
        cairo_clip(cr);
        const double ch = 114;
        if (kind == 0) {
            for (const TpTimer& t : st.timers) {
                const Rect c = {box.x + 16, y, box.w - 32, ch - 10};
                if (c.y + c.h > top && c.y < box.y + box.h) drawTimerCard(cr, c, t);
                y += ch;
            }
        } else {
            for (const TpReminder& rem : st.reminders) {
                const Rect c = {box.x + 16, y, box.w - 32, ch - 10};
                if (c.y + c.h > top && c.y < box.y + box.h) drawReminderCard(cr, c, rem);
                y += ch;
            }
        }
        contentH = count * ch;
        cairo_restore(cr);
        cairo_restore(cr);
    }

    void drawTimerCard(cairo_t* cr, Rect c, const TpTimer& t) {
        listCard(cr, c, t.done);
        const double left = t.running ? std::max(0.0, (t.endAt - tpNow()) / 1000.0) : t.remaining;
        const Color fg = g.color(t.done ? "on_primary_container" : "on_surface"), sub = g.color(t.done ? "on_primary_container" : "on_surface_variant");
        text(cr, t.label.empty() ? "Timer " + std::to_string(t.id) : t.label, c.x + 20, c.y + 14, 15, false, sub, c.w - 300);
        monoText(cr, tpClock(left), c.x + 20, c.y + 34, 34, true, fg);
        const double bw = c.w - 40, frac = t.total > 0 ? std::clamp(1 - left / t.total, 0.0, 1.0) : 0;
        rrect(cr, c.x + 20, c.y + c.h - 20, bw, 4, 2);
        src(cr, fg, 0.18);
        cairo_fill(cr);
        rrect(cr, c.x + 20, c.y + c.h - 20, std::max(4.0, bw * frac), 4, 2);
        src(cr, g.color(t.done ? "on_primary_container" : "primary"));
        cairo_fill(cr);
        double bx = c.x + c.w - 20 - 44;
        iconBtn(cr, {bx, c.y + 14, 44, 44}, "trash-bin-minimalistic", kRemove, t.id);
        bx -= 52;
        iconBtn(cr, {bx, c.y + 14, 44, 44}, "restart", kReset, t.id);
        bx -= 52;
        if (!t.done) iconBtn(cr, {bx, c.y + 14, 44, 44}, t.running ? "pause" : "play", t.running ? kPause : kResume, t.id);
    }

    void iconBtn(cairo_t* cr, Rect r, const char* ic, Act act, int a, int b = 0) {
        const bool hot = addHit(r, act, a, b);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        src(cr, g.color("surface_container_highest"), hot ? 1 : 0.7);
        cairo_fill(cr);
        ::icon(cr, ic, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, g.color("on_surface"));
    }

    void drawStopwatch(cairo_t* cr, Rect b) {
        rrect(cr, b.x, b.y, b.w, b.h, 28);
        src(cr, g.color("surface_container_low"));
        cairo_fill(cr);
        const double el = st.swAccum + (st.swRunning ? (tpNow() - st.swStart) / 1000.0 : 0);
        centeredMono(cr, tpClock(el, true), {b.x, b.y + 36, b.w, 130}, 110, true, g.color("on_surface"));
        const double bw = 150, gap = 14, total = 3 * bw + 2 * gap, x0 = b.x + (b.w - total) / 2, y = b.y + 190;
        button(cr, {x0, y, bw, 56}, st.swRunning ? "Pause" : "Start", st.swRunning ? "pause" : "play", true, st.swRunning ? kSwPause : kSwStart);
        button(cr, {x0 + bw + gap, y, bw, 56}, "Lap", "flag", false, kSwLap, 0, 0, st.swRunning || el > 0);
        button(cr, {x0 + 2 * (bw + gap), y, bw, 56}, "Reset", "restart", false, kSwReset, 0, 0, st.swRunning || el > 0);
        const double top = y + 84;
        cairo_save(cr);
        cairo_rectangle(cr, b.x, top, b.w, b.h - (top - b.y) - 8);
        cairo_clip(cr);
        double ly = top - (scrollY + band.over);
        double prev = 0;
        for (size_t i = 0; i < st.laps.size(); i++) {
            const Rect c = {b.x + b.w / 2 - 260, ly, 520, 44};
            rrect(cr, c.x, c.y, c.w, c.h, 22);
            src(cr, g.color("surface_container"));
            cairo_fill(cr);
            text(cr, "Lap " + std::to_string(i + 1), c.x + 22, c.y + 12, 16, false, g.color("on_surface_variant"));
            centeredMono(cr, tpClock(st.laps[i] - prev, true), {c.x, c.y, c.w, c.h}, 18, true, g.color("on_surface"));
            double tw = 0, th = 0;
            tw = monoWidth(tpClock(st.laps[i], true), 15, false);
            monoText(cr, tpClock(st.laps[i], true), c.x + c.w - 22 - tw, c.y + 13, 15, false, g.color("on_surface_variant"));
            prev = st.laps[i];
            ly += 52;
        }
        contentH = st.laps.size() * 52.0 + 90;
        cairo_restore(cr);
    }

    void drawReminders(cairo_t* cr, Rect b) {
        const double lw = 400;
        rrect(cr, b.x, b.y, lw, b.h, 28);
        src(cr, g.color("surface_container_low"));
        cairo_fill(cr);
        text(cr, "New reminder", b.x + 24, b.y + 20, 20, true, g.color("on_surface"));
        const double sy = b.y + 64, bw = (lw - 48 - 12) / 2;
        {  // the time: a big selector that opens the time picker
            const Rect r = {b.x + 24, sy, bw, 112};
            const bool hot = addHit(r, kOpenTime);
            rrect(cr, r.x, r.y, r.w, r.h, 16);
            src(cr, g.color("primary_container"));
            cairo_fill(cr);
            if (hot) { rrect(cr, r.x, r.y, r.w, r.h, 16); src(cr, g.color("on_primary_container"), 0.08); cairo_fill(cr); }
            char tm[8];
            if (h24) std::snprintf(tm, sizeof tm, "%02d:%02d", remH, remM); else std::snprintf(tm, sizeof tm, "%02d:%02d", remH % 12 == 0 ? 12 : remH % 12, remM);
            if (h24) centered(cr, tm, {r.x, r.y + 20, r.w, 50}, 40, true, g.color("on_primary_container"));
            else {  // 12-hour clock: the period small, after the time
                double tw = 0, th = 0;
                measure(tm, 40, true, 0, tw, th);
                const double x0 = r.x + (r.w - tw - 34) / 2;
                text(cr, tm, x0, r.y + 20 + (50 - th) / 2, 40, true, g.color("on_primary_container"));
                text(cr, remH >= 12 ? "PM" : "AM", x0 + tw + 6, r.y + 20 + (50 - th) / 2 + 20, 16, true, g.color("on_primary_container"), 0, 0.8);
            }
            centered(cr, "Time", {r.x, r.y + 78, r.w, 22}, 13, false, g.color("on_primary_container"), 0.8);
        }
        {  // the day: opens the date picker (nothing chosen: it follows the repeat below)
            const Rect r = {b.x + 24 + bw + 12, sy, bw, 112};
            const bool hot = addHit(r, kOpenDate);
            rrect(cr, r.x, r.y, r.w, r.h, 16);
            src(cr, g.color("surface_container_highest"));
            cairo_fill(cr);
            if (hot) { rrect(cr, r.x, r.y, r.w, r.h, 16); src(cr, g.color("on_surface"), 0.08); cairo_fill(cr); }
            ::icon(cr, "calendar", r.x + 16, r.y + 16, 22, g.color("on_surface_variant"));
            centered(cr, remDate ? tpDateText(remDate) : "Any day", {r.x, r.y + 46, r.w, 30}, 20, true, g.color("on_surface"));
            centered(cr, remDate ? "Only that day" : "Choose a date", {r.x, r.y + 78, r.w, 22}, 13, false, g.color("on_surface_variant"));
            if (remDate) iconBtn(cr, {r.x + r.w - 16 - 32, r.y + 11, 32, 32}, "close-circle", kClearDate, 0);
        }
        const double topEnd = sy + 112, btnTop = b.y + b.h - 24 - 56;
        const double gap = 24, chipY = topEnd + gap, fieldY = chipY + 36 + gap, fieldH = std::max(70.0, btnTop - gap - fieldY);  // the same gap above and below each part; the box takes what is left
        const char* rn[3] = {"Once", "Every day", "Weekdays"};
        double px = b.x + 24;
        if (!remDate) {
            for (int i = 0; i < 3; i++) {
                double tw = 0, th = 0;
                measure(rn[i], 14, false, 0, tw, th);
                chip(cr, {px, chipY, tw + 28, 36}, rn[i], remRepeat == i, kRepeat, i);
                px += tw + 36;
            }
        } else {
            text(cr, "It rings once, on that day.", b.x + 24, chipY + 8, 14, false, g.color("on_surface_variant"));
        }
        textBox(cr, {b.x + 24, fieldY, lw - 48, fieldH}, "What to remind you");
        button(cr, {b.x + 24, b.y + b.h - 24 - 56, lw - 48, 56}, "Add reminder", "alarm-add", true, kAddRem);
        drawList(cr, {b.x + lw + 16, b.y, b.w - lw - 16, b.h}, 1);
    }

    void drawReminderCard(cairo_t* cr, Rect c, const TpReminder& r) {
        listCard(cr, c, false);
        char tm[8];
        if (h24) std::snprintf(tm, sizeof tm, "%02d:%02d", r.hour, r.minute);
        else std::snprintf(tm, sizeof tm, "%02d:%02d", r.hour % 12 == 0 ? 12 : r.hour % 12, r.minute);  // (the same clock as the card of the new reminder)
        const Color fg = g.color("on_surface"), sub = g.color("on_surface_variant");
        const double a = r.enabled ? 1 : 0.45;
        text(cr, tm, c.x + 20, c.y + (c.h - 41) / 2, 34, true, fg, 0, a);
        if (!h24) {
            double tw = 0, th = 0;
            measure(tm, 34, true, 0, tw, th);
            text(cr, r.hour >= 12 ? "PM" : "AM", c.x + 20 + tw + 5, c.y + (c.h - 41) / 2 + 17, 13, true, fg, 0, a * 0.8);
        }
        const std::string when = r.date ? tpDateText(r.date) : std::string(r.repeat == 1 ? "Every day" : r.repeat == 2 ? "Weekdays" : "Once");
        const std::string until = tpUntil(r);
        const std::string meta = when + (r.label.empty() ? "" : "  \xC2\xB7  " + r.label);
        text(cr, meta, c.x + 150, c.y + (c.h - 44) / 2, 16, false, sub, c.w - 310, a);
        const bool done = !r.enabled && r.lastFired != 0 && r.repeat == 0;  // a one-off that has rung
        const std::string state = done ? "Done" : !r.enabled ? "Off" : until;
        if (!state.empty()) text(cr, state, c.x + 150, c.y + (c.h - 44) / 2 + 24, 14, false, g.color(r.enabled ? "primary" : "on_surface_variant"), c.w - 310, r.enabled ? a : 0.8);
        double bx = c.x + c.w - 20 - 44;
        iconBtn(cr, {bx, c.y + (c.h - 44) / 2, 44, 44}, "trash-bin-minimalistic", kRemove, r.id, 1);
        bx -= 52;
        iconBtn(cr, {bx, c.y + (c.h - 44) / 2, 44, 44}, done ? "restart" : r.enabled ? "bell" : "bell-off", kRemToggle, r.id);
    }

    const Hit* hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.contains(x, y)) return &*it;
        return nullptr;
    }

    std::string remToken() const {
        if (remDate) { char b[16]; std::snprintf(b, sizeof b, " %04d-%02d-%02d ", remDate / 10000, remDate / 100 % 100, remDate % 100); return b; }
        return remRepeat == 1 ? " daily " : remRepeat == 2 ? " weekdays " : " once ";
    }
    void send(const std::string& cmd) { tpShell(cmd); stamp = 0; reload(true); }

    bool dialogClick(double x, double y, bool press) {
        if (!press) {
            if (dialDrag) { dialDrag = false; if (tMode == 0) tMode = 1; return true; }  // the hour is set: on to the minutes
            return false;
        }
        const Hit* hp = hitAt(x, y);
        if (!hp) { if (!dlgRect.contains(x, y)) dlg = 0; return true; }
        const Hit hit = *hp;
        switch (hit.act) {
            case kDlgCancel: dlg = 0; break;
            case kDlgOk: dialogApply(); break;
            case kDayPick: dSel = hit.a; break;
            case kMonthStep:
                dMonth += hit.a;
                if (dMonth < 1) { dMonth = 12; dYear--; }
                if (dMonth > 12) { dMonth = 1; dYear++; }
                break;
            case kTimeSel: tMode = hit.a; break;
            case kPeriod: tPm = hit.a == 1; tH = (tH % 12) + (tPm ? 12 : 0); break;
            case kDial: dialDrag = true; setDial(x, y); break;
            default: break;
        }
        return true;
    }

    bool click(double x, double y, bool press) override {
        if (dlg) return dialogClick(x, y, press);
        if (!press) return false;
        const Hit* hp = hitAt(x, y);
        fieldFocus = false;
        if (!hp) return true;
        const Hit hit = *hp;
        switch (hit.act) {
            case kOpenTime: openTimeDialog(); break;
            case kOpenDate: openDateDialog(); break;
            case kClearDate: remDate = 0; break;
            case kStep: {
                int* v[5] = {&h, &m, &s, &remH, &remM};
                const int hi[5] = {99, 59, 59, 23, 59};
                int& val = *v[hit.a];
                val += hit.b;
                if (val < 0) val = hi[hit.a];
                if (val > hi[hit.a]) val = 0;
                break;
            }
            case kPreset: h = hit.a / 3600; m = hit.a / 60 % 60; s = hit.a % 60; break;
            case kField: fieldFocus = true; blink = Clock::now(); break;
            case kStart: send("timer add " + std::to_string(h) + "h" + std::to_string(m) + "m" + std::to_string(s) + "s " + label); label.clear(); break;
            case kPause: send("timer pause " + std::to_string(hit.a)); break;
            case kResume: send("timer resume " + std::to_string(hit.a)); break;
            case kReset: send("timer reset " + std::to_string(hit.a)); break;
            case kRemove: send(std::string(tab == 2 ? "reminder" : "timer") + " remove " + std::to_string(hit.a)); break;
            case kClear: send("timer clear"); break;
            case kSwStart: send("stopwatch start"); break;
            case kSwPause: send("stopwatch pause"); break;
            case kSwLap: send("stopwatch lap"); break;
            case kSwReset: send("stopwatch reset"); scrollY = 0; break;
            case kRepeat: remRepeat = hit.a; break;
            case kRemToggle: send("reminder toggle " + std::to_string(hit.a)); break;
            case kAddRem: {
                {
                    TpReminder t;
                    t.hour = remH; t.minute = remM; t.repeat = remRepeat; t.date = remDate;
                    if (tpUntil(t) == "Past") {  // a day and a time that are behind us: it would never ring
                        note = "That time has already passed.";
                        noteUntil = Clock::now() + std::chrono::seconds(5);
                        break;
                    }
                }
                char hm[8];
                std::snprintf(hm, sizeof hm, "%02d:%02d", remH, remM);
                send(std::string("reminder add ") + hm + remToken() + label);
                label.clear();
                {
                    TpReminder t;
                    t.hour = remH; t.minute = remM; t.repeat = remRepeat; t.date = remDate;
                    const std::string until = tpUntil(t);
                    note = std::string("Added") + (until.empty() ? "" : "  \xC2\xB7  " + until);
                    noteUntil = Clock::now() + std::chrono::seconds(5);
                }
                break;
            }
            default: break;
        }
        return true;
    }

    bool motion(double x, double y) override {
        if (dlg && dialDrag) { setDial(x, y); return true; }
        const Hit* hp = hitAt(x, y);
        const Act act = hp ? hp->act : kNone;
        const int a = hp ? hp->a : -1, b = hp ? hp->b : -1;
        if (act == hotAct && a == hotA && b == hotB) return false;
        hotAct = act; hotA = a; hotB = b;
        return true;
    }
    bool leave() override { const bool had = hotAct != kNone; hotAct = kNone; hotA = hotB = -1; return had; }
    bool scroll(double, double, double dy) override {
        const double view = tab == 1 ? 300 : listRect.h - 64;
        elasticWheel(band, scrollY, std::max(0.0, contentH - view), dy * 3);
        return true;
    }
    Cursor cursor(double x, double y) const override {
        const Hit* hp = hitAt(x, y);
        return !hp ? Cursor::Default : hp->act == kField ? Cursor::Text : Cursor::Pointer;
    }

    bool key(const KeyEvent& k) override {
        if (dlg) {
            if (k.sym == XKB_KEY_Escape) { dlg = 0; return true; }
            if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { dialogApply(); return true; }
            return true;
        }
        if (!fieldFocus) return false;
        blink = Clock::now();
        if (k.sym == XKB_KEY_Escape) { fieldFocus = false; return true; }
        if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) {
            if (tab == 0 && h * 3600 + m * 60 + s > 0) click(0, 0, false), send("timer add " + std::to_string(h) + "h" + std::to_string(m) + "m" + std::to_string(s) + "s " + label), label.clear();
            else if (tab == 2) {
                char hm[8];
                std::snprintf(hm, sizeof hm, "%02d:%02d", remH, remM);
                send(std::string("reminder add ") + hm + remToken() + label);
                label.clear();
            }
            fieldFocus = false;
            return true;
        }
        if (k.sym == XKB_KEY_BackSpace) {
            if (!label.empty()) { size_t n = label.size() - 1; while (n > 0 && (label[n] & 0xC0) == 0x80) n--; label.resize(n); }
            return true;
        }
        if (k.ctrl || k.alt || k.text.empty() || label.size() > 240) return false;
        label += k.text;
        return true;
    }
};
}
