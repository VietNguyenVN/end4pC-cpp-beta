#include <cairo.h>
#include "i18n.h"
#include "oled.h"
#include "shapes.h"
#include "shellfont.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ext-session-lock-v1-client-protocol.h"
#include "hidpi.h"

namespace {
struct Color { double r = 0, g = 0, b = 0; };

std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : ""; }
std::string stateDir() {
    const char* d = std::getenv("XDG_STATE_HOME");
    return (d ? std::string(d) : homeDir() + "/.local/state") + "/end4pC";
}
std::string cacheDir() {
    const char* d = std::getenv("XDG_CACHE_HOME");
    return (d ? std::string(d) : homeDir() + "/.cache") + "/end4pC";
}
std::string projectDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        std::string p(buf, n);
        p = p.substr(0, p.rfind('/'));
        return p.substr(0, p.rfind('/'));
    }
    return homeDir() + "/.config/end4pC";
}
std::string assetsDir() { return projectDir() + "/assets"; }
std::string configFile() {
    const char* d = std::getenv("XDG_DATA_HOME");
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const std::string data = d && *d ? std::string(d) : homeDir() + "/.local/share";
    const std::string home = x && *x ? std::string(x) : homeDir() + "/.config";
    const std::string now = data + "/end4pC/config.json";
    if (access(now.c_str(), F_OK) == 0) return now;
    for (const std::string& old : {home + "/end4pC-user/config.json", home + "/end4pC/config.json"})
        if (access(old.c_str(), F_OK) == 0) return old;
    return now;
}
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool jsonValue(const std::string& text, const std::string& key, std::string& out) {
    const size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k);
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
    while (e < text.size() && text[e] != ',' && text[e] != '}' && text[e] != '\n') e++;
    out = text.substr(i, e - i);
    return !out.empty();
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
void src(cairo_t* cr, const Color& c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

std::map<std::string, Color> g_colors;
void loadColors() {
    std::ifstream f(stateDir() + "/generated/colors.json");
    std::string line;
    while (std::getline(f, line)) {
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        const size_t h = line.find('#', q2 == std::string::npos ? 0 : q2);
        if (q2 == std::string::npos || h == std::string::npos || line.size() < h + 7) continue;
        const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(h + 1, 6).c_str(), nullptr, 16));
        g_colors[line.substr(q1 + 1, q2 - q1 - 1)] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
    }
    std::ifstream cf(configFile());
    std::stringstream cs;
    cs << cf.rdbuf();
    const std::string ct = cs.str();
    const size_t k = ct.find("\"uiBackground\"");
    const size_t cm = ct.find("\"colorMode\"");
    const bool light = cm != std::string::npos && ct.compare(ct.find('"', ct.find(':', cm)), 7, "\"light\"") == 0;
    if (k != std::string::npos && light && ct.compare(ct.find('"', ct.find(':', k)), 7, "\"white\"") == 0)
        for (const char* n : {"background", "surface", "surface_container_lowest"}) g_colors[n] = {1, 1, 1};
    if (k != std::string::npos && !light && ct.compare(ct.find('"', ct.find(':', k)), 6, "\"oled\"") == 0) {
        for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) g_colors[n] = {0, 0, 0};
        for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
            auto it = g_colors.find(n);
            if (it != g_colors.end()) it->second = {it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
        }
    }
}
Color color(const std::string& n) {
    auto it = g_colors.find(n);
    if (it != g_colors.end()) return it->second;
    return n.rfind("on_", 0) == 0 ? Color{0.9, 0.9, 0.9} : Color{0.12, 0.12, 0.12};
}

struct TextSize { int w = 0, h = 0; };
TextSize text(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double alpha = 1, bool draw = true) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* d = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, s.c_str(), -1);
    TextSize ts;
    pango_layout_get_pixel_size(l, &ts.w, &ts.h);
    if (draw) {
        src(cr, c, alpha);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, l);
    }
    g_object_unref(l);
    return ts;
}

cairo_surface_t* iconMask(const std::string& name, int size) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = name + "/" + std::to_string(size);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = nullptr;
    for (const char* style : {"broken", "outline", "bold"}) {
        const std::string path = assetsDir() + "/icons/" + style + "/" + name + ".svg";
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
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c) {
    const int px = hidpi::bufferExtent(size, hidpi::scaleOf(cr));
    if (cairo_surface_t* m = iconMask(name, px)) {
        src(cr, c);
        maskIconBox(cr, m, x, y, size, px);
    }
}

struct Profile {
    std::string name, user;
    cairo_surface_t* avatar = nullptr;
} g_profile;
constexpr int kAvatar = 96;

