#include <cairo.h>
#include "bubble.h"
#include "elastic.h"
#include "i18n.h"
#include "oled.h"
#include "units.h"
#include <dirent.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include "shellfont.h"
#include <poll.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static cairo_t* mkcr(cairo_surface_t* s) {
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

namespace {
using Clock = std::chrono::steady_clock;

struct Color { double r = 0, g = 0, b = 0; };
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

constexpr int kWinW = 1240, kWinH = 740;
constexpr double kWinRadius = 36, kPad = 14, kBarH = 76, kSegH = 48, kCloseSize = 44, kShapeSize = 56;

std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : ""; }
std::string envDir(const char* var, const std::string& fallback) { const char* x = std::getenv(var); return x && *x ? x : fallback; }
std::string stateDir() { return envDir("XDG_STATE_HOME", homeDir() + "/.local/state") + "/end4pC"; }
std::string configDir() { return envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/end4pC"; }
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
std::string configPath() {
    const std::string now = configDir() + "/config.json";
    if (access(now.c_str(), F_OK) == 0) return now;
    for (const std::string& old : {envDir("XDG_CONFIG_HOME", homeDir() + "/.config") + "/end4pC-user/config.json", projectDir() + "/config.json"})
        if (access(old.c_str(), F_OK) == 0) return old;
    return now;
}

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

enum class Cursor { Default, Pointer, Text };

struct KeyEvent {
    xkb_keysym_t sym = 0;
    std::string text;  // what the key types (UTF-8), empty for keys that type nothing
    bool ctrl = false, shift = false, alt = false, logo = false;
    uint32_t code = 0;         // the evdev keycode
    std::string baseName;      // the key with no modifier (Shift+1 is "1"), as xkb names it: what a Hyprland bind calls the key
};

struct ToolPage {
    virtual ~ToolPage() = default;
    virtual const char* name() const = 0;
    virtual const char* icon() const = 0;
    virtual void init() {}
    virtual void draw(cairo_t* cr, Rect r) = 0;
    virtual bool click(double, double, bool) { return false; }
    virtual bool motion(double, double) { return false; }
    virtual bool leave() { return false; }
    virtual bool scroll(double, double, double) { return false; }
    virtual bool key(const KeyEvent&) { return false; }
    virtual bool tick() { return false; }
    virtual bool animating() const { return false; }
    virtual bool grabsKeys() const { return false; }  // true: every key, shortcuts of the window included, goes to key()
    virtual void blur() {}
    virtual bool roundClip() const { return true; }  // false: the page may use the corners of its area (the clip is a plain rectangle)                            // the keyboard left the window
    virtual Cursor cursor(double, double) const { return Cursor::Default; }
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;

    bool running = true, configured = false, dirty = true;
    int width = kWinW, height = kWinH;

    std::map<std::string, Color> colors;
    time_t colorsStamp = 0;
    std::string iconStyle = "broken";
    std::vector<std::unique_ptr<ToolPage>> pages;
    int page = 0, prevPage = 0;
    double switchT = 1;
    Clock::time_point switchStart;

    double px = 0, py = 0;
    bool inside = false;
    int hoverTab = -1;
    bool hoverClose = false, pageHot = false;

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    Color color(const char* n) const {
        auto it = colors.find(n);
        return it == colors.end() ? Color{} : it->second;
    }
};

App g;
PangoLayout* g_measure = nullptr;

void loadColors() {
    g.colors.clear();
    std::ifstream f(colorsPath());
    std::string line;
    while (std::getline(f, line)) {
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        const size_t h = line.find('#', q2 == std::string::npos ? 0 : q2);
        if (q2 == std::string::npos || h == std::string::npos || line.size() < h + 7) continue;
        const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(h + 1, 6).c_str(), nullptr, 16));
        g.colors[line.substr(q1 + 1, q2 - q1 - 1)] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
    }
    g.colorsStamp = mtimeOf(colorsPath());
    std::string ui;
    if (!jsonValue(readFile(configPath()), "uiBackground", ui) || ui != "oled") return;
    for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) g.colors[n] = Color{0, 0, 0};
    for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
        auto it = g.colors.find(n);
        if (it != g.colors.end()) it->second = Color{it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
    }
}

