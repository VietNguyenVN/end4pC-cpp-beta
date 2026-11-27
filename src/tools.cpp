#include <random>
#include <utility>
#include <cairo.h>
#include "bubble.h"
#include "freeform.h"
#include "tools_tabs.h"
#include "elastic.h"
#include "i18n.h"
#include "oled.h"
#include "units.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include "shellfont.h"
#include <poll.h>
#include <sys/file.h>
#include <sys/eventfd.h>
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
#include <future>
#include <thread>
#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "hidpi.h"
#include "text-input-unstable-v3-client-protocol.h"

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

struct ImeContext {
    std::string text;
    size_t cursor = 0, anchor = 0;
    Rect caret{};
    const void* field = nullptr;
};

struct ImeEdit {
    std::string commit, preedit;
    uint32_t before = 0, after = 0;
    int32_t preeditCursor = 0;
};

struct ImeReplacement {
    size_t first, last;
    std::string text;
};

ImeReplacement imeReplacement(const std::string& text, size_t cursor, size_t anchor, const ImeEdit& edit) {
    cursor = std::min(cursor, text.size());
    anchor = std::min(anchor, text.size());
    const size_t low = std::min(cursor, anchor), high = std::max(cursor, anchor);
    size_t first = low - std::min<size_t>(low, edit.before);
    size_t last = high + std::min<size_t>(text.size() - high, edit.after);
    while (first < low && (static_cast<unsigned char>(text[first]) & 0xc0) == 0x80) ++first;
    while (last > high && last < text.size() && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
    if (edit.commit.empty()) {
        if (first == low && last == high) return {cursor, cursor, {}};
        return {first, last, text.substr(low, high - low)};
    }
    return {first, last, edit.commit};
}

bool applyImeText(std::string& text, size_t& cursor, const ImeEdit& edit, size_t limit, bool singleLine = true) {
    auto replacement = imeReplacement(text, cursor, cursor, edit);
    if (singleLine)
        for (char& ch : replacement.text) if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    if (replacement.first == replacement.last && replacement.text.empty()) return false;
    if (text.size() - (replacement.last - replacement.first) + replacement.text.size() > limit) return false;
    text.replace(replacement.first, replacement.last - replacement.first, replacement.text);
    cursor = replacement.first + replacement.text.size();
    return true;
}

ImeContext imeField(const std::string& text, size_t cursor, Rect caret) {
    cursor = std::min(cursor, text.size());
    return {text, cursor, cursor, caret, &text};
}

struct ToolPage {
private:
    bool initialized = false;

public:
    void ensureInitialized() {
        if (initialized) return;
        init();
        initialized = true;
    }
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
    virtual bool mouseButton(uint32_t) { return false; }  // a press of any button (Linux code), before the click is handled; true: taken
    virtual bool grabsKeys() const { return false; }  // true: every key, shortcuts of the window included, goes to key()
    virtual void blur() {}
    virtual bool imeContext(ImeContext&) { return false; }
    virtual void imeEdit(const ImeEdit&) {}
    virtual void imeCancel() {}
    virtual bool imeInlinePreedit() const { return false; }
    virtual bool roundClip() const { return true; }  // false: the page may use the corners of its area (the clip is a plain rectangle)                            // the keyboard left the window
    virtual Cursor cursor(double, double) const { return Cursor::Default; }
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    zwp_text_input_manager_v3* imeManager = nullptr;
    zwp_text_input_v3* ime = nullptr;
    bool imeEntered = false, imeNeedsDone = false, imeFromIme = false;  // imeFromIme: the next surrounding-text update is the result of an IME edit
    uint32_t imeSerial = 0, imeActivation = 0;
    ToolPage* imePage = nullptr;
    ImeEdit imePending;
    std::string imePreedit;
    ImeContext imeSent;
    xdg_wm_base* wm = nullptr;
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    wl_surface* surface = nullptr;
    hidpi::SurfaceScale scale;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;
    int sampleEvent = -1;

    bool solo = false;  // `end4pC-tools tasks`: the window of the Task Manager alone (no tabs)
    bool running = true, configured = false, dirty = true;
    int width = kWinW, height = kWinH;

    std::map<std::string, Color> colors;
    time_t colorsStamp = 0;
    std::string iconStyle = "broken";
    std::string hiddenToolsTabs;
    tools_tabs::Order toolsTabOrder = tools_tabs::parseOrder("");
    std::vector<std::unique_ptr<ToolPage>> ownedPages;
    std::vector<ToolPage*> pages;
    unsigned tabRevision = 0;
    int page = 0, prevPage = 0;
    double switchT = 1;
    Clock::time_point switchStart{};
    wl_callback* animationFrame = nullptr;
    using PageImage = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;
    PageImage switchImages[2]{{nullptr, cairo_surface_destroy}, {nullptr, cairo_surface_destroy}};
    double switchWidth = 0, switchHeight = 0, switchScale = 0;

    double px = 0, py = 0;
    bool inside = false;
    int hoverTab = -1;
    bool hoverClose = false, pageHot = false;
    bool tabMenu = false, hoverGear = false, tabMenuError = false, tabMenuClick = false;
    int tabMenuChoice = -1, tabMenuHover = -1;
    double menuT = 0;               // how far the tab menu is open, for its animation: 0 gone, 1 shown
    Clock::time_point menuStamp{};  // when it was last stepped; empty while it rests
    int tabDragFrom = -1;
    PageImage menuImage{nullptr, cairo_surface_destroy};
    PageImage menuBackdrop{nullptr, cairo_surface_destroy};
    double menuScale = 0;
    Rect menuContent{};
    ToolPage* menuPage = nullptr;
    struct MenuState {
        unsigned revision;
        int hover, choice, drag;
        bool error;
        bool operator==(const MenuState&) const = default;
    } menuImageState{};

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
    std::string mode;
    const bool light = jsonValue(readFile(configPath()), "colorMode", mode) && mode == "light";
    if (!jsonValue(readFile(configPath()), "uiBackground", ui)) return;
    if (light && ui == "white") {
        for (const char* n : {"background", "surface", "surface_container_lowest"}) g.colors[n] = Color{1, 1, 1};
        return;
    }
    if (ui != "oled" || light) return;
    for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) g.colors[n] = Color{0, 0, 0};
    for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
        auto it = g.colors.find(n);
        if (it != g.colors.end()) it->second = Color{it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
    }
}

