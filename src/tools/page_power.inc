// Task Manager > Battery and Devices. Battery: the battery of the computer (and a UPS), every number upower knows. Devices: the batteries of the
// mouse, keyboard, headset, gamepad, phone... Each tab exists only when there is something to show. The data is `upower -d` (one call for all the
// devices) plus the sysfs of the battery for what upower does not say (temperature, charge limit), and the profile of power-profiles-daemon.
// Without upower the sysfs of the batteries is read instead. Included by src/tools.cpp (inside its anonymous namespace), after page_tasks.inc.

struct PwDev {
    std::string path, native, model, vendor, serial, kind, state, warning, level, tech, chargeLimit;
    bool powerSupply = false, hasHistory = false, pctIgnored = false, online = false;
    double pct = -1, energy = -1, full = -1, design = -1, rate = -1, volt = -1, capacity = -1, tte = -1, ttf = -1, tempC = -1;
    int cycles = -1;
    TkSeries series;
    bool prefilled = false;
    Clock::time_point lastPush{};
};

struct PwData {
    std::vector<PwDev> devs;
    bool acOnline = false, haveUpower = true;
    std::string profile;
    Clock::time_point last{}, lastProfile{};
};
PwData& pwData() { static PwData d; return d; }

std::string pwPopen(const std::string& cmd) {
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        pclose(f);
    }
    return out;
}
double pwFirstNumber(const std::string& v) {
    const char* c = v.c_str();
    while (*c && !(std::isdigit(static_cast<unsigned char>(*c)) || *c == '-')) c++;
    return *c ? std::atof(c) : -1;
}
double pwSeconds(const std::string& v) {  // "4.5 hours", "30 minutes", "45 seconds", "2 days"
    const double n = pwFirstNumber(v);
    if (n < 0) return -1;
    if (v.find("day") != std::string::npos) return n * 86400;
    if (v.find("hour") != std::string::npos) return n * 3600;
    if (v.find("minute") != std::string::npos) return n * 60;
    return n;
}
std::string pwDuration(double s) {
    if (s < 0) return "—";
    const long t = static_cast<long>(s), h = t / 3600, m = t % 3600 / 60;
    char b[32];
    if (h > 0) std::snprintf(b, sizeof b, "%ldh %ldm", h, m);
    else std::snprintf(b, sizeof b, "%ldm", std::max(1L, m));
    return b;
}
std::string pwNum(double v, const char* unit, int dec = 1) {
    char b[40];
    std::snprintf(b, sizeof b, "%.*f %s", dec, v, unit);
    return b;
}
bool pwIsBatteryKind(const std::string& k) { return k == "battery" || k == "ups"; }

