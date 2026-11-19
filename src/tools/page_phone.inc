// Tools page: Phone. The Android phone on this computer through adb and scrcpy: the devices adb sees (USB or Wi-Fi) with a Mirror button
// that opens scrcpy, a few options for it, and the Wi-Fi pairing of Android 11 or later (adb pair / adb connect).
// scrcpy and adb are programs of their own; the page only starts them. Included by src/tools.cpp (inside its anonymous namespace).

std::string phT(const char* s) { return std::string(i18n::tr(s)); }

bool phHave(const char* bin) {
    const char* path = std::getenv("PATH");
    std::istringstream in(path ? path : "/usr/bin");
    for (std::string dir; std::getline(in, dir, ':');)
        if (!dir.empty() && access((dir + "/" + bin).c_str(), X_OK) == 0) return true;
    return false;
}

// Starts a program that outlives the Tools window (scrcpy has a window of its own).
void phSpawnDetached(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t first = fork();
    if (first < 0) return;
    if (first == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        const int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    waitpid(first, nullptr, 0);
}

struct PhoneDevice {
    std::string serial, model, state;
    bool wifi = false;
};

// The lines of `adb devices -l`: "SERIAL  device usb:1-1 product:x model:Pixel_7 device:y transport_id:2".
std::vector<PhoneDevice> phParseDevices(const std::string& out) {
    std::vector<PhoneDevice> list;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '*' || line.rfind("List of devices", 0) == 0) continue;
        std::istringstream ls(line);
        PhoneDevice d;
        ls >> d.serial >> d.state;
        if (d.serial.empty() || d.state.empty()) continue;
        for (std::string tok; ls >> tok;) {
            if (tok.rfind("model:", 0) == 0) {
                d.model = tok.substr(6);
                std::replace(d.model.begin(), d.model.end(), '_', ' ');
            }
        }
        d.wifi = d.serial.find(':') != std::string::npos || d.serial.find("_adb-tls") != std::string::npos;
        list.push_back(d);
    }
    return list;
}

// A one-line text box: a caret, a selection (the mouse drags it, Shift with the arrows, Ctrl+A), and the text slides to keep the caret in view.
struct PhField {
    std::string s;
    size_t cur = 0, anc = 0;
    double scroll = 0;
    bool hasSel() const { return cur != anc; }
    size_t lo() const { return std::min(cur, anc); }
    size_t hi() const { return std::max(cur, anc); }
    std::string selected() const { return s.substr(lo(), hi() - lo()); }
    void set(const std::string& v) { s = v; cur = anc = s.size(); scroll = 0; }
    void insert(const std::string& t) {
        const size_t b = lo();
        s.replace(b, hi() - b, t);
        cur = anc = b + t.size();
    }
    void erase(bool forward) {
        if (!hasSel()) {
            if (forward ? cur >= s.size() : cur == 0) return;
            anc = forward ? tbNextChar(s, cur) : trPrevChar(s, cur);
        }
        insert("");
    }
    void moveTo(size_t i, bool extend) {
        cur = std::min(i, s.size());
        if (!extend) anc = cur;
    }
    void selectAll() { anc = 0; cur = s.size(); }
    size_t indexAt(double x) const { return tbCaretAt(s, 15, x + scroll); }
    static double widthTo(const std::string& v, size_t i) {
        double w = 0, h = 0;
        measure(v.substr(0, std::min(i, v.size())), 15, false, 0, w, h);
        return w;
    }
};