void loadConfig() {
    const std::string t = readFile(configPath());
    g.hiddenToolsTabs.clear();
    jsonValue(t, "hiddenToolsTabs", g.hiddenToolsTabs);
    std::string order;
    jsonValue(t, "toolsTabOrder", order);
    g.toolsTabOrder = tools_tabs::parseOrder(order);
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
// Masks an icon cached at `px` real pixels into a size x size logical box (px > size on a fractionally scaled window, hidpi.h).
void maskIconBox(cairo_t* cr, cairo_surface_t* m, double x, double y, int size, int px) {
    if (px == size) { cairo_mask_surface(cr, m, x, y); return; }
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, double(size) / px, double(size) / px);
    cairo_mask_surface(cr, m, 0, 0);
    cairo_restore(cr);
}
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, bool bold = false) {
    const int px = hidpi::bufferExtent(size, hidpi::scaleOf(cr));
    cairo_surface_t* m = iconMask(name, px, bold);
    if (!m) return;
    src(cr, c);
    maskIconBox(cr, m, x, y, size, px);
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

Rect imeCaretFor(const std::string& value, size_t at, Rect field, double px) {
    double width = 0, height = 0;
    measure(value.substr(0, std::min(at, value.size())), px, false, 0, width, height);
    return {field.x + std::min(width, std::max(0.0, field.w)), field.y + (field.h - px) / 2, 2, px};
}

#include "tools/page_translator.inc"
#include "tools/page_notes.inc"
#include "tools/page_freeform.inc"
#include "tools/page_converter.inc"
#include "tools/page_video.inc"
#include "tools/page_timers.inc"
#include "tools/textbox.inc"
#include "tools/page_units.inc"
#include "tools/keybinds_store.inc"
#include "tools/page_keybinds.inc"
#include "tools/page_phone.inc"
#include "tools/page_score.inc"
// Workers get their own data snapshots and never touch page or Wayland state.
template<class F>
auto sampleInBackground(F read) {
    std::packaged_task<std::invoke_result_t<F>()> task(std::move(read));
    auto result = task.get_future();
    const int event = g.sampleEvent < 0 ? -1 : fcntl(g.sampleEvent, F_DUPFD_CLOEXEC, 0);
    std::thread([task = std::move(task), event]() mutable {
        task();
        if (event >= 0) {
            eventfd_write(event, 1);
            close(event);
        }
    }).detach();
    return result;
}

template<class T>
bool sampleReady(const std::future<T>& sample) {
    return sample.valid() && sample.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void drawSampleLoading(cairo_t* cr, Rect r) {
    const std::string label = i18n::tr("Loading…");
    double w = 0, h = 0;
    measure(label, 16, false, 0, w, h);
    text(cr, label, (r.w - w) / 2, (r.h - h) / 2, 16, false, g.color("outline"));
}

#include "tools/page_tasks.inc"
#include "tools/page_power.inc"
#include "tools/icon_theme.inc"
#include "tools/autostart_store.inc"
#include "tools/page_autostart.inc"

struct EmptyToolsPage : ToolPage {
    const char* name() const override { return ""; }
    const char* icon() const override { return ""; }
    void draw(cairo_t* cr, Rect r) override {
        const std::string message = "Use the gear to show tabs.";
        double width = 0, height = 0;
        measure(message, 16, false, 0, width, height);
        text(cr, message, r.x + (r.w - width) / 2, r.y + (r.h - height) / 2, 16, false, g.color("on_surface_variant"));
    }
};

void addPage(std::unique_ptr<ToolPage> page) {
    g.pages.push_back(page.get());
    g.ownedPages.push_back(std::move(page));
}

void filterToolsPages() {
    static EmptyToolsPage emptyPage;
    g.pages.clear();
    for (int tab : g.toolsTabOrder)
        if (!tools_tabs::isHidden(g.hiddenToolsTabs, tools_tabs::tabs[tab].id))
            g.pages.push_back(g.ownedPages[tab].get());
    if (g.pages.empty()) g.pages.push_back(&emptyPage);
    ++g.tabRevision;
}

int visiblePage(const ToolPage* page) {
    const auto found = std::find(g.pages.begin(), g.pages.end(), page);
    return found == g.pages.end() ? -1 : static_cast<int>(found - g.pages.begin());
}

int buildPages() {
    if (g.solo) {
        addPage(std::make_unique<TasksPage>());
        addPage(std::make_unique<PerfPage>());
        startPowerSample(false);
        return -1;
    }
    using namespace tools_tabs;
    const char* env = std::getenv("TOOLS_PAGE");
    const int requested = env ? std::clamp(std::atoi(env), 0, Count - 1) : -1;
    g.ownedPages.resize(Count);
    g.ownedPages[Translator] = std::make_unique<TranslatorPage>();
    g.ownedPages[Notes] = std::make_unique<NotesPage>();
    g.ownedPages[Freeform] = std::make_unique<FreeformPage>();
    g.ownedPages[Converter] = std::make_unique<ConverterPage>();
    g.ownedPages[Video] = std::make_unique<VideoPage>();
    g.ownedPages[Units] = std::make_unique<UnitsPage>();
    g.ownedPages[Timers] = std::make_unique<TimersPage>(0);
    g.ownedPages[Stopwatch] = std::make_unique<TimersPage>(1);
    g.ownedPages[Reminders] = std::make_unique<TimersPage>(2);
    g.ownedPages[Keybinds] = std::make_unique<KeybindsPage>();
    g.ownedPages[Autostart] = std::make_unique<AutostartPage>();
    g.ownedPages[Phone] = std::make_unique<PhonePage>();
    g.ownedPages[Score] = std::make_unique<ScorePage>();
    filterToolsPages();
    return requested < 0 ? -1 : visiblePage(g.ownedPages[requested].get());
}

ToolPage& cur() { return *g.pages[g.page]; }
int pageCount() { return static_cast<int>(g.pages.size()); }

Rect contentRect() { return {kPad, kBarH, g.width - 2.0 * kPad, g.height - kBarH - kPad}; }
Rect closeRect() { return {g.width - kPad - kCloseSize, (kBarH - kCloseSize) / 2, kCloseSize, kCloseSize}; }
Rect gearRect() {
    Rect r = closeRect();
    r.x -= r.w + 8;
    return r;
}

constexpr double kSegPad = 4, kBtnPadX = 18, kBtnGap = 8, kBtnIcon = 20, kLabelPx = 15;
struct TabLayout {
    std::vector<Rect> rects;
    std::vector<double> labelWidths, labelHeights;
    Rect segment{};
    int width = 0, page = -1;
    unsigned revision = 0;
    double buttonPad = kBtnPadX;
    bool compact = false, activeLabel = true;
};

const TabLayout& tabLayout() {
    static TabLayout layout;
    const size_t count = g.pages.size();
    if (layout.labelWidths.size() != count || layout.revision != g.tabRevision) {
        layout.revision = g.tabRevision;
        layout.labelWidths.resize(count);
        layout.labelHeights.resize(count);
        for (size_t i = 0; i < count; ++i)
            measure(g.pages[i]->name(), kLabelPx, true, 0, layout.labelWidths[i], layout.labelHeights[i]);
        layout.page = -1;
    }
    if (layout.width == g.width && layout.page == g.page) return layout;

    layout.width = g.width;
    layout.page = g.page;
    const double available = g.width - 2 * 158.0;
    const double activeLabelWidth = kBtnGap + layout.labelWidths[g.page];
    layout.buttonPad = kBtnPadX;
    layout.activeLabel = true;
    if (!g.solo) {
        layout.activeLabel = 2 * kSegPad + count * (kBtnIcon + 10) + activeLabelWidth <= available;
        const double labels = layout.activeLabel ? activeLabelWidth : 0;
        layout.buttonPad = std::clamp(((available - 2 * kSegPad - labels) / count - kBtnIcon) / 2, 5.0, kBtnPadX);
    }
    const double tabBase = 2 * layout.buttonPad + kBtnIcon;
    double fullWidth = 2 * kSegPad;
    for (double width : layout.labelWidths) fullWidth += tabBase + kBtnGap + width;
    layout.compact = fullWidth > g.width - 2 * 240.0;
    const double total = layout.compact ? 2 * kSegPad + count * tabBase + (layout.activeLabel ? activeLabelWidth : 0) : fullWidth;
    const double left = (g.width - total) / 2, top = (kBarH - kSegH) / 2;
    layout.segment = {left, top, total, kSegH};
    layout.rects.resize(count);
    double x = left + kSegPad;
    for (size_t i = 0; i < count; ++i) {
        const bool label = !layout.compact || (layout.activeLabel && static_cast<int>(i) == g.page);
        const double w = tabBase + (label ? kBtnGap + layout.labelWidths[i] : 0);
        layout.rects[i] = {x, top + kSegPad, w, kSegH - 2 * kSegPad};
        x += w;
    }
    return layout;
}

int tabAt(double x, double y) {
    if (!*cur().name()) return -1;
    const auto& t = tabLayout().rects;
    for (size_t i = 0; i < t.size(); i++) if (t[i].contains(x, y)) return static_cast<int>(i);
    return -1;
}

void drawTitle(cairo_t* cr) {
    const char* heading = g.solo ? "Task Manager" : "Tools";
    const double sy = (kBarH - kShapeSize) / 2, sx = kPad + 4;
    shapePath(cr, "Cookie9Sided", sx, sy, kShapeSize);
    src(cr, g.color("primary_container"));
    cairo_fill(cr);
    icon(cr, g.solo ? "cpu" : "ruler-pen", sx + (kShapeSize - 30) / 2, sy + (kShapeSize - 30) / 2, 30, g.color("on_primary_container"), true);
    double tw = 0, th = 0;
    measure(heading, 26, true, 0, tw, th);
    text(cr, heading, sx + kShapeSize + 14, (kBarH - th) / 2, 26, true, g.color("on_surface"));
}

void drawTabs(cairo_t* cr) {
    if (!*cur().name()) return;
    const auto& layout = tabLayout();
    const Rect s = layout.segment;
    rrect(cr, s.x, s.y, s.w, s.h, s.h / 2);
    src(cr, g.color("surface_container_high"));
    cairo_fill(cr);
    const auto& t = layout.rects;
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
        icon(cr, g.pages[i]->icon(), t[i].x + layout.buttonPad, t[i].y + (t[i].h - kBtnIcon) / 2, static_cast<int>(kBtnIcon), c, on);
        if (!layout.compact || (layout.activeLabel && on))
            text(cr, name, t[i].x + layout.buttonPad + kBtnIcon + kBtnGap, t[i].y + (t[i].h - layout.labelHeights[i]) / 2, kLabelPx, true, c);
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

constexpr double kTabMenuHeader = 40, kTabMenuRow = 28, kTabMenuPad = 8, kTabMenuFooter = 20, kTabGripWidth = 26;

Rect tabMenuRect() {
    constexpr double width = 286;
    const Rect gear = gearRect();
    return {gear.x + gear.w - width, gear.y + gear.h + 6, width,
            kTabMenuHeader + tools_tabs::tabs.size() * kTabMenuRow + kTabMenuFooter + kTabMenuPad};
}

Rect tabMenuRow(int index) {
    const Rect r = tabMenuRect();
    return {r.x + kTabMenuPad, r.y + kTabMenuHeader + index * kTabMenuRow, r.w - 2 * kTabMenuPad, kTabMenuRow};
}

int tabMenuAt(double x, double y) {
    for (int i = 0; i < tools_tabs::Count; ++i)
        if (tabMenuRow(i).contains(x, y)) return i;
    return -1;
}

bool overTabGrip(int index, double x) {
    return index >= 0 && x < tabMenuRow(index).x + kTabGripWidth;
}

void drawTabGear(cairo_t* cr) {
    const Rect gear = gearRect();
    rrect(cr, gear.x, gear.y, gear.w, gear.h, gear.h / 2);
    src(cr, g.color(g.tabMenu ? "secondary_container" : g.hoverGear ? "surface_container_highest" : "surface_container_high"));
    cairo_fill(cr);
    icon(cr, "settings", gear.x + (gear.w - 22) / 2, gear.y + (gear.h - 22) / 2, 22,
         g.color(g.tabMenu ? "on_secondary_container" : "on_surface_variant"));
}

// The menu grows out of the gear: it scales up from the gear's corner with a little overshoot while it fades in, and shrinks and fades when it closes.
double easeOutBack(double t) {
    constexpr double c1 = 1.6, c3 = c1 + 1;
    const double u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
void stepMenuAnimation(Clock::time_point now) {
    if (g.menuStamp == Clock::time_point{}) return;
    const double dt = std::chrono::duration<double>(now - g.menuStamp).count();
    g.menuStamp = now;
    const double target = g.tabMenu ? 1 : 0, speed = g.tabMenu ? 1 / 0.26 : 1 / 0.15;
    g.menuT = target > g.menuT ? std::min(target, g.menuT + dt * speed) : std::max(target, g.menuT - dt * speed);
    g.dirty = true;
    if (g.menuT == target) {
        g.menuStamp = Clock::time_point{};
        g.menuBackdrop.reset();
    }
}

void drawTabMenu(cairo_t* cr) {
    const Rect r = tabMenuRect();
    const auto foreground = g.color("on_surface");
    rrect(cr, r.x, r.y, r.w, r.h, 20);
    src(cr, g.color("surface_container_high"));
    cairo_fill_preserve(cr);
    src(cr, g.color("outline_variant"));
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    text(cr, g.tabMenuError ? "Could not save tab preferences" : "Visible tabs",
         r.x + 18, r.y + 12, 15, true, g.tabMenuError ? g.color("error") : foreground);
    for (int i = 0; i < tools_tabs::Count; ++i) {
        const Rect row = tabMenuRow(i);
        const int tabIndex = g.toolsTabOrder[i];
        const auto& tab = tools_tabs::tabs[tabIndex];
        const bool shown = !tools_tabs::isHidden(g.hiddenToolsTabs, tab.id);
        if (i == g.tabDragFrom || i == g.tabMenuChoice || (g.tabDragFrom < 0 && i == g.tabMenuHover)) {
            rrect(cr, row.x, row.y, row.w, row.h, 10);
            src(cr, g.color(i == g.tabDragFrom ? "secondary_container" : "surface_container_highest"));
            cairo_fill(cr);
        }
        src(cr, g.color("on_surface_variant"));
        for (int column = 0; column < 2; ++column)
            for (int dot = 0; dot < 3; ++dot) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, row.x + 10 + column * 4, row.y + 10 + dot * 4, 1, 0, 2 * M_PI);
            }
        cairo_fill(cr);
        icon(cr, g.ownedPages[tabIndex]->icon(), row.x + 31, row.y + 5, 18, g.color("on_surface_variant"));
        text(cr, tab.label, row.x + 57, row.y + 5, 13, false, foreground);
        const Rect track{row.x + row.w - 44, row.y + (row.h - 18) / 2, 34, 18};
        rrect(cr, track.x, track.y, track.w, track.h, track.h / 2);
        src(cr, g.color(shown ? "primary" : "surface_container_highest"));
        cairo_fill(cr);
        if (!shown) {
            rrect(cr, track.x + 0.5, track.y + 0.5, track.w - 1, track.h - 1, track.h / 2 - 0.5);
            src(cr, g.color("outline"));
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
        const double thumbX = shown ? track.x + track.w - 9 : track.x + 9;
        cairo_new_sub_path(cr);
        cairo_arc(cr, thumbX, track.y + track.h / 2, shown ? 6 : 5, 0, 2 * M_PI);
        src(cr, g.color(shown ? "on_primary" : "outline"));
        cairo_fill(cr);
    }
    if (g.tabDragFrom >= 0 && g.tabMenuHover >= 0 && g.tabMenuHover != g.tabDragFrom) {
        const Rect target = tabMenuRow(g.tabMenuHover);
        const double y = g.tabMenuHover < g.tabDragFrom ? target.y : target.y + target.h;
        rrect(cr, target.x + 5, y - 1, target.w - 10, 2, 1);
        src(cr, g.color("primary"));
        cairo_fill(cr);
    }
    text(cr, "Move with Alt+↑ or Alt+↓", r.x + 18, r.y + r.h - kTabMenuFooter,
         11, false, g.color("on_surface_variant"));
}

void clearMenuImages() {
    g.menuImage.reset();
    g.menuBackdrop.reset();
    g.menuPage = nullptr;
}

void drawAnimatedTabMenu(cairo_t* cr) {
    const Rect menu = tabMenuRect();
    const double deviceScale = g.scale.factor();
    const App::MenuState state{g.tabRevision, g.tabMenuHover, g.tabMenuChoice, g.tabDragFrom, g.tabMenuError};
    const bool fresh = !g.menuImage;
    if (fresh) {
        g.menuImage.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            hidpi::bufferExtent(menu.w, deviceScale), hidpi::bufferExtent(menu.h, deviceScale)));
        if (cairo_surface_status(g.menuImage.get()) != CAIRO_STATUS_SUCCESS) g.menuImage.reset();
        else cairo_surface_set_device_scale(g.menuImage.get(), deviceScale, deviceScale);
    }
    if (g.menuImage && (fresh || state != g.menuImageState)) {
        cairo_t* snapshot = mkcr(g.menuImage.get());
        cairo_set_operator(snapshot, CAIRO_OPERATOR_CLEAR);
        cairo_paint(snapshot);
        cairo_set_operator(snapshot, CAIRO_OPERATOR_OVER);
        cairo_translate(snapshot, -menu.x, -menu.y);
        drawTabMenu(snapshot);
        cairo_destroy(snapshot);
        g.menuImageState = state;
    }

    const double ax = menu.x + menu.w - 24, ay = menu.y;  // anchored under the gear
    const double t = std::clamp(g.menuT, 0.0, 1.0);
    const double scale = 0.84 + 0.16 * (g.tabMenu ? easeOutBack(t) : t);
    const double alpha = std::clamp(t * 2.4, 0.0, 1.0);
    cairo_save(cr);
    cairo_translate(cr, ax, ay);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -ax, -ay);
    if (g.menuImage) {
        cairo_set_source_surface(cr, g.menuImage.get(), menu.x, menu.y);
        cairo_paint_with_alpha(cr, alpha);
    } else {
        cairo_rectangle(cr, menu.x, menu.y, menu.w, menu.h);
        cairo_clip(cr);
        cairo_push_group(cr);
        drawTabMenu(cr);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
    }
    cairo_restore(cr);
}