// The devices that upower lists (`upower -d`): a block per device, "key: value" lines, and a line with only the kind of device inside it.
void pwParseUpower(const std::string& text, PwData& d) {
    std::vector<PwDev> out;
    bool ac = false;
    std::istringstream in(text);
    PwDev cur;
    bool open = false;
    auto flush = [&]() {
        if (!open) return;
        if (cur.kind == "line-power") { if (cur.online) ac = true; }
        else if (cur.path.find("DisplayDevice") == std::string::npos && (cur.pct >= 0 || !cur.level.empty())) out.push_back(cur);
        open = false;
    };
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("Device: ", 0) == 0) { flush(); cur = PwDev{}; cur.path = tkTrim(line.substr(8)); open = true; continue; }
        if (line.rfind("Daemon:", 0) == 0) { flush(); break; }
        if (!open) continue;
        const size_t c = line.find(':');
        const std::string t = tkTrim(line);
        if (c == std::string::npos) { if (!t.empty() && line.rfind("    ", 0) != 0) cur.kind = t; continue; }
        const std::string k = tkTrim(line.substr(0, c)), v = tkTrim(line.substr(c + 1));
        if (k == "native-path") cur.native = v;
        else if (k == "vendor") cur.vendor = v;
        else if (k == "model") cur.model = v;
        else if (k == "serial") cur.serial = v;
        else if (k == "power supply") cur.powerSupply = v == "yes";
        else if (k == "has history") cur.hasHistory = v == "yes";
        else if (k == "online") cur.online = v == "yes";
        else if (k == "state") cur.state = v;
        else if (k == "warning-level") cur.warning = v;
        else if (k == "battery-level") cur.level = v;
        else if (k == "technology") cur.tech = v;
        else if (k == "percentage") { cur.pct = pwFirstNumber(v); cur.pctIgnored = v.find("ignored") != std::string::npos; }
        else if (k == "energy") cur.energy = pwFirstNumber(v);
        else if (k == "energy-full") cur.full = pwFirstNumber(v);
        else if (k == "energy-full-design") cur.design = pwFirstNumber(v);
        else if (k == "energy-rate") cur.rate = pwFirstNumber(v);
        else if (k == "voltage") cur.volt = pwFirstNumber(v);
        else if (k == "capacity") cur.capacity = pwFirstNumber(v);
        else if (k == "charge-cycles") cur.cycles = v == "N/A" ? -1 : static_cast<int>(pwFirstNumber(v));
        else if (k == "time to empty") cur.tte = pwSeconds(v);
        else if (k == "time to full") cur.ttf = pwSeconds(v);
    }
    flush();
    d.acOnline = ac;
    // a device that was there keeps its history
    for (PwDev& n : out) {
        const auto old = std::find_if(d.devs.begin(), d.devs.end(), [&](const PwDev& o) { return o.path == n.path; });
        if (old != d.devs.end()) { n.series = old->series; n.prefilled = old->prefilled; n.lastPush = old->lastPush; }
    }
    d.devs = std::move(out);
}

// Without upower: the batteries of /sys/class/power_supply.
void pwReadSysfs(PwData& d) {
    std::vector<PwDev> out;
    bool ac = false;
    if (DIR* dir = opendir("/sys/class/power_supply")) {
        std::vector<std::string> names;
        while (const dirent* e = readdir(dir)) if (e->d_name[0] != '.') names.push_back(e->d_name);
        closedir(dir);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) {
            const std::string b = "/sys/class/power_supply/" + n + "/";
            const std::string type = tkTrim(tkRead(b + "type", 32));
            if (type == "Mains" || type == "USB") { if (tkNumber(b + "online") > 0) ac = true; continue; }
            if (type != "Battery" || tkTrim(tkRead(b + "scope", 32)) == "Device") continue;
            PwDev v;
            v.path = n; v.native = n; v.kind = "battery"; v.powerSupply = true;
            v.pct = tkNumber(b + "capacity");
            std::string st = tkTrim(tkRead(b + "status", 32));
            for (char& c : st) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::replace(st.begin(), st.end(), ' ', '-');
            v.state = st;
            auto uw = [&](const char* f) { const double x = tkNumber(b + f); return x >= 0 ? x / 1e6 : -1; };
            v.energy = uw("energy_now"); v.full = uw("energy_full"); v.design = uw("energy_full_design"); v.rate = uw("power_now"); v.volt = uw("voltage_now");
            if (v.full > 0 && v.design > 0) v.capacity = 100.0 * v.full / v.design;
            v.cycles = static_cast<int>(tkNumber(b + "cycle_count"));
            v.tech = tkTrim(tkRead(b + "technology", 32));
            v.vendor = tkTrim(tkRead(b + "manufacturer", 64));
            v.model = tkTrim(tkRead(b + "model_name", 64));
            v.serial = tkTrim(tkRead(b + "serial_number", 64));
            if (v.rate > 0) {
                if (v.state == "discharging" && v.energy >= 0) v.tte = v.energy / v.rate * 3600;
                if (v.state == "charging" && v.full > 0 && v.energy >= 0) v.ttf = (v.full - v.energy) / v.rate * 3600;
            }
            out.push_back(v);
        }
    }
    d.acOnline = ac;
    for (PwDev& nv : out) {
        const auto old = std::find_if(d.devs.begin(), d.devs.end(), [&](const PwDev& o) { return o.path == nv.path; });
        if (old != d.devs.end()) { nv.series = old->series; nv.lastPush = old->lastPush; }
    }
    d.devs = std::move(out);
}

