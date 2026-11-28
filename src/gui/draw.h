// The small drawing helpers of the separate windows (Play3r, Fil3s) (the same look and the same files as the other windows of the shell: colors.json, the shell font,
// the Solar or Material icons, the shapes of assets/shapes.txt). Included once by a program, inside its anonymous namespace.
#pragma once

struct Color { double r = 0, g = 0, b = 0; };
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : ""; }
std::string envDir(const char* var, const std::string& fallback) { const char* x = std::getenv(var); return x && *x ? x : fallback; }
std::string stateDir() { return envDir("XDG_STATE_HOME", homeDir() + "/.local/state") + "/end4pC"; }
std::string dataDir() { return envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/end4pC"; }
std::string cacheDir() { return envDir("XDG_CACHE_HOME", homeDir() + "/.cache") + "/end4pC"; }
std::string runtimeDir() { return envDir("XDG_RUNTIME_DIR", "/tmp"); }
std::string colorsPath() { return stateDir() + "/generated/colors.json"; }
std::string projectDir() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string p = exe;
        for (int i = 0; i < 2; i++) p = p.substr(0, p.rfind('/'));
        if (access((p + "/assets").c_str(), R_OK) == 0) return p;
    }
    return homeDir() + "/.config/end4pC";
}
std::string assetsDir() { return projectDir() + "/assets"; }
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
time_t mtimeOf(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}
std::string configPath() {
    const std::string now = dataDir() + "/config.json";
    return access(now.c_str(), F_OK) == 0 ? now : projectDir() + "/assets/default-config.json";
}
bool jsonValue(const std::string& text, const std::string& key, std::string& out) {
    const size_t k = text.find("\"" + key + "\"");
    const size_t colon = k == std::string::npos ? k : text.find(':', k);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) i++;
    if (i < text.size() && text[i] == '"') {
        const size_t e = text.find('"', i + 1);
        if (e == std::string::npos) return false;
        out = text.substr(i + 1, e - i - 1);
        return true;
    }
    size_t e = i;
    while (e < text.size() && (std::isdigit(static_cast<unsigned char>(text[e])) || text[e] == '-')) e++;
    out = text.substr(i, e - i);
    return true;
}

std::map<std::string, Color> g_colors;
std::string g_iconStyle = "outline";
int g_iconScale = 100;  // % of the size every icon is drawn at (Settings > Appearance > Font & Icons of the shell)
time_t g_colorsStamp = 0;
Color color(const char* n) {
    auto it = g_colors.find(n);
    return it == g_colors.end() ? Color{} : it->second;
}
void loadColors() {
    g_colors.clear();
    std::ifstream f(colorsPath());
    std::string line;
    while (std::getline(f, line)) {
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        const size_t h = line.find('#', q2 == std::string::npos ? 0 : q2);
        if (q2 == std::string::npos || h == std::string::npos || line.size() < h + 7) continue;
        const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(h + 1, 6).c_str(), nullptr, 16));
        g_colors[line.substr(q1 + 1, q2 - q1 - 1)] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
    }
    g_colorsStamp = mtimeOf(colorsPath());
}

