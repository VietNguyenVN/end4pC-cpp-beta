namespace notes_ui {
constexpr double kMdBtn = 34, kMdPad = 6, kMdGap = 2, kMdSep = 13, kMdBarH = kMdBtn + 2 * kMdPad;
constexpr double kMdBottom = 14, kMdAbove = 10;

struct MdTool {
    MdAct act;
    const char* icon;
    const char* tip;
    const char* keys;
};

constexpr MdTool kMdTools[] = {
    {MdAct::Bold, "text-bold", "Bold", "Ctrl+B"},
    {MdAct::Italic, "text-italic", "Italic", "Ctrl+I"},
    {MdAct::Strike, "text-cross", "Strikethrough", "Ctrl+Shift+X"},
    {MdAct::Code, "code", "Code", "Ctrl+`"},
    {MdAct::Heading, "hashtag/heading", "Heading", ""},
    {MdAct::Bullet, "list", "Bulleted list", ""},
    {MdAct::Numbered, "list-1", "Numbered list", ""},
    {MdAct::Task, "checklist", "Checklist", ""},
    {MdAct::Quote, "chat-line/quote", "Quote", ""},
    {MdAct::Link, "link", "Link", "Ctrl+K"},
    {MdAct::CodeBlock, "code-square", "Code block", ""},
    {MdAct::Rule, "minus-square/hr", "Divider line", ""},
};
constexpr int kMdGroups[] = {4, 1, 4, 3};
constexpr int kMdToolCount = static_cast<int>(sizeof kMdTools / sizeof kMdTools[0]);

void listGlyph(cairo_t* cr, double cx, double cy, bool numbered, const Color& ink) {
    cairo_save(cr);
    src(cr, ink);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 3; i++) {
        const double y = cy + (i - 1) * 6.2;
        cairo_move_to(cr, cx - 1.5, y);
        cairo_line_to(cr, cx + 8.5, y);
        cairo_stroke(cr);
        if (numbered) {
            const std::string d = std::to_string(i + 1);
            double tw = 0, th = 0;
            measure(d, 8.5, true, 0, tw, th);
            text(cr, d, cx - 9.5 + (4.5 - tw) / 2, y - th / 2 + 0.5, 8.5, true, ink);
        } else {
            cairo_arc(cr, cx - 6.5, y, 1.7, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
}

struct MdBar {
    struct Btn { int tool; Rect r; };
    std::vector<Btn> btns;
    std::vector<double> seps;
    Rect bar{};
    int hover = -1, pressed = -1;

    void layout(double W, const Rect& card) {
        btns.clear();
        seps.clear();
        int groups = static_cast<int>(sizeof kMdGroups / sizeof kMdGroups[0]);
        auto widthOf = [&](int n) {
            double w = 2 * kMdPad;
            int count = 0;
            for (int gi = 0; gi < n; gi++) {
                w += kMdGroups[gi] * kMdBtn + (kMdGroups[gi] - 1) * kMdGap + (gi ? kMdSep : 0);
                count += kMdGroups[gi];
            }
            (void)count;
            return w;
        };
        while (groups > 1 && widthOf(groups) > W - 32) groups--;
        const double w = widthOf(groups);
        bar = {card.x + (W - w) / 2, card.y + card.h - kMdBottom - kMdBarH, w, kMdBarH};
        double x = bar.x + kMdPad;
        int tool = 0;
        for (int gi = 0; gi < groups; gi++) {
            if (gi) { seps.push_back(x + (kMdSep - 1) / 2.0 - 0.5); x += kMdSep; }
            for (int i = 0; i < kMdGroups[gi]; i++) {
                btns.push_back({tool++, {x, bar.y + kMdPad, kMdBtn, kMdBtn}});
                x += kMdBtn + (i + 1 < kMdGroups[gi] ? kMdGap : 0);
            }
        }
    }
    int hit(double x, double y) const {
        for (size_t i = 0; i < btns.size(); i++) if (btns[i].r.contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    bool over(double x, double y) const { return bar.contains(x, y); }

    void draw(cairo_t* cr, const Color& bg, const Color& fg, unsigned on) const {
        const Color bar0 = mix(fg, bg, 0.12);
        rrect(cr, bar.x, bar.y, bar.w, bar.h, bar.h / 2);
        src(cr, bar0);
        cairo_fill(cr);
        for (double sx : seps) {
            rrect(cr, sx, bar.y + (bar.h - 18) / 2, 1.5, 18, 0.75);
            src(cr, fg, 0.28);
            cairo_fill(cr);
        }
        for (size_t i = 0; i < btns.size(); i++) {
            const MdTool& t = kMdTools[btns[i].tool];
            const Rect& r = btns[i].r;
            const bool active = (on >> btns[i].tool) & 1u, hot = static_cast<int>(i) == hover, down = static_cast<int>(i) == pressed;
            if (active || hot || down) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
                src(cr, active ? g.color("primary") : mix(fg, bar0, down ? 0.34 : 0.2), active && !hot && !down ? 1.0 : 1.0);
                cairo_fill(cr);
            }
            const Color ink = active ? g.color("on_primary") : fg;
            if (t.act == MdAct::Quote && !matsym::enabled()) {
                double tw = 0, th = 0;
                measure("\xE2\x80\x9C", 30, true, 0, tw, th);
                text(cr, "\xE2\x80\x9C", r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2 + 6, 30, true, ink);
            } else if ((t.act == MdAct::Bullet || t.act == MdAct::Numbered) && !matsym::enabled()) {
                listGlyph(cr, r.x + r.w / 2, r.y + r.h / 2, t.act == MdAct::Numbered, ink);
            } else {
                icon(cr, t.icon, r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, ink, active);
            }
        }
    }
};
}
