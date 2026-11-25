// Tools page: Autostart. The apps that start when you log in, in the order of the list; each one can have a workspace to open on (scripts/autostart.py
// starts them one after another, waiting for the window of each). The list is in autostart_store.inc. An app is added from the installed ones or as a
// command; each change is saved at once.
// Included by src/tools.cpp (inside its anonymous namespace), after autostart_store.inc.

std::string asT(const char* s) { return std::string(i18n::tr(s)); }

struct AutostartPage final : ToolPage {
    enum Id { None, Add, RemoveAll, SaveOpen, Row, RowToggle, RowUp, RowDown, RowEdit, RowDel,
              DlgScrim, DlgCard, DlgField, DlgWs, DlgSave, DlgCancel, DlgDelete,
              PickScrim, PickCard, PickItem, PickCustom, PickClose,
              CapScrim, CapCard, CapToggle, CapAdd, CapCancel };
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };

    std::vector<AsApp> rows;
    std::vector<Hit> hits;
    Hover hover;
    Elastic listScroll, pickScroll;
    Rect listArea{}, pickArea{};
    double listH = 0, pickH = 0;
    std::map<std::string, cairo_surface_t*> icons;
    Clock::time_point lastKey = Clock::now();
    bool lastCaret = true;

    // the editor
    bool dlg = false;
    int dlgIdx = -1, focus = 1;  // focus: 1 the name, 2 the command
    size_t curName = 0, curCmd = 0;
    AsApp d;

    // "Save open apps": what is open now, to choose from
    bool cap = false;
    std::vector<AsOpen> caps;
    std::vector<char> capOn;
    Elastic capScroll;
    Rect capArea{};
    double capH = 0;

    // the picker
    bool pick = false;
    std::vector<AsDesk> installed;
    std::string search;
    size_t curSearch = 0;

    std::string toast;
    Clock::time_point toastUntil{}, clearArmed{};  // clearArmed: "Remove all" waits for a second click until then

    const char* name() const override { return "Autostart"; }
    const char* icon() const override { return "rocket"; }
    void init() override { rows = asLoad(); }
    bool roundClip() const override { return false; }  // (the Add button sits in the top right corner)
    bool animating() const override { return listScroll.busy() || pickScroll.busy() || capScroll.busy(); }
    bool grabsKeys() const override { return false; }

    // ---- small pieces (as in the Keybinds page) ----------------------------------------------------------------------------------
    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    static double pillW(const std::string& s, bool ic) {
        double tw = 0, th = 0;
        measure(s, 15, true, 0, tw, th);
        return 30 + (ic ? 26 : 0) + tw;
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
    double chip(cairo_t* cr, double x, double y, const std::string& label, bool on, int id, int arg) {
        const double w = pillW(label, false) - 2, h = 34;
        const Rect r{x, y, w, h};
        const bool hv = hot(r, id, arg);
        pill(cr, r, label, nullptr, on ? g.color("primary") : g.color("surface_container_highest"), on ? g.color("on_primary") : g.color("on_surface"), hv);
        return w;
    }
    double fieldOffset(const std::string& value, size_t cur, double inner) {
        double cw = 0, ch = 0;
        measure(value.substr(0, std::min(cur, value.size())), 15, false, 0, cw, ch);
        return cw > inner - 4 ? inner - 4 - cw : 0;
    }
    void field(cairo_t* cr, Rect r, const std::string& value, const std::string& hint, bool focused, int id, int arg, size_t cur) {
        cur = std::min(cur, value.size());
        const bool hv = hot(r, id, arg);
        src(cr, hv ? mix(g.color("on_surface"), g.color("surface_container"), 0.05) : g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 16);
        cairo_fill(cr);
        if (focused) { src(cr, g.color("primary")); rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 15); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        const double pad = 16, inner = r.w - 2 * pad;
        if (value.empty()) trLine(cr, hint, r.x + pad, r.y + (r.h - 18) / 2, inner, 15, false, g.color("on_surface_variant"), 0.6);
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
    void say(const std::string& s) { toast = s; toastUntil = Clock::now() + std::chrono::milliseconds(3500); }
    void commit() { if (!asSave(rows)) say(asT("Could not save the list")); }

    // The icon of an app (28 px), or a letter on a disc when the theme does not have it.
    void drawAppIcon(cairo_t* cr, const std::string& iconName, const std::string& label, double cx, double cy, double size, double alpha) {
        auto it = icons.find(iconName);
        if (it == icons.end()) {  // the icon theme in use first (its own icon for the name), then the plain lookup
            const std::string file = thLookupIcon(iconName, static_cast<int>(size) * 2);
            cairo_surface_t* made = file.empty() ? nullptr : tkLoadIcon(file, static_cast<int>(size));
            it = icons.emplace(iconName, made ? made : tkLoadIcon(iconName, static_cast<int>(size))).first;
        }
        cairo_surface_t* ic = it->second;
        if (ic) {
            cairo_save(cr);
            cairo_translate(cr, cx - size / 2, cy - size / 2);
            cairo_scale(cr, size / cairo_image_surface_get_width(ic), size / cairo_image_surface_get_height(ic));
            cairo_set_source_surface(cr, ic, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint_with_alpha(cr, alpha);
            cairo_restore(cr);
            return;
        }
        src(cr, g.color("primary_container"), alpha);
        cairo_arc(cr, cx, cy, size / 2 + 2, 0, 2 * M_PI);
        cairo_fill(cr);
        const std::string letter = label.empty() ? "?" : label.substr(0, 1);
        double tw = 0, th = 0;
        measure(letter, 17, true, 0, tw, th);
        text(cr, letter, cx - tw / 2, cy - th / 2, 17, true, g.color("on_primary_container"), 0, alpha);
    }
    // What is shown under the name: the command, or for an app started through its desktop entry, the entry's file.
    static std::string cmdText(const std::string& cmd) {
        if (cmd.rfind("gio launch '", 0) != 0 || cmd.back() != '\'') return cmd;
        const std::string path = cmd.substr(12, cmd.size() - 13);
        const size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
    std::string wsText(int ws) const { return ws > 0 ? asT("Workspace") + " " + std::to_string(ws) : ws == kAsSpecial ? asT("Special workspace") : asT("Any workspace"); }

    // ---- the list ------------------------------------------------------------------------------------------------------------------
    void drawRow(cairo_t* cr, Rect r, int i) {
        const AsApp& a = rows[static_cast<size_t>(i)];
        const double al = a.on ? 1 : 0.5;
        const bool over = hot(r, Row, i) && listArea.contains(g.px - kPad, g.py - kBarH);
        src(cr, over ? mix(g.color("on_surface"), g.color("surface_container"), 0.06) : g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        cairo_fill(cr);
        // the order, as a number
        text(cr, std::to_string(i + 1), r.x + 18, r.y + (r.h - 18) / 2, 15, true, g.color("on_surface_variant"), 0, 0.7 * al);
        drawAppIcon(cr, a.icon, a.name, r.x + 62, r.y + r.h / 2, 30, al);
        // the right side: switch, up, down, edit, delete
        double xr = r.x + r.w - 12;
        const Rect del{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 38;
        const Rect edit{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 42;
        const Rect down{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 36;
        const Rect up{xr - 34, r.y + (r.h - 34) / 2, 34, 34};
        xr -= 46;
        const Rect sw{xr - 42, r.y + (r.h - 24) / 2, 42, 24};
        xr -= 56;
        iconBtn(cr, del, "trash-bin-minimalistic", g.color("error"), hot(del, RowDel, i));
        iconBtn(cr, edit, "pen", g.color("on_surface_variant"), hot(edit, RowEdit, i));
        if (i + 1 < static_cast<int>(rows.size())) iconBtn(cr, down, "alt-arrow-down", g.color("on_surface_variant"), hot(down, RowDown, i));
        if (i > 0) iconBtn(cr, up, "alt-arrow-up", g.color("on_surface_variant"), hot(up, RowUp, i));
        toggle(cr, sw, a.on, hot(sw, RowToggle, i));
        // the workspace as a small chip, then the name and the command
        const std::string wl = wsText(a.ws);
        double wtw = 0, wth = 0;
        measure(wl, 13, true, 0, wtw, wth);
        const double chipW = wtw + 24, chipH = 26;
        const Rect wc{xr - chipW, r.y + (r.h - chipH) / 2, chipW, chipH};
        src(cr, a.ws != 0 ? g.color("secondary_container") : g.color("surface_container_highest"), al);
        rrect(cr, wc.x, wc.y, wc.w, wc.h, chipH / 2);
        cairo_fill(cr);
        text(cr, wl, wc.x + 12, wc.y + (chipH - wth) / 2, 13, true, a.ws != 0 ? g.color("on_secondary_container") : g.color("on_surface_variant"), 0, al);
        cairo_save(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
        const double tx = r.x + 90, tw = std::max(80.0, wc.x - tx - 14);
        trLine(cr, a.name, tx, r.y + 12, tw, 15, true, g.color("on_surface"), al);
        trLine(cr, cmdText(a.cmd), tx, r.y + 36, tw, 12.5, false, g.color("on_surface_variant"), 0.75 * al);
        cairo_restore(cr);
    }

    void drawList(cairo_t* cr, Rect area) {
        listArea = area;
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        const double rowH = 68, gap = 10;
        const double total = rows.empty() ? 0 : rows.size() * (rowH + gap);
        listScroll.max = std::max(0.0, total - area.h);
        listScroll.fit();
        double y = area.y - listScroll.off();
        for (size_t i = 0; i < rows.size(); i++, y += rowH + gap)
            if (y + rowH > area.y && y < area.y + area.h) drawRow(cr, {area.x, y, area.w, rowH}, static_cast<int>(i));
        listH = total;
        if (rows.empty()) {  // an empty list: a Material shape with the page's icon, what it is and what to do, centred in the area
            const double cx = area.x + area.w / 2, cy = area.y + area.h / 2 - 50;
            shapePath(cr, "Cookie9Sided", cx - 74, cy - 74, 148);
            src(cr, g.color("secondary_container"));
            cairo_fill(cr);
            ::icon(cr, "rocket", cx - 32, cy - 32, 64, g.color("on_secondary_container"), true);
            double tw = 0, th = 0;
            const std::string m = asT("No apps start with your session yet");
            measure(m, 24, true, 0, tw, th);
            text(cr, m, cx - tw / 2, cy + 100, 24, true, g.color("on_surface"));
            const std::string h = asT("Add the apps you want to open when you log in, and pick the workspace of each one.");
            measure(h, 15, false, 0, tw, th);
            text(cr, h, cx - tw / 2, cy + 140, 15, false, g.color("on_surface_variant"), 0, 0.8);
        }
        cairo_restore(cr);
        if (listH > area.h) {
            const double th = std::max(36.0, area.h * area.h / listH), ty = area.y + (area.h - th) * (listScroll.pos / (listH - area.h));
            src(cr, g.color("on_surface_variant"), 0.35 * listScroll.thumbAlpha());
            rrect(cr, area.x + area.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    void drawHeader(cairo_t* cr, double W) {
        const double h = 48;
        const std::string addL = asT("Add app");
        const double addW = pillW(addL, true) + 8;
        const Rect add{W - addW, 0, addW, h};
        pill(cr, add, addL, "add-circle", g.color("primary"), g.color("on_primary"), hot(add, Add));
        double left = W - addW;
        if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) {  // "Save open apps": the windows open now, with their workspaces
            const std::string l = asT("Save open apps");
            const double w = pillW(l, true) + 8;
            const Rect so{left - 8 - w, 0, w, h};
            pill(cr, so, l, "download-minimalistic", g.color("secondary_container"), g.color("on_secondary_container"), hot(so, SaveOpen));
            left = so.x;
        }
        if (rows.size() > 2) {  // "Remove all": only with more than two apps; the first click arms it, the second removes them
            const bool armed = Clock::now() < clearArmed;
            const std::string l = asT(armed ? "Click again to remove all" : "Remove all");
            const double w = pillW(l, true) + 8;
            const Rect ra{left - 8 - w, 0, w, h};
            pill(cr, ra, l, "trash-bin-minimalistic", armed ? g.color("error") : g.color("surface_container_high"), armed ? g.color("on_error") : g.color("error"), hot(ra, RemoveAll));
            left = ra.x;
        }
        trLine(cr, asT("Apps that start when you log in"), 6, 2, left - 20, 19, true, g.color("on_surface"));
        trLine(cr, asT("They start one by one: the next one waits until the previous app shows its window."), 6, 29, left - 20, 13, false, g.color("on_surface_variant"), 0.8);
    }

    // ---- the editor ------------------------------------------------------------------------------------------------------------------
    double label(cairo_t* cr, double x, double y, const std::string& s) {
        text(cr, s, x, y, 13, true, g.color("on_surface_variant"), 0, 0.85);
        return 24;
    }
    bool valid() const { return !d.name.empty() && !d.cmd.empty(); }

    void drawDialog(cairo_t* cr, Rect r) {
        hits.push_back({r, DlgScrim, 0});
        src(cr, Color{0, 0, 0}, 0.5);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_fill(cr);
        const double cw = std::min(640.0, r.w - 48), ch = std::min(r.h - 20, 490.0);
        const Rect card{r.x + (r.w - cw) / 2, r.y + (r.h - ch) / 2, cw, ch};
        hits.push_back({card, DlgCard, 0});
        src(cr, g.color("surface_container_high"));
        rrect(cr, card.x, card.y, card.w, card.h, 30);
        cairo_fill(cr);
        const double pad = 26;
        text(cr, asT(dlgIdx < 0 ? "Add app" : "Edit app"), card.x + pad, card.y + 22, 22, true, g.color("on_surface"));
        const Rect close{card.x + card.w - pad - 36, card.y + 16, 40, 40};
        iconBtn(cr, close, "close-circle", g.color("on_surface_variant"), hot(close, DlgCancel, 1));
        const double x = card.x + pad, w = card.w - 2 * pad;
        double y = card.y + 72;
        y += label(cr, x, y, asT("Name"));
        field(cr, {x, y, w, 46}, d.name, asT("How it is called in the list"), focus == 1, DlgField, 1, curName);
        y += 58;
        y += label(cr, x, y, asT("Command"));
        field(cr, {x, y, w, 46}, d.cmd, asT("What to run, like firefox or discord --start-minimized"), focus == 2, DlgField, 2, curCmd);
        y += 58;
        y += label(cr, x, y, asT("Workspace"));
        double cx = x, cy = y;
        std::vector<int> options = {0, kAsSpecial};
        for (int n = 1; n <= kAsMaxWs; n++) options.push_back(n);
        for (const int n : options) {
            const std::string l = n == 0 ? asT("Any") : n == kAsSpecial ? asT("Special") : std::to_string(n);
            const double cwid = pillW(l, false) - 2;
            if (cx + cwid > x + w && cx > x) { cx = x; cy += 42; }
            chip(cr, cx, cy, l, d.ws == n, DlgWs, n);
            cx += cwid + 8;
        }
        // footer
        const double fy = card.y + card.h - 74;
        src(cr, g.color("outline_variant"), 0.5);
        cairo_rectangle(cr, card.x + pad, fy - 6, card.w - 2 * pad, 1);
        cairo_fill(cr);
        const std::string saveL = asT("Save"), cancelL = asT("Cancel");
        const double sW = pillW(saveL, false) + 24, cW = pillW(cancelL, false) + 16;
        const bool ok = valid();
        const Rect save{card.x + card.w - pad - sW, fy + 6, sW, 48}, cancel{save.x - 10 - cW, fy + 6, cW, 48};
        pill(cr, save, saveL, nullptr, ok ? g.color("primary") : g.color("surface_container_highest"), ok ? g.color("on_primary") : g.color("on_surface_variant"), ok && hot(save, DlgSave), ok ? 1 : 0.6);
        if (!ok) hits.push_back({save, DlgCard, 0});
        pill(cr, cancel, cancelL, nullptr, g.color("surface_container_highest"), g.color("on_surface"), hot(cancel, DlgCancel));
        if (dlgIdx >= 0) {
            const std::string dl = asT("Delete");
            const Rect del{card.x + pad, fy + 6, pillW(dl, true) + 8, 48};
            pill(cr, del, dl, "trash-bin-minimalistic", g.color("surface_container_highest"), g.color("error"), hot(del, DlgDelete));
        }
    }

    // ---- the picker ------------------------------------------------------------------------------------------------------------------
    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::vector<int> pickMatches() const {
        std::vector<int> out;
        const std::string q = lower(search);
        for (size_t i = 0; i < installed.size(); i++) {
            if (q.empty() || lower(installed[i].name).find(q) != std::string::npos || lower(installed[i].id).find(q) != std::string::npos) out.push_back(static_cast<int>(i));
        }
        return out;
    }
    void drawPicker(cairo_t* cr, Rect r) {
        hits.push_back({r, PickScrim, 0});
        src(cr, Color{0, 0, 0}, 0.5);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_fill(cr);
        const double cw = std::min(640.0, r.w - 48), ch = std::min(r.h - 20, 640.0);
        const Rect card{r.x + (r.w - cw) / 2, r.y + (r.h - ch) / 2, cw, ch};
        hits.push_back({card, PickCard, 0});
        src(cr, g.color("surface_container_high"));
        rrect(cr, card.x, card.y, card.w, card.h, 30);
        cairo_fill(cr);
        const double pad = 26;
        text(cr, asT("Choose an app"), card.x + pad, card.y + 22, 22, true, g.color("on_surface"));
        const Rect close{card.x + card.w - pad - 36, card.y + 16, 40, 40};
        iconBtn(cr, close, "close-circle", g.color("on_surface_variant"), hot(close, PickClose));
        field(cr, {card.x + pad, card.y + 68, card.w - 2 * pad, 46}, search, asT("Search apps"), true, PickItem, -1, curSearch);
        // the footer: a command that is not in the list
        const double fy = card.y + card.h - 74;
        const std::string cl = asT("Other command");
        const Rect custom{card.x + pad, fy + 6, pillW(cl, true) + 8, 48};
        pill(cr, custom, cl, "add-circle", g.color("surface_container_highest"), g.color("on_surface"), hot(custom, PickCustom));
        pickArea = {card.x + pad, card.y + 126, card.w - 2 * pad, fy - 8 - (card.y + 126)};
        cairo_save(cr);
        cairo_rectangle(cr, pickArea.x, pickArea.y, pickArea.w, pickArea.h);
        cairo_clip(cr);
        const std::vector<int> m = pickMatches();
        const double rowH = 52;
        pickH = m.size() * rowH;
        pickScroll.max = std::max(0.0, pickH - pickArea.h);
        pickScroll.fit();
        double y = pickArea.y - pickScroll.off();
        for (const int i : m) {
            if (y + rowH > pickArea.y && y < pickArea.y + pickArea.h) {
                const AsDesk& a = installed[static_cast<size_t>(i)];
                const Rect row{pickArea.x, y, pickArea.w, rowH - 4};
                if (hot(row, PickItem, i) && pickArea.contains(g.px - kPad, g.py - kBarH)) { src(cr, g.color("on_surface"), 0.08); rrect(cr, row.x, row.y, row.w, row.h, 16); cairo_fill(cr); }
                drawAppIcon(cr, a.icon, a.name, row.x + 28, row.y + row.h / 2, 28, 1);
                trLine(cr, a.name, row.x + 56, row.y + 8, row.w - 70, 15, true, g.color("on_surface"));
                trLine(cr, a.id, row.x + 56, row.y + 28, row.w - 70, 12, false, g.color("on_surface_variant"), 0.7);
            }
            y += rowH;
        }
        if (m.empty()) {
            const std::string e = asT("No apps found");
            double tw = 0, th = 0;
            measure(e, 16, true, 0, tw, th);
            text(cr, e, pickArea.x + (pickArea.w - tw) / 2, pickArea.y + 40, 16, true, g.color("on_surface_variant"));
        }
        cairo_restore(cr);
    }

    // ---- "Save open apps" ------------------------------------------------------------------------------------------------------------
    int capCount() const {
        int n = 0;
        for (size_t i = 0; i < caps.size(); i++) if (capOn[i] && !caps[i].dup) n++;
        return n;
    }
    void openCapture() {
        caps = asCaptureOpen(rows);
        if (caps.empty()) { say(asT("No open apps found")); return; }
        capOn.assign(caps.size(), 1);
        cap = true;
        capScroll.reset();
    }
    void addCaptured() {
        int n = 0;
        for (size_t i = 0; i < caps.size(); i++) if (capOn[i] && !caps[i].dup) { rows.push_back(caps[i].app); n++; }
        cap = false;
        if (n == 0) return;
        commit();
        say(std::to_string(n) + " " + asT(n == 1 ? "app added" : "apps added"));
    }
    void drawCapture(cairo_t* cr, Rect r) {
        hits.push_back({r, CapScrim, 0});
        src(cr, Color{0, 0, 0}, 0.5);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_fill(cr);
        const double cw = std::min(700.0, r.w - 48), ch = std::min(r.h - 20, 660.0);
        const Rect card{r.x + (r.w - cw) / 2, r.y + (r.h - ch) / 2, cw, ch};
        hits.push_back({card, CapCard, 0});
        src(cr, g.color("surface_container_high"));
        rrect(cr, card.x, card.y, card.w, card.h, 30);
        cairo_fill(cr);
        const double pad = 26;
        text(cr, asT("Save open apps"), card.x + pad, card.y + 20, 22, true, g.color("on_surface"));
        trLine(cr, asT("The apps open now, each with its workspace. Choose the ones to add to the list."), card.x + pad, card.y + 52, card.w - 2 * pad - 50, 13, false, g.color("on_surface_variant"), 0.85);
        const Rect close{card.x + card.w - pad - 36, card.y + 16, 40, 40};
        iconBtn(cr, close, "close-circle", g.color("on_surface_variant"), hot(close, CapCancel, 1));
        const double fy = card.y + card.h - 74;
        src(cr, g.color("outline_variant"), 0.5);
        cairo_rectangle(cr, card.x + pad, fy - 6, card.w - 2 * pad, 1);
        cairo_fill(cr);
        const int n = capCount();
        const std::string addL = n > 0 ? asT("Add") + " " + std::to_string(n) : asT("Add");
        const std::string cancelL = asT("Cancel");
        const double aW = pillW(addL, false) + 24, cW = pillW(cancelL, false) + 16;
        const Rect add{card.x + card.w - pad - aW, fy + 6, aW, 48}, cancel{add.x - 10 - cW, fy + 6, cW, 48};
        pill(cr, add, addL, nullptr, n > 0 ? g.color("primary") : g.color("surface_container_highest"), n > 0 ? g.color("on_primary") : g.color("on_surface_variant"), n > 0 && hot(add, CapAdd), n > 0 ? 1 : 0.6);
        if (n == 0) hits.push_back({add, CapCard, 0});
        pill(cr, cancel, cancelL, nullptr, g.color("surface_container_highest"), g.color("on_surface"), hot(cancel, CapCancel));
        capArea = {card.x + pad, card.y + 82, card.w - 2 * pad, fy - 8 - (card.y + 82)};
        cairo_save(cr);
        cairo_rectangle(cr, capArea.x, capArea.y, capArea.w, capArea.h);
        cairo_clip(cr);
        const double rowH = 58;
        capH = caps.size() * rowH;
        capScroll.max = std::max(0.0, capH - capArea.h);
        capScroll.fit();
        double y = capArea.y - capScroll.off();
        for (size_t i = 0; i < caps.size(); i++, y += rowH) {
            if (y + rowH < capArea.y || y > capArea.y + capArea.h) continue;
            const AsOpen& o = caps[i];
            const double al = o.dup ? 0.5 : 1;
            const Rect row{capArea.x, y, capArea.w, rowH - 4};
            const bool over = hot(row, CapToggle, static_cast<int>(i)) && capArea.contains(g.px - kPad, g.py - kBarH);
            src(cr, over && !o.dup ? mix(g.color("on_surface"), g.color("surface_container"), 0.06) : g.color("surface_container"));
            rrect(cr, row.x, row.y, row.w, row.h, 18);
            cairo_fill(cr);
            drawAppIcon(cr, o.app.icon, o.app.name, row.x + 30, row.y + row.h / 2, 28, al);
            const Rect sw{row.x + row.w - 12 - 42, row.y + (row.h - 24) / 2, 42, 24};
            toggle(cr, sw, capOn[i] && !o.dup, false);
            const std::string wl = o.dup ? asT("Already in the list") : wsText(o.app.ws);
            double wtw = 0, wth = 0;
            measure(wl, 13, true, 0, wtw, wth);
            const Rect wc{sw.x - 12 - (wtw + 24), row.y + (row.h - 26) / 2, wtw + 24, 26};
            src(cr, o.dup || o.app.ws == 0 ? g.color("surface_container_highest") : g.color("secondary_container"), al);
            rrect(cr, wc.x, wc.y, wc.w, wc.h, 13);
            cairo_fill(cr);
            text(cr, wl, wc.x + 12, wc.y + (26 - wth) / 2, 13, true, o.dup || o.app.ws == 0 ? g.color("on_surface_variant") : g.color("on_secondary_container"), 0, al);
            const double tx = row.x + 58, tw = std::max(80.0, wc.x - tx - 12);
            trLine(cr, o.app.name, tx, row.y + 9, tw, 15, true, g.color("on_surface"), al);
            trLine(cr, o.known ? cmdText(o.app.cmd) : asT("Command of the running process") + ": " + o.app.cmd, tx, row.y + 30, tw, 12, false, g.color("on_surface_variant"), 0.7 * al);
        }
        cairo_restore(cr);
    }

    void drawToast(cairo_t* cr, Rect r) {
        if (Clock::now() >= toastUntil || toast.empty()) return;
        double tw = 0, th = 0;
        measure(toast, 15, true, 0, tw, th);
        const double w = tw + 44, h = 48;
        const Rect t{r.x + (r.w - w) / 2, r.y + r.h - h - 16, w, h};
        src(cr, Color{0, 0, 0}, 0.25);
        rrect(cr, t.x, t.y + 3, t.w, t.h, h / 2);
        cairo_fill(cr);
        src(cr, g.color("secondary_container"));
        rrect(cr, t.x, t.y, t.w, t.h, h / 2);
        cairo_fill(cr);
        text(cr, toast, t.x + 22, t.y + (h - th) / 2, 15, true, g.color("on_secondary_container"));
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        drawHeader(cr, r.w);
        drawList(cr, {0, 70, r.w, r.h - 70});
        drawToast(cr, r);
        if (dlg) drawDialog(cr, r);
        else if (pick) drawPicker(cr, r);
        else if (cap) drawCapture(cr, r);
    }

    // ---- input -----------------------------------------------------------------------------------------------------------------------
    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (!it->r.contains(x, y)) continue;
            if (it->id == DlgCard || it->id == PickCard || it->id == CapCard) return {};  // (a click on the card is not a click on the scrim)
            if ((dlg || pick || cap) && it->id < DlgScrim) continue;
            if (cap && it->id == CapToggle && !capArea.contains(x, y)) continue;
            if (pick && !dlg && it->id == PickItem && it->arg >= 0 && !pickArea.contains(x, y)) continue;
            if (it->id == Row || it->id == RowToggle || it->id == RowUp || it->id == RowDown || it->id == RowEdit || it->id == RowDel) { if (!listArea.contains(x, y)) continue; }
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
        if (h.id == DlgField || (h.id == PickItem && h.arg < 0)) return Cursor::Text;
        if (h.id == None || h.id == DlgScrim || h.id == PickScrim || h.id == CapScrim) return Cursor::Default;
        return Cursor::Pointer;
    }

    void openEditor(int idx, const AsApp& draft) {
        dlg = true;
        pick = false;
        dlgIdx = idx;
        d = draft;
        focus = 1;
        curName = d.name.size();
        curCmd = d.cmd.size();
    }
    void closeEditor() { dlg = false; dlgIdx = -1; }
    void openPicker() {
        if (installed.empty()) installed = asInstalled();
        pick = true;
        search.clear();
        curSearch = 0;
        pickScroll.reset();
    }
    void saveEditor() {
        if (!valid()) return;
        d.name = asClean(d.name);
        d.cmd = asClean(d.cmd);
        if (dlgIdx >= 0 && dlgIdx < static_cast<int>(rows.size())) rows[static_cast<size_t>(dlgIdx)] = d;
        else rows.push_back(d);
        commit();
        say(asT("Saved"));
        closeEditor();
    }
    void removeRow(int i) {
        if (i < 0 || i >= static_cast<int>(rows.size())) return;
        rows.erase(rows.begin() + i);
        commit();
        say(asT("App removed"));
    }
    void choose(int i) {
        if (i < 0 || i >= static_cast<int>(installed.size())) return;
        const AsDesk& a = installed[static_cast<size_t>(i)];
        AsApp n;
        n.name = a.name;
        n.icon = a.icon;
        n.cmd = asLaunch(a.path);
        openEditor(-1, n);
    }

    bool act(const Hover& h) {
        switch (h.id) {
            case Add: openPicker(); break;
            case SaveOpen: openCapture(); break;
            case CapScrim: case CapCancel: cap = false; break;
            case CapToggle: if (h.arg >= 0 && h.arg < static_cast<int>(caps.size()) && !caps[static_cast<size_t>(h.arg)].dup) capOn[static_cast<size_t>(h.arg)] = !capOn[static_cast<size_t>(h.arg)]; break;
            case CapAdd: addCaptured(); break;
            case RemoveAll:
                if (Clock::now() < clearArmed) { rows.clear(); clearArmed = {}; commit(); say(asT("All apps removed")); }
                else clearArmed = Clock::now() + std::chrono::seconds(4);
                break;
            case Row: case RowEdit: if (h.arg >= 0 && h.arg < static_cast<int>(rows.size())) openEditor(h.arg, rows[static_cast<size_t>(h.arg)]); break;
            case RowToggle: if (h.arg >= 0 && h.arg < static_cast<int>(rows.size())) { rows[static_cast<size_t>(h.arg)].on = !rows[static_cast<size_t>(h.arg)].on; commit(); } break;
            case RowUp: if (h.arg > 0 && h.arg < static_cast<int>(rows.size())) { std::swap(rows[static_cast<size_t>(h.arg)], rows[static_cast<size_t>(h.arg) - 1]); commit(); } break;
            case RowDown: if (h.arg >= 0 && h.arg + 1 < static_cast<int>(rows.size())) { std::swap(rows[static_cast<size_t>(h.arg)], rows[static_cast<size_t>(h.arg) + 1]); commit(); } break;
            case RowDel: removeRow(h.arg); break;
            case DlgScrim: case DlgCancel: closeEditor(); break;
            case DlgSave: saveEditor(); break;
            case DlgDelete: { const int i = dlgIdx; closeEditor(); removeRow(i); break; }
            case DlgField: focus = h.arg; break;
            case DlgWs: d.ws = std::clamp(h.arg, kAsSpecial, kAsMaxWs); break;
            case PickScrim: case PickClose: pick = false; break;
            case PickItem: if (h.arg >= 0) choose(h.arg); break;
            case PickCustom: openEditor(-1, AsApp{}); break;
            default: return false;
        }
        return true;
    }
    bool click(double x, double y, bool press) override {
        if (!press) return false;
        const Hover h = hitAt(x, y);
        if (h.id == None) return false;
        const bool done = act(h);
        if (h.id == DlgField) {  // the caret goes where the click was
            const std::string& v = h.arg == 1 ? d.name : d.cmd;
            for (const Hit& t : hits) if (t.id == DlgField && t.arg == h.arg) {
                const double inner = t.r.w - 32;
                const size_t cur = h.arg == 1 ? curName : curCmd;
                (h.arg == 1 ? curName : curCmd) = tbCaretAt(v, 15, x - (t.r.x + 16) - fieldOffset(v, cur, inner));
                break;
            }
        }
        return done;
    }
    bool scroll(double x, double y, double dy) override {
        if (dlg) return false;
        if (cap) { if (capArea.contains(x, y)) { capScroll.by(dy * 3); return true; } return false; }
        if (pick) { if (pickArea.contains(x, y)) { pickScroll.by(dy * 3); return true; } return false; }
        if (listArea.contains(x, y)) { listScroll.by(dy * 3); return true; }
        return false;
    }

    // Typing into a box: the text goes in at the caret; the arrows, Home, End, Delete and Backspace work around it; Ctrl+V pastes.
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
            case XKB_KEY_Right: cur = tbNextChar(s, cur); return true;
            case XKB_KEY_Home: cur = 0; return true;
            case XKB_KEY_End: cur = s.size(); return true;
            case XKB_KEY_BackSpace: if (cur > 0) { const size_t p = trPrevChar(s, cur); s.erase(p, cur - p); cur = p; } return true;
            case XKB_KEY_Delete: if (cur < s.size()) s.erase(cur, tbNextChar(s, cur) - cur); return true;
            default: break;
        }
        if (k.text.empty()) return false;
        if (s.size() + k.text.size() > max) return true;
        s.insert(cur, k.text);
        cur += k.text.size();
        return true;
    }
    bool imeContext(ImeContext& context) override {
        if (cap || (!dlg && !pick)) return false;
        const auto& value = !dlg ? search : focus == 1 ? d.name : d.cmd;
        const size_t cursor = !dlg ? curSearch : focus == 1 ? curName : curCmd;
        for (const auto& hit : hits) {
            if ((dlg && hit.id == DlgField && hit.arg == focus) || (!dlg && pick && hit.id == PickItem && hit.arg == -1)) {
                context = imeField(value, cursor, imeCaretFor(value, cursor,
                    {hit.r.x + 16, hit.r.y, hit.r.w - 32, hit.r.h}, 15));
                return true;
            }
        }
        return false;
    }
    void imeEdit(const ImeEdit& edit) override {
        lastKey = Clock::now();
        if (!dlg) { if (applyImeText(search, curSearch, edit, 60)) pickScroll.reset(); }
        else if (focus == 1) applyImeText(d.name, curName, edit, 80);
        else if (focus == 2) applyImeText(d.cmd, curCmd, edit, 600);
    }

    bool key(const KeyEvent& k) override {
        lastKey = Clock::now();
        if (dlg) {
            if (k.sym == XKB_KEY_Escape) { closeEditor(); return true; }
            if (k.sym == XKB_KEY_Tab || k.sym == XKB_KEY_ISO_Left_Tab) { focus = focus == 1 ? 2 : 1; return true; }
            if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { saveEditor(); return true; }
            return focus == 1 ? typeInto(d.name, curName, k, 80) : typeInto(d.cmd, curCmd, k, 600);
        }
        if (cap) {
            if (k.sym == XKB_KEY_Escape) { cap = false; return true; }
            if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { if (capCount() > 0) addCaptured(); return true; }
            return false;
        }
        if (pick) {
            if (k.sym == XKB_KEY_Escape) { pick = false; return true; }
            if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { const std::vector<int> m = pickMatches(); if (!m.empty()) choose(m[0]); return true; }
            const bool used = typeInto(search, curSearch, k, 60);
            if (used) pickScroll.reset();
            return used;
        }
        if (k.sym == XKB_KEY_Page_Down) { listScroll.by(listArea.h * 0.8); return true; }
        if (k.sym == XKB_KEY_Page_Up) { listScroll.by(-listArea.h * 0.8); return true; }
        return false;
    }

    bool tick() override {
        bool changed = listScroll.step();
        if (pickScroll.step()) changed = true;
        if (capScroll.step()) changed = true;
        if (!toast.empty() && Clock::now() >= toastUntil) { toast.clear(); changed = true; }
        if (clearArmed != Clock::time_point{} && Clock::now() >= clearArmed) { clearArmed = {}; changed = true; }
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        const bool caretChanged = caretOn != lastCaret;
        lastCaret = caretOn;
        return changed || caretChanged;
    }
};