cairo_surface_t* loadCover(const std::string& path, int w, int h) {
    int iw = 0, ih = 0;
    if (!gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(w) / iw, double(h) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(w, int(iw * scale + 0.5)), std::max(h, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < ph; y++) {
        const guchar* sp = px + y * rs;
        uint32_t* dp = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < pw; x++, sp += nch) {
            const uint32_t a = nch == 4 ? sp[3] : 255;
            dp[x] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(img);
    g_object_unref(pb);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, img, (w - pw) / 2.0, (h - ph) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    return out;
}

void loadProfile() {
    const std::string ii = readFile(homeDir() + "/.config/illogical-impulse/config.json");
    std::string pic, own, display;
    jsonValue(readFile(configFile()), "avatarPicture", own);
    jsonValue(ii, "avatarPicture", pic);
    jsonValue(ii, "displayName", display);
    const passwd* pw = getpwuid(getuid());
    g_profile.user = pw ? pw->pw_name : "";
    g_profile.name = display.empty() ? g_profile.user : display;
    for (const std::string& c : {own, pic, homeDir() + "/.face", homeDir() + "/.face.icon"})
        if (!c.empty() && access(c.c_str(), R_OK) == 0) { g_profile.avatar = loadCover(c, kAvatar, kAvatar); break; }
}

struct Weather {
    bool ok = false;
    double temp = 0;
    int code = 0;
    bool day = true;
    bool fahrenheit = false;
    std::string city;
};

std::mutex g_weatherMx;
Weather g_weather;
int g_wake[2] = {-1, -1};

std::string curl(const std::string& url) {
    const std::string cmd = "curl -s --max-time 8 '" + url + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    pclose(f);
    return out;
}

bool weatherUnitF() {
    std::string u;
    jsonValue(readFile(configFile()), "weatherUnit", u);
    return u == "fahrenheit";
}

std::string weatherCachePath() { return cacheDir() + "/weather.json"; }

void loadWeatherCache() {
    const std::string t = readFile(weatherCachePath());
    std::string v;
    Weather w;
    if (!jsonValue(t, "temp", v)) return;
    const bool wantF = weatherUnitF();
    std::string cu;
    if (!jsonValue(t, "unit", cu) || (cu == "fahrenheit") != wantF) return;
    w.temp = std::atof(v.c_str());
    if (jsonValue(t, "code", v)) w.code = std::atoi(v.c_str());
    if (jsonValue(t, "day", v)) w.day = std::atoi(v.c_str()) != 0;
    jsonValue(t, "city", w.city);
    w.ok = true;
    w.fahrenheit = wantF;
    std::lock_guard<std::mutex> lk(g_weatherMx);
    g_weather = w;
}

void fetchWeather() {
    std::string lat, lon, city;
    std::string cityCfg;
    jsonValue(readFile(configFile()), "weatherCity", cityCfg);
    if (!cityCfg.empty()) {
        std::string q;
        for (char c : cityCfg) q += c == ' ' ? std::string("%20") : std::string(1, c);
        const std::string j = curl("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + q);
        if (!jsonValue(j, "latitude", lat) || !jsonValue(j, "longitude", lon)) return;
        jsonValue(j, "name", city);
    } else {
        const std::string j = curl("https://ipwho.is/");
        if (j.find("\"success\": true") == std::string::npos && j.find("\"success\":true") == std::string::npos) return;
        if (!jsonValue(j, "latitude", lat) || !jsonValue(j, "longitude", lon)) return;
        jsonValue(j, "city", city);
    }
    const std::string j = curl("https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current=temperature_2m,weather_code,is_day&timezone=auto" + std::string(weatherUnitF() ? "&temperature_unit=fahrenheit" : ""));
    const size_t cur = j.find("\"current\":{");
    if (cur == std::string::npos) return;
    const std::string c = j.substr(cur);
    std::string t, code, day;
    if (!jsonValue(c, "temperature_2m", t) || !jsonValue(c, "weather_code", code) || !jsonValue(c, "is_day", day)) return;
    Weather w;
    w.ok = true;
    w.temp = std::atof(t.c_str());
    w.code = std::atoi(code.c_str());
    w.day = std::atoi(day.c_str()) != 0;
    w.city = city;
    w.fahrenheit = weatherUnitF();
    mkdir(cacheDir().c_str(), 0755);
    if (FILE* f = std::fopen(weatherCachePath().c_str(), "w")) {
        std::fprintf(f, "{\"city\": \"%s\", \"unit\": \"%s\", \"temp\": %.1f, \"code\": %d, \"day\": %d}\n", city.c_str(), w.fahrenheit ? "fahrenheit" : "celsius", w.temp, w.code, w.day ? 1 : 0);
        std::fclose(f);
    }
    {
        std::lock_guard<std::mutex> lk(g_weatherMx);
        g_weather = w;
    }
    if (g_wake[1] >= 0) { const char b = 'w'; (void)!write(g_wake[1], &b, 1); }
}

int weatherIntervalMin() {
    std::string v;
    int m = 10;
    if (jsonValue(readFile(configFile()), "weatherInterval", v)) m = std::atoi(v.c_str());
    return std::clamp(m, 5, 120);
}

void weatherLoop() {
    for (;;) {
        struct stat st {};
        const bool fresh = stat(weatherCachePath().c_str(), &st) == 0 && time(nullptr) - st.st_mtime < weatherIntervalMin() * 60;
        if (!fresh) fetchWeather();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void describe(const Weather& w, std::string& label, std::string& ic) {
    const int c = w.code;
    if (c == 0) { label = "Clear sky"; ic = w.day ? "sun-2" : "moon"; }
    else if (c == 1 || c == 2) { label = c == 1 ? "Mostly clear" : "Partly cloudy"; ic = w.day ? "cloud-sun" : "cloudy-moon"; }
    else if (c == 3) { label = "Overcast"; ic = "cloud"; }
    else if (c == 45 || c == 48) { label = "Fog"; ic = "fog"; }
    else if (c >= 51 && c <= 57) { label = "Drizzle"; ic = "cloud-rain"; }
    else if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) { label = "Rain"; ic = "cloud-rain"; }
    else if ((c >= 71 && c <= 77) || c == 85 || c == 86) { label = "Snow"; ic = "cloud-snowfall"; }
    else if (c >= 95) { label = "Thunderstorm"; ic = "cloud-bolt"; }
    else { label = "Cloudy"; ic = "cloud"; }
}

int pamConv(int n, const pam_message** msg, pam_response** resp, void* data) {
    auto* pw = static_cast<const std::string*>(data);
    auto* r = static_cast<pam_response*>(calloc(n, sizeof(pam_response)));
    if (!r) return PAM_BUF_ERR;
    for (int i = 0; i < n; i++)
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) r[i].resp = strdup(pw->c_str());
    *resp = r;
    return PAM_SUCCESS;
}

void unlockKeyring(const std::string& password) {
    std::string cfg;
    jsonValue(readFile(configFile()), "unlockKeyring", cfg);
    if (cfg == "0") return;
    std::string state;
    if (FILE* f = popen("busctl --user get-property org.freedesktop.secrets /org/freedesktop/secrets/collection/login org.freedesktop.Secret.Collection Locked 2>/dev/null", "r")) {
        char buf[64] = {};
        const size_t n = fread(buf, 1, sizeof buf - 1, f);
        pclose(f);
        state.assign(buf, n);
    }
    if (state.rfind("b false", 0) == 0) return;
    if (FILE* p = popen("killall -q -u \"$(id -un)\" gnome-keyring-daemon; exec gnome-keyring-daemon --daemonize --login >/dev/null 2>&1", "w")) {
        (void)!fwrite(password.data(), 1, password.size(), p);
        pclose(p);
    }
}

bool pamCheck(const std::string& user, std::string password) {
    const char* service = access("/etc/pam.d/end4pC-lock", R_OK) == 0 ? "end4pC-lock" : access("/etc/pam.d/hyprlock", R_OK) == 0 ? "hyprlock" : "login";
    pam_conv conv = {pamConv, &password};
    pam_handle_t* h = nullptr;
    bool ok = false;
    if (pam_start(service, user.c_str(), &conv, &h) == PAM_SUCCESS) {
        ok = pam_authenticate(h, 0) == PAM_SUCCESS;
        pam_end(h, PAM_SUCCESS);
        if (ok) unlockKeyring(password);
    }
    explicit_bzero(password.data(), password.size());
    return ok;
}

enum class LockAnim { None, SplitH, SplitV, Shape };

struct Output {
    wl_output* output = nullptr;
    uint32_t name = 0;
    wl_surface* surface = nullptr;
    hidpi::SurfaceScale scale;
    ext_session_lock_surface_v1* lockSurface = nullptr;
    int w = 0, h = 0;
    bool configured = false;
    cairo_surface_t* cache = nullptr;  // the lock screen painted once, while it animates
    int cacheW = 0, cacheH = 0;
    bool cacheDirty = true, framePending = false;
    wl_callback* frameCb = nullptr;
    std::chrono::steady_clock::time_point frameAt;
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    ext_session_lock_manager_v1* manager = nullptr;
    ext_session_lock_v1* lock = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;
    xkb_context* xkbCtx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkbState = nullptr;
    bool locked = false, finished = false, done = false;
    std::string password;
    enum { Idle, Checking, Wrong } status = Idle;
    std::atomic<int> authResult{-1};
    std::chrono::steady_clock::time_point lockedAt;
    LockAnim animMode = LockAnim::None;
    enum { AnimIdle, AnimEntering, AnimExiting } animPhase = AnimIdle;
    bool animStarted = false, unlocking = false;
    std::chrono::steady_clock::time_point animStart;
    double animSeconds = 0.55, shapeStart = 0;
    std::string shapeName = "Cookie7Sided", baseRole;  // baseRole: the color outside the shape when the wallpaper is centered
} g;

std::map<std::string, cairo_surface_t*> g_bgCache;

cairo_surface_t* background(int w, int h) {
    const std::string key = std::to_string(w) + "x" + std::to_string(h);
    if (auto it = g_bgCache.find(key); it != g_bgCache.end()) return it->second;
    std::string path = readFile(stateDir() + "/wallpaper.txt");
    while (!path.empty() && (path.back() == '\n' || path.back() == ' ')) path.pop_back();
    cairo_surface_t* small = path.empty() ? nullptr : loadCover(path, w, h);
    g_bgCache[key] = small;
    return small;
}

void onBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kBufferListener = {onBufferRelease};

// The wallpaper alone, as the desktop shows it.
void paintWallpaper(cairo_t* cr, int W, int H) {
    cairo_set_source_rgb(cr, 0.06, 0.06, 0.07);
    cairo_paint(cr);
    if (cairo_surface_t* bg = background(W, H)) {
        cairo_save(cr);
        cairo_scale(cr, double(W) / cairo_image_surface_get_width(bg), double(H) / cairo_image_surface_get_height(bg));
        cairo_set_source_surface(cr, bg, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// The lock screen as it is when it has come in: the wallpaper with its veil, and the clock, the weather, the avatar and the password box.
void paintFrame(cairo_t* cr, int W, int H) {
    paintWallpaper(cr, W, H);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.38);
    cairo_paint(cr);

    const Color white{0.97, 0.97, 0.97};
    const double M = 72;
    double y = H - M;

    {
        const double pw = 320, ph = 52;
        y -= ph;
        rrect(cr, M, y, pw, ph, ph / 2);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        icon(cr, g.status == App::Wrong ? "danger-circle" : "lock-keyhole-minimalistic", M + 16, y + (ph - 22) / 2, 22, g.status == App::Wrong ? Color{1, 0.6, 0.6} : white);
        const double tx = M + 52;
        if (g.status == App::Checking) text(cr, "Checking…", tx, y + (ph - 20) / 2 - 2, 18, false, white, 0.8);
        else if (g.status == App::Wrong && g.password.empty()) text(cr, "Wrong password", tx, y + (ph - 20) / 2 - 2, 18, false, Color{1, 0.6, 0.6});
        else if (g.password.empty()) text(cr, "Password", tx, y + (ph - 20) / 2 - 2, 18, false, white, 0.6);
        else {
            size_t chars = 0;
            for (unsigned char c : g.password) if ((c & 0xC0) != 0x80) chars++;
            const size_t fit = static_cast<size_t>((pw - 52 - 20) / 16);
            const size_t shown = std::min(chars, fit);
            for (size_t i = 0; i < shown; i++) {
                cairo_arc(cr, tx + 6 + i * 16, y + ph / 2.0, 4.5, 0, 2 * M_PI);
                src(cr, white);
                cairo_fill(cr);
            }
        }
    }

    Weather w;
    {
        std::lock_guard<std::mutex> lk(g_weatherMx);
        w = g_weather;
    }
    y -= 26;
    if (w.ok) {
        std::string label, ic;
        describe(w, label, ic);
        y -= 32;
        icon(cr, ic, M, y + 2, 28, white);
        char t[32];
        std::snprintf(t, sizeof t, "%.0f°%c", w.temp, w.fahrenheit ? 'F' : 'C');
        const TextSize ts = text(cr, t, M + 40, y, 24, true, white);
        text(cr, label + (w.city.empty() ? "" : "  ·  " + w.city), M + 40 + ts.w + 14, y + 4, 18, false, white, 0.75);
        y -= 8;
    }

    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char buf[64];
    i18n::strftime(buf, sizeof buf, "%A, %e %B", &lt);
    {
        std::string d = buf;
        const size_t dbl = d.find("  ");
        if (dbl != std::string::npos) d.erase(dbl, 1);
        y -= 28;
        text(cr, d, M + 4, y, 22, false, white, 0.85);
        y -= 4;
    }
    std::strftime(buf, sizeof buf, "%H:%M", &lt);
    {
        const TextSize ts = text(cr, buf, 0, 0, 132, true, white, 1, false);
        y -= ts.h - 26;
        text(cr, buf, M - 4, y, 132, true, white);
    }

    {
        y -= kAvatar - 10;
        const double cx = M + kAvatar / 2.0, cy = y + kAvatar / 2.0;
        cairo_arc(cr, cx, cy, kAvatar / 2.0 + 4, 0, 2 * M_PI);
        src(cr, color("primary"));
        cairo_fill(cr);
        cairo_save(cr);
        cairo_arc(cr, cx, cy, kAvatar / 2.0, 0, 2 * M_PI);
        cairo_clip(cr);
        if (g_profile.avatar) {
            cairo_set_source_surface(cr, g_profile.avatar, M, y);
            cairo_paint(cr);
        } else {
            src(cr, color("primary_container"));
            cairo_paint(cr);
            icon(cr, "user-circle", cx - 28, cy - 28, 56, color("on_primary_container"));
        }
        cairo_restore(cr);
        std::string up;
        {
            double secs = 0;
            std::ifstream f("/proc/uptime");
            f >> secs;
            const int total = static_cast<int>(secs) / 60, d = total / 1440, h = (total % 1440) / 60, m = total % 60;
            if (d > 0) up += std::to_string(d) + "d";
            if (h > 0) up += (up.empty() ? "" : ", ") + std::to_string(h) + "h";
            if (m > 0 || up.empty()) up += (up.empty() ? "" : ", ") + std::to_string(m) + "m";
            up = "Up • " + up;
        }
        const TextSize nameS = text(cr, g_profile.name, 0, 0, 26, true, white, 1, false);
        const TextSize upS = text(cr, up, 0, 0, 16, false, white, 1, false);
        const double top = cy - (nameS.h + upS.h - 2) / 2.0, tx = M + kAvatar + 22;
        text(cr, g_profile.name, tx, top, 26, true, white);
        text(cr, up, tx + 1, top + nameS.h - 2, 16, false, white, 0.75);
    }

}

// ---- how the lock screen comes in and goes out (cfg lockAnimation) ----------------------------------------------------------------
double easeOutCubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

double entryOpen(double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (g.animMode == LockAnim::Shape) return std::min(1.0, t * t * (3 - 2 * t) * 1.15);
    return easeOutCubic(t);
}

double exitOpen(double t) { return easeOutCubic(1 - t); }

double animOpen() {
    if (g.animPhase == App::AnimIdle) return 1;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g.animStart).count() / g.animSeconds;
    return g.animPhase == App::AnimEntering ? entryOpen(t) : exitOpen(t);
}

constexpr double kSeamAlpha = 0;

// The frame (already painted in cache) as it shows when the animation is open of 0..1.
void compose(cairo_t* cr, int W, int H, cairo_surface_t* cache, double open) {
    const Color base = g.baseRole.empty() ? Color{0, 0, 0} : color(g.baseRole);
    src(cr, base);
    cairo_paint(cr);
    if (g.animMode == LockAnim::SplitH || g.animMode == LockAnim::SplitV) {
        paintWallpaper(cr, W, H);  // (what is not covered yet is the wallpaper, as the desktop shows it, and not black: the halves come over it with their veil)
        const bool horizontal = g.animMode == LockAnim::SplitH;  // (the halves are the top and the bottom one)
        const double len = horizontal ? H : W, off = (1 - open) * len / 2.0;
        auto half = [&](int k) {  // 0: top or left, 1: bottom or right
            const double d = k == 0 ? -off : off, o0 = k == 0 ? 0 : len / 2.0;
            const double x = horizontal ? 0 : o0 + d, y = horizontal ? o0 + d : 0;
            cairo_set_source_surface(cr, cache, horizontal ? 0 : d, horizontal ? d : 0);
            cairo_rectangle(cr, x, y, horizontal ? W : len / 2.0, horizontal ? len / 2.0 : H);
            cairo_fill(cr);
        };
        half(1);
        half(0);
        if (kSeamAlpha > 0 && off > 0.5) {
            const double a = kSeamAlpha * std::min(1.0, off / 60.0);
            for (int k = 0; k < 2; k++) {
                const double e = len / 2.0 + (k == 0 ? -off : off) - 1.5;
                cairo_set_source_rgba(cr, 1, 1, 1, a);
                if (horizontal) cairo_rectangle(cr, 0, e, W, 3); else cairo_rectangle(cr, e, 0, 3, H);
                cairo_fill(cr);
            }
        }
        return;
    }
    if (g.animMode == LockAnim::Shape) {
        const double full = std::hypot(double(W), double(H)) * 1.4, size = g.shapeStart + (full - g.shapeStart) * std::clamp(open, 0.0, 1.0);
        if (size < 2) return;
        cairo_save(cr);
        cairo_translate(cr, W / 2.0, H / 2.0);
        cairo_rotate(cr, (1 - open) * 0.6);
        shapes::path(cr, shapes::table(assetsDir()), g.shapeName, -size / 2, -size / 2, size);
        cairo_restore(cr);  // (the path keeps the rotation it was built with)
        cairo_clip(cr);
        cairo_set_source_surface(cr, cache, 0, 0);
        cairo_paint(cr);
    }
}

void frameDone(void* d, wl_callback* cb, uint32_t) {
    Output* o = static_cast<Output*>(d);
    o->framePending = false;
    o->frameCb = nullptr;
    wl_callback_destroy(cb);
}
const wl_callback_listener kFrameListener = {frameDone};

void draw(Output& o) {
    if (!o.configured || o.w <= 0 || o.h <= 0) return;
    const bool animating = g.animPhase != App::AnimIdle;
    if (animating && o.framePending) return;  // (the last frame has not been shown yet)
    const int W = o.w, H = o.h;
    const double sc = o.scale.factor();
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, sc, "end4pC-lock")) return;
    cairo_t* cr = mkcr(hf.surface);

    if (animating) {
        if (!o.cache || o.cacheW != W || o.cacheH != H || o.cacheDirty || cairo_image_surface_get_width(o.cache) != hf.pw) {
            if (o.cache) cairo_surface_destroy(o.cache);
            o.cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, hf.pw, hf.ph);  // (at the buffer's pixels, painting in logical units)
            cairo_surface_set_device_scale(o.cache, sc, sc);
            cairo_t* cc = mkcr(o.cache);
            paintFrame(cc, W, H);
            cairo_destroy(cc);
            o.cacheW = W;
            o.cacheH = H;
            o.cacheDirty = false;
        }
        compose(cr, W, H, o.cache, animOpen());
        o.framePending = true;
        o.frameAt = std::chrono::steady_clock::now();
        o.frameCb = wl_surface_frame(o.surface);
        wl_callback_add_listener(o.frameCb, &kFrameListener, &o);
    } else {
        paintFrame(cr, W, H);
    }

    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, g.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    hidpi::present(o.surface, buffer, hf, o.scale);
}
void drawAll() { for (auto& o : g.outputs) draw(*o); }

void onLockSurfaceConfigure(void* d, ext_session_lock_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
    auto* o = static_cast<Output*>(d);
    ext_session_lock_surface_v1_ack_configure(ls, serial);
    o->w = static_cast<int>(w);
    o->h = static_cast<int>(h);
    o->configured = true;
    o->cacheDirty = true;
    if (!g.animStarted) {  // the first screen that is ready starts the animation of coming in
        g.animStarted = true;
        if (g.animMode != LockAnim::None) { g.animPhase = App::AnimEntering; g.animStart = std::chrono::steady_clock::now(); }
    }
    draw(*o);
}
const ext_session_lock_surface_v1_listener kLockSurfaceListener = {onLockSurfaceConfigure};

void createLockSurface(Output& o) {
    if (!g.lock || o.lockSurface) return;
    o.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(o.scale, o.surface, g.viewporter, g.fscaleMgr, [op = &o] { op->cacheDirty = true; draw(*op); });
    o.lockSurface = ext_session_lock_v1_get_lock_surface(g.lock, o.surface, o.output);
    ext_session_lock_surface_v1_add_listener(o.lockSurface, &kLockSurfaceListener, &o);
}

void onLocked(void*, ext_session_lock_v1*) {
    g.locked = true;
    g.lockedAt = std::chrono::steady_clock::now();
}
void onFinished(void*, ext_session_lock_v1*) { g.finished = true; }
const ext_session_lock_v1_listener kLockListener = {onLocked, onFinished};

void onKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!km) return;
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.keymap) xkb_keymap_unref(g.keymap);
    g.keymap = km;
    g.xkbState = xkb_state_new(km);
}
void onKbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void onKbLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
void onKbModifiers(void*, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (g.xkbState) xkb_state_update_mask(g.xkbState, depressed, latched, locked, 0, 0, group);
}
void onKbRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

