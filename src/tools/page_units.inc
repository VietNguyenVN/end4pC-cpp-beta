// Tools page: Units. Included by src/tools.cpp (inside its anonymous namespace), after the translator page (it reuses TrJob and trLine).
// Converts an amount between the units people use most (length, weight, temperature, liquid volume, speed, area, time, data) and between
// currencies (open.er-api.com rates, about 160 currencies, cached on disk and refreshed every few hours).

constexpr int kUnRatesMaxAgeS = 6 * 3600;

using namespace unitconv;

struct UnitsPage final : ToolPage {
    enum Id { None, CatChip, FromChip, ToChip, Swap, Paste, Clear, Copy, Refresh, Cell, PopItem };
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };

    std::vector<UnCategory> cats;
    int cat = 0;
    std::vector<std::pair<std::string, int>> from{9, {"", 0}};  // (unused: kept per category below)
    std::vector<std::string> fromId, toId;
    std::string amount = "1";
    std::map<std::string, double> rates;  // per US dollar
    long long ratesAt = 0;
    TrJob job;
    bool fetching = false, fetchFailed = false;
    Clock::time_point copiedUntil, lastKey = Clock::now();
    bool lastCaret = true;

    int popup = 0;  // 1: the source unit list, 2: the target one
    std::string filter;
    double popScroll = 0, gridScroll = 0;
    std::vector<Hit> hits;
    Hover hover;
    Rect fromCard, toCard, gridCard, popArea, gridList;

    const char* name() const override { return "Units"; }
    const char* icon() const override { return "calculator-minimalistic"; }

    static std::string dataDir() { return configDir() + "/tools"; }
    static std::string ratesPath() { return dataDir() + "/rates.json"; }
    static std::string statePath() { return dataDir() + "/units-state.txt"; }

    // ---- data -------------------------------------------------------------------------------------------------------------------
    static bool parseRates(const std::string& j, std::map<std::string, double>& out, long long& at) { return unParseRates(j, out, at); }

    void buildCurrencies() { cats[0].units = unCurrencyUnits(rates); }

    void init() override {
        cats = unStaticCategories();
        fromId.assign(cats.size(), "");
        toId.assign(cats.size(), "");
        const char* f[] = {"USD", "m", "kg", "c", "l", "kmh", "m2", "min", "mb"}, *t[] = {"EUR", "ft", "lb", "f", "gal", "mph", "ft2", "h", "gb"};
        for (size_t i = 0; i < cats.size(); i++) { fromId[i] = f[i]; toId[i] = t[i]; }
        std::string raw = readFile(ratesPath());
        if (!raw.empty() && parseRates(raw, rates, ratesAt)) buildCurrencies();
        std::istringstream in(readFile(statePath()));
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "cat") cat = std::clamp(std::atoi(v.c_str()), 0, static_cast<int>(cats.size()) - 1);
            else if (k == "amount" && v.size() < 30) amount = v;
            else if (k.size() > 5 && k.compare(0, 5, "from.") == 0) { const size_t n = std::atoi(k.c_str() + 5); if (n < cats.size()) fromId[n] = v; }
            else if (k.size() > 3 && k.compare(0, 3, "to.") == 0) { const size_t n = std::atoi(k.c_str() + 3); if (n < cats.size()) toId[n] = v; }
        }
        if (cat == 0) refreshIfOld();
    }
    void saveState() const {
        std::string s = "cat=" + std::to_string(cat) + "\namount=" + amount + "\n";
        for (size_t i = 0; i < cats.size(); i++) s += "from." + std::to_string(i) + "=" + fromId[i] + "\nto." + std::to_string(i) + "=" + toId[i] + "\n";
        trWriteAtomic(dataDir(), "units-state.txt", s);
    }

    void refreshIfOld() {
        if (fetching) return;
        if (!rates.empty() && time(nullptr) - ratesAt < kUnRatesMaxAgeS) return;
        startFetch();
    }
    void startFetch() {
        fetchFailed = false;
        fetching = job.start({"curl", "-sf", "-m", "15", "https://open.er-api.com/v6/latest/USD"}, "", 20000);
        if (!fetching) fetchFailed = true;
    }
    void finishFetch() {
        fetching = false;
        std::map<std::string, double> r;
        long long at = 0;
        if (job.ok && parseRates(job.out, r, at)) {
            rates = r;
            ratesAt = at;
            trWriteAtomic(dataDir(), "rates.json", job.out);
            buildCurrencies();
        } else fetchFailed = true;
    }

    const UnCategory& C() const { return cats[static_cast<size_t>(cat)]; }
    const UnUnit* find(const std::string& id) const {
        for (const UnUnit& u : C().units) if (u.id == id) return &u;
        return C().units.empty() ? nullptr : &C().units[0];
    }
    const UnUnit* fromU() const { return find(fromId[static_cast<size_t>(cat)]); }
    const UnUnit* toU() const { return find(toId[static_cast<size_t>(cat)]); }

    double convert(double v, const UnUnit& a, const UnUnit& b) const { return unConvert(cat == kUnTemperature, v, a, b); }

    bool parseAmount(double& v) const {
        if (amount.empty() || amount == "-" || amount == ".") return false;
        char* end = nullptr;
        v = std::strtod(amount.c_str(), &end);
        return end && *end == 0;
    }
    static std::string fmt(double v) { return unFmt(v); }
    std::string resultText() const {
        double v = 0;
        const UnUnit *a = fromU(), *b = toU();
        if (!a || !b || !parseAmount(v)) return "—";
        return fmt(convert(v, *a, *b));
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------------------
    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    static double pillWidth(const std::string& label, bool withIcon) {
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        return 28 + (withIcon ? 26 : 0) + tw;
    }
    static void pill(cairo_t* cr, Rect r, const std::string& label, const char* ic, const Color& bg, const Color& fg, bool hov, double alpha = 1) {
        src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        double x = r.x + (r.w - (ic ? 26 : 0) - tw) / 2;
        if (ic) { ::icon(cr, ic, x, r.y + (r.h - 18) / 2, 18, fg); x += 26; }
        text(cr, label, x, r.y + (r.h - th) / 2, 15, true, fg, 0, alpha);
    }

    void drawCategories(cairo_t* cr, double w) {
        double gap = 8, x = 0;
        std::vector<double> widths;
        double total = 0;
        for (const UnCategory& c : cats) { widths.push_back(pillWidth(c.name, true)); total += widths.back() + gap; }
        const bool compact = total - gap > w;  // short of room: only the chosen one keeps its name
        for (size_t i = 0; i < cats.size(); i++) {
            const bool on = static_cast<int>(i) == cat;
            const double pw = compact && !on ? 52 : widths[i];
            const Rect r{x, 0, pw, 44};
            const Color bg = on ? g.color("primary") : g.color("surface_container_high"), fg = on ? g.color("on_primary") : g.color("on_surface");
            pill(cr, r, compact && !on ? "" : cats[i].name, cats[i].icon, bg, fg, hot(r, CatChip, static_cast<int>(i)));
            x += pw + gap;
        }
    }

    Rect drawUnitChip(cairo_t* cr, const Rect& card, const UnUnit* u, int id, const Color& bg, const Color& fg) {
        const std::string label = u ? (cat == 0 ? u->sym + "  " + u->name : u->name + "  ·  " + u->sym) : "";
        double tw = 0, th = 0;
        measure(label, 16, true, 0, tw, th);
        const double w = std::min(tw + 66, card.w - 32);
        const Rect r{card.x + 16, card.y + 16, w, 44};
        const bool hov = hot(r, id);
        src(cr, hov ? mix(fg, bg, 0.12) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        cairo_fill(cr);
        trLine(cr, label, r.x + 20, r.y + (r.h - th) / 2, r.w - 66, 16, true, fg);
        ::icon(cr, "alt-arrow-down", r.x + r.w - 38, r.y + 10, 24, fg);
        return r;
    }

    void bigNumber(cairo_t* cr, const std::string& s, const Rect& box, const Color& fg, double alpha, bool caret) {
        double px = 54, tw = 0, th = 0;
        for (; px > 22; px -= 4) { measure(s, px, true, 0, tw, th); if (tw <= box.w - 6) break; }
        measure(s, px, true, 0, tw, th);
        const double y = box.y + (box.h - th) / 2;
        text(cr, s, box.x, y, px, true, fg, 0, alpha);
        if (caret) { src(cr, fg); cairo_rectangle(cr, box.x + tw + 2, y + th * 0.12, 3, th * 0.76); cairo_fill(cr); }
    }

    void drawFrom(cairo_t* cr) {
        const Rect c = fromCard;
        src(cr, g.color("surface_container_high"));
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        const Color fg = g.color("on_surface");
        drawUnitChip(cr, c, fromU(), FromChip, g.color("surface_container_highest"), fg);
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        lastCaret = caretOn;
        const Rect box{c.x + 24, c.y + 78, c.w - 48, 90};
        if (amount.empty()) text(cr, "0", box.x, box.y + 14, 54, true, g.color("on_surface_variant"), 0, 0.4);
        bigNumber(cr, amount, box, fg, 1, caretOn && !popup);
        const UnUnit* a = fromU();
        if (a) text(cr, a->name, c.x + 24, c.y + 172, 15, false, g.color("on_surface_variant"));
        const Color bg = g.color("secondary_container"), sf = g.color("on_secondary_container");
        const double y = c.y + c.h - 60;
        Rect p{c.x + 16, y, pillWidth("Paste", true), 44};
        pill(cr, p, "Paste", "clipboard", bg, sf, hot(p, Paste));
        Rect k{p.x + p.w + 8, y, pillWidth("Clear", true), 44};
        pill(cr, k, "Clear", "eraser", bg, sf, hot(k, Clear), amount.empty() ? 0.45 : 1);
    }

    std::string rateLine() const {
        const UnUnit *a = fromU(), *b = toU();
        if (!a || !b) return "";
        return "1 " + a->sym + " = " + fmt(convert(1, *a, *b)) + " " + b->sym;
    }
    std::string ageText() const {
        if (fetching) return "Updating rates…";
        if (rates.empty()) return fetchFailed ? "Rates unavailable (offline?)" : "No rates yet";
        const long long age = static_cast<long long>(time(nullptr)) - ratesAt;
        std::string s = age < 90 ? "just now" : age < 5400 ? std::to_string(age / 60) + " min ago" : age < 172800 ? std::to_string(age / 3600) + " h ago" : std::to_string(age / 86400) + " days ago";
        return std::string(i18n::tr("Rates updated")) + " " + s + (fetchFailed ? " · " + std::string(i18n::tr("offline")) : "");
    }

    void drawTo(cairo_t* cr) {
        const Rect c = toCard;
        const Color base = g.color("primary_container"), fg = g.color("on_primary_container");
        src(cr, base);
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        drawUnitChip(cr, {c.x + 24, c.y, c.w - 24, c.h}, toU(), ToChip, mix(fg, base, 0.14), fg);  // (the swap button overlaps the card's left edge)
        const Rect box{c.x + 48, c.y + 78, c.w - 72, 90};
        bigNumber(cr, resultText(), box, fg, 1, false);
        const UnUnit* b = toU();
        if (b) text(cr, b->name, c.x + 48, c.y + 172, 15, false, fg, 0, 0.75);
        const std::string rl = rateLine();
        const bool can = resultText() != "—";
        const double y = c.y + c.h - 60;
        Rect cp{c.x + 40, y, pillWidth("Copy", true) + 16, 44};
        const bool flash = Clock::now() < copiedUntil;
        pill(cr, cp, flash ? "Copied" : "Copy", flash ? "check-circle" : "copy", g.color("primary"), g.color("on_primary"), can && hot(cp, Copy), can ? 1 : 0.45);
        double x = cp.x + cp.w + 16;
        if (cat == 0) {
            const Rect rf{c.x + c.w - 60, y, 44, 44};
            src(cr, hot(rf, Refresh) ? mix(fg, mix(fg, base, 0.14), 0.14) : mix(fg, base, 0.14));
            cairo_new_sub_path(cr);
            cairo_arc(cr, rf.x + 22, rf.y + 22, 22, 0, 2 * M_PI);
            cairo_fill(cr);
            ::icon(cr, "refresh", rf.x + 11, rf.y + 11, 22, fg);
            trLine(cr, ageText(), x, y - 26, c.x + c.w - 16 - x, 12, false, fg, 0.7);
            x = std::min(x, rf.x);
        }
        if (!rl.empty()) trLine(cr, rl, x, y + 12, c.x + c.w - 80 - x, 14, true, fg, 0.9);
    }

    void drawSwap(cairo_t* cr) {
        const double cx = (fromCard.x + fromCard.w + toCard.x) / 2, cy = fromCard.y + fromCard.h / 2;
        const Rect r{cx - 24, cy - 24, 48, 48};
        src(cr, g.color("background"));
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 29, 0, 2 * M_PI);
        cairo_fill(cr);
        const Color bg = g.color("tertiary_container"), fg = g.color("on_tertiary_container");
        src(cr, hot(r, Swap) ? mix(fg, bg, 0.14) : bg);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 24, 0, 2 * M_PI);
        cairo_fill(cr);
        ::icon(cr, "transfer-horizontal", cx - 11, cy - 11, 22, fg);
    }

    static constexpr double kCellH = 64, kCellGap = 8;
    void drawGrid(cairo_t* cr) {
        const Rect c = gridCard;
        src(cr, g.color("surface_container"));
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        text(cr, "All conversions", c.x + 24, c.y + 18, 18, true, g.color("on_surface"));
        gridList = {c.x + 12, c.y + 58, c.w - 24, c.h - 68};
        double v = 0;
        const UnUnit* a = fromU();
        if (!a || !parseAmount(v)) return;
        std::vector<const UnUnit*> others;
        for (const UnUnit& u : C().units) if (u.id != a->id) others.push_back(&u);
        const int cols = std::max(1, static_cast<int>((gridList.w + kCellGap) / (220 + kCellGap)));
        const double cw = (gridList.w - (cols - 1) * kCellGap) / cols;
        const int rows = (static_cast<int>(others.size()) + cols - 1) / cols;
        gridScroll = std::clamp(gridScroll, 0.0, std::max(0.0, rows * (kCellH + kCellGap) - kCellGap - gridList.h));
        cairo_save(cr);
        cairo_rectangle(cr, gridList.x, gridList.y, gridList.w, gridList.h);
        cairo_clip(cr);
        for (size_t i = 0; i < others.size(); i++) {
            const Rect r{gridList.x + (i % cols) * (cw + kCellGap), gridList.y + (i / cols) * (kCellH + kCellGap) - gridScroll, cw, kCellH};
            if (r.y + r.h < gridList.y || r.y > gridList.y + gridList.h) continue;
            const bool on = others[i]->id == toId[static_cast<size_t>(cat)];
            const Color bg = on ? g.color("secondary_container") : g.color("surface_container_high"), fg = on ? g.color("on_secondary_container") : g.color("on_surface");
            const bool hov = hot(r, Cell, static_cast<int>(i)) && gridList.contains(g.px, g.py);
            src(cr, hov ? mix(fg, bg, 0.08) : bg);
            rrect(cr, r.x, r.y, r.w, r.h, 20);
            cairo_fill(cr);
            trLine(cr, fmt(convert(v, *a, *others[i])) + " " + others[i]->sym, r.x + 16, r.y + 10, r.w - 32, 18, true, fg);
            trLine(cr, others[i]->name, r.x + 16, r.y + 36, r.w - 32, 13, false, fg, 0.7);
        }
        cairo_restore(cr);
    }

    std::vector<int> popupItems() const {
        std::vector<int> v;
        std::string f = filter;
        for (char& ch : f) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (size_t i = 0; i < C().units.size(); i++) {
            std::string hay = C().units[i].sym + " " + C().units[i].name;
            for (char& ch : hay) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (f.empty() || hay.find(f) != std::string::npos) v.push_back(static_cast<int>(i));
        }
        return v;
    }
    void drawPopup(cairo_t* cr) {
        if (!popup) return;
        const Rect card = popup == 1 ? fromCard : toCard;
        popArea = {card.x + 10, card.y + 66, card.w - 20, card.h - 76};
        src(cr, g.color("surface_container_highest"));
        rrect(cr, popArea.x, popArea.y, popArea.w, popArea.h, 22);
        cairo_fill(cr);
        const auto items = popupItems();
        const double pad = 10, gap = 5, chH = 38, searchH = cats[static_cast<size_t>(cat)].units.size() > 14 ? 34 : 0;
        if (searchH > 0) {
            trLine(cr, filter.empty() ? std::string(i18n::tr("Type to search")) : filter, popArea.x + pad + 8, popArea.y + pad + 7, popArea.w - 2 * pad - 16, 15, !filter.empty(), g.color("on_surface_variant"), filter.empty() ? 0.6 : 1);
        }
        const Rect list{popArea.x + pad, popArea.y + pad + searchH, popArea.w - 2 * pad, popArea.h - 2 * pad - searchH};
        const int cols = list.w > 330 ? 2 : 1, rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        const double cw = (list.w - (cols - 1) * gap) / cols;
        popScroll = std::clamp(popScroll, 0.0, std::max(0.0, rows * (chH + gap) - gap - list.h));
        cairo_save(cr);
        cairo_rectangle(cr, list.x, list.y, list.w, list.h);
        cairo_clip(cr);
        const std::string cur = popup == 1 ? fromId[static_cast<size_t>(cat)] : toId[static_cast<size_t>(cat)];
        for (size_t k = 0; k < items.size(); k++) {
            const Rect r{list.x + (k % cols) * (cw + gap), list.y + (k / cols) * (chH + gap) - popScroll, cw, chH};
            if (r.y + r.h < list.y || r.y > list.y + list.h) continue;
            const UnUnit& u = C().units[static_cast<size_t>(items[k])];
            const bool on = u.id == cur;
            const Color bg = on ? g.color("primary") : g.color("surface_container_high"), fg = on ? g.color("on_primary") : g.color("on_surface");
            const bool hov = hot(r, PopItem, items[k]) && list.contains(g.px, g.py);
            src(cr, hov ? mix(fg, bg, 0.14) : bg);
            rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            cairo_fill(cr);
            trLine(cr, cat == 0 ? u.sym + "  " + u.name : u.name + "  ·  " + u.sym, r.x + 16, r.y + 9, r.w - 32, 14, true, fg);
        }
        cairo_restore(cr);
        if (items.empty()) text(cr, "Nothing found", list.x + 12, list.y + 12, 15, false, g.color("on_surface_variant"), 0, 0.7);
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        constexpr double gap = 10, topH = 44, cardsH = 262;
        drawCategories(cr, r.w);
        const double paneW = (r.w - gap) / 2, y = topH + 14;
        fromCard = {0, y, paneW, cardsH};
        toCard = {paneW + gap, y, paneW, cardsH};
        gridCard = {0, y + cardsH + gap, r.w, std::max(120.0, r.h - y - cardsH - gap)};
        drawFrom(cr);
        drawTo(cr);
        drawGrid(cr);
        drawSwap(cr);
        drawPopup(cr);
    }

    // ---- input ------------------------------------------------------------------------------------------------------------------
    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (popup && it->id != PopItem) continue;
            if (!it->r.contains(x, y)) continue;
            if (it->id == Cell && !gridList.contains(x, y)) continue;
            if (it->id == PopItem && !popArea.contains(x, y)) continue;
            return {it->id, it->arg};
        }
        return {};
    }
    bool motion(double x, double y) override {
        const Hover h = hitAt(x, y);
        const bool changed = h.id != hover.id || h.arg != hover.arg;
        hover = h;
        return changed;
    }
    bool leave() override {
        const bool changed = hover.id != None;
        hover = {};
        return changed;
    }
    Cursor cursor(double x, double y) const override {
        if (hitAt(x, y).id != None) return Cursor::Pointer;
        return !popup && fromCard.contains(x, y) && y > fromCard.y + 70 && y < fromCard.y + 170 ? Cursor::Text : Cursor::Default;
    }

    void flashCopied() { copiedUntil = Clock::now() + std::chrono::milliseconds(1600); }
    void setUnit(bool source, int idx) {
        (source ? fromId : toId)[static_cast<size_t>(cat)] = C().units[static_cast<size_t>(idx)].id;
        popup = 0;
        filter.clear();
        saveState();
    }
    void setCategory(int c) {
        cat = c;
        popup = 0;
        gridScroll = 0;
        if (cat == 0) refreshIfOld();
        saveState();
    }
    void swap() {  // the units trade places and the old result becomes the amount
        double v = 0;
        const UnUnit *x = fromU(), *y = toU();
        std::string next;
        if (x && y && parseAmount(v)) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.10g", convert(v, *x, *y));
            next = buf;
        }
        std::swap(fromId[static_cast<size_t>(cat)], toId[static_cast<size_t>(cat)]);
        if (!next.empty() && next.find('e') == std::string::npos) amount = next;
        saveState();
    }
    bool typeAmount(const std::string& t) {
        bool changed = false;
        for (char ch : t) {
            if (ch == ',') ch = '.';
            const bool digit = ch >= '0' && ch <= '9';
            if (!digit && ch != '.' && ch != '-') continue;
            if (ch == '.' && amount.find('.') != std::string::npos) continue;
            if (ch == '-' && !(amount.empty() && cat == kUnTemperature)) continue;
            if (amount.size() >= 18) continue;
            amount += ch;
            changed = true;
        }
        return changed;
    }
    void pasteClipboard() {
        std::string t;
        if (!trRunSync({"wl-paste", "-n", "-t", "text"}, "", &t, 1500) || t.empty()) return;
        std::string clean;
        for (char ch : t) if (ch != ' ' && ch != '\n' && ch != '\t' && ch != '$' && ch != '€' && ch != '\xe2' && ch != '\x82' && ch != '\xac') clean += ch;
        amount.clear();
        typeAmount(clean);
        saveState();
    }
    bool act(const Hover& h) {
        switch (h.id) {
            case CatChip: setCategory(h.arg); break;
            case FromChip: popup = popup == 1 ? 0 : 1; filter.clear(); popScroll = 0; break;
            case ToChip: popup = popup == 2 ? 0 : 2; filter.clear(); popScroll = 0; break;
            case PopItem: setUnit(popup == 1, h.arg); break;
            case Swap: swap(); break;
            case Paste: pasteClipboard(); break;
            case Clear: amount.clear(); saveState(); break;
            case Copy: {
                const std::string r = resultText();
                if (r == "—") return false;
                std::string plain;
                for (char ch : r) if (ch != ',') plain += ch;
                if (trRunSync({"wl-copy"}, plain, nullptr, 2000)) flashCopied();
                break;
            }
            case Refresh: startFetch(); break;
            case Cell: {
                const UnUnit* a = fromU();
                int n = 0;
                for (const UnUnit& u : C().units) {
                    if (a && u.id == a->id) continue;
                    if (n++ == h.arg) { toId[static_cast<size_t>(cat)] = u.id; saveState(); break; }
                }
                break;
            }
            default: return false;
        }
        return true;
    }

    bool click(double x, double y, bool press) override {
        if (!press) return false;
        const Hover h = hitAt(x, y);
        if (popup && h.id != PopItem) { popup = 0; return true; }
        if (h.id != None) return act(h);
        return false;
    }
    bool scroll(double x, double y, double dy) override {
        const double step = dy * 3;
        if (popup && popArea.contains(x, y)) popScroll += step;
        else if (gridCard.contains(x, y)) gridScroll += step;
        else return false;
        return true;
    }

    bool key(const KeyEvent& k) override {
        lastKey = Clock::now();
        if (popup) {
            if (k.sym == XKB_KEY_Escape) { popup = 0; filter.clear(); return true; }
            if (k.sym == XKB_KEY_BackSpace) { if (!filter.empty()) filter.pop_back(); popScroll = 0; return true; }
            if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) {
                const auto items = popupItems();
                if (!items.empty()) setUnit(popup == 1, items[0]);
                return true;
            }
            if (!k.ctrl && !k.alt && !k.text.empty()) { filter += k.text; popScroll = 0; return true; }
            return false;
        }
        if (k.ctrl) {
            if (k.sym == XKB_KEY_v) { pasteClipboard(); return true; }
            if (k.sym == XKB_KEY_c) return act({Copy, 0});
            return false;
        }
        if (k.sym == XKB_KEY_BackSpace) { if (!amount.empty()) amount.pop_back(); saveState(); return true; }
        if (k.sym == XKB_KEY_Delete) { amount.clear(); saveState(); return true; }
        if (k.alt || k.text.empty()) return false;
        const bool used = typeAmount(k.text);
        if (used) saveState();
        return used;
    }

    bool tick() override {
        bool changed = false;
        if (fetching && job.running() && job.poll()) { finishFetch(); changed = true; }
        if (copiedUntil != Clock::time_point{} && Clock::now() >= copiedUntil) { copiedUntil = Clock::time_point{}; changed = true; }
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        return changed || caretOn != lastCaret;
    }
    bool animating() const override { return fetching; }
};