void clearSwitchImages() {
    for (auto& image : g.switchImages) image.reset();
}

void drawPageAt(cairo_t* cr, int index, Rect r, double dx, double alpha, cairo_surface_t* image = nullptr) {
    cairo_save(cr);
    if (g.pages[index]->roundClip()) rrect(cr, r.x, r.y, r.w, r.h, 28);
    else cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_clip(cr);
    cairo_translate(cr, r.x + dx, r.y);
    if (image) {
        cairo_set_source_surface(cr, image, 0, 0);
        cairo_paint_with_alpha(cr, alpha);
    } else {
        if (alpha < 1) cairo_push_group(cr);
        g.pages[index]->draw(cr, {0, 0, r.w, r.h});
        if (alpha < 1) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, alpha);
        }
    }
    cairo_restore(cr);
}

void drawContent(cairo_t* cr) {
    const Rect r = contentRect();
    if (g.switchT >= 1) { drawPageAt(cr, g.page, r, 0, 1); return; }
    const double scale = g.scale.factor();
    if (r.w != g.switchWidth || r.h != g.switchHeight || scale != g.switchScale) {
        clearSwitchImages();
        g.switchWidth = r.w;
        g.switchHeight = r.h;
        g.switchScale = scale;
    }
    const int pages[] = {g.prevPage, g.page};
    for (int i = 0; i < 2; ++i) {
        auto& image = g.switchImages[i];
        if (image) continue;
        image.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hidpi::bufferExtent(r.w, scale), hidpi::bufferExtent(r.h, scale)));
        if (cairo_surface_status(image.get()) != CAIRO_STATUS_SUCCESS) { image.reset(); continue; }
        cairo_surface_set_device_scale(image.get(), scale, scale);
        cairo_t* snapshot = mkcr(image.get());
        g.pages[pages[i]]->draw(snapshot, {0, 0, r.w, r.h});
        cairo_destroy(snapshot);
    }
    const double e = easeOut(g.switchT), dir = g.page > g.prevPage ? 1 : -1, slide = 64;
    drawPageAt(cr, g.prevPage, r, -dir * slide * e, 1 - e, g.switchImages[0].get());
    drawPageAt(cr, g.page, r, dir * slide * (1 - e), e, g.switchImages[1].get());
}

