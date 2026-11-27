// Tools page: Score. Football matches of 21 leagues with crests, kick-off times, live scores and the model's chances (home / draw / away).
// The data comes from scripts/score/score.py (ESPN's public scoreboard and a statistical model of its own): the page asks it for a tab separated
// list, first what is on disk (instant) and then, if that is old, a fresh one. The numbers are estimates, not advice.
// Included by src/tools.cpp (inside its anonymous namespace).

std::string scT(const char* s) { return std::string(i18n::tr(s)); }

struct ScTeam {
    std::string id, name, abbr, logo, score;
};
struct ScMatch {
    std::string id, leagueId, league, country, detail, state;
    time_t kickoff = 0;
    int dayKey = 0;  // local day, YYYYMMDD
    ScTeam home, away;
    double pHome = -1, pDraw = -1, pAway = -1;  // -1: the model has no data for this match
    std::string pickLabel, pickKey, safeLabel, safeKey;  // the algorithm's best-value pick and its most probable one (empty: no pick)
    double pickProb = 0, safeProb = 0, pickPrice = 0, safePrice = 0;  // the odds (a price of 0: none)
    bool pickReal = false, safeReal = false;
    int homeRank = 0, awayRank = 0;  // the place in the league table (0: unknown)  // the odds are the site's own (not the model's fair price)
    bool live() const { return state == "in"; }
    bool done() const { return state == "post"; }
};

int scDayKey(time_t t) {
    struct tm lt{};
    localtime_r(&t, &lt);
    return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}
time_t scParseIso(const std::string& iso) {  // "2026-11-26T14:25Z" (UTC)
    struct tm t{};
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min) != 5) return 0;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    return timegm(&t);
}
std::vector<std::string> scSplitTabs(const std::string& line) {
    std::vector<std::string> v;
    size_t a = 0;
    while (true) {
        const size_t b = line.find('\t', a);
        v.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}
double scProb(const std::string& s) { return s.empty() ? -1 : std::atof(s.c_str()); }

// The lines of `score.py --board`: '#', time made, file time; then one line per match.
std::vector<ScMatch> scParseBoard(const std::string& out, time_t& madeAt) {
    std::vector<ScMatch> list;
    std::istringstream in(out);
    std::string line;
    const int today = scDayKey(time(nullptr));
    while (std::getline(in, line)) {
        const auto f = scSplitTabs(line);
        if (!f.empty() && f[0] == "#") { madeAt = f.size() > 2 ? static_cast<time_t>(std::atoll(f[2].c_str())) : 0; continue; }
        if (f.size() < 20) continue;
        ScMatch m;
        m.id = f[0]; m.leagueId = f[1]; m.league = f[2]; m.country = f[3]; m.state = f[5]; m.detail = f[6];
        m.kickoff = scParseIso(f[4]);
        m.home = {f[7], f[8], f[9], f[10], f[11]};
        m.away = {f[12], f[13], f[14], f[15], f[16]};
        m.pHome = scProb(f[17]); m.pDraw = scProb(f[18]); m.pAway = scProb(f[19]);
        if (f.size() >= 26) { m.pickLabel = f[20]; m.pickProb = std::max(0.0, scProb(f[21])); m.pickKey = f[22]; m.safeLabel = f[23]; m.safeProb = std::max(0.0, scProb(f[24])); m.safeKey = f[25]; }
        if (f.size() >= 30) { m.pickPrice = scProb(f[26]) > 0 ? scProb(f[26]) : 0; m.pickReal = f[27] == "1"; m.safePrice = scProb(f[28]) > 0 ? scProb(f[28]) : 0; m.safeReal = f[29] == "1"; }
        if (f.size() >= 32) { m.homeRank = std::atoi(f[30].c_str()); m.awayRank = std::atoi(f[31].c_str()); }
        m.dayKey = scDayKey(m.kickoff);
        if (m.live() && m.dayKey < today) m.dayKey = today;  // (still in play after midnight: it belongs to today)
        list.push_back(std::move(m));
    }
    std::stable_sort(list.begin(), list.end(), [](const ScMatch& a, const ScMatch& b) { return a.kickoff < b.kickoff; });
    return list;
}

// What `score.py --detail <id>` knows about one match (see the script for the records).
struct ScForm { std::string result, against, ha, score; };
struct ScMeet { std::string date, competition, home, away; int homeGoals = 0, awayGoals = 0; };
struct ScSeason { int games = 0; double gf = 0, ga = 0, cf = 0, ca = 0; };
struct ScDetail {
    std::map<std::string, double> chance;
    double xHome = -1, xAway = -1, xCorners = -1;
    ScSeason season[2];
    std::vector<std::pair<std::string, double>> scores;
    std::string pickLabel;
    double pickChance = -1;
    std::vector<ScForm> form[2];
    std::vector<ScMeet> meets;
    int rank[2] = {0, 0};
    bool has(const char* k) const { return chance.count(k) > 0; }
    double of(const char* k) const { const auto it = chance.find(k); return it == chance.end() ? -1 : it->second; }
};
ScDetail scParseDetail(const std::string& out) {
    ScDetail d;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        const auto f = scSplitTabs(line);
        if (f.size() < 2) continue;
        const auto num = [&](size_t i) { return i < f.size() ? std::atof(f[i].c_str()) : 0.0; };
        if (f[0] == "P" && f.size() >= 3) d.chance[f[1]] = num(2);
        else if (f[0] == "X") { d.xHome = num(1); d.xAway = num(2); d.xCorners = num(3); }
        else if (f[0] == "S" && f.size() >= 7) d.season[f[1] == "home" ? 0 : 1] = {static_cast<int>(num(2)), num(3), num(4), num(5), num(6)};
        else if (f[0] == "G" && f.size() >= 3) d.scores.push_back({f[1], num(2)});
        else if (f[0] == "K" && f.size() >= 3) { d.pickLabel = f[1]; d.pickChance = num(2); }
        else if (f[0] == "F" && f.size() >= 6) d.form[f[1] == "home" ? 0 : 1].push_back({f[2], f[3], f[4], f[5]});
        else if (f[0] == "H" && f.size() >= 7) d.meets.push_back({f[1], f[2], f[3], f[5], static_cast<int>(num(4)), static_cast<int>(num(6))});
        else if (f[0] == "R" && f.size() >= 3) d.rank[f[1] == "home" ? 0 : 1] = static_cast<int>(num(2));
    }
    return d;
}

// The track record of the picks (`score.py --record`).
struct ScEntry {
    std::string date, league, home, away, label, key;
    double prob = 0, price = 0;
    bool real = false;
    int homeGoals = -1, awayGoals = -1, hit = -1;  // hit: 1 right, 0 wrong, -1 not known yet
};
struct ScRecord {
    int n = 0, hits = 0, n1x2 = 0, hits1x2 = 0, nRecent = 0, hitsRecent = 0, safeN = 0, safeHits = 0;
    std::vector<ScEntry> entries;
};
ScRecord scParseRecord(const std::string& out) {
    ScRecord r;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        const auto f = scSplitTabs(line);
        if (f.size() >= 9 && f[0] == "S") { r.n = std::atoi(f[1].c_str()); r.hits = std::atoi(f[2].c_str()); r.n1x2 = std::atoi(f[3].c_str()); r.hits1x2 = std::atoi(f[4].c_str()); r.nRecent = std::atoi(f[5].c_str()); r.hitsRecent = std::atoi(f[6].c_str()); r.safeN = std::atoi(f[7].c_str()); r.safeHits = std::atoi(f[8].c_str()); }
        else if (f.size() >= 14 && f[0] == "E") {
            ScEntry e;
            e.date = f[2]; e.league = f[3]; e.home = f[4]; e.away = f[5]; e.label = f[6]; e.key = f[7];
            e.prob = std::atof(f[8].c_str()); e.price = f[9].empty() ? 0 : std::atof(f[9].c_str()); e.real = f[10] == "1";
            if (!f[11].empty() && !f[12].empty()) { e.homeGoals = std::atoi(f[11].c_str()); e.awayGoals = std::atoi(f[12].c_str()); }
            e.hit = f[13].empty() ? -1 : std::atoi(f[13].c_str());
            r.entries.push_back(std::move(e));
        }
    }
    return r;
}

// A pick on the slip.
struct ScLeg {
    std::string matchId, key, label, home, away, league, kickoff;
    time_t time = 0;
    double prob = 0, price = 0;
    bool real = false;  // the odds are the site's own
};