void loadConfig() {
    const std::string t = readFile(configPath());
    std::string style, lang;
    g.iconStyle = jsonValue(t, "iconStyle", style) && !style.empty() ? style : "broken";
    std::string mat;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(t, "materialIcons", mat) && mat == "1";
    jsonValue(t, "language", lang);
    i18n::load(lang, assetsDir(), configDir() + "/i18n");
    loadColors();
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
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double easeOut(double t) { return 1 - std::pow(1 - t, 3); }

void setFont(PangoLayout* l, double px, bool bold) {
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
}
void measure(const std::string& s, double px, bool bold, double wrap, double& w, double& h) {
    setFont(g_measure, px, bold);
    pango_layout_set_width(g_measure, wrap > 0 ? static_cast<int>(wrap * PANGO_SCALE) : -1);
    pango_layout_set_wrap(g_measure, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(g_measure, s.c_str(), -1);
    int iw = 0, ih = 0;
    pango_layout_get_pixel_size(g_measure, &iw, &ih);
    w = iw;
    h = ih;
}
double fontBaseline(double px, bool bold) {
    static cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    pango_layout_set_text(l, "Ag", -1);
    const double b = pango_layout_get_baseline(l) / static_cast<double>(PANGO_SCALE);
    g_object_unref(l);
    cairo_destroy(cr);
    return b;
}

double text(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double wrap = 0, double alpha = 1) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    if (wrap > 0) { pango_layout_set_width(l, static_cast<int>(wrap * PANGO_SCALE)); pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR); }
    pango_layout_set_text(l, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    return h;
}

cairo_surface_t* iconMask(const std::string& name, int size, bool bold) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = (bold ? "bold!" : g.iconStyle) + "/" + name + "/" + std::to_string(size) + (matsym::enabled() ? "|m" : "");
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = matsym::makeMask(name, bold, size);
    for (const std::string& style : {bold ? std::string("bold") : g.iconStyle, std::string("outline"), std::string("bold")}) {
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
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, bool bold = false) {
    cairo_surface_t* m = iconMask(name, size, bold);
    if (!m) return;
    src(cr, c);
    cairo_mask_surface(cr, m, x, y);
}

// Material shape outlines (assets/shapes.txt: bezier points in a unit square).
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

struct PlaceholderCard { const char* icon; const char* label; };

struct PlaceholderGrid {
    Rect area;
    int hover = -1;

    std::vector<Rect> cells() const {
        constexpr double gap = 10;
        const double cw = (area.w - 3 * gap) / 4, ch = (area.h - gap) / 2;
        auto cell = [&](int c, int r, int cs, int rs) { return Rect{c * (cw + gap), r * (ch + gap), cs * cw + (cs - 1) * gap, rs * ch + (rs - 1) * gap}; };
        return {cell(0, 0, 2, 2), cell(2, 0, 1, 1), cell(3, 0, 1, 1), cell(2, 1, 1, 1), cell(3, 1, 1, 1)};
    }
    bool motion(double x, double y) {
        int now = -1;
        const auto c = cells();
        for (size_t i = 0; i < c.size(); i++) if (c[i].contains(x, y)) now = static_cast<int>(i);
        const bool changed = now != hover;
        hover = now;
        return changed;
    }
    bool leave() { const bool changed = hover >= 0; hover = -1; return changed; }

    void draw(cairo_t* cr, Rect r, const char* heroIcon, const char* title, const std::vector<PlaceholderCard>& small) {
        area = r;
        static const char* bg[5] = {"primary_container", "secondary_container", "tertiary_container", "surface_container_high", "surface_container"};
        static const char* fg[5] = {"on_primary_container", "on_secondary_container", "on_tertiary_container", "on_surface", "on_surface"};
        const auto c = cells();
        for (size_t i = 0; i < c.size(); i++) {
            const Color base = g.color(bg[i]);
            src(cr, hover == static_cast<int>(i) ? mix(g.color(fg[i]), base, 0.1) : base);
            rrect(cr, c[i].x, c[i].y, c[i].w, c[i].h, 28);
            cairo_fill(cr);
            if (i == 0) drawHero(cr, c[0], heroIcon, title, g.color(fg[0]));
            else if (i - 1 < small.size()) drawSmall(cr, c[i], small[i - 1], g.color(fg[i]));
        }
    }

private:
    static void drawHero(cairo_t* cr, const Rect& c, const char* ic, const char* title, const Color& fg) {
        cairo_save(cr);
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_clip(cr);
        shapePath(cr, "Cookie12Sided", c.x + c.w - 170, c.y + c.h - 170, 260);
        src(cr, fg, 0.10);
        cairo_fill(cr);
        cairo_restore(cr);
        icon(cr, ic, c.x + 28, c.y + 28, 64, fg);
        double tw = 0, th = 0;
        measure(title, 34, true, 0, tw, th);
        text(cr, title, c.x + 28, c.y + c.h - 28 - th - 26, 34, true, fg);
        text(cr, "Coming soon", c.x + 30, c.y + c.h - 28 - 20, 15, false, fg, 0, 0.75);
    }
    static void drawSmall(cairo_t* cr, const Rect& c, const PlaceholderCard& card, const Color& fg) {
        icon(cr, card.icon, c.x + 20, c.y + 20, 36, fg);
        double tw = 0, th = 0;
        measure(card.label, 16, true, 0, tw, th);
        text(cr, card.label, c.x + 20, c.y + c.h - 20 - th, 16, true, fg);
    }
};

#include "tools/page_translator.inc"
#include "tools/page_notes.inc"
#include "tools/page_converter.inc"
#include "tools/page_video.inc"
#include "tools/page_timers.inc"
#include "tools/page_units.inc"
#include "tools/keybinds_store.inc"
#include "tools/page_keybinds.inc"

void buildPages() {
    g.pages.push_back(std::make_unique<TranslatorPage>());
    g.pages.push_back(std::make_unique<NotesPage>());
    g.pages.push_back(std::make_unique<ConverterPage>());
    g.pages.push_back(std::make_unique<VideoPage>());
    g.pages.push_back(std::make_unique<UnitsPage>());
    g.pages.push_back(std::make_unique<TimersPage>(0));
    g.pages.push_back(std::make_unique<TimersPage>(1));
    g.pages.push_back(std::make_unique<TimersPage>(2));
    g.pages.push_back(std::make_unique<KeybindsPage>());
    for (auto& p : g.pages) p->init();
}

ToolPage& cur() { return *g.pages[g.page]; }
int pageCount() { return static_cast<int>(g.pages.size()); }

Rect contentRect() { return {kPad, kBarH, g.width - 2.0 * kPad, g.height - kBarH - kPad}; }
Rect closeRect() { return {g.width - kPad - kCloseSize, (kBarH - kCloseSize) / 2, kCloseSize, kCloseSize}; }

constexpr double kSegPad = 4, kBtnPadX = 18, kBtnGap = 8, kBtnIcon = 20, kLabelPx = 15;
// With room for all the labels each page shows its name; without it only the chosen one does, and the others are their icon.
bool tabsCompact() {
    double total = 2 * kSegPad;
    for (auto& p : g.pages) {
        double tw = 0, th = 0;
        measure(p->name(), kLabelPx, true, 0, tw, th);
        total += 2 * kBtnPadX + kBtnIcon + kBtnGap + tw;
    }
    return total > g.width - 2 * 240.0;  // (the title is on the left and the close button on the right)
}
std::vector<Rect> tabRects() {
    std::vector<double> w;
    double total = 2 * kSegPad;
    const bool compact = tabsCompact();
    int index = 0;
    for (auto& p : g.pages) {
        double tw = 0, th = 0;
        measure(p->name(), kLabelPx, true, 0, tw, th);
        const bool label = !compact || index == g.page;
        w.push_back(2 * kBtnPadX + kBtnIcon + (label ? kBtnGap + tw : 0));
        total += w.back();
        index++;
    }
    std::vector<Rect> out;
    double x = (g.width - total) / 2 + kSegPad;
    const double y = (kBarH - kSegH) / 2 + kSegPad;
    for (double bw : w) { out.push_back({x, y, bw, kSegH - 2 * kSegPad}); x += bw; }
    return out;
}
Rect segRect() {
    const auto t = tabRects();
    return {t.front().x - kSegPad, t.front().y - kSegPad, t.back().x + t.back().w - t.front().x + 2 * kSegPad, kSegH};
}
int tabAt(double x, double y) {
    const auto t = tabRects();
    for (size_t i = 0; i < t.size(); i++) if (t[i].contains(x, y)) return static_cast<int>(i);
    return -1;
}

void drawTitle(cairo_t* cr) {
    const double sy = (kBarH - kShapeSize) / 2, sx = kPad + 4;
    shapePath(cr, "Cookie9Sided", sx, sy, kShapeSize);
    src(cr, g.color("primary_container"));
    cairo_fill(cr);
    icon(cr, "ruler-pen", sx + (kShapeSize - 30) / 2, sy + (kShapeSize - 30) / 2, 30, g.color("on_primary_container"), true);
    double tw = 0, th = 0;
    measure("Tools", 26, true, 0, tw, th);
    text(cr, "Tools", sx + kShapeSize + 14, (kBarH - th) / 2, 26, true, g.color("on_surface"));
}

void drawTabs(cairo_t* cr) {
    const Rect s = segRect();
    rrect(cr, s.x, s.y, s.w, s.h, s.h / 2);
    src(cr, g.color("surface_container_high"));
    cairo_fill(cr);
    const auto t = tabRects();
    const double e = easeOut(g.switchT);
    const Rect a = t[g.prevPage], b = t[g.page];
    const double hx = lerp(a.x, b.x, e), hw = lerp(a.w, b.w, e);
    rrect(cr, hx, b.y, hw, b.h, b.h / 2);
    src(cr, g.color("primary"));
    cairo_fill(cr);
    for (size_t i = 0; i < t.size(); i++) {
        const bool on = static_cast<int>(i) == g.page, hot = static_cast<int>(i) == g.hoverTab;
        if (hot && !on) {
            rrect(cr, t[i].x, t[i].y, t[i].w, t[i].h, t[i].h / 2);
            src(cr, g.color("surface_container_highest"));
            cairo_fill(cr);
        }
        const double over = std::clamp(1 - std::abs(t[i].x + t[i].w / 2 - (hx + hw / 2)) / (t[i].w / 2), 0.0, 1.0);
        const Color c = mix(g.color("on_primary"), g.color(hot ? "on_surface" : "on_surface_variant"), over);
        const char* name = g.pages[i]->name();
        double tw = 0, th = 0;
        measure(name, kLabelPx, true, 0, tw, th);
        icon(cr, g.pages[i]->icon(), t[i].x + kBtnPadX, t[i].y + (t[i].h - kBtnIcon) / 2, static_cast<int>(kBtnIcon), c, on);
        if (!tabsCompact() || on) text(cr, name, t[i].x + kBtnPadX + kBtnIcon + kBtnGap, t[i].y + (t[i].h - th) / 2, kLabelPx, true, c);
    }
}

void drawClose(cairo_t* cr) {
    const Rect c = closeRect();
    cairo_new_sub_path(cr);
    cairo_arc(cr, c.x + c.w / 2, c.y + c.h / 2, c.w / 2, 0, 2 * M_PI);
    src(cr, g.hoverClose ? mix(g.color("error"), g.color("surface_container_highest"), 0.3) : g.color("surface_container_highest"));
    cairo_fill(cr);
    icon(cr, "close-circle", c.x + (c.w - 22) / 2, c.y + (c.h - 22) / 2, 22, g.hoverClose ? g.color("error") : g.color("on_surface_variant"));
}

void drawPageAt(cairo_t* cr, int index, Rect r, double dx, double alpha) {
    cairo_save(cr);
    if (g.pages[index]->roundClip()) rrect(cr, r.x, r.y, r.w, r.h, 28);
    else cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_clip(cr);
    cairo_translate(cr, r.x + dx, r.y);
    cairo_push_group(cr);
    g.pages[index]->draw(cr, {0, 0, r.w, r.h});
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

void drawContent(cairo_t* cr) {
    const Rect r = contentRect();
    if (g.switchT >= 1) { drawPageAt(cr, g.page, r, 0, 1); return; }
    const double e = easeOut(g.switchT), dir = g.page > g.prevPage ? 1 : -1, slide = 64;
    drawPageAt(cr, g.prevPage, r, -dir * slide * e, 1 - e);
    drawPageAt(cr, g.page, r, dir * slide * (1 - e), e);
}

void wlBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kBufferListener = {wlBufferRelease};

void render() {
    if (!g.configured || !g.surface) return;
    const int W = g.width, H = g.height, stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-tools", MFD_CLOEXEC);
    if (fd < 0) return;
    void* data = ftruncate(fd, size) < 0 ? MAP_FAILED : mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    rrect(cr, 0, 0, W, H, kWinRadius);
    src(cr, g.color("background"));
    cairo_fill(cr);
    drawTitle(cr);
    drawTabs(cr);
    drawClose(cr);
    drawContent(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, fd, size);
    wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buf, &kBufferListener, nullptr);
    wl_surface_attach(g.surface, buf, 0, 0);
    wl_surface_damage_buffer(g.surface, 0, 0, W, H);
    wl_surface_commit(g.surface);
    g.dirty = false;
}

void updateCursor() {
    if (!g.cursorMgr || !g.pointer) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.hoverTab >= 0 || g.hoverClose) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    else if (g.pageHot) {
        const Rect r = contentRect();
        const Cursor c = cur().cursor(g.px - r.x, g.py - r.y);
        shape = c == Cursor::Pointer ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : c == Cursor::Text ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT : shape;
    }
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

void switchTo(int index) {
    index = ((index % pageCount()) + pageCount()) % pageCount();
    if (index == g.page) return;
    cur().leave();
    g.prevPage = g.page;
    g.page = index;
    g.switchT = 0;
    g.switchStart = Clock::now();
    g.dirty = true;
}

void motion() {
    const int tab = tabAt(g.px, g.py);
    const bool close = closeRect().contains(g.px, g.py);
    const Rect r = contentRect();
    const bool hot = r.contains(g.px, g.py) && g.switchT >= 1;
    if (tab != g.hoverTab || close != g.hoverClose) g.dirty = true;
    g.hoverTab = tab;
    g.hoverClose = close;
    if (hot) { if (cur().motion(g.px - r.x, g.py - r.y)) g.dirty = true; }
    else if (g.pageHot && cur().leave()) g.dirty = true;
    g.pageHot = hot;
    updateCursor();
}

void click(bool press) {
    const Rect r = contentRect();
    if (press && g.hoverClose) { g.running = false; return; }
    if (press && g.hoverTab >= 0) { switchTo(g.hoverTab); return; }
    if (g.switchT >= 1 && (r.contains(g.px, g.py) || !press) && cur().click(g.px - r.x, g.py - r.y, press)) g.dirty = true;
}

bool windowShortcut(const KeyEvent& k) {
    if (!k.ctrl) return false;
    if (k.sym == XKB_KEY_w || k.sym == XKB_KEY_q) g.running = false;
    else if (k.sym == XKB_KEY_Tab || k.sym == XKB_KEY_ISO_Left_Tab) switchTo(g.page + (k.shift ? -1 : 1));
    else if (k.sym == XKB_KEY_Right) switchTo(g.page + 1);
    else if (k.sym == XKB_KEY_Left) switchTo(g.page - 1);
    else if (k.sym >= XKB_KEY_1 && k.sym < XKB_KEY_1 + static_cast<xkb_keysym_t>(pageCount())) switchTo(static_cast<int>(k.sym - XKB_KEY_1));
    else return false;
    return true;
}
void plainShortcut(const KeyEvent& k) {
    if (k.ctrl || k.alt) return;
    if (k.sym == XKB_KEY_Escape) g.running = false;
    else if (k.sym == XKB_KEY_Right) switchTo(g.page + 1);
    else if (k.sym == XKB_KEY_Left) switchTo(g.page - 1);
    else if (k.sym >= XKB_KEY_1 && k.sym < XKB_KEY_1 + static_cast<xkb_keysym_t>(pageCount())) switchTo(static_cast<int>(k.sym - XKB_KEY_1));
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    auto active = [](const char* mod) { return xkb_state_mod_name_is_active(g.xkbState, mod, XKB_STATE_MODS_EFFECTIVE) > 0; };
    KeyEvent k;
    k.sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    k.ctrl = active(XKB_MOD_NAME_CTRL);
    k.shift = active(XKB_MOD_NAME_SHIFT);
    k.alt = active(XKB_MOD_NAME_ALT);
    k.logo = active(XKB_MOD_NAME_LOGO);
    k.code = key;
    if (g.xkbMap) {
        const xkb_keysym_t* syms = nullptr;
        if (xkb_keymap_key_get_syms_by_level(g.xkbMap, key + 8, xkb_state_key_get_layout(g.xkbState, key + 8), 0, &syms) > 0) {
            char nm[64];
            if (xkb_keysym_get_name(syms[0], nm, sizeof nm) > 0) k.baseName = nm;
        }
    }
    const bool grab = g.switchT >= 1 && cur().grabsKeys();
    if (!grab && windowShortcut(k)) return false;
    char buf[16];
    const int n = k.ctrl ? 0 : xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) >= 32 && buf[0] != 127) k.text.assign(buf, n);
    if (g.switchT >= 1 && cur().key(k)) { g.dirty = true; return true; }
    if (!grab) plainShortcut(k);
    return false;
}