// The modal menu repaints independently of the page behind it.
void drawMenuBackdrop(cairo_t* cr) {
    const Rect r = contentRect();
    const double scale = g.scale.factor();
    if (g.switchT < 1) { drawContent(cr); return; }
    if (!g.menuBackdrop || g.menuPage != g.pages[g.page] || g.menuContent.w != r.w || g.menuContent.h != r.h) {
        g.menuBackdrop.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            hidpi::bufferExtent(r.w, scale), hidpi::bufferExtent(r.h, scale)));
        if (cairo_surface_status(g.menuBackdrop.get()) != CAIRO_STATUS_SUCCESS) {
            g.menuBackdrop.reset();
            drawContent(cr);
            return;
        }
        cairo_surface_set_device_scale(g.menuBackdrop.get(), scale, scale);
        cairo_t* snapshot = mkcr(g.menuBackdrop.get());
        drawPageAt(snapshot, g.page, {0, 0, r.w, r.h}, 0, 1);
        cairo_destroy(snapshot);
        g.menuContent = r;
        g.menuPage = g.pages[g.page];
    }
    drawPageAt(cr, g.page, r, 0, 1, g.menuBackdrop.get());
}

void onAnimationFrame(void*, wl_callback* callback, uint32_t) {
    wl_callback_destroy(callback);
    g.animationFrame = nullptr;
}
const wl_callback_listener kAnimationFrameListener = {onAnimationFrame};