struct PhonePage final : ToolPage {
    enum Id { None, Mirror, Disconnect, Toggle, Size, Pair, Connect, FieldAddr, FieldCode, Paste, NewQr };
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };
    static constexpr int kSizes[4] = {0, 1024, 1440, 1920};

    std::vector<PhoneDevice> devices;
    bool haveScrcpy = false, haveAdb = false, haveQr = false, listed = false;
    bool screenOff = false, awake = true, noAudio = false, onTop = false;
    int maxSize = 0;
    PhField fa, fc;  // the address and the pairing code
    int focus = 0;   // 1: the address, 2: the pairing code
    int dragging = 0;
    Clock::time_point lastPress{};
    int lastPressField = 0;
    TrJob listJob, cmdJob, mdnsJob, pairJob;
    Clock::time_point nextList{}, nextMdns{}, toastUntil{}, lastKey = Clock::now();
    std::string toast, qrName, qrPass, qrStatus;
    cairo_surface_t* qrImage = nullptr;
    int qrModules = 0;
    std::vector<Hit> hits;
    Hover hover;
    Rect addrBox{}, codeBox{};

    const char* name() const override { return "Phone"; }
    const char* icon() const override { return "smartphone"; }
    ~PhonePage() override { if (qrImage) cairo_surface_destroy(qrImage); }
    bool grabsKeys() const override { return focus != 0; }
    void blur() override { focus = 0; dragging = 0; }
    bool animating() const override { return focus != 0 || Clock::now() < toastUntil; }

    // ---- state ----------------------------------------------------------------------------------------------------------------
    static std::string confPath() { return stateDir() + "/phone.conf"; }
    void init() override {
        std::ifstream f(confPath());
        for (std::string line; std::getline(f, line);) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "screenOff") screenOff = v == "1";
            else if (k == "awake") awake = v == "1";
            else if (k == "noAudio") noAudio = v == "1";
            else if (k == "onTop") onTop = v == "1";
            else if (k == "maxSize") maxSize = std::atoi(v.c_str());
            else if (k == "addr") fa.set(v);
        }
        haveScrcpy = phHave("scrcpy");
        haveAdb = phHave("adb");
        haveQr = phHave("qrencode");
        if (haveAdb && haveQr) newQr();
    }
    void save() const {
        mkdir(stateDir().c_str(), 0755);  // (it exists in practice: the shell keeps its state there)
        std::ofstream f(confPath(), std::ios::trunc);
        f << "screenOff=" << screenOff << "\nawake=" << awake << "\nnoAudio=" << noAudio << "\nonTop=" << onTop << "\nmaxSize=" << maxSize << "\naddr=" << fa.s << "\n";
    }
    void say(const std::string& s) { toast = s; toastUntil = Clock::now() + std::chrono::milliseconds(6000); }

    // ---- the QR code: the phone scans it and offers itself for pairing; adb finds it by name and pairs with the password ----------------
    static std::string randomText(size_t n) {
        static const char* kChars = "abcdefghijkmnpqrstuvwxyz23456789";
        std::random_device rd;
        std::string out;
        for (size_t i = 0; i < n; i++) out += kChars[rd() % 32];
        return out;
    }
    void newQr() {
        qrName = "end4pC-" + randomText(6);
        qrPass = randomText(12);
        const std::string file = runtimeDir() + "/end4pC-phone-qr.png", text = "WIFI:T:ADB;S:" + qrName + ";P:" + qrPass + ";;";
        if (qrImage) { cairo_surface_destroy(qrImage); qrImage = nullptr; }
        qrModules = 0;
        if (!trRunSync({"qrencode", "-s", "1", "-m", "0", "-l", "M", "-o", file, "--", text}, "", nullptr, 3000)) { qrStatus = phT("Could not make the QR code"); return; }
        qrImage = cairo_image_surface_create_from_png(file.c_str());
        std::remove(file.c_str());
        if (cairo_surface_status(qrImage) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(qrImage); qrImage = nullptr; return; }
        qrModules = cairo_image_surface_get_width(qrImage);
        qrStatus = phT("Waiting for the phone") + "\xE2\x80\xA6";
        nextMdns = Clock::now();
    }
    // A line of `adb mdns services`: "NAME  _adb-tls-pairing._tcp  IP:PORT".
    std::string pairingAddress(const std::string& out) const {
        std::istringstream in(out);
        for (std::string line; std::getline(in, line);) {
            if (line.find(qrName) == std::string::npos) continue;
            const size_t sp = line.find_last_of(" \t");
            const std::string tok = sp == std::string::npos ? line : line.substr(sp + 1);
            if (tok.find(':') != std::string::npos) return tok;
        }
        return "";
    }
    bool tickQr(Clock::time_point now) {
        bool changed = false;
        if (pairJob.running()) {
            if (pairJob.poll()) {
                const bool ok = pairJob.out.find("Successfully paired") != std::string::npos;
                std::string first = trClean(pairJob.out);
                first = first.substr(0, first.find('\n'));
                say(ok ? phT("Paired: the phone shows up in a moment") : (first.empty() ? phT("Could not pair") : first));
                newQr();
                nextList = now;
                changed = true;
            }
            return changed;
        }
        if (!haveQr || qrModules == 0 || !haveAdb) return false;
        if (mdnsJob.running()) {
            if (!mdnsJob.poll()) return false;
            const std::string where = pairingAddress(mdnsJob.out);
            if (!where.empty()) {
                qrStatus = phT("Pairing") + "\xE2\x80\xA6";
                pairJob.start({"adb", "pair", where, qrPass}, "", 20000);
                changed = true;
            }
            nextMdns = now + std::chrono::milliseconds(1500);
        } else if (now >= nextMdns) {
            mdnsJob.start({"adb", "mdns", "services"}, "", 5000);
        }
        return changed;
    }

    // ---- adb ------------------------------------------------------------------------------------------------------------------
    bool tick() override {
        bool changed = false;
        const auto now = Clock::now();
        if (listJob.running() && listJob.poll()) {
            if (listJob.ok) devices = phParseDevices(listJob.out);
            listed = true;
            nextList = now + std::chrono::milliseconds(2500);
            changed = true;
        } else if (!listJob.running() && haveAdb && now >= nextList) {
            listJob.start({"adb", "devices", "-l"}, "", 6000);
            nextList = now + std::chrono::milliseconds(2500);
        }
        if (cmdJob.running() && cmdJob.poll()) {
            std::string first = trClean(cmdJob.out);
            first = first.substr(0, first.find('\n'));
            say(first.empty() ? (cmdJob.ok ? phT("Done") : phT("The command failed")) : first);
            nextList = now;
            changed = true;
        }
        if (tickQr(now)) changed = true;
        if (!toast.empty() && now >= toastUntil) { toast.clear(); changed = true; }
        return changed;
    }
    void runAdb(const std::vector<std::string>& args) {
        if (cmdJob.running()) return;
        std::vector<std::string> argv = {"adb"};
        argv.insert(argv.end(), args.begin(), args.end());
        if (!cmdJob.start(argv, "", 15000)) say(phT("Could not run adb"));
    }
    void mirror(const PhoneDevice& d) {
        std::vector<std::string> a = {"scrcpy", "-s", d.serial, "--window-title", "end4pC \xC2\xB7 " + (d.model.empty() ? d.serial : d.model)};
        if (screenOff) a.push_back("--turn-screen-off");
        if (awake) a.push_back("--stay-awake");
        if (noAudio) a.push_back("--no-audio");
        if (onTop) a.push_back("--always-on-top");
        if (maxSize > 0) { a.push_back("--max-size"); a.push_back(std::to_string(maxSize)); }
        phSpawnDetached(a);
        say(phT("Opening the phone screen") + "\xE2\x80\xA6");
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------------
    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    void card(cairo_t* cr, Rect r) {
        src(cr, g.color("surface_container"));
        rrect(cr, r.x, r.y, r.w, r.h, 24);
        cairo_fill(cr);
    }
    double pillW(const std::string& s, bool ic) const {
        double tw = 0, th = 0;
        measure(s, 15, true, 0, tw, th);
        return 30 + (ic ? 26 : 0) + tw;
    }
    void pill(cairo_t* cr, Rect r, const std::string& label, const char* ic, bool filled, bool hov, double alpha = 1) {
        const Color bg = filled ? g.color("primary") : g.color("secondary_container"), fg = filled ? g.color("on_primary") : g.color("on_secondary_container");
        src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        double x = r.x + (r.w - (ic ? 26 : 0) - tw) / 2;
        if (ic) { ::icon(cr, ic, x, r.y + (r.h - 18) / 2, 18, fg); x += 26; }
        text(cr, label, x, r.y + (r.h - th) / 2, 15, true, fg, 0, alpha);
    }
    void toggle(cairo_t* cr, Rect r, bool on, bool hov) {
        src(cr, on ? g.color("primary") : g.color("surface_container_highest"));
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        if (!on) { src(cr, g.color("outline"), 0.8); rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2 - 1); cairo_set_line_width(cr, 1.5); cairo_stroke(cr); }
        const double k = r.h - 8, kx = on ? r.x + r.w - k - 4 : r.x + 4;
        src(cr, on ? g.color("on_primary") : g.color("outline"));
        cairo_arc(cr, kx + k / 2, r.y + r.h / 2, hov ? k / 2 + 1 : k / 2, 0, 2 * M_PI);
        cairo_fill(cr);
    }
    void line(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double wrap = 0, double alpha = 1) { text(cr, s, x, y, px, bold, c, wrap, alpha); }

    void drawDevices(cairo_t* cr, Rect c) {
        card(cr, c);
        const Color fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        ::icon(cr, "smartphone", c.x + 24, c.y + 22, 28, g.color("primary"));
        line(cr, phT("Phone"), c.x + 62, c.y + 22, 20, true, fg);
        const bool ready = haveScrcpy && haveAdb;
        const std::string sub = !haveAdb ? phT("Install adb to use the phone: it comes with the android-tools package.")
                                : !haveScrcpy ? phT("Install scrcpy to use the phone: it is the scrcpy package.")
                                : phT("Connect the phone with a cable and turn on USB debugging, or pair it over Wi-Fi below.");
        line(cr, sub, c.x + 62, c.y + 50, 14, false, dim, c.w - 90);
        double y = c.y + 92;
        if (!ready) return;
        if (!listed) { line(cr, phT("Looking for phones") + "\xE2\x80\xA6", c.x + 24, y, 15, false, dim); return; }
        if (devices.empty()) { line(cr, phT("No phone found"), c.x + 24, y, 15, true, dim); return; }
        for (size_t i = 0; i < devices.size() && i < 4; i++) {
            const PhoneDevice& d = devices[i];
            const bool ok = d.state == "device";
            const Rect row{c.x + 12, y, c.w - 24, 60};
            src(cr, g.color("surface_container_high"));
            rrect(cr, row.x, row.y, row.w, row.h, 20);
            cairo_fill(cr);
            ::icon(cr, d.wifi ? "wi-fi-router" : "smartphone", row.x + 18, row.y + 18, 24, ok ? g.color("primary") : dim);
            line(cr, d.model.empty() ? d.serial : d.model, row.x + 56, row.y + 10, 16, true, fg, row.w - 330);
            const std::string st = ok ? (d.wifi ? phT("Connected over Wi-Fi") : phT("Connected by USB"))
                                   : d.state == "unauthorized" ? phT("Accept the debugging request on the phone")
                                   : d.state == "offline" ? phT("Offline: unplug it and plug it again") : d.state;
            line(cr, d.serial + "  \xC2\xB7  " + st, row.x + 56, row.y + 34, 13, false, dim, row.w - 330);
            double x = row.x + row.w - 12;
            if (d.wifi) {
                const Rect b{x - 44, row.y + 10, 40, 40};
                const bool hv = hot(b, Disconnect, static_cast<int>(i));
                if (hv) { src(cr, fg, 0.12); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); }
                ::icon(cr, "close-circle", b.x + 10, b.y + 10, 20, dim);
                x -= 48;
            }
            const std::string label = phT("Mirror");
            const double w = pillW(label, true);
            const Rect b{x - w, row.y + 10, w, 40};
            pill(cr, b, label, "screencast", true, ok && hot(b, Mirror, static_cast<int>(i)), ok ? 1 : 0.4);
            if (!ok) hits.pop_back();
            y += 68;
        }
    }

    void drawOptions(cairo_t* cr, Rect c) {
        card(cr, c);
        const Color fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        line(cr, phT("Mirror options"), c.x + 24, c.y + 20, 18, true, fg);
        struct Opt { const char* label; bool on; };
        const Opt opts[4] = {{"Turn the phone screen off", screenOff}, {"Keep the phone awake", awake}, {"Do not send the sound", noAudio}, {"Keep the window on top", onTop}};
        double y = c.y + 62;
        for (int i = 0; i < 4; i++) {
            const Rect row{c.x + 12, y, c.w - 24, 44};
            const bool hv = hot(row, Toggle, i);
            if (hv) { src(cr, fg, 0.06); rrect(cr, row.x, row.y, row.w, row.h, 16); cairo_fill(cr); }
            line(cr, phT(opts[i].label), row.x + 12, row.y + 12, 15, false, fg, row.w - 90);
            toggle(cr, {row.x + row.w - 62, row.y + 9, 50, 26}, opts[i].on, hv);
            y += 46;
        }
        line(cr, phT("Largest side"), c.x + 24, y + 12, 15, false, dim);
        double x = c.x + 24;
        y += 40;
        const std::string labels[4] = {phT("Auto"), "1024", "1440", "1920"};
        for (int i = 0; i < 4; i++) {
            const double w = pillW(labels[i], false) - 2;
            const Rect b{x, y, w, 34};
            pill(cr, b, labels[i], nullptr, maxSize == kSizes[i], hot(b, Size, i));
            x += w + 8;
        }
    }

    void drawField(cairo_t* cr, Rect r, PhField& f, const std::string& hint, bool focused, int id) {
        const bool hv = hot(r, id);
        src(cr, g.color(hv ? "surface_container_highest" : "surface_container_high"));
        rrect(cr, r.x, r.y, r.w, r.h, 16);
        cairo_fill(cr);
        if (focused) { src(cr, g.color("primary")); rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, 15); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        const double inner = r.w - 24, cw = PhField::widthTo(f.s, f.cur);
        if (cw - f.scroll > inner - 4) f.scroll = cw - inner + 4;
        if (cw < f.scroll) f.scroll = cw;
        if (PhField::widthTo(f.s, f.s.size()) <= inner) f.scroll = 0;
        double tw = 0, th = 0;
        measure(f.s.empty() ? hint : f.s, 15, false, 0, tw, th);
        cairo_save(cr);
        rrect(cr, r.x + 4, r.y, r.w - 8, r.h, 12);
        cairo_clip(cr);
        if (focused && f.hasSel()) {
            const double a = PhField::widthTo(f.s, f.lo()), b = PhField::widthTo(f.s, f.hi());
            src(cr, g.color("primary"), 0.3);
            cairo_rectangle(cr, r.x + 12 + a - f.scroll, r.y + (r.h - 22) / 2, b - a, 22);
            cairo_fill(cr);
        }
        text(cr, f.s.empty() ? hint : f.s, r.x + 12 - f.scroll, r.y + (r.h - th) / 2, 15, false, g.color(f.s.empty() ? "on_surface_variant" : "on_surface"), 0, f.s.empty() ? 0.7 : 1);
        const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count();
        if (focused && !f.hasSel() && (idle < 1000 || idle % 1000 < 600)) {
            src(cr, g.color("primary"));
            cairo_rectangle(cr, r.x + 12 + cw - f.scroll, r.y + (r.h - 20) / 2, 2, 20);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    void drawQr(cairo_t* cr, Rect box) {
        src(cr, {1, 1, 1});
        rrect(cr, box.x, box.y, box.w, box.h, 16);
        cairo_fill(cr);
        if (!qrImage || qrModules <= 0) return;
        const double side = box.w - 20;
        cairo_save(cr);
        cairo_translate(cr, box.x + 10, box.y + 10);
        cairo_scale(cr, side / qrModules, side / qrModules);
        cairo_set_source_surface(cr, qrImage, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    void drawWifi(cairo_t* cr, Rect c) {
        card(cr, c);
        const Color fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        line(cr, phT("Wi-Fi"), c.x + 24, c.y + 20, 18, true, fg);
        double y = c.y + 58;
        if (haveQr && haveAdb) {
            const Rect qr{c.x + 24, y, 152, 152};
            drawQr(cr, qr);
            const double tx = qr.x + qr.w + 20, tw = c.x + c.w - 24 - tx;
            line(cr, phT("Pair with a QR code"), tx, y + 2, 16, true, fg, tw);
            line(cr, phT("On the phone: Settings, Developer options, Wireless debugging, Pair device with QR code. Then scan this code."), tx, y + 30, 13, false, dim, tw);
            line(cr, qrStatus, tx, y + 118, 14, true, g.color("primary"), tw);
            y += 152 + 18;
            line(cr, phT("Or with a pairing code"), c.x + 24, y, 14, true, dim);
            y += 28;
        } else {
            line(cr, phT("On the phone: Settings, Developer options, Wireless debugging. The address and the pairing code are in Pair device with pairing code."), c.x + 24, y - 6, 13, false, dim, c.w - 48);
            y += 54;
        }
        addrBox = {c.x + 24, y, c.w - 48, 42};
        drawField(cr, addrBox, fa, phT("Address, like 192.168.1.20:37099"), focus == 1, FieldAddr);
        codeBox = {c.x + 24, y + 50, c.w - 48 - 120, 42};
        drawField(cr, codeBox, fc, phT("Pairing code"), focus == 2, FieldCode);
        const Rect pb{codeBox.x + codeBox.w + 8, codeBox.y, c.x + c.w - 24 - (codeBox.x + codeBox.w + 8), 42};
        pill(cr, pb, phT("Paste"), nullptr, false, hot(pb, Paste));
        const double by = y + 102;
        const std::string pairL = phT("Pair"), connL = phT("Connect");
        const Rect b1{c.x + 24, by, pillW(pairL, false), 40}, b2{b1.x + b1.w + 8, by, pillW(connL, false), 40};
        const bool busy = cmdJob.running();
        pill(cr, b1, pairL, nullptr, false, hot(b1, Pair), busy ? 0.5 : 1);
        pill(cr, b2, connL, nullptr, true, hot(b2, Connect), busy ? 0.5 : 1);
        if (!toast.empty()) line(cr, toast, b2.x + b2.w + 16, by + 10, 14, true, g.color("primary"), c.x + c.w - 24 - (b2.x + b2.w + 16));
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        constexpr double gap = 12;
        const size_t rows = std::min<size_t>(devices.size(), 4);
        const double devH = 100 + std::max<size_t>(rows, 1) * 68 + 8;
        drawDevices(cr, {0, 0, r.w, devH});
        const double y = devH + gap, h = std::max(300.0, r.h - y);
        const double w = (r.w - gap) / 2;
        drawOptions(cr, {0, y, w, h});
        drawWifi(cr, {w + gap, y, w, h});
    }

    // ---- input ----------------------------------------------------------------------------------------------------------------
    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.contains(x, y)) return {it->id, it->arg};
        return {};
    }
    PhField& field(int which) { return which == 1 ? fa : fc; }
    const Rect& boxOf(int which) const { return which == 1 ? addrBox : codeBox; }
    bool motion(double x, double y) override {
        if (dragging) {
            PhField& f = field(dragging);
            f.moveTo(f.indexAt(x - (boxOf(dragging).x + 12)), true);
            lastKey = Clock::now();
            return true;
        }
        const Hover h = hitAt(x, y);
        const bool changed = h.id != hover.id || h.arg != hover.arg;
        hover = h;
        return changed;
    }
    bool leave() override { const bool was = hover.id != None; hover = {}; return was; }
    Cursor cursor(double x, double y) const override {
        const Hover h = hitAt(x, y);
        if (h.id == FieldAddr || h.id == FieldCode) return Cursor::Text;
        return h.id != None ? Cursor::Pointer : Cursor::Default;
    }

    void paste() {
        std::string t;
        if (!trRunSync({"wl-paste", "-n", "-t", "text"}, "", &t, 1500) || t.empty()) return;
        std::string clean;
        for (char ch : t) if (ch != '\n' && ch != '\r' && ch != '\t') clean += ch;
        field(focus == 1 ? 1 : 2).insert(clean);
        if (focus == 1) save();
    }
    bool act(const Hover& h) {
        switch (h.id) {
            case Mirror: if (h.arg < static_cast<int>(devices.size())) mirror(devices[static_cast<size_t>(h.arg)]); return true;
            case Disconnect: if (h.arg < static_cast<int>(devices.size())) runAdb({"disconnect", devices[static_cast<size_t>(h.arg)].serial}); return true;
            case Toggle: {
                bool* v[4] = {&screenOff, &awake, &noAudio, &onTop};
                *v[h.arg] = !*v[h.arg];
                save();
                return true;
            }
            case Size: maxSize = kSizes[h.arg]; save(); return true;
            case Pair:
                if (fa.s.empty() || fc.s.empty()) say(phT("Write the address and the pairing code first"));
                else runAdb({"pair", fa.s, fc.s});
                return true;
            case Connect:
                if (fa.s.empty()) say(phT("Write the address first"));
                else { runAdb({"connect", fa.s}); save(); }
                return true;
            case Paste: if (focus == 0) focus = 2; paste(); return true;
            default: return false;
        }
    }
    bool click(double x, double y, bool press) override {
        if (!press) { const bool was = dragging != 0; dragging = 0; return was; }
        const Hover h = hitAt(x, y);
        if (h.id == FieldAddr || h.id == FieldCode) {
            const int which = h.id == FieldAddr ? 1 : 2;
            PhField& f = field(which);
            const auto now = Clock::now();
            const bool twice = which == lastPressField && now - lastPress < std::chrono::milliseconds(400);
            focus = which;
            lastPress = now;
            lastPressField = which;
            lastKey = now;
            if (twice) { f.selectAll(); return true; }
            f.moveTo(f.indexAt(x - (boxOf(which).x + 12)), false);
            dragging = which;
            return true;
        }
        const bool had = focus != 0;
        focus = 0;
        return h.id != None ? act(h) : had;
    }
    bool key(const KeyEvent& k) override {
        lastKey = Clock::now();
        PhField& f = field(focus == 1 ? 1 : 2);
        f.cur = std::min(f.cur, f.s.size());
        f.anc = std::min(f.anc, f.s.size());
        const bool shift = k.shift;
        if (k.ctrl) {
            if (k.sym == XKB_KEY_v) { paste(); return true; }
            if (k.sym == XKB_KEY_a) { f.selectAll(); return true; }
            if ((k.sym == XKB_KEY_c || k.sym == XKB_KEY_x) && f.hasSel()) {
                trRunSync({"wl-copy"}, f.selected(), nullptr, 1500);
                if (k.sym == XKB_KEY_x) { f.erase(false); if (focus == 1) save(); }
                return true;
            }
            return false;
        }
        switch (k.sym) {
            case XKB_KEY_Escape: focus = 0; return true;
            case XKB_KEY_Tab: focus = focus == 1 ? 2 : 1; return true;
            case XKB_KEY_Return: case XKB_KEY_KP_Enter: return act({focus == 1 ? Connect : Pair, 0});
            case XKB_KEY_Left: f.moveTo(f.hasSel() && !shift ? f.lo() : (f.cur > 0 ? trPrevChar(f.s, f.cur) : 0), shift); return true;
            case XKB_KEY_Right: f.moveTo(f.hasSel() && !shift ? f.hi() : tbNextChar(f.s, f.cur), shift); return true;
            case XKB_KEY_Home: f.moveTo(0, shift); return true;
            case XKB_KEY_End: f.moveTo(f.s.size(), shift); return true;
            case XKB_KEY_BackSpace: f.erase(false); if (focus == 1) save(); return true;
            case XKB_KEY_Delete: f.erase(true); if (focus == 1) save(); return true;
            default: break;
        }
        if (k.alt || k.text.empty() || k.text[0] == ' ') return false;
        f.insert(k.text);
        if (focus == 1) save();
        return true;
    }
};
constexpr int PhonePage::kSizes[4];