// What upower does not tell: the temperature and the charge limit of a battery, from its sysfs.
void pwExtras(PwDev& v) {
    if (v.native.empty() || !pwIsBatteryKind(v.kind)) return;
    const std::string b = "/sys/class/power_supply/" + v.native + "/";
    const double t = tkNumber(b + "temp");
    v.tempC = t > 0 ? t / 10.0 : -1;
    const double lim = tkNumber(b + "charge_control_end_threshold");
    v.chargeLimit = lim > 0 && lim < 100 ? std::to_string(static_cast<int>(lim)) + "%" : "";
    if (v.cycles < 0) { const double c = tkNumber(b + "cycle_count"); if (c > 0) v.cycles = static_cast<int>(c); }
}

// The last hour of charge that upower keeps (so the graph is full when the tab opens): GetHistory over D-Bus, as "a(udu) N time value state ...".
void pwPrefill(PwDev& v) {
    v.prefilled = true;
    if (!v.hasHistory || v.pctIgnored || v.path.empty() || v.path.find_first_of("'\"$` ;&|<>") != std::string::npos) return;
    const std::string out = pwPopen("busctl call --system org.freedesktop.UPower '" + v.path + "' org.freedesktop.UPower.Device GetHistory suu charge 3600 60 2>/dev/null");
    std::istringstream in(out);
    std::string tag;
    int n = 0;
    in >> tag >> n;
    std::vector<std::pair<unsigned long, double>> pts;
    for (int i = 0; i < n; i++) {
        unsigned long t = 0;
        double val = 0;
        unsigned st = 0;
        if (!(in >> t >> val >> st)) break;
        if (st != 0 && val > 0) pts.push_back({t, val});
    }
    std::sort(pts.begin(), pts.end());
    for (const auto& p : pts) v.series.push(p.second);
}

// A preview with made-up devices, to see the tabs on a machine without a battery: TASKS_FAKE_POWER=1 end4pC-tools tasks
void pwFake(PwData& d) {
    d.last = Clock::now();
    d.lastProfile = d.last;
    if (!d.devs.empty()) {  // (the numbers move a little each time)
        for (PwDev& v : d.devs) if (v.pct >= 0 && !v.pctIgnored) { v.pct = std::clamp(v.pct + (v.state == "charging" ? 0.1 : -0.1), 1.0, 100.0); v.series.push(v.pct); }
        return;
    }
    d.acOnline = false;
    d.profile = "balanced";
    auto mk = [](const char* path, const char* kind, bool supply, double pct, const char* state) {
        PwDev v;
        v.path = path; v.native = path; v.kind = kind; v.powerSupply = supply; v.pct = pct; v.state = state; v.prefilled = true; v.hasHistory = true;
        return v;
    };
    PwDev bat = mk("BAT0", "battery", true, 76, "discharging");
    bat.model = "L19M4PF4"; bat.vendor = "SMP"; bat.serial = "4A0F-9B31"; bat.tech = "lithium-polymer";
    bat.energy = 38.2; bat.full = 50.4; bat.design = 56.0; bat.rate = 9.8; bat.volt = 11.8; bat.capacity = 90; bat.cycles = 214; bat.tempC = 36.5; bat.chargeLimit = "80%"; bat.tte = 3 * 3600 + 51 * 60;
    for (int i = 0; i < 60; i++) bat.series.push(93 - i * 0.28 + std::sin(i * 0.7) * 0.6);
    PwDev mouse = mk("mouse0", "mouse", false, 54, "discharging");
    mouse.model = "Logitech MX Master 3"; mouse.vendor = "Logitech"; mouse.serial = "6a-cb-be-41";
    for (int i = 0; i < 60; i++) mouse.series.push(60 - i * 0.1);
    PwDev kb = mk("kbd0", "keyboard", false, 0, "discharging");
    kb.model = "Keychron K2"; kb.pct = -1; kb.level = "normal";
    PwDev head = mk("head0", "headset", false, 31, "charging");
    head.model = "WH-1000XM4"; head.vendor = "Sony"; head.ttf = 5400;
    for (int i = 0; i < 60; i++) head.series.push(18 + i * 0.22);
    PwDev pad = mk("pad0", "gaming-input", false, 12, "discharging");
    pad.model = "DualSense Wireless Controller"; pad.warning = "low";
    for (int i = 0; i < 60; i++) pad.series.push(30 - i * 0.3);
    d.devs = {bat, mouse, kb, head, pad};
}