struct ToolsBuffer {
    hidpi::Frame frame;
    wl_buffer* buffer = nullptr;
    bool busy = false;

    void clear() {
        if (buffer) wl_buffer_destroy(buffer);
        hidpi::abandon(frame);
        buffer = nullptr;
        busy = false;
    }
};
ToolsBuffer toolsBuffers[3];

void onToolsBufferRelease(void* data, wl_buffer*) { static_cast<ToolsBuffer*>(data)->busy = false; }
const wl_buffer_listener kBufferListener = {onToolsBufferRelease};

ToolsBuffer* acquireToolsBuffer(int width, int height, double scale) {
    ToolsBuffer* spare = nullptr;
    for (auto& slot : toolsBuffers) {
        if (slot.busy) continue;
        if (slot.buffer && slot.frame.lw == width && slot.frame.lh == height && slot.frame.scale == scale) return &slot;
        if (!spare) spare = &slot;
    }
    if (!spare) return nullptr;
    spare->clear();
    auto& frame = spare->frame;
    if (!hidpi::begin(frame, width, height, scale, "end4pC-tools")) return nullptr;
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, frame.fd, static_cast<int32_t>(frame.size));
    if (pool) {
        spare->buffer = wl_shm_pool_create_buffer(pool, 0, frame.pw, frame.ph, frame.stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }
    close(frame.fd);
    frame.fd = -1;
    if (!spare->buffer) { spare->clear(); return nullptr; }
    wl_buffer_add_listener(spare->buffer, &kBufferListener, spare);
    return spare;
}

