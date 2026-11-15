
std::mutex g_aiMx;
std::string g_aiJson;
std::atomic<uint64_t> g_aiVersion{0};
std::atomic<bool> g_aiWanted{false}, g_aiStarted{false}, g_aiRefresh{false};
std::string g_aiScript;
uint64_t g_aiSeen = 0;

std::string aiRun() {
    std::string out;
    FILE* p = popen(("python3 '" + g_aiScript + "' 2>/dev/null").c_str(), "r");
    if (!p) return out;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

void aiLoop() {
    for (;;) {
        if (g_aiWanted.load()) {
            const std::string out = aiRun();
            if (!out.empty()) {
                {
                    std::lock_guard<std::mutex> lk(g_aiMx);
                    g_aiJson = out;
                }
                g_aiVersion++;
                if (g_weatherWrite >= 0) { const char b = 'a'; (void)!write(g_weatherWrite, &b, 1); }
            }
        }
        for (int i = 0; i < 60; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_aiRefresh.exchange(false)) break;
        }
    }
}

void aiSvcWant(const App& app, bool want) {
    if (want && !g_aiStarted.exchange(true)) {
        g_aiScript = assetsDir() + "/../scripts/ai-usage.py";
        g_aiWanted = true;
        std::thread(aiLoop).detach();
    }
    if (want && !g_aiWanted.exchange(true)) g_aiRefresh = true;
    if (!want) g_aiWanted = false;
    (void)app;
}

bool barModulesRead(App& app) {
    bool changed = false;
    WeatherData w;
    uint64_t v = 0;
    const bool ok = weatherSvcGet(w, &v);
    App::BarWeather bw;
    if (ok) bw = {true, static_cast<int>(std::lround(w.temp)), w.code, w.day};
    if (bw.ok != app.barWeather.ok || bw.temp != app.barWeather.temp || bw.code != app.barWeather.code || bw.day != app.barWeather.day) { app.barWeather = bw; changed = true; }

    std::string j;
    {
        std::lock_guard<std::mutex> lk(g_aiMx);
        j = g_aiJson;
    }
    auto num = [](const std::string& t, const char* key) { std::string s; return wsScalar(t, key, s) ? std::atof(s.c_str()) : 0.0; };
    auto avail = [](const std::string& t) { std::string s; return wsScalar(t, "available", s) && s == "true"; };
    const std::string c = wsObject(j, "claude"), x = wsObject(j, "codex");
    App::BarAi ai[2];
    ai[0] = {avail(c), avail(c) ? num(c, "tokens") / (std::max(1, app.cfg.aiTokenLimit) * 1000.0) : 0.0, static_cast<int>(num(c, "sessions")), static_cast<long long>(num(c, "resetsAt"))};
    ai[1] = {avail(x), avail(x) ? num(x, "percentage") : 0.0, 0, 0};
    for (int k = 0; k < 2; k++)
        if (ai[k].seen != app.barAi[k].seen || std::abs(ai[k].pct - app.barAi[k].pct) > 0.0005 || ai[k].sessions != app.barAi[k].sessions) { app.barAi[k] = ai[k]; changed = true; }
        else app.barAi[k].resetsAt = ai[k].resetsAt;
    if (changed) measureBarModules(app);
    return changed;
}

bool awinRead(App& app);

void barModulesApply(App& app) {
    if (barModulesRead(app) && (inBarLayout(app, "weather") || inBarLayout(app, "aiusage"))) drawAll(app);
}

void barModulesSync(App& app) {
    const bool daily = app.deskClock.open() && app.cfg.clockStyle == "daily";
    weatherSvcWant(app, inBarLayout(app, "weather") || app.weather.open() || daily);
    weatherSvcConfigure(app);
    aiSvcWant(app, inBarLayout(app, "aiusage"));
    if (inBarLayout(app, "activewindow")) awinRead(app);
    barModulesRead(app);
    syncCava(app);
    measureBarModules(app);
}

