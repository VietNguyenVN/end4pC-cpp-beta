constexpr double kEdPx = 18;

void trFillLayout(PangoLayout* l, const std::string& s, double width, double px) {
    setFont(l, px, false);
    pango_layout_set_width(l, static_cast<int>(width * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(l, s.data(), static_cast<int>(s.size()));
}

void trLine(cairo_t* cr, const std::string& s, double x, double y, double w, double px, bool bold, const Color& c, double alpha = 1) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    pango_layout_set_width(l, static_cast<int>(w * PANGO_SCALE));
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(l, TRUE);
    pango_layout_set_text(l, s.data(), static_cast<int>(s.size()));
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

double trBlock(cairo_t* cr, const std::string& s, Rect box, double scroll, const Color& c, double alpha) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    trFillLayout(l, s, box.w, kEdPx);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_save(cr);
    cairo_rectangle(cr, box.x, box.y, box.w, box.h);
    cairo_clip(cr);
    src(cr, c, alpha);
    cairo_move_to(cr, box.x, box.y - scroll);
    pango_cairo_show_layout(cr, l);
    cairo_restore(cr);
    g_object_unref(l);
    return h;
}

struct TrEditor {
    std::string s;
    size_t cur = 0, anc = 0;
    double scroll = 0, wantX = -1;
    bool reveal = false;
    Rect box;

    bool hasSel() const { return cur != anc; }
    size_t selBegin() const { return std::min(cur, anc); }
    size_t selEnd() const { return std::max(cur, anc); }
    std::string selected() const { return s.substr(selBegin(), selEnd() - selBegin()); }

    void set(const std::string& t) { s = trHead(trClean(t), kTrMaxText); cur = anc = s.size(); scroll = 0; wantX = -1; reveal = true; }

    bool insert(const std::string& raw) {
        std::string t = trClean(raw);
        const size_t room = kTrMaxText - (s.size() - (selEnd() - selBegin()));
        t = trHead(t, std::min(t.size(), room > kTrMaxText ? 0 : room));
        if (t.empty() && !hasSel()) return false;
        s.replace(selBegin(), selEnd() - selBegin(), t);
        cur = anc = selBegin() + t.size();
        wantX = -1;
        reveal = true;
        return true;
    }
    bool erase(bool forward) {
        if (!hasSel()) {
            if (forward ? cur >= s.size() : cur == 0) return false;
            if (forward) anc = trNextChar(s, cur);
            else anc = trPrevChar(s, cur);
        }
        return insert("");
    }
    void moveTo(size_t i, bool extend, bool keepWant = false) {
        cur = std::min(i, s.size());
        if (!extend) anc = cur;
        if (!keepWant) wantX = -1;
        reveal = true;
    }

    PangoLayout* layout() const { trFillLayout(g_measure, s, box.w, kEdPx); return g_measure; }
    size_t indexAt(double x, double y) const {
        PangoLayout* l = layout();
        int idx = 0, trail = 0;
        pango_layout_xy_to_index(l, static_cast<int>((x - box.x) * PANGO_SCALE), static_cast<int>((y - box.y + scroll) * PANGO_SCALE), &idx, &trail);
        size_t i = static_cast<size_t>(idx);
        if (trail > 0) i = trNextChar(s, i);
        return std::min(i, s.size());
    }
    PangoRectangle caretPos() const {
        PangoRectangle p{};
        pango_layout_index_to_pos(layout(), static_cast<int>(cur), &p);
        return p;
    }
    void lineEnds(size_t& start, size_t& end, bool& last) const {
        PangoLayout* l = layout();
        int line = 0, x = 0;
        pango_layout_index_to_line_x(l, static_cast<int>(cur), 0, &line, &x);
        PangoLayoutLine* ln = pango_layout_get_line_readonly(l, line);
        start = static_cast<size_t>(ln->start_index);
        end = start + static_cast<size_t>(ln->length);
        last = line + 1 >= pango_layout_get_line_count(l);
    }
    void home(bool extend) { size_t a, b; bool last; lineEnds(a, b, last); moveTo(a, extend); }
    void end(bool extend) {
        size_t a, b;
        bool last;
        lineEnds(a, b, last);
        moveTo(last ? b : trPrevChar(s, b), extend);
    }
    void vertical(int dir, bool extend) {
        PangoLayout* l = layout();
        PangoRectangle p = caretPos();
        if (wantX < 0) wantX = p.x;
        const int ty = dir < 0 ? p.y - p.height / 2 : p.y + p.height + p.height / 2;
        int total = 0, th = 0;
        pango_layout_get_size(l, &total, &th);
        if (ty < 0) { moveTo(0, extend); return; }
        if (ty >= th) { moveTo(s.size(), extend); return; }
        int idx = 0, trail = 0;
        pango_layout_xy_to_index(l, static_cast<int>(wantX), ty, &idx, &trail);
        size_t i = static_cast<size_t>(idx);
        if (trail > 0) i = trNextChar(s, i);
        moveTo(i, extend, true);
    }

    void clampScroll(double viewH, double contentH) {
        scroll = std::clamp(scroll, 0.0, std::max(0.0, contentH - viewH));
    }
    void drawSelection(cairo_t* cr, PangoLayout* l, const Color& c) const {
        if (!hasSel()) return;
        PangoLayoutIter* it = pango_layout_get_iter(l);
        do {
            PangoLayoutLine* ln = pango_layout_iter_get_line_readonly(it);
            int y0 = 0, y1 = 0;
            pango_layout_iter_get_line_yrange(it, &y0, &y1);
            const int a = std::max<int>(ln->start_index, static_cast<int>(selBegin())), b = std::min<int>(ln->start_index + ln->length, static_cast<int>(selEnd()));
            if (a >= b) continue;
            int* ranges = nullptr;
            int n = 0;
            pango_layout_line_get_x_ranges(ln, a, b, &ranges, &n);
            for (int k = 0; k < n; k++) {
                cairo_rectangle(cr, box.x + ranges[2 * k] / double(PANGO_SCALE), box.y - scroll + y0 / double(PANGO_SCALE),
                                (ranges[2 * k + 1] - ranges[2 * k]) / double(PANGO_SCALE), (y1 - y0) / double(PANGO_SCALE));
            }
            g_free(ranges);
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
        src(cr, c, 0.35);
        cairo_fill(cr);
    }
    void draw(cairo_t* cr, Rect area, const Color& fg, const Color& selColor, bool caretOn) {
        box = area;
        PangoLayout* l = pango_cairo_create_layout(cr);
        trFillLayout(l, s, box.w, kEdPx);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(l, &w, &h);
        const PangoRectangle p = caretPos();
        if (reveal) {
            const double top = p.y / double(PANGO_SCALE), bot = (p.y + p.height) / double(PANGO_SCALE);
            if (top < scroll) scroll = top;
            if (bot > scroll + box.h) scroll = bot - box.h;
            reveal = false;
        }
        clampScroll(box.h, h);
        cairo_save(cr);
        cairo_rectangle(cr, box.x - 2, box.y, box.w + 4, box.h);
        cairo_clip(cr);
        drawSelection(cr, l, selColor);
        src(cr, fg);
        cairo_move_to(cr, box.x, box.y - scroll);
        pango_cairo_show_layout(cr, l);
        if (caretOn) {
            cairo_rectangle(cr, box.x + p.x / double(PANGO_SCALE) - 1, box.y - scroll + p.y / double(PANGO_SCALE), 2, p.height / double(PANGO_SCALE));
            cairo_fill(cr);
        }
        cairo_restore(cr);
        g_object_unref(l);
    }
};