void submit() {
    if (g.password.empty() || g.status == App::Checking) return;
    g.status = App::Checking;
    g.authResult = -1;
    std::string pw = g.password;
    const std::string user = g_profile.user;
    g.password.clear();
    std::thread([pw, user]() mutable {
        const bool ok = pamCheck(user, pw);
        explicit_bzero(pw.data(), pw.size());
        g.authResult = ok ? 1 : 0;
        const char b = 'a';
        (void)!write(g_wake[1], &b, 1);
    }).detach();
    drawAll();
}

void onKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !g.xkbState || g.status == App::Checking || g.unlocking) return;
    const uint32_t code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, code);
    const bool ctrl = xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (g.status == App::Wrong) g.status = App::Idle;
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) { submit(); return; }
    if (sym == XKB_KEY_Escape || (ctrl && (sym == XKB_KEY_u || sym == XKB_KEY_U))) {
        explicit_bzero(g.password.data(), g.password.size());
        g.password.clear();
    } else if (sym == XKB_KEY_BackSpace) {
        while (!g.password.empty() && (static_cast<unsigned char>(g.password.back()) & 0xC0) == 0x80) g.password.pop_back();
        if (!g.password.empty()) g.password.pop_back();
    } else if (!ctrl) {
        char buf[16];
        const int n = xkb_state_key_get_utf8(g.xkbState, code, buf, sizeof buf);
        if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return;
        g.password.append(buf, n);
    } else {
        return;
    }
    drawAll();
}
const wl_keyboard_listener kKeyboardListener = {onKeymap, onKbEnter, onKbLeave, onKey, onKbModifiers, onKbRepeatInfo};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
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
    } else if (!std::strcmp(iface, ext_session_lock_manager_v1_interface.name)) {
        g.manager = static_cast<ext_session_lock_manager_v1*>(wl_registry_bind(reg, name, &ext_session_lock_manager_v1_interface, 1));
    } else if (!std::strcmp(iface, wl_output_interface.name)) {
        auto o = std::make_unique<Output>();
        o->name = name;
        o->output = static_cast<wl_output*>(wl_registry_bind(reg, name, &wl_output_interface, std::min(version, 3u)));
        Output* raw = o.get();
        g.outputs.push_back(std::move(o));
        createLockSurface(*raw);
    }
}
void onGlobalRemove(void*, wl_registry*, uint32_t name) {
    for (auto it = g.outputs.begin(); it != g.outputs.end(); ++it)
        if ((*it)->name == name) {
            Output& o = **it;
            if (o.frameCb) wl_callback_destroy(o.frameCb);
            if (o.cache) cairo_surface_destroy(o.cache);
            if (o.lockSurface) ext_session_lock_surface_v1_destroy(o.lockSurface);
            hidpi::detach(o.scale);
            if (o.surface) wl_surface_destroy(o.surface);
            if (o.output) wl_output_destroy(o.output);
            g.outputs.erase(it);
            return;
        }
}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