void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};

void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) {
    xdg_surface_ack_configure(s, serial);
    g.configured = true;
    g.dirty = true;
}
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(720, w); g.height = std::max(480, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { g.running = false; }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
    g.enterSerial = serial;
    g.cursorShape = -1;
    g.inside = true;
    g.px = wl_fixed_to_double(x);
    g.py = wl_fixed_to_double(y);
    motion();
}
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) {
    g.inside = false;
    g.hoverTab = -1;
    g.hoverClose = false;
    if (g.pageHot) cur().leave();
    g.pageHot = false;
    g.dirty = true;
}
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    g.px = wl_fixed_to_double(x);
    g.py = wl_fixed_to_double(y);
    motion();
}
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    if (button == 0x110) click(state == WL_POINTER_BUTTON_STATE_PRESSED);
}
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || g.switchT < 1) return;
    const Rect r = contentRect();
    if (r.contains(g.px, g.py) && cur().scroll(g.px - r.x, g.py - r.y, wl_fixed_to_double(value))) g.dirty = true;
}
void onPFrame(void*, wl_pointer*) {}
void onPAxisSource(void*, wl_pointer*, uint32_t) {}
void onPAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
const wl_pointer_listener kPointerListener = {onPEnter, onPLeave, onPMotion, onPButton, onPAxis, onPFrame, onPAxisSource, onPAxisStop, onPAxisDiscrete, nullptr, nullptr, nullptr};

void onKKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!g.xkbCtx) g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (km) {
                if (g.xkbState) xkb_state_unref(g.xkbState);
                if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
                g.xkbMap = km;
                g.xkbState = xkb_state_new(km);
            }
        }
    }
    close(fd);
}
void onKEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void onKLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {
    g.repeatKey = 0;
    for (auto& p : g.pages) p->blur();
    g.dirty = true;
}
void onKKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (key == g.repeatKey) g.repeatKey = 0;
        return;
    }
    if (handleKey(key) && g.kbRate > 0) {
        g.repeatKey = key;
        g.repeatAt = Clock::now() + std::chrono::milliseconds(g.kbDelay);
    } else g.repeatKey = 0;
}
void onKMods(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
    if (g.xkbState) xkb_state_update_mask(g.xkbState, dep, lat, lock, 0, 0, group);
}
void onKRepeat(void*, wl_keyboard*, int32_t rate, int32_t delay) { g.kbRate = rate; g.kbDelay = delay; }
const wl_keyboard_listener kKeyboardListener = {onKKeymap, onKEnter, onKLeave, onKKey, onKMods, onKRepeat};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) {
        g.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) {
        g.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr);
    }
}
void onSeatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {onSeatCaps, onSeatName};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
        g.wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(g.wm, &kWmListener, nullptr);
    } else if (!std::strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        g.cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, 1));
}
void onGlobalRemove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

