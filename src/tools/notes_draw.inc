namespace notes_ui {
struct Tone { const char* bg; const char* fg; };
constexpr Tone kTones[] = {{"primary_container", "on_primary_container"}, {"secondary_container", "on_secondary_container"},
                           {"tertiary_container", "on_tertiary_container"}, {"surface_container_high", "on_surface"},
                           {"surface_container_highest", "on_surface"}};

const Tone& toneOf(const std::string& id) {
    unsigned h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    return kTones[h % (sizeof kTones / sizeof kTones[0])];
}

// A box of text clipped by ellipsis. maxH 0 = a single line. `user` text (note content) is never translated; the interface
// texts are (pango_layout_set_text looks them up unless it gets an explicit length).
void box(cairo_t* cr, const std::string& s, double x, double y, double w, double maxH, double px, bool bold, const Color& c, double alpha, bool user) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    pango_layout_set_width(l, static_cast<int>(w * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(l, maxH > 0 ? static_cast<int>(maxH * PANGO_SCALE) : -1);
    pango_layout_set_text(l, s.c_str(), user ? static_cast<int>(s.size()) : -1);
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}
double textW(const std::string& s, double px, bool bold) { double w = 0, h = 0; measure(s, px, bold, 0, w, h); return w; }

std::string titleOf(const std::string& content) {
    std::string t = content.substr(0, content.find('\n'));
    t.erase(0, t.find_first_not_of("# \t"));
    return t.substr(0, 200);
}
std::string previewOf(const std::string& content) {
    const size_t nl = content.find('\n');
    if (nl == std::string::npos) return "";
    size_t from = content.find_first_not_of(" \t\r\n", nl);
    if (from == std::string::npos) return "";
    size_t len = std::min<size_t>(content.size() - from, 500);
    while (from + len < content.size() && (content[from + len] & 0xC0) == 0x80) len--;
    return content.substr(from, len);
}
std::string dateOf(time_t t) {
    std::tm tm{}, now{};
    const time_t n = time(nullptr);
    localtime_r(&t, &tm);
    localtime_r(&n, &now);
    const char* fmt = tm.tm_year != now.tm_year ? "%d %b %y %H:%M" : "%d %b %H:%M";
    char buf[48];
    i18n::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}
std::string lowered(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

void roundButton(cairo_t* cr, const Rect& r, const char* ic, const Color& bg, const Color& fg, bool hot, bool on = false) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
    src(cr, on ? g.color("primary") : mix(fg, bg, hot ? 0.2 : 0.1));
    cairo_fill(cr);
    const int sz = static_cast<int>(r.w * 0.5);
    icon(cr, ic, r.x + (r.w - sz) / 2, r.y + (r.h - sz) / 2, sz, on ? g.color("on_primary") : fg, on);
}

void plusMark(cairo_t* cr, double cx, double cy, double arm, const Color& c) {
    src(cr, c);
    rrect(cr, cx - arm, cy - 2.5, 2 * arm, 5, 2.5);
    cairo_fill(cr);
    rrect(cr, cx - 2.5, cy - arm, 5, 2 * arm, 2.5);
    cairo_fill(cr);
}
}