void unlockAndExit() {
    ext_session_lock_v1_unlock_and_destroy(g.lock);
    g.lock = nullptr;
    wl_display_roundtrip(g.display);
    g.done = true;
}

// The password was right: the screen goes out with its animation (the session stays locked until it ends), then it unlocks.
void startUnlock() {
    bool shown = false;
    for (const auto& o : g.outputs) shown = shown || o->configured;
    if (g.animMode == LockAnim::None || !shown) { unlockAndExit(); return; }
    g.unlocking = true;
    g.animPhase = App::AnimExiting;
    g.animStart = std::chrono::steady_clock::now();
    for (auto& o : g.outputs) { o->cacheDirty = true; o->framePending = false; }
    drawAll();
}

// Moves the animation on: false when there is none.
void animTick(bool& redraw) {
    if (g.animPhase == App::AnimIdle) return;
    const auto now = std::chrono::steady_clock::now();
    for (auto& o : g.outputs)
        if (o->framePending && now - o->frameAt > std::chrono::milliseconds(120)) o->framePending = false;  // (no frame callback came: do not stall)
    if (std::chrono::duration<double>(now - g.animStart).count() < g.animSeconds) { redraw = true; return; }
    if (g.animPhase == App::AnimExiting) { unlockAndExit(); return; }
    g.animPhase = App::AnimIdle;
    for (auto& o : g.outputs) {
        if (o->cache) { cairo_surface_destroy(o->cache); o->cache = nullptr; }
        o->framePending = false;
    }
    redraw = true;
}
}