bool connectWayland() {
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-tools: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g.display);
    wl_display_roundtrip(g.display);
    if (!g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-tools: missing Wayland globals\n"); return false; }
    g.surface = wl_compositor_create_surface(g.compositor);
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Tools");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-tools");
    xdg_toplevel_set_min_size(g.toplevel, 720, 480);
    wl_surface_commit(g.surface);
    return true;
}

int pollTimeoutMs() {
    int timeout = 500;
    if (g.switchT < 1 || cur().animating()) timeout = 16;
    if (g.repeatKey) {
        const long left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        timeout = static_cast<int>(std::min<long>(timeout, std::max<long>(1, left)));
    }
    return timeout;
}

void tickAll() {
    const auto now = Clock::now();
    if (g.switchT < 1) {
        g.switchT = std::min(1.0, std::chrono::duration<double>(now - g.switchStart).count() / 0.26);
        g.dirty = true;
        if (g.switchT >= 1) motion();
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g.colorsStamp) { loadColors(); g.dirty = true; }
    if (cur().tick() || cur().animating()) g.dirty = true;
}

void teardown() {
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
    if (g.xkbCtx) xkb_context_unref(g.xkbCtx);
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    if (g.surface) wl_surface_destroy(g.surface);
    if (g.display) { wl_display_roundtrip(g.display); wl_display_disconnect(g.display); }
    g.pages.clear();  // last: the roundtrip above can still deliver a pointer event, which asks for the current page
}
}

volatile sig_atomic_t g_term = 0;  // (pkill from the toggle: leave the loop, so a shortcut capture in progress can give the keys back)
void onTermSignal(int) { g_term = 1; }

int main() {
    {
        const int lock = open((runtimeDir() + "/end4pC-tools.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0) return 0;
    }
    applyShellFont(fontFamilyFromConfig(configDir() + "/config.json"), fontWeightFromConfig(configDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(configDir() + "/config.json", "fontWeightTitle", 550));
    if (!connectWayland()) return 1;
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);
    loadConfig();
    buildPages();
    if (const char* p = std::getenv("TOOLS_PAGE")) g.page = g.prevPage = std::clamp(std::atoi(p), 0, pageCount() - 1);

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd pfd = {wl_display_get_fd(g.display), POLLIN, 0};
        if (poll(&pfd, 1, pollTimeoutMs()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (pfd.revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (pfd.revents & (POLLERR | POLLHUP)) break;
        tickAll();
        if (g.dirty) render();
    }
    kbGrab(false);
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
