// Tools page: Keybinds. Every Hyprland key bind of the setup (the shell's, apps, windows, workspaces, media...) is one line of a table
// (see keybinds_store.inc). Here they are listed by category with a search, switched on and off, edited, deleted and added; a new
// shortcut is captured from the keyboard itself. Each change rewrites the table and reloads Hyprland.
// Included by src/tools.cpp (inside its anonymous namespace), after keybinds_store.inc.

std::string kbT(const char* s) { return std::string(i18n::tr(s)); }

struct KeybindsPage final : ToolPage {
    enum Id { None, Search, SearchClear, Add, Restore, Cat, Alt, Row, RowToggle, RowEdit, RowDel, Undo,
              DlgScrim, DlgCapture, DlgClear, DlgKind, DlgField, DlgPick, DlgFlag, DlgCat, DlgSave, DlgCancel, DlgDelete };
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };

    std::vector<Kb> rows;
    std::string search;
    int catSel = -1;
    bool showAlt = false;
    Elastic listScroll;
    double listH = 0;
    std::vector<Hit> hits;
    Hover hover;
    Rect listArea{};
    Clock::time_point lastKey = Clock::now();
    bool lastCaret = true;

    // the editor
    bool dlg = false, capturing = false;
    int dlgIdx = -1, focus = 0;  // focus: 1 the argument, 2 the name
    size_t curSearch = 0, curArg = 0, curDesc = 0;  // the caret of each text box: a byte offset
    Kb d;
    Elastic dlgScroll;
    double dlgContentH = 0;
    Rect dlgInner{};
    Clock::time_point capStart;

    // messages
    std::string toast;
    Clock::time_point toastUntil{}, restoreArmed{};
    bool canUndo = false;
    Kb undoRow;
    int undoPos = 0;

    const char* name() const override { return "Keybinds"; }
    const char* icon() const override { return "keyboard"; }
    ~KeybindsPage() override { kbGrab(false); }

    void init() override { rows = kbLoad(); }
    bool grabsKeys() const override { return capturing; }
    bool roundClip() const override { return false; }  // (the Add button sits in the top right corner)
    void blur() override { if (capturing) stopCapture(); }
    bool animating() const override { return capturing || listScroll.busy() || dlgScroll.busy(); }

    // ---- data ---------------------------------------------------------------------------------------------------------------------
    std::vector<std::string> categories() const {
        std::vector<std::string> c;
        for (const Kb& b : rows) if (std::find(c.begin(), c.end(), b.cat) == c.end()) c.push_back(b.cat);
        return c;
    }
    std::string kindLabel(const Kb& b) const {
        const KbKind* k = kbKind(b.kind);
        return k ? kbT(k->label) : b.kind;
    }
    std::string subtitle(const Kb& b) const {
        const KbKind* k = kbKind(b.kind);
        if (b.kind == "exec") return b.arg.size() > 2 && b.arg[0] == '{' && b.arg.back() == '}' ? kindLabel(b) + "  \xC2\xB7  " + b.arg.substr(1, b.arg.size() - 2) : b.arg;
        if (b.kind == "shell") return kbT("Shell action") + "  \xC2\xB7  " + b.arg;
        if (k && k->picks.size() > 0 && !b.arg.empty())
            for (const auto& p : k->picks) if (b.arg == p.first) return kindLabel(b) + "  \xC2\xB7  " + kbT(p.second);
        return b.arg.empty() || b.arg == "#" ? kindLabel(b) : kindLabel(b) + "  \xC2\xB7  " + b.arg;
    }
    std::string keysText(const Kb& b) const {
        std::string s;
        for (const std::string& t : kbSplitKeys(b.keys)) s += kbCap(t, b.rng) + " ";
        return s;
    }
    bool matches(const Kb& b) const {
        if (catSel >= 0 && b.cat != categories()[static_cast<size_t>(catSel)]) return false;
        return textMatch(b);
    }
    bool textMatch(const Kb& b) const {
        if (search.empty()) return showAlt || !b.has('h');
        const std::string hay = kbLower(b.desc + " " + b.cat + " " + kindLabel(b) + " " + b.arg + " " + keysText(b) + " " + b.keys);
        std::istringstream in(kbLower(search));
        for (std::string w; in >> w;) if (hay.find(w) == std::string::npos) return false;
        return true;
    }
    // The row that uses the same shortcut as row i (both enabled), or -1.
    int clashOf(int i) const {
        if (rows[static_cast<size_t>(i)].has('d')) return -1;
        const std::string n = kbNorm(rows[static_cast<size_t>(i)].keys);
        for (size_t j = 0; j < rows.size(); j++)
            if (static_cast<int>(j) != i && !rows[j].has('d') && rows[j].rng == rows[static_cast<size_t>(i)].rng && kbNorm(rows[j].keys) == n) return static_cast<int>(j);
        return -1;
    }
    int clashOfDraft() const {
        if (d.keys.empty()) return -1;
        const std::string n = kbNorm(d.keys);
        for (size_t j = 0; j < rows.size(); j++)
            if (static_cast<int>(j) != dlgIdx && !rows[j].has('d') && rows[j].rng == d.rng && kbNorm(rows[j].keys) == n) return static_cast<int>(j);
        return -1;
    }
    void say(const std::string& s) { toast = s; toastUntil = Clock::now() + std::chrono::milliseconds(4500); }
    void commit(const std::string& msg) {
        if (kbSave(rows)) { kbApply(); say(msg); }
        else say(kbT("Could not save the keybinds"));
    }

    // ---- small pieces -------------------------------------------------------------------------------------------------------------
    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    static double capW(const std::string& s, double px) {
        double tw = 0, th = 0;
        measure(s, px, true, 0, tw, th);
        return std::max(px * 2.0, tw + px * 1.3);
    }
    // One keycap: a raised rounded key with its name.
    static void cap(cairo_t* cr, double x, double y, double w, double h, const std::string& s, double px, const Color& bg, const Color& fg) {
        src(cr, mix(fg, bg, 0.22));
        rrect(cr, x, y + 2, w, h, h * 0.3);
        cairo_fill(cr);
        src(cr, bg);
        rrect(cr, x, y, w, h, h * 0.3);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(s, px, true, 0, tw, th);
        text(cr, s, x + (w - tw) / 2, y + (h - th) / 2, px, true, fg);
    }
    static double capsWidth(const std::vector<std::string>& caps, double px) {
        double w = 0;
        for (const std::string& c : caps) w += capW(c, px) + 5;
        return std::max(0.0, w - 5);
    }
    std::vector<std::string> capsOf(const std::string& keys, const std::string& rng) const {
        std::vector<std::string> o;
        for (const std::string& t : kbSplitKeys(keys)) {
            const std::string c = kbCap(t, rng);
            if (!o.empty() && o.back() == c && c == "Super") continue;  // (SUPER + Super_L: one Super)
            o.push_back(c);
        }
        return o;
    }
    // Keycaps ending at xr (right aligned) or starting at xl.
    double drawCaps(cairo_t* cr, const std::vector<std::string>& caps, double x, double y, double h, double px, const Color& bg, const Color& fg, bool rightAlign) {
        double w = capsWidth(caps, px), cx = rightAlign ? x - w : x;
        for (const std::string& c : caps) {
            const double cw = capW(c, px);
            cap(cr, cx, y, cw, h, c, px, bg, fg);
            cx += cw + 5;
        }
        return w;
    }
    void pill(cairo_t* cr, Rect r, const std::string& label, const char* ic, const Color& bg, const Color& fg, bool hov, double alpha = 1) {
        src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        double x = r.x + (r.w - (ic ? 26 : 0) - tw) / 2;
        if (ic) { ::icon(cr, ic, x, r.y + (r.h - 18) / 2, 18, fg); x += 26; }
        text(cr, label, x, r.y + (r.h - th) / 2, 15, true, fg, 0, alpha);
    }
    static double pillW(const std::string& s, bool ic) {
        double tw = 0, th = 0;
        measure(s, 15, true, 0, tw, th);
        return 30 + (ic ? 26 : 0) + tw;
    }
    void iconBtn(cairo_t* cr, Rect r, const char* ic, const Color& fg, bool hov, double alpha = 1) {
        if (hov) { src(cr, fg, 0.12); rrect(cr, r.x, r.y, r.w, r.h, r.h / 2); cairo_fill(cr); }
        ::icon(cr, ic, r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, fg);
        (void)alpha;
    }
    void toggle(cairo_t* cr, Rect r, bool on, bool hov) {
        src(cr, on ? g.color("primary") : g.color("surface_container_highest"));
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        if (!on) { src(cr, g.color("outline"), 0.8); rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2 - 1); cairo_set_line_width(cr, 1.5); cairo_stroke(cr); }
        const double k = r.h - 8, kx = on ? r.x + r.w - k - 4 : r.x + 4;
        src(cr, on ? g.color("on_primary") : g.color("outline"));
        cairo_arc(cr, kx + k / 2, r.y + r.h / 2, hov ? k / 2 + 1 : k / 2, 0, 2 * M_PI);
        cairo_fill(cr);
    }
    // A chip; returns its width.
    double chip(cairo_t* cr, double x, double y, const std::string& label, bool on, int id, int arg, const char* ic = nullptr, double alpha = 1) {
        const double w = pillW(label, ic != nullptr) - 2, h = 34;
        const Rect r{x, y, w, h};
        const bool hv = hot(r, id, arg);
        const Color bg = on ? g.color("primary") : g.color("surface_container_highest"), fg = on ? g.color("on_primary") : g.color("on_surface");
        pill(cr, r, label, ic, bg, fg, hv, alpha);
        return w;
    }
    double chipFlow(cairo_t* cr, double x, double y, double w, const std::vector<std::string>& labels, int sel, int id, const std::vector<const char*>& icons = {}) {
        double cx = x, cy = y;
        for (size_t i = 0; i < labels.size(); i++) {
            const double cw = pillW(labels[i], i < icons.size() && icons[i]) - 2;
            if (cx + cw > x + w && cx > x) { cx = x; cy += 42; }
            chip(cr, cx, cy, labels[i], static_cast<int>(i) == sel, id, static_cast<int>(i), i < icons.size() ? icons[i] : nullptr);
            cx += cw + 8;
        }
        return cy + 34 - y;
    }
    double fieldOffset(const std::string& value, size_t cur, double inner) {  // how far the text is pushed left so that the caret stays in view
        double cw = 0, ch = 0;
        measure(value.substr(0, std::min(cur, value.size())), 15, false, 0, cw, ch);
        return cw > inner - 4 ? inner - 4 - cw : 0;
    }
    void field(cairo_t* cr, Rect r, const std::string& value, const std::string& hint, bool focused, int id, int arg) {
        size_t& cur = arg == 1 ? curArg : curDesc;
        cur = std::min(cur, value.size());
        const bool hv = hot(r, id, arg);
        src(cr, hv ? mix(g.color("on_surface"), g.color("surface_container"), 0.05) : g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 16);
        cairo_fill(cr);
        if (focused) { src(cr, g.color("primary")); rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 15); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        const double pad = 16, inner = r.w - 2 * pad;
        if (value.empty()) { trLine(cr, hint, r.x + pad, r.y + (r.h - 18) / 2, inner, 15, false, g.color("on_surface_variant"), 0.6); }
        else {
            double tw = 0, th = 0, cw = 0, ch = 0;
            measure(value, 15, false, 0, tw, th);
            measure(value.substr(0, cur), 15, false, 0, cw, ch);
            cairo_save(cr);
            cairo_rectangle(cr, r.x + pad, r.y, inner, r.h);
            cairo_clip(cr);
            const double off = focused ? fieldOffset(value, cur, inner) : 0;
            text(cr, value, r.x + pad + off, r.y + (r.h - th) / 2, 15, false, g.color("on_surface"));
            cairo_restore(cr);
            if (focused && lastCaret) { src(cr, g.color("primary")); cairo_rectangle(cr, r.x + pad + cw + off + 1, r.y + (r.h - 22) / 2, 2, 22); cairo_fill(cr); }
        }
        if (value.empty() && focused && lastCaret) { src(cr, g.color("primary")); cairo_rectangle(cr, r.x + pad, r.y + (r.h - 22) / 2, 2, 22); cairo_fill(cr); }
    }

    // ---- the list -----------------------------------------------------------------------------------------------------------------
    void drawRow(cairo_t* cr, Rect r, int i) {
        const Kb& b = rows[static_cast<size_t>(i)];
        const bool off = b.has('d'), clash = clashOf(i) >= 0;
        const double a = off ? 0.5 : 1;
        const bool inList = listArea.contains(g.px - kPad, g.py - kBarH);
        const bool over = hot(r, Row, i) && inList;
        src(cr, over ? mix(g.color("on_surface"), g.color("surface_container"), 0.06) : g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        cairo_fill(cr);
        const KbKind* k = kbKind(b.kind);
        src(cr, g.color("primary_container"), a);
        cairo_arc(cr, r.x + 34, r.y + r.h / 2, 18, 0, 2 * M_PI);
        cairo_fill(cr);
        ::icon(cr, k ? k->icon : "keyboard", r.x + 34 - 10, r.y + r.h / 2 - 10, 20, g.color("on_primary_container"));
        // right side: switch, edit, delete
        double xr = r.x + r.w - 12;
        const Rect del{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 38;
        const Rect edit{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 42;
        const Rect sw{xr - 42, r.y + (r.h - 24) / 2, 42, 24};
        xr -= 54;
        iconBtn(cr, del, "trash-bin-minimalistic", g.color("error"), hot(del, RowDel, i));
        iconBtn(cr, edit, "pen", g.color("on_surface_variant"), hot(edit, RowEdit, i));
        toggle(cr, sw, !off, hot(sw, RowToggle, i));
        // the keys
        const double capH = 26, capPx = 12.5;
        const std::vector<std::string> caps = capsOf(b.keys, b.rng);
        double capsW = capsWidth(caps, capPx);
        const double maxCaps = r.w * 0.5;
        const bool tight = capsW > maxCaps;
        const double px = tight ? 11 : capPx;
        if (tight) capsW = capsWidth(caps, px);
        const double keyLeft = xr - capsW;
        const Color capBg = clash ? mix(g.color("error"), g.color("surface_container_highest"), 0.25) : g.color("surface_container_highest");
        cairo_save(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
        drawCaps(cr, caps, keyLeft, r.y + (r.h - capH) / 2 - 1, capH, px, capBg, g.color("on_surface"), false);
        const double tx = r.x + 66, tw = std::max(60.0, keyLeft - tx - 14);
        trLine(cr, b.desc.empty() ? kindLabel(b) : kbT(b.desc.c_str()), tx, r.y + 11, tw, 15, true, g.color("on_surface"), a);
        if (clash) {
            const Kb& o = rows[static_cast<size_t>(clashOf(i))];
            trLine(cr, kbT("Same keys as") + ": " + (o.desc.empty() ? kindLabel(o) : kbT(o.desc.c_str())), tx, r.y + 34, tw, 12.5, false, g.color("error"), 0.95);
        } else trLine(cr, subtitle(b), tx, r.y + 34, tw, 12.5, false, g.color("on_surface_variant"), 0.75 * a);
        cairo_restore(cr);
    }

    // The height of the whole list: the same layout as drawList, without drawing.
    double listHeight(const Rect& area) const {
        const bool two = area.w >= 940;
        double y = 0;
        for (const std::string& c : categories()) {
            size_t n = 0;
            for (const Kb& b : rows) if (b.cat == c && matches(b)) n++;
            if (n == 0) continue;
            y += 38 + (two ? (n + 1) / 2 : n) * (64 + 10) + 8;
        }
        return y;
    }

    void drawList(cairo_t* cr, Rect area) {
        listArea = area;
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        const bool two = area.w >= 940;
        const double gap = 10, rowH = 64, cw = two ? (area.w - gap) / 2 : area.w;
        listScroll.max = std::max(0.0, listHeight(area) - area.h);  // (clamped before drawing: no empty band for a frame)
        listScroll.fit();
        double y = area.y - listScroll.off();
        int shown = 0;
        for (const std::string& c : categories()) {
            std::vector<int> idx;
            for (size_t i = 0; i < rows.size(); i++) if (rows[i].cat == c && matches(rows[i])) idx.push_back(static_cast<int>(i));
            if (idx.empty()) continue;
            if (y + 34 > area.y && y < area.y + area.h) {
                text(cr, kbT(c.c_str()), area.x + 6, y + 6, 15, true, g.color("primary"));
                double tw = 0, th = 0;
                measure(kbT(c.c_str()), 15, true, 0, tw, th);
                text(cr, std::to_string(idx.size()), area.x + 6 + tw + 10, y + 7, 13, false, g.color("on_surface_variant"), 0, 0.7);
            }
            y += 38;
            for (size_t n = 0; n < idx.size(); n++) {
                const double x = area.x + (two ? (n % 2) * (cw + gap) : 0), yy = y + (two ? n / 2 : n) * (rowH + gap);
                if (yy + rowH > area.y && yy < area.y + area.h) drawRow(cr, {x, yy, cw, rowH}, idx[n]);
                shown++;
            }
            const size_t lines = two ? (idx.size() + 1) / 2 : idx.size();
            y += lines * (rowH + gap) + 8;
        }
        listH = y + listScroll.off() - area.y;
        if (shown == 0) {
            ::icon(cr, "magnifer", area.x + area.w / 2 - 28, area.y + 70, 56, g.color("on_surface_variant"));
            const std::string m = kbT("No keybinds found");
            double tw = 0, th = 0;
            measure(m, 18, true, 0, tw, th);
            text(cr, m, area.x + (area.w - tw) / 2, area.y + 140, 18, true, g.color("on_surface_variant"));
        }
        cairo_restore(cr);
        if (listH > area.h) {  // a thin scroll thumb
            const double th = std::max(36.0, area.h * area.h / listH), ty = area.y + (area.h - th) * (listScroll.pos / (listH - area.h));
            src(cr, g.color("on_surface_variant"), 0.35 * listScroll.thumbAlpha());
            rrect(cr, area.x + area.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    void drawHeader(cairo_t* cr, double W) {
        const double h = 48;
        const std::string addL = kbT("Add keybind");
        const double addW = pillW(addL, true) + 8;
        const Rect add{W - addW, 0, addW, h}, restore{W - addW - 8 - h, 0, h, h};
        pill(cr, add, addL, "add-circle", g.color("primary"), g.color("on_primary"), hot(add, Add));
        const bool armed = Clock::now() < restoreArmed;
        if (armed) {
            const std::string l = kbT("Click again to restore");
            const double w = pillW(l, true) + 8;
            const Rect r{W - addW - 8 - w, 0, w, h};
            pill(cr, r, l, "undo-left-round", g.color("error"), g.color("on_error"), hot(r, Restore));
        } else {
            src(cr, hot(restore, Restore) ? mix(g.color("on_surface"), g.color("surface_container_high"), 0.1) : g.color("surface_container_high"));
            rrect(cr, restore.x, restore.y, restore.w, restore.h, h / 2);
            cairo_fill(cr);
            ::icon(cr, "undo-left-round", restore.x + (h - 22) / 2, restore.y + (h - 22) / 2, 22, g.color("on_surface"));
        }
        // the search field
        const double sw = W - addW - 8 - (armed ? pillW(kbT("Click again to restore"), true) + 8 : h) - 12;
        const Rect s{0, 0, sw, h};
        const bool focus = !dlg;
        src(cr, g.color("surface_container_high"));
        rrect(cr, s.x, s.y, s.w, s.h, h / 2);
        cairo_fill(cr);
        hits.push_back({s, Search, 0});
        ::icon(cr, "magnifer", 18, (h - 22) / 2, 22, g.color("on_surface_variant"));
        curSearch = std::min(curSearch, search.size());
        if (search.empty()) text(cr, kbT("Search by name, keys or command"), 52, (h - 20) / 2, 16, false, g.color("on_surface_variant"), 0, 0.65);
        else {
            double tw = 0, th = 0, cw = 0, ch = 0;
            measure(search, 16, false, 0, tw, th);
            measure(search.substr(0, curSearch), 16, false, 0, cw, ch);
            trLine(cr, search, 52, (h - th) / 2, s.w - 52 - 52, 16, false, g.color("on_surface"));
            if (lastCaret && focus) { src(cr, g.color("primary")); cairo_rectangle(cr, 52 + std::min(cw, s.w - 104) + 1, (h - 22) / 2, 2, 22); cairo_fill(cr); }
            const Rect x{s.w - 44, 4, 40, 40};
            iconBtn(cr, x, "close-circle", g.color("on_surface_variant"), hot(x, SearchClear));
        }
        if (search.empty() && focus && lastCaret) { src(cr, g.color("primary")); cairo_rectangle(cr, 50, (h - 22) / 2, 2, 22); cairo_fill(cr); }
    }

    void drawChips(cairo_t* cr, double y, double W) {
        const auto cats = categories();
        double x = 0;
        auto count = [&](int ci) {
            int n = 0;
            for (const Kb& b : rows) if ((ci < 0 || b.cat == cats[static_cast<size_t>(ci)]) && textMatch(b)) n++;
            return n;
        };
        x += chip(cr, x, y, kbT("All") + "  " + std::to_string(count(-1)), catSel < 0, Cat, -1) + 8;
        for (size_t i = 0; i < cats.size(); i++) {
            const std::string l = kbT(cats[i].c_str()) + "  " + std::to_string(count(static_cast<int>(i)));
            if (x + pillW(l, false) > W - 150) break;
            x += chip(cr, x, y, l, catSel == static_cast<int>(i), Cat, static_cast<int>(i)) + 8;
        }
        const std::string al = kbT("Alternates");
        const double aw = pillW(al, showAlt) - 2;  // (the check icon is there only when it is on)
        chip(cr, W - aw, y, al, showAlt, Alt, 0, showAlt ? "check-circle" : nullptr);
    }

    // ---- the editor ---------------------------------------------------------------------------------------------------------------
    void liveMods(bool& logo, bool& ctrl, bool& shift, bool& alt) const {
        auto on = [](const char* m) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, m, XKB_STATE_MODS_EFFECTIVE) > 0; };
        logo = on(XKB_MOD_NAME_LOGO); ctrl = on(XKB_MOD_NAME_CTRL); shift = on(XKB_MOD_NAME_SHIFT); alt = on(XKB_MOD_NAME_ALT);
    }
    double label(cairo_t* cr, double x, double y, const std::string& s) {
        text(cr, s, x, y, 13, true, g.color("on_surface_variant"), 0, 0.85);
        return 24;
    }

    void drawCapture(cairo_t* cr, Rect r) {
        const bool hv = hot(r, DlgCapture);
        src(cr, hv ? mix(g.color("on_surface"), g.color("surface_container"), 0.05) : g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        cairo_fill(cr);
        if (capturing) {
            const double pulse = 0.55 + 0.45 * std::sin(std::chrono::duration<double>(Clock::now() - capStart).count() * 5);
            src(cr, g.color("primary"), pulse);
            rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 21);
            cairo_set_line_width(cr, 2.5);
            cairo_stroke(cr);
            bool logo, ctrl, shift, alt;
            liveMods(logo, ctrl, shift, alt);
            std::vector<std::string> caps;
            for (const std::string& t : kbSplitKeys(kbModsText(logo, ctrl, shift, alt))) caps.push_back(kbCap(t, ""));
            const std::string hint = kbT(caps.empty() ? "Press the shortcut now" : "Now press the last key");
            double tw = 0, th = 0;
            measure(hint, 16, true, 0, tw, th);
            const double cw = capsWidth(caps, 16);
            if (!caps.empty()) drawCaps(cr, caps, r.x + (r.w - cw) / 2, r.y + 12, 38, 16, g.color("primary_container"), g.color("on_primary_container"), false);
            text(cr, hint, r.x + (r.w - tw) / 2, r.y + (caps.empty() ? 24 : 58), 16, true, g.color("primary"));
            if (caps.empty()) {
                const std::string esc = kbT("Esc cancels, Backspace clears");
                measure(esc, 12.5, false, 0, tw, th);
                text(cr, esc, r.x + (r.w - tw) / 2, r.y + 54, 12.5, false, g.color("on_surface_variant"), 0, 0.8);
            }
        } else if (d.keys.empty()) {
            ::icon(cr, "keyboard", r.x + r.w / 2 - 15, r.y + 12, 30, g.color("on_surface_variant"));
            const std::string hint = kbT("Click here, then press the shortcut");
            double tw = 0, th = 0;
            measure(hint, 15, true, 0, tw, th);
            text(cr, hint, r.x + (r.w - tw) / 2, r.y + r.h - 34, 15, true, g.color("on_surface_variant"));
        } else {
            const auto caps = capsOf(d.keys, d.rng);
            const double cw = capsWidth(caps, 17);
            drawCaps(cr, caps, r.x + (r.w - cw) / 2, r.y + 14, 40, 17, g.color("primary_container"), g.color("on_primary_container"), false);
            const std::string hint = kbT(d.rng.empty() ? "Click to change" : "Click to change the modifiers");
            double tw = 0, th = 0;
            measure(hint, 12.5, false, 0, tw, th);
            text(cr, hint, r.x + (r.w - tw) / 2, r.y + r.h - 28, 12.5, false, g.color("on_surface_variant"), 0, 0.8);
            const Rect x{r.x + r.w - 44, r.y + 6, 36, 36};
            iconBtn(cr, x, "close-circle", g.color("on_surface_variant"), hot(x, DlgClear));
        }
    }

    void drawDialog(cairo_t* cr, Rect r) {
        hits.push_back({r, DlgScrim, 0});
        src(cr, Color{0, 0, 0}, 0.5);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_fill(cr);
        const double cw = std::min(880.0, r.w - 48), ch = std::min(r.h - 20, 640.0);
        const Rect card{r.x + (r.w - cw) / 2, r.y + (r.h - ch) / 2, cw, ch};
        hits.push_back({card, DlgCapture + 100, 0});  // (a click inside the card is not a click on the scrim)
        src(cr, g.color("surface_container_high"));
        rrect(cr, card.x, card.y, card.w, card.h, 30);
        cairo_fill(cr);
        const double pad = 26;
        // title
        const std::string title = kbT(dlgIdx < 0 ? "Add keybind" : "Edit keybind");
        text(cr, title, card.x + pad, card.y + 22, 22, true, g.color("on_surface"));
        const Rect close{card.x + card.w - pad - 36, card.y + 16, 40, 40};
        iconBtn(cr, close, "close-circle", g.color("on_surface_variant"), hot(close, DlgCancel, 1));
        // footer
        const double fy = card.y + card.h - 74;
        src(cr, g.color("outline_variant"), 0.5);
        cairo_rectangle(cr, card.x + pad, fy - 6, card.w - 2 * pad, 1);
        cairo_fill(cr);
        const std::string saveL = kbT("Save"), cancelL = kbT("Cancel");
        const double sW = pillW(saveL, false) + 24, cW = pillW(cancelL, false) + 16;
        const bool ok = valid();
        const Rect save{card.x + card.w - pad - sW, fy + 6, sW, 48}, cancel{save.x - 10 - cW, fy + 6, cW, 48};
        pill(cr, save, saveL, nullptr, ok ? g.color("primary") : g.color("surface_container_highest"), ok ? g.color("on_primary") : g.color("on_surface_variant"), ok && hot(save, DlgSave), ok ? 1 : 0.6);
        if (!ok) hits.push_back({save, DlgSave + 100, 0});
        pill(cr, cancel, cancelL, nullptr, g.color("surface_container_highest"), g.color("on_surface"), hot(cancel, DlgCancel));
        if (dlgIdx >= 0) {
            const std::string dl = kbT("Delete");
            const Rect del{card.x + pad, fy + 6, pillW(dl, true) + 8, 48};
            pill(cr, del, dl, "trash-bin-minimalistic", g.color("surface_container_highest"), g.color("error"), hot(del, DlgDelete));
        }
        // the form (scrolls when it is taller than the card)
        dlgInner = {card.x, card.y + 64, card.w, fy - 8 - (card.y + 64)};
        cairo_save(cr);
        cairo_rectangle(cr, dlgInner.x, dlgInner.y, dlgInner.w, dlgInner.h);
        cairo_clip(cr);
        const double x = card.x + pad, w = card.w - 2 * pad;
        double y = dlgInner.y + 4 - dlgScroll.off();
        y += label(cr, x, y, kbT("Shortcut"));
        drawCapture(cr, {x, y, w, 92});
        y += 98;
        const int clash = clashOfDraft();
        if (clash >= 0) {
            const Kb& o = rows[static_cast<size_t>(clash)];
            text(cr, kbT("Already used by") + ": " + (o.desc.empty() ? kindLabel(o) : kbT(o.desc.c_str())) + "  (" + kbT("both would run") + ")", x + 4, y + 2, 13, true, g.color("error"));
            y += 24;
        }
        y += 14;
        y += label(cr, x, y, kbT("Action"));
        std::vector<std::string> labels;
        std::vector<const char*> icons;
        int sel = -1;
        std::vector<const KbKind*> listed;
        for (const KbKind& k : kbKinds()) {
            if (!k.listed && d.kind != k.id) continue;
            if (d.kind == k.id) sel = static_cast<int>(labels.size());
            labels.push_back(kbT(k.label));
            icons.push_back(k.icon);
            listed.push_back(&k);
        }
        y += chipFlow(cr, x, y, w, labels, sel, DlgKind, icons) + 18;
        const KbKind* kind = kbKind(d.kind);
        if (kind && kind->argLabel) {
            y += label(cr, x, y, kbT(kind->argLabel));
            if (d.rng.empty()) {
                if (kind->textArg) { field(cr, {x, y, w, 46}, d.arg, kbT(kind->argLabel), focus == 1, DlgField, 1); y += 54; }
                if (!kind->picks.empty()) {
                    std::vector<std::string> pl;
                    int ps = -1;
                    for (size_t i = 0; i < kind->picks.size(); i++) { pl.push_back(kbT(kind->picks[i].second)); if (d.arg == kind->picks[i].first) ps = static_cast<int>(i); }
                    y += chipFlow(cr, x, y, w, pl, ps, DlgPick) + 8;
                }
            } else {
                text(cr, kbT("Applies to the number keys 1 to 0"), x + 4, y + 2, 14, false, g.color("on_surface_variant"));
                y += 28;
            }
            y += 14;
        }
        y += label(cr, x, y, kbT("Name"));
        field(cr, {x, y, w, 46}, d.desc, kbT("What it does"), focus == 2, DlgField, 2);
        y += 62;
        y += label(cr, x, y, kbT("Options"));
        const char* fl[] = {"Works with the screen locked", "Repeat while held", "Run when the key is released", "Alternate (hidden from the list)"};
        const char fc[] = {'l', 'r', 'e', 'h'};
        double cx = x, cy = y;
        for (int i = 0; i < 4; i++) {
            const std::string l = kbT(fl[i]);
            const double cwid = pillW(l, true) - 2;
            if (cx + cwid > x + w && cx > x) { cx = x; cy += 42; }
            chip(cr, cx, cy, l, d.has(fc[i]), DlgFlag, i, d.has(fc[i]) ? "check-circle" : nullptr);
            cx += cwid + 8;
        }
        y = cy + 34 + 18;
        y += label(cr, x, y, kbT("Category"));
        std::vector<std::string> cl = categories();
        if (std::find(cl.begin(), cl.end(), "Custom") == cl.end()) cl.push_back("Custom");
        std::vector<std::string> cll;
        int cs = -1;
        for (size_t i = 0; i < cl.size(); i++) { cll.push_back(kbT(cl[i].c_str())); if (cl[i] == d.cat) cs = static_cast<int>(i); }
        y += chipFlow(cr, x, y, w, cll, cs, DlgCat) + 20;
        cairo_restore(cr);
        dlgContentH = y + dlgScroll.off() - dlgInner.y;
        dlgScroll.max = std::max(0.0, dlgContentH - dlgInner.h);
        dlgScroll.fit();
        if (dlgContentH > dlgInner.h) {  // a thin scroll thumb on the card's edge
            const double th = std::max(30.0, dlgInner.h * dlgInner.h / dlgContentH), ty = dlgInner.y + (dlgInner.h - th) * (dlgScroll.pos / (dlgContentH - dlgInner.h));
            src(cr, g.color("on_surface_variant"), 0.4 * dlgScroll.thumbAlpha());
            rrect(cr, card.x + card.w - 12, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    bool valid() const {
        if (d.keys.empty() || d.kind.empty()) return false;
        const KbKind* k = kbKind(d.kind);
        if (k && k->argLabel && d.arg.empty()) return false;
        return true;
    }

    void drawToast(cairo_t* cr, Rect r) {
        if (Clock::now() >= toastUntil || toast.empty()) return;
        const double tw = [&] { double w = 0, h = 0; measure(toast, 15, true, 0, w, h); return w; }();
        const double uw = canUndo ? pillW(kbT("Undo"), false) : 0, w = tw + 44 + (canUndo ? uw + 12 : 0), h = 48;
        const Rect t{r.x + (r.w - w) / 2, r.y + r.h - h - 16, w, h};
        src(cr, Color{0, 0, 0}, 0.25);
        rrect(cr, t.x, t.y + 3, t.w, t.h, h / 2);
        cairo_fill(cr);
        src(cr, g.color("secondary_container"));
        rrect(cr, t.x, t.y, t.w, t.h, h / 2);
        cairo_fill(cr);
        text(cr, toast, t.x + 22, t.y + (h - 20) / 2, 15, true, g.color("on_secondary_container"));
        if (canUndo) {
            const Rect u{t.x + t.w - uw - 8, t.y + 6, uw, 36};
            pill(cr, u, kbT("Undo"), nullptr, g.color("secondary"), g.color("on_secondary"), hot(u, Undo));
        }
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        drawHeader(cr, r.w);
        drawChips(cr, 62, r.w);
        drawList(cr, {0, 112, r.w, r.h - 112});
        drawToast(cr, r);
        if (dlg) drawDialog(cr, r);
    }

    // ---- input --------------------------------------------------------------------------------------------------------------------
    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (!it->r.contains(x, y)) continue;
            if (it->id >= DlgCapture + 100) return {};      // a click on the card (or the disabled Save) does nothing
            if (dlg && it->id < DlgScrim) continue;
            if (it->id == Row || it->id == RowToggle || it->id == RowEdit || it->id == RowDel) { if (!listArea.contains(x, y)) continue; }
            if (it->id >= DlgCapture && it->id <= DlgCat && it->id != DlgSave && it->id != DlgCancel && it->id != DlgDelete && !dlgInner.contains(x, y)) continue;
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
        const Hover h = hitAt(x, y);
        if (h.id == Search || (h.id == DlgField)) return Cursor::Text;
        if (h.id == DlgScrim || h.id == None) return Cursor::Default;
        return Cursor::Pointer;
    }

    void startCapture() {
        kbGrab(true);
        capturing = true;
        capStart = Clock::now();
        focus = 0;
    }
    void stopCapture() {
        capturing = false;
        kbGrab(false);
    }
    void openEditor(int idx) {
        dlg = true;
        dlgIdx = idx;
        d = idx < 0 ? Kb{"Custom", "", "exec", "", "", "", ""} : rows[static_cast<size_t>(idx)];
        dlgScroll.reset();
        focus = 0;
        curArg = d.arg.size();
        curDesc = d.desc.size();
        capturing = false;
        if (idx < 0) startCapture();
    }
    void closeEditor() {
        if (capturing) stopCapture();
        dlg = false;
        focus = 0;
    }
    void saveEditor() {
        if (!valid()) return;
        if (capturing) stopCapture();
        const KbKind* k = kbKind(d.kind);
        if (d.desc.empty()) d.desc = k ? k->label : d.kind;
        if (k && !k->argLabel) d.arg.clear();
        d.set('m', d.kind == "drag" || d.kind == "resize");
        if (dlgIdx < 0) rows.push_back(d);
        else rows[static_cast<size_t>(dlgIdx)] = d;
        dlg = false;
        canUndo = false;
        commit(kbT(dlgIdx < 0 ? "Keybind added" : "Keybind saved"));
    }
    void removeRow(int i) {
        undoRow = rows[static_cast<size_t>(i)];
        undoPos = i;
        rows.erase(rows.begin() + i);
        canUndo = true;
        commit(kbT("Keybind deleted"));
    }
    void restoreDefaults() {
        if (Clock::now() >= restoreArmed) { restoreArmed = Clock::now() + std::chrono::milliseconds(4000); return; }
        restoreArmed = {};
        unlink(kbUserPath().c_str());
        rows = kbLoad();
        kbApply();
        canUndo = false;
        catSel = -1;
        say(kbT("The original keybinds are back"));
    }

    bool act(const Hover& h) {
        switch (h.id) {
            case Search: return false;
            case SearchClear: search.clear(); curSearch = 0; listScroll.reset(); break;
            case Add: openEditor(-1); break;
            case Restore: restoreDefaults(); break;
            case Cat: catSel = h.arg; listScroll.reset(); break;
            case Alt: showAlt = !showAlt; listScroll.reset(); break;
            case Row: case RowEdit: openEditor(h.arg); break;
            case RowToggle: { Kb& b = rows[static_cast<size_t>(h.arg)]; b.set('d', !b.has('d')); canUndo = false; commit(kbT(b.has('d') ? "Keybind turned off" : "Keybind turned on")); break; }
            case RowDel: removeRow(h.arg); break;
            case Undo:
                if (canUndo) { rows.insert(rows.begin() + std::min<size_t>(static_cast<size_t>(undoPos), rows.size()), undoRow); canUndo = false; commit(kbT("Keybind restored")); }
                break;
            case DlgScrim: closeEditor(); break;
            case DlgCancel: closeEditor(); break;
            case DlgSave: saveEditor(); break;
            case DlgDelete: { const int i = dlgIdx; closeEditor(); if (i >= 0) removeRow(i); break; }
            case DlgCapture: if (capturing) stopCapture(); else startCapture(); break;
            case DlgClear: d.keys.clear(); break;
            case DlgField: focus = h.arg; if (capturing) stopCapture(); break;
            case DlgKind: {
                std::vector<const KbKind*> listed;
                for (const KbKind& k : kbKinds()) if (k.listed || d.kind == k.id) listed.push_back(&k);
                if (h.arg >= 0 && h.arg < static_cast<int>(listed.size()) && d.rng.empty()) {
                    const std::string old = d.kind;
                    d.kind = listed[static_cast<size_t>(h.arg)]->id;
                    if (d.kind != old) {
                        const KbKind* nk = kbKind(d.kind);
                        d.arg = nk && !nk->picks.empty() && !nk->textArg ? nk->picks[0].first : "";
                    }
                }
                break;
            }
            case DlgPick: {
                const KbKind* k = kbKind(d.kind);
                if (k && h.arg >= 0 && h.arg < static_cast<int>(k->picks.size())) {
                    d.arg = k->picks[static_cast<size_t>(h.arg)].first;
                    if (d.desc.empty() || d.desc == lastAutoName) { d.desc = kbT(k->picks[static_cast<size_t>(h.arg)].second); lastAutoName = d.desc; }
                }
                break;
            }
            case DlgFlag: { const char fc[] = {'l', 'r', 'e', 'h'}; d.set(fc[h.arg], !d.has(fc[h.arg])); break; }
            case DlgCat: {
                std::vector<std::string> cl = categories();
                if (std::find(cl.begin(), cl.end(), "Custom") == cl.end()) cl.push_back("Custom");
                if (h.arg >= 0 && h.arg < static_cast<int>(cl.size())) d.cat = cl[static_cast<size_t>(h.arg)];
                break;
            }
            default: return false;
        }
        return true;
    }
    std::string lastAutoName;

    bool click(double x, double y, bool press) override {
        if (!press) return false;
        const Hover h = hitAt(x, y);
        if (h.id == Search) { if (dlg) closeEditor(); for (const Hit& t : hits) if (t.id == Search) { curSearch = caretAt(search, 16, x - (t.r.x + 52)); break; }
            return true;
        }
        if (h.id == None) return false;
        const bool wasField = h.id == DlgField;
        const bool done = act(h);
        if (wasField) {  // the caret goes where the click was
            const std::string& v = h.arg == 1 ? d.arg : d.desc;
            for (const Hit& t : hits) if (t.id == DlgField && t.arg == h.arg) {
                const double inner = t.r.w - 32;
                const size_t cur = h.arg == 1 ? curArg : curDesc;
                (h.arg == 1 ? curArg : curDesc) = caretAt(v, 15, x - (t.r.x + 16) - fieldOffset(v, cur, inner));
                break;
            }
        }
        return done;
    }
    bool scroll(double x, double y, double dy) override {
        if (capturing) return captureScroll(dy);
        if (dlg) { if (dlgInner.contains(x, y)) { dlgScroll.by(dy * 3); return true; } return false; }
        if (listArea.contains(x, y)) { listScroll.by(dy * 3); return true; }
        return false;
    }

    static size_t nextChar(const std::string& v, size_t i) { return tbNextChar(v, i); }
    size_t caretAt(const std::string& v, double px, double x) { return tbCaretAt(v, px, x); }

    // Typing into a box: the text goes in at the caret, and the arrows, Home, End, Delete and Backspace work around it.
    bool typeInto(std::string& s, size_t& cur, const KeyEvent& k, size_t max) {
        cur = std::min(cur, s.size());
        if (k.ctrl && k.sym == XKB_KEY_v) {
            std::string t;
            if (trRunSync({"wl-paste", "-n", "-t", "text"}, "", &t, 1500)) {
                for (char& c : t) if (c == '\n' || c == '\t') c = ' ';
                if (s.size() + t.size() <= max) { s.insert(cur, t); cur += t.size(); }
            }
            return true;
        }
        if (k.ctrl || k.alt) return false;
        switch (k.sym) {
            case XKB_KEY_Left: cur = cur > 0 ? trPrevChar(s, cur) : 0; return true;
            case XKB_KEY_Right: cur = nextChar(s, cur); return true;
            case XKB_KEY_Home: cur = 0; return true;
            case XKB_KEY_End: cur = s.size(); return true;
            case XKB_KEY_BackSpace: if (cur > 0) { const size_t p = trPrevChar(s, cur); s.erase(p, cur - p); cur = p; } return true;
            case XKB_KEY_Delete: if (cur < s.size()) s.erase(cur, nextChar(s, cur) - cur); return true;
            default: break;
        }
        if (k.text.empty()) return false;
        if (s.size() + k.text.size() > max) return true;
        s.insert(cur, k.text);
        cur += k.text.size();
        return true;
    }

    // While a shortcut is being captured, a click joins it (a bare left click is still the click on the box): Super + right click, a side button...
    bool mouseButton(uint32_t button) override {
        if (!capturing) return false;
        bool logo, ctrl, shift, alt;
        liveMods(logo, ctrl, shift, alt);
        if (button == 0x110 && !logo && !ctrl && !shift && !alt) return false;
        d.keys = kbModsText(logo, ctrl, shift, alt) + "mouse:" + std::to_string(button);
        stopCapture();
        return true;
    }
    bool captureScroll(double dy) {
        bool logo, ctrl, shift, alt;
        liveMods(logo, ctrl, shift, alt);
        d.keys = kbModsText(logo, ctrl, shift, alt) + (dy > 0 ? "mouse_down" : "mouse_up");
        stopCapture();
        return true;
    }
    bool captureKey(const KeyEvent& k) {
        if (kbIsModifierSym(k.sym)) return true;
        const bool bare = !k.ctrl && !k.alt && !k.shift && !k.logo;
        if (bare && k.sym == XKB_KEY_Escape) { stopCapture(); return true; }
        if (bare && k.sym == XKB_KEY_BackSpace) { d.keys.clear(); stopCapture(); return true; }
        const std::string mods = kbModsText(k.logo, k.ctrl, k.shift, k.alt);
        d.keys = d.rng.empty() ? mods + kbKeyName(k.baseName, k.code) : mods + "#";
        stopCapture();
        return true;
    }
    bool dialogKey(const KeyEvent& k) {
        if (k.sym == XKB_KEY_Escape) { closeEditor(); return true; }
        if (k.sym == XKB_KEY_Tab || k.sym == XKB_KEY_ISO_Left_Tab) { focus = focus == 1 ? 2 : 1; const KbKind* kd = kbKind(d.kind); if (focus == 1 && !(kd && kd->textArg)) focus = 2; return true; }
        if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { if (valid()) saveEditor(); return true; }
        if (focus == 1) return typeInto(d.arg, curArg, k, 600);
        if (focus == 2) { const bool used = typeInto(d.desc, curDesc, k, 80); if (used) lastAutoName.clear(); return used; }
        return false;
    }
    bool imeContext(ImeContext& context) override {
        if (capturing || (dlg && !focus)) return false;
        const auto& value = !dlg ? search : focus == 1 ? d.arg : d.desc;
        const size_t cursor = !dlg ? curSearch : focus == 1 ? curArg : curDesc;
        for (const auto& hit : hits) {
            if ((!dlg && hit.id == Search) || (dlg && hit.id == DlgField && hit.arg == focus)) {
                const double inset = dlg ? 16 : 52;
                context = imeField(value, cursor, imeCaretFor(value, cursor,
                    {hit.r.x + inset, hit.r.y, std::max(0.0, hit.r.w - inset - 16), hit.r.h}, dlg ? 15 : 16));
                return true;
            }
        }
        return false;
    }
    void imeEdit(const ImeEdit& edit) override {
        lastKey = Clock::now();
        if (!dlg) { if (applyImeText(search, curSearch, edit, 80)) listScroll.reset(); }
        else if (focus == 1) applyImeText(d.arg, curArg, edit, 600);
        else if (focus == 2 && applyImeText(d.desc, curDesc, edit, 80)) lastAutoName.clear();
    }

    bool key(const KeyEvent& k) override {
        lastKey = Clock::now();
        if (capturing) return captureKey(k);
        if (dlg) return dialogKey(k);
        if (k.sym == XKB_KEY_Escape) { if (!search.empty()) { search.clear(); curSearch = 0; listScroll.reset(); return true; } return false; }
        if (k.sym == XKB_KEY_Page_Down) { listScroll.by(listArea.h * 0.8); return true; }
        if (k.sym == XKB_KEY_Page_Up) { listScroll.by(-listArea.h * 0.8); return true; }
        const bool used = typeInto(search, curSearch, k, 80);
        if (used) listScroll.reset();
        return used;
    }

    bool tick() override {
        bool changed = listScroll.step();
        if (dlgScroll.step()) changed = true;
        if (capturing && Clock::now() - capStart > std::chrono::seconds(25)) { stopCapture(); changed = true; }
        if (!toast.empty() && Clock::now() >= toastUntil) { toast.clear(); canUndo = false; changed = true; }
        if (restoreArmed != Clock::time_point{} && Clock::now() >= restoreArmed) { restoreArmed = {}; changed = true; }
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        const bool caretChanged = caretOn != lastCaret;
        lastCaret = caretOn;
        return changed || caretChanged;
    }
};