struct ScorePage : ToolPage {
    enum Id { None, DayChip, RefreshBtn, Row, BackBtn, ModeBtn, SearchBox, FilterResult, FilterPeriod, LegToggle, SlipMinus, SlipPlus, SlipSuggest, SlipCode, SlipClear, SlipRemove, SlipCopy, SlipDay };
    enum Mode { Matches, Picks, Record, Slip };
    static bool needsList(int id) { return id == Row || id == FilterResult || id == FilterPeriod || id == LegToggle || id >= SlipMinus; }
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };
    static constexpr double kRowH = 66, kRowGap = 8, kHeadH = 38, kCrest = 30;
    static constexpr int kStaleSeconds = 30 * 60;

    std::vector<ScMatch> matches;
    std::vector<int> days;  // the local days that have matches, in order
    int dayKey = 0;
    time_t madeAt = 0;
    TrJob job;
    int stage = 0;  // 0: idle, 1: reading the copy on disk, 2: fetching a fresh list
    bool failed = false, everLoaded = false;
    std::map<std::string, cairo_surface_t*> crests;
    std::vector<Hit> hits;
    Hover hover;
    Rect listArea;
    double listPos = 0, scrollMax = 0;
    Elastic band;
    Clock::time_point dayShown = Clock::now(), spinStart = Clock::now(), detailShown = Clock::now();
    double lastFade = 1;
    double mx = -1, my = -1;  // the pointer, in the coordinates of the page
    // The detail of one match (opened by clicking a row).
    bool detailOpen = false, detailClosing = false;
    ScMatch dMatch;
    ScDetail detail;
    TrJob djob;
    int dStage = 0;  // 0: idle, 1: reading what is on disk, 2: fetching form, meetings and standings
    bool dRemoteDone = false;
    double dPos = 0, dMax = 0;
    Elastic dBand;
    // The Picks and Record views.
    int mode = Matches;
    ScRecord record;
    bool recordLoaded = false;
    TrJob rjob;
    bool rLoading = false;
    double rPos = 0, rMax = 0;
    Elastic rBand;
    Clock::time_point modeShown = Clock::now(), lastKey = Clock::now();
    // Record: a search box and two filters (result, period).
    std::string query;
    size_t qCur = 0;
    bool qFocus = false, lastCaret = true;
    Rect searchBox;
    int fResult = 0, fPeriod = 0;
    // The slip.
    std::vector<ScLeg> slip, legTargets;
    int slipLegs = 4, slipMissing = 0, slipDay = 0;  // slipDay: the day the parlay is built from (0: any day)
    std::string slipCode, slipError;
    TrJob sjob;
    Clock::time_point copiedUntil{};
    int sStage = 0;

    const char* name() const override { return "Score"; }
    const char* icon() const override { return "football"; }
    bool roundClip() const override { return false; }  // (the chips reach the top corners)
    ~ScorePage() override { for (auto& kv : crests) if (kv.second) cairo_surface_destroy(kv.second); }

    static std::string scriptPath() { return projectDir() + "/scripts/score/score.py"; }
    bool fetching() const { return stage != 0; }
    double detailAlpha() const {
        const double t = std::chrono::duration<double>(Clock::now() - detailShown).count() / 0.24, e = std::clamp(t, 0.0, 1.0);
        return detailClosing ? 1 - e : 1 - (1 - e) * (1 - e) * (1 - e);
    }

    void init() override { startJob(1); }
    void startJob(int which, bool force = false) {
        std::vector<std::string> argv{"python3", "-I", scriptPath(), "--board"};
        if (which == 1) argv.push_back("--cached");
        if (force) argv.push_back("--force");
        std::string odds;
        if (which == 2 && jsonValue(readFile(configPath()), "scoreOdds", odds) && !odds.empty()) { argv.push_back("--odds"); argv.push_back(odds); }  // (the site whose odds the picks use)
        failed = false;
        stage = job.start(argv, "", which == 1 ? 15000 : 240000) ? which : 0;
        if (stage == 0) failed = true;
        spinStart = Clock::now();
    }
    void finishJob() {
        const int which = stage;
        stage = 0;
        time_t made = 0;
        if (job.ok) {
            auto list = scParseBoard(job.out, made);
            if (!list.empty() || which == 2) { matches = std::move(list); madeAt = made; everLoaded = true; rebuildDays(); }
        } else if (which == 2) failed = true;
        if (which == 1 && (!everLoaded || time(nullptr) - madeAt > kStaleSeconds)) startJob(2);
    }
    void rebuildDays() {
        days.clear();
        for (const ScMatch& m : matches) if (days.empty() || days.back() != m.dayKey) if (std::find(days.begin(), days.end(), m.dayKey) == days.end()) days.push_back(m.dayKey);
        std::sort(days.begin(), days.end());
        const int today = scDayKey(time(nullptr));
        if (std::find(days.begin(), days.end(), dayKey) == days.end()) {
            dayKey = 0;
            for (int d : days) if (d >= today) { dayKey = d; break; }
            if (!dayKey && !days.empty()) dayKey = days.back();
            dayShown = Clock::now();
        }
    }

    static std::string dayLabel(int key) {
        const time_t now = time(nullptr);
        if (key == scDayKey(now)) return scT("Today");
        if (key == scDayKey(now + 86400)) return scT("Tomorrow");
        struct tm t{};
        t.tm_year = key / 10000 - 1900; t.tm_mon = key / 100 % 100 - 1; t.tm_mday = key % 100; t.tm_hour = 12;
        mktime(&t);
        char buf[32];
        std::strftime(buf, sizeof buf, "%a %d", &t);
        return buf;
    }

    cairo_surface_t* crestFor(const std::string& path) {
        if (path.empty()) return nullptr;
        const auto it = crests.find(path);
        if (it != crests.end()) return it->second;
        cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); s = nullptr; }
        crests[path] = s;
        return s;
    }
    void drawCrest(cairo_t* cr, const ScTeam& t, double x, double y, double box, const Color& fg, double alpha, bool bottom = false) {
        cairo_surface_t* s = crestFor(t.logo);
        if (!s) {  // no crest: the abbreviation on a disc
            src(cr, g.color("surface_container_highest"), alpha);
            cairo_arc(cr, x + box / 2, y + box / 2, box / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            double tw = 0, th = 0;
            measure(t.abbr.substr(0, 3), 10, true, 0, tw, th);
            text(cr, t.abbr.substr(0, 3), x + (box - tw) / 2, y + (box - th) / 2, 10, true, fg, 0, alpha);
            return;
        }
        const double w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s), sc = std::min(box / w, box / h);
        cairo_save(cr);
        cairo_translate(cr, x + (box - w * sc) / 2, y + (bottom ? box - h * sc : (box - h * sc) / 2));  // bottom: crests of different shapes sit on the same line
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint_with_alpha(cr, alpha);
        cairo_restore(cr);
    }

    // A place in the table, on a small Material shape, centred on (cx, cy).
    void rankBadge(cairo_t* cr, int rank, double cx, double cy, double size, double alpha) {
        if (rank <= 0) return;
        shapePath(cr, "Cookie9Sided", cx - size / 2, cy - size / 2, size);
        src(cr, g.color("primary_container"), alpha);
        cairo_fill(cr);
        const std::string t = std::to_string(rank);
        double tw = 0, th = 0;
        measure(t, size * 0.46, true, 0, tw, th);
        text(cr, t, cx - tw / 2, cy - th / 2, size * 0.46, true, g.color("on_primary_container"), 0, alpha);
    }
    // The two pills of the top row that are only information: filled (primary) and outlined. Both are 44 tall, like the buttons beside them.
    double fillPill(cairo_t* cr, double x, const std::string& label, double maxW, double alpha) {
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        const double w = std::min(maxW, tw + 36);
        src(cr, g.color("primary"), alpha);
        rrect(cr, x, 0, w, 44, 22);
        cairo_fill(cr);
        trLine(cr, label, x + 18, (44 - th) / 2, w - 36, 15, true, g.color("on_primary"), alpha);
        return w;
    }
    double outlinePill(cairo_t* cr, double right, const std::string& label, double alpha) {  // right edge at `right`; returns its left edge
        double tw = 0, th = 0;
        measure(label, 14, true, 0, tw, th);
        const double w = tw + 32, x = right - w;
        src(cr, g.color("outline"), 0.8 * alpha);
        cairo_set_line_width(cr, 1.5);
        rrect(cr, x + 0.75, 0.75, w - 1.5, 42.5, 21.25);
        cairo_stroke(cr);
        text(cr, label, x + 16, (44 - th) / 2, 14, true, g.color("on_surface_variant"), 0, alpha);
        return x;
    }
    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    static double fitWidth(const std::string& s, double px, bool bold, double maxW) {
        double tw = 0, th = 0;
        measure(s, px, bold, 0, tw, th);
        return std::min(tw, maxW);
    }
    // Text that ends at xRight (the home side) or starts at xLeft, cut with an ellipsis when it does not fit.
    void sideText(cairo_t* cr, const std::string& s, double edge, bool rightAligned, double y, double maxW, double px, bool bold, const Color& fg, double alpha) {
        const double w = fitWidth(s, px, bold, maxW);
        trLine(cr, s, rightAligned ? edge - w : edge, y, maxW, px, bold, fg, alpha);
    }

    #include "page_score_views.inc"
    #include "page_score_slip.inc"

    void drawDayChips(cairo_t* cr, double limit) {
        double gap = 8, x = 0;
        std::vector<double> widths;
        for (int d : days) { double tw = 0, th = 0; measure(dayLabel(d), 15, true, 0, tw, th); widths.push_back(tw + 36); }
        double total = 0;
        for (double w : widths) total += w + gap;
        for (size_t i = 0; i < days.size(); i++) {
            const bool on = days[i] == dayKey;
            const Rect r{x, 0, widths[i], 44};
            if (r.x + r.w > limit && !on) { x += widths[i] + gap; continue; }
            const Color bg = on ? g.color("primary") : g.color("surface_container_high"), fg = on ? g.color("on_primary") : g.color("on_surface");
            const bool hov = hot(r, DayChip, days[i]);
            src(cr, hov ? mix(fg, bg, 0.14) : bg);
            rrect(cr, r.x, r.y, r.w, r.h, 22);
            cairo_fill(cr);
            double tw = 0, th = 0;
            measure(dayLabel(days[i]), 15, true, 0, tw, th);
            text(cr, dayLabel(days[i]), r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, 15, true, fg);
            x += widths[i] + gap;
        }
        (void)total;
    }

    double drawRefresh(cairo_t* cr, double W) {  // returns where the status text starts (what is left of it is free)
        const Rect r{W - 44, 0, 44, 44};
        const Color bg = g.color("surface_container_high"), fg = g.color("on_surface");
        const bool hov = hot(r, RefreshBtn);
        src(cr, hov ? mix(fg, bg, 0.14) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        cairo_fill(cr);
        cairo_save(cr);
        cairo_translate(cr, r.x + 22, r.y + 22);
        if (fetching()) cairo_rotate(cr, std::chrono::duration<double>(Clock::now() - spinStart).count() * 5.0);
        ::icon(cr, "refresh", -11, -11, 22, fg);
        cairo_restore(cr);
        std::string status = fetching() ? scT("Updating…") : failed ? scT("Could not load the matches") : "";
        if (status.empty() && madeAt > 0) {
            const long mins = std::max(0L, static_cast<long>((time(nullptr) - madeAt) / 60));
            status = mins < 1 ? scT("Updated just now") : scT("Updated") + " " + std::to_string(mins) + " min";
        }
        if (status.empty()) return r.x - 12;
        const double left = outlinePill(cr, r.x - 12, status, 1);
        return left;
    }

    // One row of the list; y is where it starts on screen.
    void drawMatch(cairo_t* cr, const ScMatch& m, const Rect& r, double alpha) {
        const Color bg = g.color("surface_container_high"), fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        const bool over = mx >= 0 && listArea.contains(mx, my) && r.contains(mx, my);
        src(cr, over ? mix(fg, bg, 0.06) : bg, alpha);
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        cairo_fill(cr);
        // Kick-off time (or the state) on the left.
        char tbuf[16];
        struct tm lt{};
        localtime_r(&m.kickoff, &lt);
        std::strftime(tbuf, sizeof tbuf, "%H:%M", &lt);
        const double tx = r.x + 18, midY = r.y + r.h / 2;
        double tw = 0, th = 0;
        const std::string main = m.live() ? (m.detail.empty() ? scT("Live") : m.detail) : m.done() ? scT("FT") : std::string(tbuf);
        const Color tc = m.live() ? g.color("error") : fg;
        measure(main, 16, true, 0, tw, th);
        text(cr, main, tx, midY - th / 2 - (m.done() || m.live() ? 0 : 0), 16, true, tc, 0, alpha);
        if (m.live()) {
            src(cr, g.color("error"), alpha);
            cairo_arc(cr, tx - 9, midY, 3.5, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        // The two teams around the score.
        const double probW = 150, leftW = 86;
        const double mid = r.x + leftW + (r.w - leftW - probW) / 2, scoreW = 116;
        const double sideW = (r.w - leftW - probW - scoreW) / 2 - kCrest - 18;
        drawCrest(cr, m.home, mid - scoreW / 2 - 8 - kCrest, midY - kCrest / 2, kCrest, dim, alpha);
        sideText(cr, m.home.name, mid - scoreW / 2 - 8 - kCrest - 10, true, midY - 10, std::max(40.0, sideW), 15, true, fg, alpha);
        drawCrest(cr, m.away, mid + scoreW / 2 + 8, midY - kCrest / 2, kCrest, dim, alpha);
        sideText(cr, m.away.name, mid + scoreW / 2 + 8 + kCrest + 10, false, midY - 10, std::max(40.0, sideW), 15, true, fg, alpha);
        rankBadge(cr, m.homeRank, mid - scoreW / 2 + 16, midY, 26, alpha);
        rankBadge(cr, m.awayRank, mid + scoreW / 2 - 16, midY, 26, alpha);
        const bool played = m.live() || m.done();
        const std::string score = played ? (m.home.score.empty() ? "0" : m.home.score) + " - " + (m.away.score.empty() ? "0" : m.away.score) : "vs";
        measure(score, played ? 20 : 14, true, 0, tw, th);
        text(cr, score, mid - tw / 2, midY - th / 2, played ? 20 : 14, true, played ? fg : dim, 0, alpha);
        // The model's chances: home / draw / away.
        if (m.pHome >= 0) {
            const double bx = r.x + r.w - probW - 16, bw = probW, by = midY - 11;
            const double parts[3] = {m.pHome, m.pDraw, m.pAway};
            const char* roles[3] = {"primary", "outline", "tertiary"};
            double x = bx;
            for (int i = 0; i < 3; i++) {
                const double w = std::max(0.0, bw * parts[i] - (i < 2 ? 3 : 0));
                const int best = parts[0] >= parts[1] && parts[0] >= parts[2] ? 0 : parts[1] >= parts[2] ? 1 : 2;
                src(cr, g.color(roles[i]), (i == best ? 1.0 : 0.55) * alpha);
                rrect(cr, x, by, w, 6, 3);
                cairo_fill(cr);
                x += bw * parts[i];
            }
            char pb[48];
            std::snprintf(pb, sizeof pb, "%d%%", static_cast<int>(std::lround(parts[0] * 100)));
            const std::string a = pb;
            std::snprintf(pb, sizeof pb, "%d%%", static_cast<int>(std::lround(parts[1] * 100)));
            const std::string b = pb;
            std::snprintf(pb, sizeof pb, "%d%%", static_cast<int>(std::lround(parts[2] * 100)));
            const std::string c = pb;
            double w1 = 0, w2 = 0, h1 = 0;
            measure(c, 12, true, 0, w2, h1);
            measure(b, 12, false, 0, w1, h1);
            text(cr, a, bx, by + 12, 12, true, dim, 0, alpha);
            text(cr, b, bx + (bw - w1) / 2, by + 12, 12, false, dim, 0, alpha);
            text(cr, c, bx + bw - w2, by + 12, 12, true, dim, 0, alpha);
        } else {
            const std::string none = "—";
            measure(none, 14, false, 0, tw, th);
            text(cr, none, r.x + r.w - probW / 2 - 16 - tw / 2, midY - th / 2, 14, false, dim, 0, 0.6);
        }
    }

    // The notice under every view: this page is for entertainment only.
    static constexpr double kFootH = 36;
    static double cardHeight(const Rect& r) { return std::max(120.0, r.h - 58 - kFootH); }
    void drawNotice(cairo_t* cr, Rect r) {
        const std::string note = scT("For entertainment only. The numbers are estimates, not advice. Do not bet money you cannot afford to lose. 18+ only.");
        double tw = 0, th = 0;
        measure(note, 12, false, 0, tw, th);
        const double w = std::min(r.w - 40, tw + 30), x = (r.w - w) / 2, y = r.h - kFootH + (kFootH - th) / 2 + 2;
        ::icon(cr, "danger-triangle", x, y - 1, 16, g.color("on_surface_variant"));
        trLine(cr, note, x + 22, y, w - 22, 12, false, g.color("on_surface_variant"), 0.85);
    }
    void draw(cairo_t* cr, Rect r) override {
        drawBody(cr, r);
        drawNotice(cr, r);
    }
    void drawBody(cairo_t* cr, Rect r) {
        hits.clear();
        legTargets.clear();
        if (detailOpen) {
            const double a = detailAlpha();
            if (detailClosing && a <= 0) { detailOpen = detailClosing = false; dStage = 0; djob.kill(); dayShown = Clock::now(); }
            else { drawDetail(cr, r, a); return; }
        }
        const double statusLeft = drawRefresh(cr, r.w), pillsLeft = drawModePills(cr, statusLeft - 16);
        if (mode == Matches || mode == Picks) drawDayChips(cr, pillsLeft - 12);
        else if (mode == Record) drawRecordHeader(cr, pillsLeft);
        if (mode == Slip) { drawSlipHeader(cr, pillsLeft); drawSlip(cr, r); return; }
        if (mode == Picks) { drawPicks(cr, r); return; }
        if (mode == Record) { drawRecord(cr, r); return; }
        const Rect card{0, 58, r.w, cardHeight(r)};
        src(cr, g.color("surface_container"));
        rrect(cr, card.x, card.y, card.w, card.h, 28);
        cairo_fill(cr);
        listArea = {card.x + 12, card.y + 12, card.w - 24, card.h - 24};
        std::vector<const ScMatch*> shown = ordered();
        if (shown.empty()) {
            const std::string msg = fetching() && !everLoaded ? scT("Loading…") : failed && !everLoaded ? scT("Could not load the matches") : scT("No matches");
            double tw = 0, th = 0;
            measure(msg, 16, false, 0, tw, th);
            text(cr, msg, card.x + (card.w - tw) / 2, card.y + (card.h - th) / 2, 16, false, g.color("on_surface_variant"));
            scrollMax = 0;
            return;
        }
        // The rows: a header for every league, then its matches.
        double contentH = 0;
        std::string lastLeague;
        for (const ScMatch* m : shown) {
            if (m->leagueId != lastLeague) { contentH += (lastLeague.empty() ? 0 : 6) + kHeadH; lastLeague = m->leagueId; }
            contentH += kRowH + kRowGap;
        }
        contentH -= kRowGap;
        scrollMax = std::max(0.0, contentH - listArea.h);
        listPos = std::clamp(listPos, 0.0, scrollMax);
        const double since = std::chrono::duration<double>(Clock::now() - dayShown).count();
        lastFade = since;
        cairo_save(cr);
        cairo_rectangle(cr, listArea.x, listArea.y, listArea.w, listArea.h);
        cairo_clip(cr);
        double y = listArea.y - (listPos + band.over);
        lastLeague.clear();
        int index = 0;
        const double rowW = listArea.w - (scrollMax > 0 ? 10 : 0);
        for (const ScMatch* m : shown) {
            if (m->leagueId != lastLeague) {
                if (!lastLeague.empty()) y += 6;
                lastLeague = m->leagueId;
                if (y + kHeadH > listArea.y && y < listArea.y + listArea.h)
                    text(cr, m->league, listArea.x + 8, y + 10, 14, true, g.color("on_surface_variant"));
                y += kHeadH;
            }
            const double a = std::clamp((since - 0.03 * std::min(index, 8)) / 0.2, 0.0, 1.0), eased = 1 - (1 - a) * (1 - a) * (1 - a);
            const Rect row{listArea.x, y + (1 - eased) * 12, rowW, kRowH};
            if (y + kRowH > listArea.y && y < listArea.y + listArea.h) { drawMatch(cr, *m, row, eased); hits.push_back({row, Row, static_cast<int>(m - matches.data())}); }
            y += kRowH + kRowGap;
            index++;
        }
        cairo_restore(cr);
        if (scrollMax > 0 && band.thumbAlpha() > 0) {
            const double th = std::max(36.0, listArea.h * listArea.h / contentH), ty = listArea.y + (listArea.h - th) * (listPos / scrollMax);
            src(cr, g.color("on_surface_variant"), 0.35 * band.thumbAlpha());
            rrect(cr, listArea.x + listArea.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    // The matches of the chosen day: the leagues with a match in play or still to come first, each league once with its own matches in order.
    static int stateRank(const ScMatch& m) { return m.live() ? 0 : m.done() ? 2 : 1; }
    std::vector<const ScMatch*> ordered() const {
        std::vector<const ScMatch*> v;
        for (const ScMatch& m : matches) if (m.dayKey == dayKey) v.push_back(&m);
        std::map<std::string, std::pair<int, time_t>> leagueKey;  // best state, first kick-off
        for (const ScMatch* m : v) {
            auto it = leagueKey.find(m->leagueId);
            const std::pair<int, time_t> k{stateRank(*m), m->kickoff};
            if (it == leagueKey.end()) leagueKey[m->leagueId] = k;
            else it->second = std::min(it->second, k);
        }
        std::stable_sort(v.begin(), v.end(), [&](const ScMatch* a, const ScMatch* b) {
            if (a->leagueId != b->leagueId) return leagueKey[a->leagueId] < leagueKey[b->leagueId];
            return std::make_pair(stateRank(*a), a->kickoff) < std::make_pair(stateRank(*b), b->kickoff);
        });
        return v;
    }

    // ---- the detail of a match -----------------------------------------------------------------------------------------------------
    void openDetail(int index) {
        if (index < 0 || index >= static_cast<int>(matches.size())) return;
        dMatch = matches[static_cast<size_t>(index)];
        detail = {};
        detail.rank[0] = dMatch.homeRank;  // (the places in the table come with the list: the badges and the shapes are there from the first frame)
        detail.rank[1] = dMatch.awayRank;
        dRemoteDone = false;
        dPos = 0;
        dBand.reset();
        detailOpen = true;
        detailClosing = false;
        detailShown = Clock::now();
        dStage = djob.start({"python3", "-I", scriptPath(), "--detail", dMatch.id, "--local"}, "", 15000) ? 1 : 0;
    }
    void closeDetail() {
        if (!detailOpen || detailClosing) return;
        detailClosing = true;
        detailShown = Clock::now();
    }
    void finishDetailJob() {
        const int which = dStage;
        dStage = 0;
        if (djob.ok) {
            detail = scParseDetail(djob.out);
            if (detail.rank[0] == 0) detail.rank[0] = dMatch.homeRank;
            if (detail.rank[1] == 0) detail.rank[1] = dMatch.awayRank;
        }
        if (which == 1) dStage = djob.start({"python3", "-I", scriptPath(), "--detail", dMatch.id}, "", 120000) ? 2 : 0;
        else dRemoteDone = true;
    }

    static std::string pct(double p) {
        char b[16];
        std::snprintf(b, sizeof b, "%d%%", static_cast<int>(std::lround(p * 100)));
        return b;
    }
    static std::string num1(double v) {
        char b[24];
        std::snprintf(b, sizeof b, "%.1f", v);
        return b;
    }

    // A titled card; returns the area inside it. The caller draws, then calls endCard to move y.
    Rect beginCard(cairo_t* cr, double x, double y, double w, double h, const std::string& title, double alpha) {
        src(cr, g.color("surface_container_high"), alpha);
        rrect(cr, x, y, w, h, 24);
        cairo_fill(cr);
        if (!title.empty()) text(cr, title, x + 20, y + 16, 14, true, g.color("on_surface_variant"), 0, alpha);
        return {x + 20, y + (title.empty() ? 16 : 46), w - 40, h - (title.empty() ? 32 : 62)};
    }
    void chanceCell(cairo_t* cr, const Rect& c, const std::string& label, double p, bool best, const ScLeg& leg, double alpha) {
        const bool hov = hotLeg(c, leg), in = inSlip(leg);
        src(cr, in ? g.color("primary_container") : hov ? mix(g.color("on_surface"), g.color("surface_container_highest"), 0.08) : g.color("surface_container_highest"), alpha);
        rrect(cr, c.x, c.y, c.w, c.h, 16);
        cairo_fill(cr);
        text(cr, label, c.x + 14, c.y + 10, 13, false, g.color("on_surface_variant"), 0, alpha);
        text(cr, pct(p), c.x + 14, c.y + 28, 18, true, g.color("on_surface"), 0, alpha);
        ::icon(cr, in ? "check-circle" : "add-circle", c.x + c.w - 32, c.y + 8, 20, g.color(in ? "primary" : "on_surface_variant"), in);
        src(cr, g.color("outline"), 0.3 * alpha);
        rrect(cr, c.x + 14, c.y + c.h - 14, c.w - 28, 4, 2);
        cairo_fill(cr);
        src(cr, g.color(best ? "primary" : "secondary"), alpha);
        rrect(cr, c.x + 14, c.y + c.h - 14, std::max(4.0, (c.w - 28) * std::clamp(p, 0.0, 1.0)), 4, 2);
        cairo_fill(cr);
    }
    // The result of a match on a Material shape: W a cookie, L a burst, D a diamond, each with its letter.
    void formShape(cairo_t* cr, const std::string& result, double x, double y, double size, double alpha) {
        const char r = result.empty() ? 'D' : result[0];
        shapePath(cr, r == 'W' ? "Cookie9Sided" : r == 'L' ? "SoftBurst" : "Diamond", x, y, size);
        src(cr, g.color(r == 'W' ? "primary" : r == 'L' ? "error" : "secondary"), alpha);
        cairo_fill(cr);
        const std::string t = scT(r == 'W' ? "W" : r == 'L' ? "L" : "D");  // (W, D, L: the letters of win, draw and loss)
        double tw = 0, th = 0;
        measure(t, size * 0.42, true, 0, tw, th);
        text(cr, t, x + (size - tw) / 2, y + (size - th) / 2, size * 0.42, true, g.color(r == 'W' ? "on_primary" : r == 'L' ? "on_error" : "on_secondary"), 0, alpha);
    }

    static bool state0Empty(const ScMatch& m) { return !m.live() && !m.done(); }
    void drawDetail(cairo_t* cr, Rect r, double alpha) {
        const Color fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        const double slide = (1 - alpha) * 28;
        // Top row: Back and the competition.
        {
            const Rect b{0, 0, 112, 44};
            const bool hov = hot(b, BackBtn);
            const Color bg = g.color("surface_container_high");
            src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
            rrect(cr, b.x, b.y, b.w, b.h, 22);
            cairo_fill(cr);
            ::icon(cr, "alt-arrow-left", b.x + 16, b.y + 11, 22, fg);
            text(cr, scT("Back"), b.x + 46, b.y + 11, 15, true, fg, 0, alpha);
            char tb[48];
            struct tm lt{};
            localtime_r(&dMatch.kickoff, &lt);
            std::strftime(tb, sizeof tb, "%a %d %b · %H:%M", &lt);
            const std::string info = dMatch.league + "  ·  " + tb;
            const double infoLeft = outlinePill(cr, r.w, info, alpha);
            fillPill(cr, b.w + 12, dMatch.home.name + "  –  " + dMatch.away.name, std::max(100.0, infoLeft - 12 - (b.w + 12)), alpha);
        }
        const Rect card{0, 58, r.w, cardHeight(r)};
        src(cr, g.color("surface_container"), alpha);
        rrect(cr, card.x, card.y, card.w, card.h, 28);
        cairo_fill(cr);
        const Rect view{card.x + 12, card.y + 12, card.w - 24, card.h - 24};
        listArea = view;
        dPos = std::clamp(dPos, 0.0, dMax);
        cairo_save(cr);
        cairo_rectangle(cr, view.x, view.y, view.w, view.h);
        cairo_clip(cr);
        const double W = view.w - 10, X = view.x + slide;
        double y = view.y - (dPos + dBand.over);
        const double top = y;
        // 1. The two teams: each crest on a big Material shape, the names under them, the score (or the time) between them. The space above the
        // league name and below the team names is the same.
        constexpr double kHeroPad = 22, kHeroShape = 124, kHeroGap = 10, kHeroTop = 28;
        {
            const double alpha = 1.0, X = view.x;  // (the top card is there at once, like the shapes of the task manager: no fade, no slide)
            double nameW0 = 0, nameH = 0, leagueH = 0;
            measure("Ag", 24, true, 0, nameW0, nameH);
            measure("Ag", 13, true, 0, nameW0, leagueH);
            const double shapeY = y + kHeroPad + leagueH + 12, nameY = shapeY + kHeroShape + kHeroGap;
            const double h = nameY + nameH - y + kHeroPad - 3;  // (-3: the line box of the name is a bit taller than its letters)
            (void)kHeroTop;
            const Color bg = g.color("primary_container"), on = g.color("on_primary_container");
            src(cr, bg, alpha);
            rrect(cr, X, y, W, h, 28);
            cairo_fill(cr);
            const double cx = X + W / 2;
            double tw = 0, th = 0;
            // A big soft shape fills the side of the team that is higher in the table (clipped by the card).
            {
                const int better = detail.rank[0] > 0 && detail.rank[1] > 0 && detail.rank[0] != detail.rank[1] ? (detail.rank[0] < detail.rank[1] ? 0 : 1) : -1;
                const double size = h * 1.7, sy = y + (h - size) / 2 + h * 0.08;
                cairo_save(cr);
                rrect(cr, X, y, W, h, 28);
                cairo_clip(cr);
                shapePath(cr, "Puffy", X - size * 0.38, sy, size);  // half of a big shape on each side; the one of the team higher in the table is stronger
                src(cr, on, (better == 0 ? 0.15 : 0.07));
                cairo_fill(cr);
                shapePath(cr, "SoftBoom", X + W - size * 0.62, sy, size);
                src(cr, on, (better == 1 ? 0.15 : 0.07));
                cairo_fill(cr);
                cairo_restore(cr);
            }
            measure(dMatch.league, 13, true, 0, tw, th);
            trLine(cr, dMatch.league, cx - std::min(tw, W * 0.5) / 2, y + kHeroPad, W * 0.5, 13, true, on, 0.8 * alpha);
            for (int side = 0; side < 2; side++) {
                const ScTeam& t = side == 0 ? dMatch.home : dMatch.away;
                const double mid = side == 0 ? X + W * 0.22 : X + W * 0.78;
                shapePath(cr, side == 0 ? "Cookie12Sided" : "SoftBurst", mid - kHeroShape / 2, shapeY, kHeroShape);
                src(cr, g.color("surface_container_highest"), alpha);
                cairo_fill(cr);
                drawCrest(cr, t, mid - 34, shapeY + (kHeroShape - 68) / 2, 68, on, alpha);
                if (detail.rank[side] > 0) {  // the place in the table, on a small shape at the bottom corner of the big one
                    const double bs = 38, bx = mid + kHeroShape / 2 - bs * 0.85, by = shapeY + kHeroShape - bs * 0.9;
                    shapePath(cr, "Cookie9Sided", bx, by, bs);
                    src(cr, g.color("primary"), alpha);
                    cairo_fill(cr);
                    const std::string rk = std::to_string(detail.rank[side]);
                    measure(rk, 15, true, 0, tw, th);
                    text(cr, rk, bx + (bs - tw) / 2, by + (bs - th) / 2, 15, true, g.color("on_primary"), 0, alpha);
                }
                const double nw = std::min(fitWidth(t.name, 24, true, W * 0.4), W * 0.4);
                trLine(cr, t.name, mid - nw / 2, nameY, W * 0.4, 24, true, on, alpha);
            }
            const bool played = dMatch.live() || dMatch.done();
            const std::string big = played ? (dMatch.home.score.empty() ? "0" : dMatch.home.score) + " - " + (dMatch.away.score.empty() ? "0" : dMatch.away.score) : "vs";
            const double bigPx = played ? 64 : 44;
            measure(big, bigPx, true, 0, tw, th);
            const double midY = shapeY + kHeroShape / 2;
            text(cr, big, cx - tw / 2, midY - th / 2 - 8, bigPx, true, on, 0, alpha);
            // Under the score: the state of the match, or the time of the kick-off and the day.
            char tb[32], db[32];
            struct tm lt{};
            localtime_r(&dMatch.kickoff, &lt);
            std::strftime(tb, sizeof tb, "%H:%M", &lt);
            std::strftime(db, sizeof db, "%a %d %b", &lt);
            const std::string state = dMatch.live() ? (dMatch.detail.empty() ? scT("Live") : dMatch.detail) : dMatch.done() ? scT("Full time") : std::string(tb);
            measure(state, 17, true, 0, tw, th);
            const double sy = nameY + nameH - 3 - kHeroPad - 6 - th - 18;  // two lines at the bottom of the card, level with the names
            text(cr, state, cx - tw / 2, sy, 17, true, dMatch.live() ? g.color("error") : on, 0, alpha);
            measure(db, 12, false, 0, tw, th);
            text(cr, db, cx - tw / 2, sy + 22, 12, false, on, 0, 0.75 * alpha);
            y += h + 12;
        }
        // 2. Chances.
        if (detail.has("home")) {
            const double ph = detail.of("home"), pd = detail.of("draw"), pa = detail.of("away");
            const double h = 150;
            const Rect in = beginCard(cr, X, y, W, h, scT("Who wins"), alpha);
            const double parts[3] = {ph, pd, pa};
            const char* roles[3] = {"primary", "outline", "tertiary"};
            const std::string names[3] = {dMatch.home.name, scT("Draw"), dMatch.away.name};
            const int best = ph >= pd && ph >= pa ? 0 : pd >= pa ? 1 : 2;
            double x = in.x;
            for (int i = 0; i < 3; i++) {
                const double w = std::max(0.0, in.w * parts[i] - (i < 2 ? 4 : 0));
                src(cr, g.color(roles[i]), (i == best ? 1.0 : 0.5) * alpha);
                rrect(cr, x, in.y, w, 16, 8);
                cairo_fill(cr);
                x += in.w * parts[i];
            }
            for (int i = 0; i < 3; i++) {
                const double colW = in.w / 3, cx0 = in.x + i * colW;
                {
                    static const char* keys1x2[3] = {"home", "draw", "away"};
                    const ScLeg leg = legFor(dMatch, keys1x2[i], i == 0 ? dMatch.home.name + " wins" : i == 1 ? "Draw" : dMatch.away.name + " wins", parts[i], 0, false);
                    double pw = 0, ph2 = 0;
                    measure(pct(parts[i]), 22, true, 0, pw, ph2);
                    const double bw = std::max(pw, fitWidth(names[i], 13, false, colW - 8)) + 24;  // the box wraps the percentage and the name, aligned like them
                    const Rect hit{i == 0 ? in.x - 12 : i == 1 ? cx0 + (colW - bw) / 2 : cx0 + colW - bw + 12, in.y + 22, bw, 66};
                    const bool hv = hotLeg(hit, leg);
                    if (inSlip(leg) || hv) {
                        src(cr, inSlip(leg) ? g.color("primary_container") : g.color("on_surface"), inSlip(leg) ? alpha : 0.06 * alpha);
                        rrect(cr, hit.x, hit.y, hit.w, hit.h, 16);
                        cairo_fill(cr);
                    }
                }
                double tw = 0, th = 0;
                const std::string p = pct(parts[i]);
                measure(p, 22, true, 0, tw, th);
                const double ax = i == 0 ? cx0 : i == 1 ? cx0 + (colW - tw) / 2 : cx0 + colW - tw;
                text(cr, p, ax, in.y + 28, 22, true, fg, 0, alpha);
                const double nw = fitWidth(names[i], 13, false, colW - 8);
                const double nx = i == 0 ? cx0 : i == 1 ? cx0 + (colW - nw) / 2 : cx0 + colW - nw;
                trLine(cr, names[i], nx, in.y + 60, colW - 8, 13, false, dim, alpha);
            }
            y += h + 12;
            // 3. Markets.
            struct Mk { const char* key; const char* label; };
            const Mk mk[] = {{"o15", "Over 1.5 goals"}, {"o25", "Over 2.5 goals"}, {"o35", "Over 3.5 goals"}, {"btts", "Both teams score"},
                             {"dc1x", "Home or draw"}, {"dcx2", "Draw or away"}, {"dc12", "No draw"}, {"c85", "Over 8.5 corners"},
                             {"c75", "Over 7.5 corners"}, {"c95", "Over 9.5 corners"}, {"c105", "Over 10.5 corners"}};
            std::vector<const Mk*> have;
            for (const Mk& m : mk) if (detail.has(m.key)) have.push_back(&m);
            const int cols = std::max(2, static_cast<int>((W - 40 + 10) / 190));
            const double cw = (W - 40 - (cols - 1) * 10) / cols, chH = 76;
            const int rows = (static_cast<int>(have.size()) + cols - 1) / cols;
            const double mh = 46 + rows * (chH + 10) - 10 + 20;
            const Rect mi = beginCard(cr, X, y, W, mh, scT("Markets"), alpha);
            double bestP = 0;
            for (const Mk* m : have) bestP = std::max(bestP, detail.of(m->key));
            for (size_t i = 0; i < have.size(); i++) {
                const size_t rowOf = i / cols, inRow = std::min<size_t>(cols, have.size() - rowOf * cols);  // (the last row stretches to fill the width)
                const double cellW = (mi.w - (inRow - 1) * 10) / inRow;
                const Rect c{mi.x + (i % cols) * (cellW + 10), mi.y + rowOf * (chH + 10), cellW, chH};
                chanceCell(cr, c, scT(have[i]->label), detail.of(have[i]->key), detail.of(have[i]->key) >= bestP, legFor(dMatch, have[i]->key, have[i]->label, detail.of(have[i]->key), 0, false), alpha);
            }
            y += mh + 12;
        }
        // 4. What the model expects: four tiles on a grid (goals, corners, the likeliest score, the pick) and the likely scores under them.
        if (detail.xHome >= 0 || !detail.scores.empty()) {
            struct Tile { std::string caption, value, sub; const char* shape; const char* ic; double bar; };
            std::vector<Tile> tiles;
            if (detail.xHome >= 0) tiles.push_back({scT("Goals expected"), num1(detail.xHome) + " - " + num1(detail.xAway), "", "Cookie9Sided", "football", detail.xHome / std::max(0.1, detail.xHome + detail.xAway)});
            if (detail.xCorners > 0) tiles.push_back({scT("Corners expected"), num1(detail.xCorners), scT("in total"), "Flower", "flag", -1});
            if (!detail.scores.empty()) tiles.push_back({scT("Most likely score"), detail.scores[0].first, pct(detail.scores[0].second), "Clover4Leaf", "cup-first", -1});
            if (!detail.pickLabel.empty()) tiles.push_back({scT("Model pick"), pct(detail.pickChance), scT(detail.pickLabel.c_str()), "SoftBurst", "star", -1});
            const bool chips = detail.scores.size() > 1;
            const double tileH = 104, h = 46 + tileH + (chips ? 14 + 22 + 44 : 0) + 20;
            const Rect in = beginCard(cr, X, y, W, h, scT("What the model expects"), alpha);
            const int n = std::max<int>(1, static_cast<int>(tiles.size()));
            const double gap = 10, tw0 = (in.w - (n - 1) * gap) / n;
            for (size_t i = 0; i < tiles.size(); i++) {
                const Tile& t = tiles[i];
                const Rect c{in.x + i * (tw0 + gap), in.y, tw0, tileH};
                const bool pick = i + 1 == tiles.size() && !detail.pickLabel.empty();
                src(cr, g.color(pick ? "primary_container" : "surface_container_highest"), alpha);
                rrect(cr, c.x, c.y, c.w, c.h, 20);
                cairo_fill(cr);
                const Color fgc = g.color(pick ? "on_primary_container" : "on_surface"), sub = g.color(pick ? "on_primary_container" : "on_surface_variant");
                shapePath(cr, t.shape, c.x + c.w - 16 - 40, c.y + 14, 40);
                src(cr, fgc, 0.14 * alpha);
                cairo_fill(cr);
                ::icon(cr, t.ic, c.x + c.w - 16 - 40 + 10, c.y + 14 + 10, 20, fgc);
                text(cr, t.caption, c.x + 16, c.y + 14, 13, false, sub, 0, 0.85 * alpha);
                text(cr, t.value, c.x + 16, c.y + 36, 28, true, fgc, 0, alpha);
                if (t.bar >= 0) {  // goals: the share of the home team
                    const double bw = c.w - 32;
                    src(cr, g.color("tertiary"), 0.6 * alpha);
                    rrect(cr, c.x + 16, c.y + c.h - 22, bw, 6, 3);
                    cairo_fill(cr);
                    src(cr, g.color("primary"), alpha);
                    rrect(cr, c.x + 16, c.y + c.h - 22, std::max(6.0, bw * t.bar), 6, 3);
                    cairo_fill(cr);
                } else if (!t.sub.empty()) {
                    trLine(cr, t.sub, c.x + 16, c.y + c.h - 30, c.w - 32, 14, pick, sub, 0.9 * alpha);
                }
            }
            if (chips) {
                const double cy = in.y + tileH + 14;
                text(cr, scT("Likely scores"), in.x + 4, cy, 13, false, dim, 0, 0.85 * alpha);
                const size_t m = std::min<size_t>(detail.scores.size(), 5);
                const double cw = (in.w - (m - 1) * gap) / m;
                for (size_t i = 0; i < m; i++) {
                    const auto& sc = detail.scores[i];
                    const Rect c{in.x + i * (cw + gap), cy + 22, cw, 44};
                    src(cr, g.color("secondary_container"), alpha);
                    rrect(cr, c.x, c.y, c.w, c.h, 22);
                    cairo_fill(cr);
                    double tw = 0, th = 0;
                    measure(sc.first, 17, true, 0, tw, th);
                    text(cr, sc.first, c.x + 20, c.y + (c.h - th) / 2, 17, true, g.color("on_secondary_container"), 0, alpha);
                    const std::string p = pct(sc.second);
                    measure(p, 14, false, 0, tw, th);
                    text(cr, p, c.x + c.w - 20 - tw, c.y + (c.h - th) / 2, 14, false, g.color("on_secondary_container"), 0, 0.8 * alpha);
                }
            }
            y += h + 12;
        }
        // 5. Recent form of each team: a row per match with the result on a Material shape (W, D and L each have their own).
        {
            constexpr double rowH = 44, gap = 6;
            const double rowsN = std::max<double>(1, std::max(detail.form[0].size(), detail.form[1].size()));
            const double h = 46 + 34 + rowsN * (rowH + gap) + 8;
            const Rect in = beginCard(cr, X, y, W, h, scT("Recent form"), alpha);
            if (!dRemoteDone && detail.form[0].empty() && detail.form[1].empty()) {
                text(cr, scT("Loading…"), in.x, in.y, 14, false, dim, 0, 0.8 * alpha);
            } else {
                for (int side = 0; side < 2; side++) {
                    const double colW = (in.w - 24) / 2, x = in.x + side * (colW + 24);
                    int w = 0, d = 0, l = 0;
                    for (const ScForm& f : detail.form[side]) { const char c = f.result.empty() ? 'D' : f.result[0]; (c == 'W' ? w : c == 'L' ? l : d)++; }
                    const std::string tally = std::to_string(w) + scT("W") + "  " + std::to_string(d) + scT("D") + "  " + std::to_string(l) + scT("L");
                    double tw = 0, th = 0;
                    measure(tally, 13, true, 0, tw, th);
                    trLine(cr, side == 0 ? dMatch.home.name : dMatch.away.name, x + 4, in.y, colW - tw - 20, 15, true, fg, alpha);
                    text(cr, tally, x + colW - tw - 4, in.y + 2, 13, true, dim, 0, alpha);
                    for (size_t i = 0; i < detail.form[side].size(); i++) {  // newest first
                        const ScForm& f = detail.form[side][detail.form[side].size() - 1 - i];
                        const double ry = in.y + 34 + i * (rowH + gap);
                        src(cr, g.color("surface_container_highest"), alpha);
                        rrect(cr, x, ry, colW, rowH, 16);
                        cairo_fill(cr);
                        formShape(cr, f.result, x + 8, ry + (rowH - 30) / 2, 30, alpha);
                        const bool away = f.ha == "away";
                        trLine(cr, f.against, x + 50, ry + (rowH - 17) / 2, colW - 50 - 100, 14, false, fg, alpha);
                        const std::string where = away ? scT("Away game") : scT("At home");
                        measure(f.score, 14, true, 0, tw, th);
                        text(cr, f.score, x + colW - 14 - tw, ry + (rowH - th) / 2, 14, true, fg, 0, alpha);
                        double ww = 0, wh = 0;
                        measure(where, 12, false, 0, ww, wh);
                        text(cr, where, x + colW - 14 - tw - 14 - ww, ry + (rowH - wh) / 2, 12, false, dim, 0, 0.8 * alpha);
                    }
                }
            }
            y += h + 12;
        }
        // 6. Previous meetings: the date, the two teams around the score, the competition.
        {
            const double rowH = 44, rowsN = std::max<double>(1, detail.meets.size());
            const double h = 46 + rowsN * rowH + 12;
            const Rect in = beginCard(cr, X, y, W, h, scT("Last meetings"), alpha);
            if (detail.meets.empty()) {
                text(cr, dRemoteDone ? scT("No recent meetings found") : scT("Loading…"), in.x, in.y, 14, false, dim, 0, 0.8 * alpha);
            }
            const double mid = in.x + in.w / 2, scoreW = 78, sideW = in.w / 2 - scoreW / 2 - 150;
            for (size_t i = 0; i < detail.meets.size(); i++) {
                const ScMeet& m = detail.meets[i];
                const double ry = in.y + i * rowH, my2 = ry + (rowH - 8) / 2;
                src(cr, g.color("surface_container_highest"), alpha);
                rrect(cr, in.x, ry, in.w, rowH - 6, 16);
                cairo_fill(cr);
                text(cr, m.date.substr(0, 10), in.x + 16, ry + (rowH - 6 - 16) / 2, 13, false, dim, 0, alpha);
                const bool homeWon = m.homeGoals > m.awayGoals, awayWon = m.awayGoals > m.homeGoals;
                sideText(cr, m.home, mid - scoreW / 2 - 12, true, ry + (rowH - 6 - 18) / 2, std::max(60.0, sideW), 15, homeWon, fg, alpha);
                sideText(cr, m.away, mid + scoreW / 2 + 12, false, ry + (rowH - 6 - 18) / 2, std::max(60.0, sideW), 15, awayWon, fg, alpha);
                const std::string sc = std::to_string(m.homeGoals) + " - " + std::to_string(m.awayGoals);
                double tw = 0, th = 0;
                measure(sc, 16, true, 0, tw, th);
                src(cr, g.color("secondary_container"), alpha);
                rrect(cr, mid - scoreW / 2, ry + 4, scoreW, rowH - 14, (rowH - 14) / 2);
                cairo_fill(cr);
                text(cr, sc, mid - tw / 2, ry + (rowH - 6 - th) / 2, 16, true, g.color("on_secondary_container"), 0, alpha);
                const double cw = fitWidth(m.competition, 13, false, 130);
                trLine(cr, m.competition, in.x + in.w - 16 - cw, ry + (rowH - 6 - 16) / 2, 130, 13, false, dim, alpha);
                (void)my2;
            }
            y += h + 12;
        }
        // 7. Key stats: for each one, the two values around its name and a bar from the middle toward each side (the longer the bigger).
        if (detail.season[0].games > 0 || detail.xHome >= 0 || detail.has("home")) {
            struct Stat { std::string label, left, right; double l, r; };
            std::vector<Stat> rows;
            if (detail.has("home")) rows.push_back({scT("Win chance"), pct(detail.of("home")), pct(detail.of("away")), detail.of("home"), detail.of("away")});
            if (detail.xHome >= 0) rows.push_back({scT("Goals expected"), num1(detail.xHome), num1(detail.xAway), detail.xHome, detail.xAway});
            if (detail.season[0].games > 0) {
                const ScSeason &a = detail.season[0], &b = detail.season[1];
                rows.push_back({scT("Matches played"), std::to_string(a.games), std::to_string(b.games), static_cast<double>(a.games), static_cast<double>(b.games)});
                rows.push_back({scT("Goals scored"), num1(a.gf), num1(b.gf), a.gf, b.gf});
                rows.push_back({scT("Goals conceded"), num1(a.ga), num1(b.ga), a.ga, b.ga});
                rows.push_back({scT("Corners won"), num1(a.cf), num1(b.cf), a.cf, b.cf});
                rows.push_back({scT("Corners conceded"), num1(a.ca), num1(b.ca), a.ca, b.ca});
            }
            constexpr double rowH = 58;
            const double h = 46 + rows.size() * rowH + 6;
            const Rect in = beginCard(cr, X, y, W, h, scT("Key stats"), alpha);
            const double mid = in.x + in.w / 2, half = in.w / 2 - 2;
            for (size_t i = 0; i < rows.size(); i++) {
                const Stat& st = rows[i];
                const double ry = in.y + i * rowH;
                text(cr, st.left, in.x, ry, 16, true, fg, 0, alpha);
                double tw = 0, th = 0;
                measure(st.right, 16, true, 0, tw, th);
                text(cr, st.right, in.x + in.w - tw, ry, 16, true, fg, 0, alpha);
                measure(st.label, 15, true, 0, tw, th);
                text(cr, st.label, mid - tw / 2, ry, 15, true, fg, 0, alpha);
                const double top2 = std::max(0.0001, std::max(st.l, st.r)) * 1.15, by = ry + 28;
                src(cr, g.color("surface_container_highest"), alpha);  // the two tracks
                rrect(cr, in.x, by, half, 8, 4);
                cairo_fill(cr);
                rrect(cr, mid + 2, by, half, 8, 4);
                cairo_fill(cr);
                const double lw = std::clamp(st.l / top2, 0.03, 1.0) * half, rw = std::clamp(st.r / top2, 0.03, 1.0) * half;
                src(cr, g.color("primary"), (st.l >= st.r ? 1.0 : 0.55) * alpha);
                rrect(cr, mid - lw, by, lw, 8, 4);
                cairo_fill(cr);
                src(cr, g.color("tertiary"), (st.r >= st.l ? 1.0 : 0.55) * alpha);
                rrect(cr, mid + 2, by, rw, 8, 4);
                cairo_fill(cr);
            }
            y += h + 12;
        }
        const double contentH = y - 12 - top;
        dMax = std::max(0.0, contentH - view.h);
        cairo_restore(cr);
        if (dMax > 0 && dBand.thumbAlpha() > 0) {
            const double th = std::max(36.0, view.h * view.h / contentH), ty = view.y + (view.h - th) * (dPos / dMax);
            src(cr, g.color("on_surface_variant"), 0.35 * dBand.thumbAlpha());
            rrect(cr, view.x + view.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    // ---- input ------------------------------------------------------------------------------------------------------------------
    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->id != None && it->r.contains(x, y) && (!needsList(it->id) || listArea.contains(x, y))) return {it->id, it->arg};
        return {};
    }
    bool motion(double x, double y) override {
        mx = x; my = y;
        const Hover h = hitAt(x, y);
        const bool changed = h.id != hover.id || h.arg != hover.arg || (!detailOpen && listArea.contains(x, y));
        hover = h;
        return changed;
    }
    bool leave() override {
        const bool changed = hover.id != None || mx >= 0;
        mx = my = -1;
        hover = {};
        return changed;
    }
    Cursor cursor(double x, double y) const override {
        const Hover h = hitAt(x, y);
        return h.id == SearchBox ? Cursor::Text : h.id != None ? Cursor::Pointer : Cursor::Default;
    }
    bool click(double x, double y, bool press) override {
        if (!press) return false;
        const Hover h = hitAt(x, y);
        (void)0;
        if (h.id == DayChip && h.arg != dayKey) { dayKey = h.arg; listPos = 0; band.reset(); dayShown = Clock::now(); return true; }
        const bool wasFocus = qFocus;
        qFocus = false;
        if (h.id == SearchBox) { qFocus = true; qCur = query.size(); lastKey = Clock::now(); return true; }
        if (h.id == LegToggle && h.arg >= 0 && h.arg < static_cast<int>(legTargets.size())) { toggleLeg(legTargets[static_cast<size_t>(h.arg)]); return true; }
        if (h.id >= SlipMinus && slipAct(h.id, h.arg)) return true;
        if (h.id == FilterResult) { fResult = h.arg; rPos = 0; rBand.reset(); return true; }
        if (h.id == FilterPeriod) { fPeriod = h.arg; rPos = 0; rBand.reset(); return true; }
        if (h.id == ModeBtn) { setMode(h.arg); return true; }
        if (h.id == Row) { openDetail(h.arg); return true; }
        if (h.id == BackBtn) { if (detailOpen) closeDetail(); else if (mode != Matches) setMode(mode); return true; }
        if (h.id == RefreshBtn) { if (!fetching()) startJob(2, true); return true; }
        return false;
    }
    bool grabsKeys() const override { return qFocus && mode == Record && !detailOpen; }
    bool key(const KeyEvent& k) override {
        lastKey = Clock::now();
        if (qFocus && mode == Record && !detailOpen) {
            qCur = std::min(qCur, query.size());
            switch (k.sym) {
                case XKB_KEY_Escape: case XKB_KEY_Return: case XKB_KEY_KP_Enter: qFocus = false; return true;
                case XKB_KEY_Left: qCur = qCur > 0 ? trPrevChar(query, qCur) : 0; return true;
                case XKB_KEY_Right: qCur = tbNextChar(query, qCur); return true;
                case XKB_KEY_Home: qCur = 0; return true;
                case XKB_KEY_End: qCur = query.size(); return true;
                case XKB_KEY_BackSpace: if (qCur > 0) { const size_t p = trPrevChar(query, qCur); query.erase(p, qCur - p); qCur = p; rPos = 0; } return true;
                case XKB_KEY_Delete: if (qCur < query.size()) { query.erase(qCur, tbNextChar(query, qCur) - qCur); rPos = 0; } return true;
                default: break;
            }
            if (!k.ctrl && !k.alt && !k.text.empty() && query.size() < 80) { query.insert(qCur, k.text); qCur += k.text.size(); rPos = 0; return true; }
            return false;
        }
        if (k.sym == XKB_KEY_Escape) {
            if (detailOpen && !detailClosing) { closeDetail(); return true; }
            if (!detailOpen && mode != Matches) { setMode(mode); return true; }
        }
        return false;
    }
    bool imeContext(ImeContext& context) override {
        if (!qFocus || mode != Record || detailOpen || searchBox.w <= 0) return false;
        context = imeField(query, qCur, imeCaretFor(query, qCur, {searchBox.x + 46, searchBox.y, searchBox.w - 60, searchBox.h}, 15));
        return true;
    }
    void imeEdit(const ImeEdit& edit) override {
        if (applyImeText(query, qCur, edit, 80)) { rPos = 0; lastKey = Clock::now(); }
    }
    bool scroll(double x, double y, double dy) override {
        if (!listArea.contains(x, y)) return false;
        if (detailOpen) { elasticWheel(dBand, dPos, dMax, dy * 3); return true; }
        if (mode == Record || mode == Slip) { elasticWheel(rBand, rPos, rMax, dy * 3); return true; }
        elasticWheel(band, listPos, scrollMax, dy * 3);
        return true;
    }
    bool tick() override {
        bool changed = false;
        if (stage != 0 && job.running() && job.poll()) { finishJob(); changed = true; }
        if (dStage != 0 && djob.running() && djob.poll()) { finishDetailJob(); changed = true; }
        if (rLoading && rjob.running() && rjob.poll()) { finishRecord(); changed = true; }
        if (sStage != 0 && sjob.running() && sjob.poll()) { finishSlipJob(); changed = true; }
        if (copiedUntil != Clock::time_point{} && Clock::now() >= copiedUntil) { copiedUntil = Clock::time_point{}; changed = true; }
        if (qFocus) { const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0; if (on != lastCaret) { lastCaret = on; changed = true; } }
        if (band.step() | dBand.step() | rBand.step()) changed = true;
        return changed;
    }
    bool animating() const override {
        const bool slide = detailOpen && std::chrono::duration<double>(Clock::now() - detailShown).count() < 0.3;
        return fetching() || band.busy() || dBand.busy() || rBand.busy() || lastFade < 0.6 || slide || dStage != 0 || rLoading || qFocus || sStage != 0 || copiedUntil > Clock::now();
    }
};
