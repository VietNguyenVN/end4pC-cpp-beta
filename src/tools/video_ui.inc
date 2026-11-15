namespace vid {
struct HitRect {
    Rect r;
    int id;
};

double label(cairo_t* cr, const char* key, double x, double cy, double px, bool bold, const Color& c, double maxW = 0, bool literal = false) {
    std::string s = literal ? key : i18n::tr(key);
    if (maxW > 0) s = ellipsize(s, px, bold, maxW);
    double w = 0, h = 0;
    measure(s, px, bold, 0, w, h);
    text(cr, s, x, cy - h / 2, px, bold, c);
    return w;
}
double label(cairo_t* cr, const std::string& s, double x, double cy, double px, bool bold, const Color& c, double maxW = 0) {
    return label(cr, s.c_str(), x, cy, px, bold, c, maxW, true);
}
double textWidth(const std::string& s, double px, bool bold) {
    double w = 0, h = 0;
    measure(s, px, bold, 0, w, h);
    return w;
}
std::string tr(const char* key) { return i18n::tr(key); }

struct PillStyle {
    Color bg, fg;
    bool hover = false;
};

double pill(cairo_t* cr, Rect r, const char* iconName, const std::string& caption, const PillStyle& st) {
    src(cr, st.hover ? mix(st.fg, st.bg, 0.14) : st.bg);
    rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
    cairo_fill(cr);
    const int ic = static_cast<int>(std::lround(r.h * 0.5));
    const double tw = caption.empty() ? 0 : textWidth(caption, 15, true);
    const double gap = iconName && !caption.empty() ? 8 : 0;
    double x = r.x + (r.w - ((iconName ? ic : 0) + gap + tw)) / 2;
    if (iconName) { icon(cr, iconName, x, r.y + (r.h - ic) / 2, ic, st.fg); x += ic + gap; }
    if (!caption.empty()) label(cr, caption, x, r.y + r.h / 2, 15, true, st.fg);
    return r.w;
}
double pillWidth(const char* iconName, const std::string& caption, double h, double padX) {
    const int ic = static_cast<int>(std::lround(h * 0.5));
    return 2 * padX + (iconName ? ic : 0) + (iconName && !caption.empty() ? 8 : 0) + (caption.empty() ? 0 : textWidth(caption, 15, true));
}

void card(cairo_t* cr, Rect r, const Color& c, double radius = 28) {
    src(cr, c);
    rrect(cr, r.x, r.y, r.w, r.h, radius);
    cairo_fill(cr);
}
}