void render() {
    if (!g.configured || !g.surface || g.animationFrame) return;
    const int W = g.width, H = g.height;
    ToolsBuffer* slot = acquireToolsBuffer(W, H, g.scale.factor());
    if (!slot) return;
    cairo_t* cr = mkcr(slot->frame.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    rrect(cr, 0, 0, W, H, kWinRadius);
    src(cr, g.color("background"));
    cairo_fill(cr);
    drawTitle(cr);
    drawTabs(cr);
    if (!g.solo) drawTabGear(cr);
    drawClose(cr);
    const bool menuVisible = g.tabMenu || g.menuT > 0;
    if (menuVisible) {
        if (g.menuScale != g.scale.factor()) {
            clearMenuImages();
            g.menuScale = g.scale.factor();
        }
        drawMenuBackdrop(cr);
    } else {
        clearMenuImages();
        drawContent(cr);
    }
    if (g.switchT >= 1 && !g.imePreedit.empty() && g.imePage == &cur() && !cur().imeInlinePreedit()) {
        ImeContext context;
        if (cur().imeContext(context) && context.field == g.imeSent.field) {
            const Rect r = contentRect(), caret = context.caret;
            double tw = 0, th = 0;
            measure(g.imePreedit, 15, false, 0, tw, th);
            const double w = std::min(tw + 16, r.w), h = th + 8;
            const double x = std::clamp(r.x + caret.x, r.x, r.x + r.w - w);
            const double y = std::clamp(r.y + caret.y + caret.h, r.y, std::max(r.y, r.y + r.h - h));
            rrect(cr, x, y, w, h, 6);
            src(cr, g.color("surface_container_highest"));
            cairo_fill(cr);
            text(cr, g.imePreedit, x + 8, y + 4, 15, false, g.color("on_surface"), w - 16);
        }
    }
    if (menuVisible) drawAnimatedTabMenu(cr);
    cairo_destroy(cr);
    cairo_surface_flush(slot->frame.surface);
    if (g.switchT < 1 || menuVisible) {
        g.animationFrame = wl_surface_frame(g.surface);
        if (g.animationFrame) wl_callback_add_listener(g.animationFrame, &kAnimationFrameListener, nullptr);
    }
    slot->busy = true;
    hidpi::present(g.surface, slot->buffer, slot->frame, g.scale);
    // Building a page's first snapshot can take longer than an animation frame.
    if (g.switchT < 1 && g.switchStart == Clock::time_point{}) g.switchStart = Clock::now();
    g.dirty = false;
}

void updateCursor() {
    if (!g.cursorMgr || !g.pointer) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.tabDragFrom >= 0) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    else if (overTabGrip(g.tabMenuHover, g.px)) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    else if (g.hoverTab >= 0 || g.hoverClose || g.hoverGear || g.tabMenuHover >= 0) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    else if (g.pageHot) {
        const Rect r = contentRect();
        const Cursor c = cur().cursor(g.px - r.x, g.py - r.y);
        shape = c == Cursor::Pointer ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : c == Cursor::Text ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT : shape;
    }
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

// The page the window was left on is remembered by its name, so it survives new pages being added.
std::string lastPagePath() { return stateDir() + "/tools-page.txt"; }
void rememberPage() {
    if (g.solo) return;
    mkdir(stateDir().c_str(), 0755);
    std::ofstream f(lastPagePath(), std::ios::trunc);
    f << g.pages[g.page]->name() << "\n";
}
int rememberedPage() {
    if (g.solo) return 0;
    std::ifstream f(lastPagePath());
    std::string name;
    std::getline(f, name);
    for (int i = 0; i < pageCount(); i++) if (name == g.pages[i]->name()) return i;
    return 0;
}

#include "tools/ime.inc"

void switchTo(int index) {
    index = ((index % pageCount()) + pageCount()) % pageCount();
    if (index == g.page) return;
    disableIme();
    cur().leave();
    g.prevPage = g.page;
    g.page = index;
    cur().ensureInitialized();
    rememberPage();
    clearSwitchImages();
    g.switchT = 0;
    g.switchStart = {};
    g.dirty = true;
}

void refreshToolsPages() {
    ToolPage* previous = g.pages[g.page];
    filterToolsPages();
    const int current = visiblePage(previous);
    g.page = g.prevPage = std::max(0, current);
    if (current < 0) {
        previous->leave();
        previous->blur();
    }
    cur().ensureInitialized();
    if (*cur().name()) rememberPage();
    clearSwitchImages();
    g.switchT = 1;
    g.switchStart = {};
    g.dirty = true;
}

void toggleToolTab(int index) {
    std::string hidden;
    for (int i = 0; i < tools_tabs::Count; ++i) {
        const auto& tab = tools_tabs::tabs[i];
        bool hide = tools_tabs::isHidden(g.hiddenToolsTabs, tab.id);
        if (i == index) hide = !hide;
        tools_tabs::appendHidden(hidden, tab.id, hide);
    }
    const std::string reply = sendToolsCommand("set hiddenToolsTabs " + (hidden.empty() ? "-" : hidden));
    g.tabMenuError = reply != "ok\n";
    if (!g.tabMenuError) {
        g.hiddenToolsTabs = std::move(hidden);
        refreshToolsPages();
    }
    g.dirty = true;
}

void reorderToolTab(int from, int to) {
    if (from < 0 || to < 0 || to >= tools_tabs::Count || from == to) return;
    auto order = g.toolsTabOrder;
    tools_tabs::moveTab(order, from, to);
    g.tabMenuError = sendToolsCommand("set toolsTabOrder " + tools_tabs::formatOrder(order)) != "ok\n";
    if (!g.tabMenuError) {
        g.toolsTabOrder = order;
        g.tabMenuChoice = to;
        refreshToolsPages();
    }
    g.dirty = true;
}

void motion() {
    const int tab = g.tabMenu ? -1 : tabAt(g.px, g.py);
    const bool close = closeRect().contains(g.px, g.py);
    const bool gear = !g.solo && gearRect().contains(g.px, g.py);
    const int menuRow = g.tabMenu ? tabMenuAt(g.px, g.py) : -1;
    const Rect r = contentRect();
    const bool hot = !g.tabMenu && r.contains(g.px, g.py) && g.switchT >= 1;
    if (tab != g.hoverTab || close != g.hoverClose || gear != g.hoverGear || menuRow != g.tabMenuHover) g.dirty = true;
    g.hoverTab = tab;
    g.hoverClose = close;
    g.hoverGear = gear;
    g.tabMenuHover = menuRow;
    if (hot) { if (cur().motion(g.px - r.x, g.py - r.y)) g.dirty = true; }
    else if (g.pageHot && cur().leave()) g.dirty = true;
    g.pageHot = hot;
    updateCursor();
}

void showTabMenu(bool open) {
    if (open) disableIme();
    if (open != g.tabMenu) g.menuStamp = Clock::now();  // (the animation runs from wherever it is: a quick second click turns it round)
    g.tabMenu = open;
    g.tabMenuChoice = -1;
    g.tabDragFrom = -1;
    g.tabMenuError = false;
    g.dirty = true;
    motion();
}

void click(bool press) {
    const Rect r = contentRect();
    if (!press && g.tabMenuClick) {
        g.tabMenuClick = false;
        if (g.tabDragFrom >= 0) {
            const int from = std::exchange(g.tabDragFrom, -1);
            g.menuBackdrop.reset();
            reorderToolTab(from, tabMenuAt(g.px, g.py));
            g.dirty = true;
            motion();
        }
        return;
    }
    if (press && g.hoverClose) { g.running = false; return; }
    if (press && (g.hoverGear || g.tabMenu)) {
        g.tabMenuClick = true;
        if (g.hoverGear) showTabMenu(!g.tabMenu);
        else if (g.tabMenuHover >= 0) {
            g.tabMenuChoice = g.tabMenuHover;
            if (overTabGrip(g.tabMenuHover, g.px)) {
                g.tabDragFrom = g.tabMenuHover;
                g.dirty = true;
                updateCursor();
            } else toggleToolTab(g.toolsTabOrder[g.tabMenuHover]);
        } else if (!tabMenuRect().contains(g.px, g.py)) showTabMenu(false);
        return;
    }
    if (press && g.hoverTab >= 0) { switchTo(g.hoverTab); return; }
    if (g.switchT >= 1 && (r.contains(g.px, g.py) || !press) && cur().click(g.px - r.x, g.py - r.y, press)) g.dirty = true;
}

bool windowShortcut(const KeyEvent& k) {
    if (!k.ctrl) return false;
    if (k.sym == XKB_KEY_w || k.sym == XKB_KEY_q) g.running = false;
    else if (!g.solo && k.sym == XKB_KEY_comma) showTabMenu(true);
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
    if (g.tabMenu) {
        if (k.ctrl && (k.sym == XKB_KEY_w || k.sym == XKB_KEY_q)) { g.running = false; return false; }  // (the window shortcuts that close it still work with the menu open)
        if (k.sym == XKB_KEY_Escape && g.tabDragFrom >= 0) {
            g.tabDragFrom = -1;
            g.dirty = true;
            updateCursor();
        } else if (k.sym == XKB_KEY_Escape || (k.ctrl && k.sym == XKB_KEY_comma)) showTabMenu(false);
        else if (g.tabDragFrom >= 0) return false;
        else if (k.sym == XKB_KEY_Up || k.sym == XKB_KEY_Down) {
            const int step = k.sym == XKB_KEY_Up ? -1 : 1;
            const int count = tools_tabs::Count;
            if (k.alt) reorderToolTab(g.tabMenuChoice, g.tabMenuChoice + step);
            else {
                const int from = g.tabMenuChoice < 0 ? (step < 0 ? 0 : count - 1) : g.tabMenuChoice;
                g.tabMenuChoice = (from + step + count) % count;
                g.dirty = true;
            }
        } else if ((k.sym == XKB_KEY_Return || k.sym == XKB_KEY_space) && g.tabMenuChoice >= 0) {
            toggleToolTab(g.toolsTabOrder[g.tabMenuChoice]);
        }
        return false;
    }
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
    g.hoverGear = false;
    g.tabMenuHover = -1;
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
    if (!g.tabMenu && !g.hoverGear && state == WL_POINTER_BUTTON_STATE_PRESSED && g.switchT >= 1 && cur().grabsKeys() && cur().mouseButton(button)) { g.dirty = true; return; }
    if (button == 0x110) click(state == WL_POINTER_BUTTON_STATE_PRESSED);
}
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    if (g.tabMenu || axis != WL_POINTER_AXIS_VERTICAL_SCROLL || g.switchT < 1) return;
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
    else if (!std::strcmp(iface, wp_viewporter_interface.name) && !hidpi::disabled())  // (sharp drawing at fractional scales, hidpi.h)
        g.viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
    else if (!std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) && !hidpi::disabled())
        g.fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, zwp_text_input_manager_v3_interface.name)) {
        g.imeManager = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(reg, name, &zwp_text_input_manager_v3_interface, 1));
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
    if (wl_display_roundtrip(g.display) < 0) return false;
    if (!g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-tools: missing Wayland globals\n"); return false; }
    if (g.imeManager && g.seat) {
        g.ime = zwp_text_input_manager_v3_get_text_input(g.imeManager, g.seat);
        zwp_text_input_v3_add_listener(g.ime, &kImeListener, nullptr);
    }
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, g.solo ? "Task Manager" : "Tools");
    xdg_toplevel_set_app_id(g.toplevel, g.solo ? "end4pC-tasks" : "end4pC-tools");
    xdg_toplevel_set_min_size(g.toplevel, 720, 480);
    wl_surface_commit(g.surface);
    return true;
}