bool kbdRead(App& app) {
    const std::string out = wmKeyboardsTsv();
    std::vector<std::string> f;
    {
        std::string line = out.substr(0, out.find('\n'));
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
        while (!line.empty() && f.size() < 5) f.push_back("");
    }
    decltype(app.kbd.list) list;
    int active = 0;
    std::string device;
    if (f.size() >= 5 && !f[1].empty()) {
        device = f[0];
        active = std::atoi(f[3].c_str());
        const auto split = [](const std::string& s) {
            std::vector<std::string> v;
            std::stringstream ss(s);
            for (std::string part; std::getline(ss, part, ',');) v.push_back(part);
            return v;
        };
        const auto codes = split(f[1]);
        std::vector<std::string> variants = split(f[2]);
        variants.resize(codes.size());
        app.kbd.variants = variants;
        const auto& names = kbdRuleNames();
        for (size_t i = 0; i < codes.size(); i++) {
            App::KbdLayout l;
            l.code = codes[i];
            const std::string variant = i < variants.size() ? variants[i] : "";
            const auto byVariant = names.find(codes[i] + "/" + variant), byCode = names.find(codes[i]);
            l.name = !variant.empty() && byVariant != names.end() ? byVariant->second : byCode != names.end() ? byCode->second : codes[i];
            list.push_back(l);
        }
        active = std::clamp(active, 0, std::max(0, static_cast<int>(list.size()) - 1));
        if (!list.empty() && !f[4].empty()) list[active].name = f[4];  // Hyprland's own name for the one in use.
    }
    int width = 0, codeW = 0, m3W = 0;
    if (!list.empty()) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cairo_t* cr = mkcr(surf);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string("Sans Bold 10");
        pango_layout_set_font_description(layout, fd);
        std::string code = list[active].code;
        for (char& ch : code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        pango_layout_set_text(layout, code.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        width = 4 + 18 + 4 + tw + 6;
        codeW = tw;
        PangoFontDescription* m3f = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, m3f);
        pango_layout_set_text(layout, list[active].code.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        m3W = tw;
        pango_font_description_free(m3f);
        pango_font_description_free(fd);
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
    bool changed = list.size() != app.kbd.list.size() || active != app.kbd.active || device != app.kbd.device || width != app.kbd.barW || m3W != app.kbd.m3W;
    for (size_t i = 0; !changed && i < list.size(); i++) changed = list[i].code != app.kbd.list[i].code || list[i].name != app.kbd.list[i].name;
    app.kbd.list = std::move(list);
    app.kbd.active = active;
    app.kbd.device = device;
    app.kbd.barW = width;
    app.kbd.codeW = codeW;
    app.kbd.m3W = m3W;
    return changed;
}

void kbdSwitch(App& app, int idx) {
    if (idx < 0 || idx >= static_cast<int>(app.kbd.list.size()) || app.kbd.device.empty()) return;
    wmSwitchKeyboard(app.kbd.device, idx);
    app.kbd.active = idx;
}

struct KbdAvail { std::string code, name; };
const std::vector<KbdAvail>& kbdAvailable() {
    static const std::vector<KbdAvail> all = [] {
        std::vector<KbdAvail> out;
        std::ifstream f("/usr/share/X11/xkb/rules/base.lst");
        std::string line, section;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            if (line[0] == '!') { section = line.substr(line.find_first_not_of("! ")); continue; }
            if (section != "layout") continue;
            std::istringstream in(line);
            KbdAvail a;
            in >> a.code;
            std::getline(in, a.name);
            const size_t b = a.name.find_first_not_of(' ');
            if (a.code.empty() || b == std::string::npos) continue;
            a.name = a.name.substr(b);
            out.push_back(std::move(a));
        }
        return out;
    }();
    return all;
}

constexpr int kKbdMaxLayouts = 4;

std::vector<const KbdAvail*> kbdMatches(const App& app, const std::string& query) {
    const auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string q = lower(query);
    std::vector<const KbdAvail*> first, rest;
    for (const KbdAvail& a : kbdAvailable()) {
        if (std::any_of(app.kbd.list.begin(), app.kbd.list.end(), [&](const App::KbdLayout& l) { return l.code == a.code; })) continue;
        const std::string name = lower(a.name);
        if (q.empty() || a.code == q || name.rfind(q, 0) == 0) first.push_back(&a);
        else if (a.code.find(q) != std::string::npos || name.find(q) != std::string::npos) rest.push_back(&a);
    }
    first.insert(first.end(), rest.begin(), rest.end());
    if (first.size() > 20) first.resize(20);
    return first;
}

void kbdSetLayouts(App& app, const std::vector<std::string>& codes, const std::vector<std::string>& variants) {
    if (codes.empty()) return;
    const auto join = [](const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i]; return s; };
    std::string variantText = join(variants);
    if (variantText.find_first_not_of(',') == std::string::npos) variantText.clear();
    const std::string script = assetsDir() + "/../scripts/hyprconf.py";
    captureArgv({"python3", script, "--set", "input:kb_layout", join(codes), "--set", "input:kb_variant", variantText});
    kbdRead(app);
}

void kbdAdd(App& app, const std::string& code) {
    if (static_cast<int>(app.kbd.list.size()) >= kKbdMaxLayouts) return;
    std::vector<std::string> codes, variants = app.kbd.variants;
    for (const auto& l : app.kbd.list) codes.push_back(l.code);
    codes.push_back(code);
    variants.resize(codes.size());
    kbdSetLayouts(app, codes, variants);
}

void kbdRemove(App& app, int idx) {
    if (app.kbd.list.size() < 2 || idx < 0 || idx >= static_cast<int>(app.kbd.list.size())) return;
    std::vector<std::string> codes, variants = app.kbd.variants;
    for (const auto& l : app.kbd.list) codes.push_back(l.code);
    variants.resize(codes.size());
    codes.erase(codes.begin() + idx);
    variants.erase(variants.begin() + idx);
    kbdSetLayouts(app, codes, variants);
}