void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}
void src(cairo_t* cr, const Color& c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
Color mix(const Color& a, const Color& b, double p) { return {a.r * p + b.r * (1 - p), a.g * p + b.g * (1 - p), a.b * p + b.b * (1 - p)}; }

PangoLayout* g_measure = nullptr;
void setFont(PangoLayout* l, double px, bool bold) {
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
}
double textWidth(const std::string& s, double px, bool bold) {
    setFont(g_measure, px, bold);
    pango_layout_set_width(g_measure, -1);
    pango_layout_set_text(g_measure, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(g_measure, &w, &h);
    return w;
}
// Height of the text wrapped to `wrap` pixels (0: one line).
double textHeight(const std::string& s, double px, bool bold, double wrap = 0) {
    setFont(g_measure, px, bold);
    pango_layout_set_width(g_measure, wrap > 0 ? static_cast<int>(wrap * PANGO_SCALE) : -1);
    pango_layout_set_wrap(g_measure, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(g_measure, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(g_measure, &w, &h);
    return h;
}
// Text at (x, y) (its top left). `width` > 0 cuts it with an ellipsis (or wraps it when wrap is true); align 0 left, 1 centre, 2 right inside `width`.
double text(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double alpha = 1, double width = 0, int align = 0, bool wrap = false, int maxLines = 0) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    if (width > 0) {
        pango_layout_set_width(l, static_cast<int>(width * PANGO_SCALE));
        if (wrap) {
            pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
            if (maxLines > 0) { pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END); pango_layout_set_height(l, -maxLines); }  // at most that many lines, the last one cut with an ellipsis
        } else {
            pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
            pango_layout_set_single_paragraph_mode(l, TRUE);
        }
        pango_layout_set_alignment(l, align == 1 ? PANGO_ALIGN_CENTER : align == 2 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
    }
    pango_layout_set_text(l, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    return h;
}

cairo_t* mkcr(cairo_surface_t* s) {
    static cairo_font_options_t* fo = nullptr;
    if (!fo) {
        fo = cairo_font_options_create();
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    }
    cairo_t* c = cairo_create(s);
    cairo_set_font_options(c, fo);
    return c;
}

cairo_surface_t* iconMask(const std::string& name, int size) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = g_iconStyle + "/" + name + "/" + std::to_string(size) + (matsym::enabled() ? "|m" : "");
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = matsym::makeMask(name, false, size);
    for (const std::string& style : {g_iconStyle, std::string("outline"), std::string("bold")}) {
        if (mask) break;
        const std::string path = assetsDir() + "/icons/" + style + "/" + matsym::baseName(name) + ".svg";
        if (access(path.c_str(), R_OK) != 0) continue;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr);
        if (!h) continue;
        mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(mask);
        RsvgRectangle vp = {0, 0, double(size), double(size)};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
        break;
    }
    cache[key] = mask;
    return mask;
}
double g_scale = 1;  // the scale of the window being drawn (the icons are cached at real pixels)
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, double alpha = 1) {
    if (g_iconScale != 100) {  // the icon keeps its centre
        const int scaled = std::max(8, static_cast<int>(std::lround(size * g_iconScale / 100.0)));
        x += (size - scaled) / 2.0;
        y += (size - scaled) / 2.0;
        size = scaled;
    }
    const int px = hidpi::bufferExtent(size, g_scale);
    cairo_surface_t* m = iconMask(name, px);
    if (!m) return;
    src(cr, c, alpha);
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, double(size) / px, double(size) / px);
    cairo_mask_surface(cr, m, 0, 0);
    cairo_restore(cr);
}

std::map<std::string, std::vector<double>> loadShapes() {
    std::map<std::string, std::vector<double>> out;
    std::ifstream f(assetsDir() + "/shapes.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string name;
        ss >> name;
        std::vector<double> nums;
        for (double v; ss >> v;) nums.push_back(v);
        if (!name.empty() && nums.size() >= 8 && (nums.size() - 2) % 6 == 0) out[name] = std::move(nums);
    }
    return out;
}
void shapePath(cairo_t* cr, const std::string& name, double x, double y, double size) {
    static const auto all = loadShapes();
    auto it = all.find(name);
    if (it == all.end()) return;
    const auto& n = it->second;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x + n[0] * size, y + n[1] * size);
    for (size_t i = 2; i + 5 < n.size(); i += 6)
        cairo_curve_to(cr, x + n[i] * size, y + n[i + 1] * size, x + n[i + 2] * size, y + n[i + 3] * size, x + n[i + 4] * size, y + n[i + 5] * size);
    cairo_close_path(cr);
}

// A picture from bytes (a cover) as a Cairo image, or nullptr.
cairo_surface_t* imageFromBytes(const unsigned char* data, size_t size) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GError* err = nullptr;
    if (!gdk_pixbuf_loader_write(loader, data, size, &err) | !gdk_pixbuf_loader_close(loader, nullptr)) {
        if (err) g_error_free(err);
        g_object_unref(loader);
        return nullptr;
    }
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pb) { g_object_unref(loader); return nullptr; }
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const guchar* q = px + y * rs + x * nch;
            const unsigned a = nch == 4 ? q[3] : 255;
            *reinterpret_cast<uint32_t*>(dst + y * drs + x * 4) = (a << 24) | (((q[0] * a + 127) / 255) << 16) | (((q[1] * a + 127) / 255) << 8) | ((q[2] * a + 127) / 255);
        }
    cairo_surface_mark_dirty(img);
    g_object_unref(loader);
    return img;
}