int pollTimeoutMs() {
    int timeout = 500;
    if (!g.animationFrame && (g.switchT < 1 || g.menuStamp != Clock::time_point{} ||
            (g.tabDragFrom < 0 && cur().animating()))) timeout = 16;
    if (g.repeatKey) {
        const long left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        timeout = static_cast<int>(std::min<long>(timeout, std::max<long>(1, left)));
    }
    return timeout;
}

void tickAll() {
    const auto now = Clock::now();
    if (!g.animationFrame) stepMenuAnimation(now);
    if (g.switchT < 1 && !g.animationFrame && g.switchStart != Clock::time_point{}) {
        g.switchT = std::min(1.0, std::chrono::duration<double>(now - g.switchStart).count() / 0.26);
        g.dirty = true;
        if (g.switchT >= 1) { clearSwitchImages(); motion(); }
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g.colorsStamp) { loadColors(); clearSwitchImages(); clearMenuImages(); g.dirty = true; }
    if (g.solo && finishPowerSample()) {
        if (powerDiscoveryPending) {
            powerDiscoveryPending = false;
            if (PowerPage::available(true)) addPage(std::make_unique<PowerPage>(true));
            if (PowerPage::available(false)) addPage(std::make_unique<PowerPage>(false));
        }
        g.dirty = true;
    }
    if (g.switchT >= 1) {
        const bool pageChanged = cur().tick();
        if (g.menuStamp == Clock::time_point{} && g.tabDragFrom < 0 &&
            (pageChanged || cur().animating())) {
            g.menuBackdrop.reset();
            g.dirty = true;
        }
    }
}