void pwSample(PwData& d, bool details) {
    if (std::getenv("TASKS_FAKE_POWER")) { pwFake(d); return; }
    const auto now = Clock::now();
    d.last = now;
    const std::string up = pwPopen("upower -d 2>/dev/null");
    d.haveUpower = up.find("Device:") != std::string::npos;
    if (d.haveUpower) pwParseUpower(up, d);
    else pwReadSysfs(d);
    if (!details) { d.last = {}; return; }
    if (d.profile.empty() || now - d.lastProfile > std::chrono::seconds(15)) {
        d.lastProfile = now;
        d.profile = tkTrim(pwPopen("powerprofilesctl get 2>/dev/null"));
    }
    for (PwDev& v : d.devs) {
        pwExtras(v);
        if (!v.prefilled) pwPrefill(v);
        const double sec = v.prefilled && !v.series.v.empty() && v.hasHistory ? 60.0 : 3.0;
        if (v.pct >= 0 && !v.pctIgnored && (v.lastPush == Clock::time_point{} || std::chrono::duration<double>(now - v.lastPush).count() >= sec)) {
            v.series.push(v.pct);
            v.lastPush = now;
        }
    }
}

std::future<PwData> pendingPower;
bool powerDiscoveryPending = true;

void startPowerSample(bool details) {
    if (pendingPower.valid()) return;
    pendingPower = sampleInBackground([snapshot = pwData(), details]() mutable {
        pwSample(snapshot, details);
        return std::move(snapshot);
    });
}

bool finishPowerSample() {
    if (!sampleReady(pendingPower)) return false;
    pwData() = pendingPower.get();
    return true;
}

const char* pwKindLabel(const std::string& k) {
    if (k == "battery") return "Battery";
    if (k == "ups") return "UPS";
    if (k == "mouse") return "Mouse";
    if (k == "keyboard") return "Keyboard";
    if (k == "headset" || k == "headphones") return "Headset";
    if (k == "gaming-input") return "Gamepad";
    if (k == "phone") return "Phone";
    if (k == "tablet") return "Tablet";
    if (k == "pen") return "Pen";
    return "Device";
}
const char* pwDevIcon(const PwDev& v) {
    const std::string& k = v.kind;
    if (k == "mouse") return "mouse";
    if (k == "keyboard") return "keyboard";
    if (k == "headset" || k == "headphones") return "headphones-round";
    if (k == "gaming-input") return "gamepad-minimalistic";
    if (k == "phone") return "smartphone";
    if (k == "tablet") return "tablet";
    if (v.state == "charging") return "battery-charge-minimalistic";
    return v.pct > 60 ? "battery-full-minimalistic" : v.pct > 25 ? "battery-half-minimalistic" : "battery-low-minimalistic";
}
std::string pwStateText(const std::string& s) {
    if (s == "charging") return tkT("Charging");
    if (s == "discharging") return tkT("Discharging");
    if (s == "fully-charged") return tkT("Full");
    if (s == "empty") return tkT("Empty");
    if (s == "pending-charge") return tkT("Waiting to charge");
    if (s == "pending-discharge") return tkT("Waiting to discharge");
    return s.empty() ? "—" : s;
}
std::string pwLevelText(const std::string& l) {
    if (l == "full") return tkT("Full");
    if (l == "high") return tkT("High");
    if (l == "normal") return tkT("Normal");
    if (l == "low") return tkT("Low");
    if (l == "critical") return tkT("Critical");
    return l;
}

struct PowerPage : ToolPage {
    bool internal;  // true: Battery (the computer and UPS); false: Devices (the batteries of what is plugged or paired)
    explicit PowerPage(bool inner) : internal(inner) {}
    const char* name() const override { return internal ? "Battery" : "Devices"; }
    const char* icon() const override { return internal ? "battery-full-minimalistic" : "mouse"; }