// The animation of coming in and going out, from the config (off when the shell's animations are off).
void readAnimConfig() {
    const std::string cfg = readFile(configFile());
    std::string v;
    g.animMode = LockAnim::None;
    if (jsonValue(cfg, "lockAnimation", v))
        g.animMode = v == "split_h" ? LockAnim::SplitH : v == "split_v" ? LockAnim::SplitV : v == "shape" ? LockAnim::Shape : LockAnim::None;
    if (jsonValue(cfg, "animEnabled", v) && v == "0") g.animMode = LockAnim::None;
    double speed = 100;
    if (jsonValue(cfg, "animSpeed", v) && !v.empty()) speed = std::clamp(std::atof(v.c_str()), 50.0, 200.0);
    g.animSeconds = (g.animMode == LockAnim::Shape ? 0.75 : 0.55) * 100.0 / speed;
    if (jsonValue(cfg, "centeredWallpaperShape", v) && !v.empty()) g.shapeName = v;
    if (jsonValue(cfg, "centeredWallpaper", v) && v == "1") {  // the shape starts as the one the desktop shows, and outside it is the desktop's color
        g.baseRole = "primary_container";
        if (jsonValue(cfg, "centeredWallpaperColor", v) && !v.empty()) g.baseRole = v;
        g.shapeStart = 400;
        if (jsonValue(cfg, "centeredWallpaperSize", v) && !v.empty()) g.shapeStart = std::clamp(std::atof(v.c_str()), 400.0, 800.0);
    }
}