void teardown() {
    if (g.animationFrame) wl_callback_destroy(g.animationFrame);
    clearSwitchImages();
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
    if (g.xkbCtx) xkb_context_unref(g.xkbCtx);
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    disableIme();
    if (g.ime) zwp_text_input_v3_destroy(g.ime);
    if (g.imeManager) zwp_text_input_manager_v3_destroy(g.imeManager);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& slot : toolsBuffers) slot.clear();
    if (g.display) { wl_display_roundtrip(g.display); wl_display_disconnect(g.display); }
    if (g.sampleEvent >= 0) close(g.sampleEvent);
    g.pages.clear();
    g.ownedPages.clear();  // last: the roundtrip above can still deliver a pointer event, which asks for the current page
}
}

volatile sig_atomic_t g_term = 0;  // (pkill from the toggle: leave the loop, so a shortcut capture in progress can give the keys back)
void onTermSignal(int) { g_term = 1; }

int main(int argc, char** argv) {
    g.solo = argc > 1 && std::string(argv[1]) == "tasks";
    if (g.solo) prctl(PR_SET_NAME, "end4pC-tasks");  // (its own process name: the toggle of Tools does not close it)
    {
        const int lock = open((runtimeDir() + (g.solo ? "/end4pC-tasks.lock" : "/end4pC-tools.lock")).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0) return 0;
    }
    applyShellFont(fontFamilyFromConfig(configDir() + "/config.json"), fontWeightFromConfig(configDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(configDir() + "/config.json", "fontWeightTitle", 550));
    if (!connectWayland()) return 1;
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);
    loadConfig();
    if (g.solo) g.sampleEvent = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    const int requested = buildPages();
    g.page = g.prevPage = requested < 0 ? rememberedPage() : requested;

    cur().ensureInitialized();

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {g.sampleEvent, POLLIN, 0}};
        if (poll(fds, 2, pollTimeoutMs()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) { eventfd_t count; eventfd_read(g.sampleEvent, &count); }
        tickAll();
        if (g.dirty) {
            render();
            syncIme();
        }
    }
    kbGrab(false);
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