    // Is there something for this tab? (asked before the tabs are built)
    static bool available(bool inner) {
        const PwData& d = pwData();
        for (const PwDev& v : d.devs) if (pwIsBatteryKind(v.kind) && v.powerSupply == inner) return true;
        if (inner) return false;
        for (const PwDev& v : d.devs) if (!pwIsBatteryKind(v.kind)) return true;
        return false;
    }
    bool mine(const PwDev& v) const { return internal ? (pwIsBatteryKind(v.kind) && v.powerSupply) : !(pwIsBatteryKind(v.kind) && v.powerSupply); }
    std::vector<const PwDev*> items() const {
        std::vector<const PwDev*> out;
        for (const PwDev& v : pwData().devs) if (mine(v)) out.push_back(&v);
        return out;
    }

    int sel = 0, hover = -1;
    Elastic listScroll;
    Rect listArea;
    std::vector<std::pair<Rect, int>> hits;

    void init() override { startPowerSample(true); }

    bool tick() override {
        const bool moved = listScroll.step();
        PwData& d = pwData();
        if (d.last == Clock::time_point{} || Clock::now() - d.last >= std::chrono::seconds(3)) startPowerSample(true);
        return moved;
    }
    bool animating() const override { return listScroll.busy(); }
    bool motion(double x, double y) override {
        int h = -1;
        if (listArea.contains(x, y)) for (const auto& p : hits) if (p.first.contains(x, y)) h = p.second;
        const bool changed = h != hover;
        hover = h;
        return changed;
    }
    bool leave() override { const bool c = hover >= 0; hover = -1; return c; }
    bool click(double x, double y, bool press) override {
        if (!press || !listArea.contains(x, y)) return false;
        for (const auto& p : hits) if (p.first.contains(x, y)) { sel = p.second; return true; }
        return false;
    }
    bool scroll(double x, double y, double dy) override {
        if (!listArea.contains(x, y)) return false;
        listScroll.by(dy * 3);
        return true;
    }
    Cursor cursor(double x, double y) const override { return listArea.contains(x, y) ? Cursor::Pointer : Cursor::Default; }
    bool key(const KeyEvent& k) override {
        const int n = static_cast<int>(items().size());
        if (k.sym == XKB_KEY_Down) { sel = std::min(n - 1, sel + 1); return true; }
        if (k.sym == XKB_KEY_Up) { sel = std::max(0, sel - 1); return true; }
        return false;
    }

    static std::string titleOf(const PwDev& v, size_t count) {
        if (v.kind == "battery" || v.kind == "ups") return std::string(tkT(pwKindLabel(v.kind))) + (count > 1 && !v.native.empty() ? " " + v.native : "");
        return v.model.empty() ? tkT(pwKindLabel(v.kind)) : v.model;
    }
    static std::string valueOf(const PwDev& v) {
        if (v.pct >= 0 && !v.pctIgnored) return tkPercent(v.pct) + "  ·  " + pwStateText(v.state);
        const std::string lvl = pwLevelText(v.level), st = pwStateText(v.state);
        return v.state.empty() || v.state == "unknown" || st == lvl ? lvl : lvl + "  ·  " + st;
    }