// `end4pC-lock --dump-frames DIR`: writes pictures of every animation at some points of it (no lock is taken), to look at them.
void dumpFrames(const std::string& dir) {
    constexpr int W = 1280, H = 720;
    cairo_surface_t* cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t* cc = mkcr(cache);
    paintFrame(cc, W, H);
    cairo_destroy(cc);
    struct Case { const char* name; LockAnim mode; bool centered; };
    for (const Case& c : {Case{"split-h", LockAnim::SplitH, false}, Case{"split-v", LockAnim::SplitV, false}, Case{"shape", LockAnim::Shape, false}, Case{"shape-centered", LockAnim::Shape, true}}) {
        g.animMode = c.mode;
        g.baseRole = c.centered ? "primary_container" : "";
        g.shapeStart = c.centered ? 400 : 0;
        for (int i = 0; i <= 6; i++) {
            cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
            cairo_t* cr = mkcr(s);
            compose(cr, W, H, cache, entryOpen(i / 6.0));
            cairo_destroy(cr);
            cairo_surface_write_to_png(s, (dir + "/" + c.name + "-" + std::to_string(i) + ".png").c_str());
            cairo_surface_destroy(s);
        }
    }
    cairo_surface_destroy(cache);
}

int main(int argc, char** argv) {
    if (pipe(g_wake) != 0) return 1;
    loadColors();
    applyShellFont(fontFamilyFromConfig(configFile()), fontWeightFromConfig(configFile(), "fontWeightText", 450), fontWeightFromConfig(configFile(), "fontWeightTitle", 550));
    {
        std::string lang;
        jsonValue(readFile(configFile()), "language", lang);
        const std::string cd = configFile().substr(0, configFile().rfind('/'));
        i18n::load(lang, assetsDir(), cd + "/i18n");
    }
    loadProfile();
    loadWeatherCache();
    readAnimConfig();
    if (argc >= 3 && std::strcmp(argv[1], "--dump-frames") == 0) { dumpFrames(argv[2]); return 0; }

    g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-lock: no Wayland display\n"); return 1; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g.display);
    wl_display_roundtrip(g.display);
    if (!g.manager || !g.compositor || !g.shm) { std::fprintf(stderr, "end4pC-lock: the compositor lacks ext-session-lock-v1\n"); return 1; }

    g.lock = ext_session_lock_manager_v1_lock(g.manager);
    ext_session_lock_v1_add_listener(g.lock, &kLockListener, nullptr);
    for (auto& o : g.outputs) createLockSurface(*o);
    wl_display_roundtrip(g.display);
    if (g.finished) { std::fprintf(stderr, "end4pC-lock: another lock screen is already running\n"); return 1; }

    std::thread(weatherLoop).detach();

    int lastMinute = -1;
    while (!g.done && !g.finished) {
        wl_display_flush(g.display);
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        pollfd fds[2] = {{wl_display_get_fd(g.display), POLLIN, 0}, {g_wake[0], POLLIN, 0}};
        if (poll(fds, 2, g.animPhase != App::AnimIdle ? 30 : 1000) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);

        bool redraw = false;
        if (fds[1].revents & POLLIN) {
            char b[16];
            const ssize_t n = read(g_wake[0], b, sizeof b);
            for (ssize_t i = 0; i < n; i++) {
                if (b[i] == 'w') redraw = true;
                if (b[i] == 'a') {
                    if (g.authResult == 1) { startUnlock(); break; }
                    g.status = App::Wrong;
                    redraw = true;
                }
            }
        }
        if (g.done) break;
        animTick(redraw);
        if (g.done) break;
        const time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        if (lt.tm_min != lastMinute) { lastMinute = lt.tm_min; redraw = true; }
        if (redraw) drawAll();
#ifdef LOCK_DEBUG
        if (g.locked && std::chrono::steady_clock::now() - g.lockedAt > std::chrono::seconds(LOCK_DEBUG)) unlockAndExit();
#endif
    }
    if (g.finished && g.lock) std::fprintf(stderr, "end4pC-lock: the compositor ended the lock\n");
    std::fflush(nullptr);
    _exit(g.done ? 0 : 1);
}