    void drawCards(cairo_t* cr, const Rect& area, const std::vector<const PwDev*>& list) {
        listArea = area;
        hits.clear();
        constexpr double gap = 8, minH = 92, maxH = 128;
        const double n = std::max<size_t>(1, list.size());
        const double itemH = std::clamp((area.h - (n - 1) * gap) / n, minH, maxH);
        listScroll.max = std::max(0.0, n * (itemH + gap) - gap - area.h);
        listScroll.fit();
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        double y = area.y - listScroll.off();
        for (size_t i = 0; i < list.size(); i++) {
            const PwDev& v = *list[i];
            const Rect r = {area.x, y, area.w, itemH};
            y += itemH + gap;
            hits.push_back({r, static_cast<int>(i)});
            if (r.y + r.h < area.y || r.y > area.y + area.h) continue;
            const bool on = static_cast<int>(i) == sel, hot = static_cast<int>(i) == hover;
            const Color bg = g.color(on ? "primary_container" : "surface_container"), fg = g.color(on ? "on_primary_container" : "on_surface");
            src(cr, hot && !on ? mix(g.color("on_surface"), bg, 0.06) : bg);
            rrect(cr, r.x, r.y, r.w, r.h, 24);
            cairo_fill(cr);
            cairo_save(cr);
            rrect(cr, r.x, r.y, r.w, r.h, 24);
            cairo_clip(cr);
            shapePath(cr, "Cookie9Sided", r.x + r.w - 118, r.y - 52, r.h * 1.9);
            src(cr, g.color(on ? "primary" : "on_surface"), on ? 0.2 : 0.06);
            cairo_fill(cr);
            const std::vector<double>& s = v.series.v;
            if (s.size() > 1) {
                const Rect sp = {r.x, r.y + r.h * 0.56, r.w, r.h * 0.44};
                const double step = sp.w / (kTkHistory - 1), x0 = sp.x + sp.w - step * (s.size() - 1);
                auto yOf = [&](double val) { return sp.y + sp.h - std::clamp(val / 100.0, 0.0, 1.0) * sp.h; };
                cairo_move_to(cr, x0, sp.y + sp.h);
                for (size_t k = 0; k < s.size(); k++) cairo_line_to(cr, x0 + step * k, yOf(s[k]));
                cairo_line_to(cr, x0 + step * (s.size() - 1), sp.y + sp.h);
                src(cr, fg, 0.14);
                cairo_fill(cr);
                cairo_move_to(cr, x0, yOf(s[0]));
                for (size_t k = 1; k < s.size(); k++) cairo_line_to(cr, x0 + step * k, yOf(s[k]));
                src(cr, fg, 0.65);
                cairo_set_line_width(cr, 1.6);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
            const double isz = std::clamp(r.h * 0.42, 34.0, 52.0);
            ::icon(cr, pwDevIcon(v), r.x + r.w - 20 - isz, r.y + 16, static_cast<int>(isz), fg);
            text(cr, tkFit(titleOf(v, list.size()), 16, true, r.w - 36 - isz), r.x + 18, r.y + 16, 16, true, fg);
            text(cr, tkFit(valueOf(v), 13, false, r.w - 36 - isz), r.x + 18, r.y + 42, 13, false, fg, 0, 0.85);
        }
        cairo_restore(cr);
    }

    // Every number that is known, nothing that is not.
    std::vector<std::pair<std::string, std::string>> statsOf(const PwDev& v) const {
        std::vector<std::pair<std::string, std::string>> st;
        const PwData& d = pwData();
        st.push_back({tkT("State"), pwStateText(v.state)});
        if (v.pct >= 0 && !v.pctIgnored) st.push_back({tkT("Charge"), tkPercent(v.pct)});
        else if (!v.level.empty() && pwLevelText(v.level) != pwStateText(v.state)) st.push_back({tkT("Level"), pwLevelText(v.level)});
        if (v.state == "discharging" && v.tte > 0) st.push_back({tkT("Time left"), pwDuration(v.tte)});
        else if (v.state == "charging" && v.ttf > 0) st.push_back({tkT("Time to full"), pwDuration(v.ttf)});
        if (v.rate >= 0) st.push_back({tkT("Power"), pwNum(v.rate, "W")});
        if (v.volt > 0) st.push_back({tkT("Voltage"), pwNum(v.volt, "V")});
        if (v.energy >= 0) {
            char b[48];
            if (v.full > 0) std::snprintf(b, sizeof b, "%.1f / %.1f Wh", v.energy, v.full);
            else std::snprintf(b, sizeof b, "%.1f Wh", v.energy);
            st.push_back({tkT("Energy"), b});
        }
        if (v.design > 0) st.push_back({tkT("Design capacity"), pwNum(v.design, "Wh")});
        if (v.capacity > 0) st.push_back({tkT("Health"), tkPercent(v.capacity)});
        if (v.cycles >= 0) st.push_back({tkT("Charge cycles"), std::to_string(v.cycles)});
        if (v.tempC > 0) st.push_back({tkT("Temperature"), pwNum(v.tempC, "°C", 0)});
        if (!v.chargeLimit.empty()) st.push_back({tkT("Charge limit"), v.chargeLimit});
        if (internal && !d.profile.empty()) st.push_back({tkT("Power profile"), d.profile});
        if (internal) st.push_back({tkT("Power adapter"), d.acOnline ? tkT("Connected") : tkT("Not connected")});
        if (!v.tech.empty() && v.tech != "unknown") st.push_back({tkT("Technology"), v.tech});
        if (!v.vendor.empty()) st.push_back({tkT("Manufacturer"), v.vendor});
        if (!v.model.empty()) st.push_back({tkT("Model"), v.model});
        if (!v.serial.empty()) st.push_back({tkT("Serial number"), v.serial});
        if (!v.warning.empty() && v.warning != "none" && v.warning != "unknown") st.push_back({tkT("Warning"), v.warning});
        if (st.size() > 16) st.resize(16);
        return st;
    }

    void draw(cairo_t* cr, Rect r) override {
        PwData& d = pwData();
        const auto list = items();
        if (list.empty()) {
            const std::string s = tkT("Nothing to show");
            double tw = 0, th = 0;
            measure(s, 18, false, 0, tw, th);
            text(cr, s, (r.w - tw) / 2, r.h / 2 - th / 2, 18, false, g.color("outline"));
            return;
        }
        sel = std::clamp(sel, 0, static_cast<int>(list.size()) - 1);
        const double listW = 270;
        drawCards(cr, {0, 0, listW, r.h}, list);
        const PwDev& v = *list[sel];
        const Rect dr = {listW + 14, 0, r.w - listW - 14, r.h};
        text(cr, tkFit(titleOf(v, list.size()), 26, true, dr.w * 0.62), dr.x + 4, dr.y + 2, 26, true, g.color("on_surface"));
        std::string sub = tkT(pwKindLabel(v.kind));
        if (!v.model.empty() && (v.kind == "battery" || v.kind == "ups")) sub = v.model;
        double sw = 0, sh = 0;
        measure(sub, 14, false, 0, sw, sh);
        sub = tkFit(sub, 14, false, dr.w * 0.6);
        measure(sub, 14, false, 0, sw, sh);
        text(cr, sub, dr.x + dr.w - 4 - sw, dr.y + 12, 14, false, g.color("on_surface_variant"));

        const auto st = statsOf(v);
        const int cols = 4, rows = (static_cast<int>(st.size()) + cols - 1) / cols;
        const double statH = rows * 76 + (rows - 1) * 8, top = dr.y + 44;
        tkStatGrid(cr, {dr.x, dr.y + dr.h - statH, dr.w, statH}, st, cols);
        const Rect area = {dr.x, top, dr.w, std::max(120.0, dr.h - 44 - statH - 10)};
        if (v.pct >= 0 && !v.pctIgnored) {
            const Color c = g.color(v.pct <= 15 && v.state != "charging" ? "error" : "primary");
            tkChart(cr, area, {&v.series}, {c}, 100, "100%", v.prefilled && v.hasHistory && v.series.v.size() > 3 ? tkT("Last hour") : tkT("Last minutes"));
        } else {  // only a level (many mice and headsets): a card with the level instead of a graph that would be made up
            src(cr, g.color("primary_container"));
            rrect(cr, area.x, area.y, area.w, area.h, 26);
            cairo_fill(cr);
            cairo_save(cr);
            rrect(cr, area.x, area.y, area.w, area.h, 26);
            cairo_clip(cr);
            shapePath(cr, "Cookie9Sided", area.x + area.w - 260, area.y - 90, std::max(260.0, area.h * 1.6));
            src(cr, g.color("primary"), 0.18);
            cairo_fill(cr);
            cairo_restore(cr);
            const Color fg = g.color("on_primary_container");
            const double isz = std::min(110.0, area.h * 0.5);
            ::icon(cr, pwDevIcon(v), area.x + area.w - 40 - isz, area.y + (area.h - isz) / 2, static_cast<int>(isz), fg);
            const std::string big = pwLevelText(v.level);
            text(cr, big, area.x + 32, area.y + area.h / 2 - 40, 52, true, fg);
            text(cr, tkT("This device reports a level, not a percentage."), area.x + 34, area.y + area.h / 2 + 28, 14, false, fg, area.w * 0.55, 0.85);
        }
    }
};
