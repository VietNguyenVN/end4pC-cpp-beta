// The "privacy" bar module: shows which apps are using the microphone, a camera or the screen (a screen share or a recording). It is invisible while
// nothing does, so it takes no room in the bar. scripts/privacy_scan.sh reads PipeWire's graph into a cache file, like the drives module does with lsblk.
constexpr int kPrivPollS = 3;
constexpr double kPrivStep = 20, kPrivIcon = 16;

std::string privacyFile() { return ccCacheDir() + "/privacy.txt"; }

bool privacyShown(const App& app) {
    for (const auto* l : {&app.cfg.layoutLeft, &app.cfg.layoutCenter, &app.cfg.layoutRight, &app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight})
        if (std::find(l->begin(), l->end(), "privacy") != l->end()) return true;
    return false;
}
// The kinds in use now, in a fixed order: "mic", "cam", "screen".
// Always, when "always show" is on: then the kinds that are on in the settings, in use or not (the one in use is lit).
std::vector<std::string> privacyKinds(const App& app) {
    const Config& c = app.cfg;
    std::vector<std::string> k;
    if (c.privacyMic && (c.privacyAlways || !app.privacy.mic.empty())) k.push_back("mic");
    if (c.privacyCam && (c.privacyAlways || !app.privacy.cam.empty())) k.push_back("cam");
    if (c.privacyScreen && (c.privacyAlways || !app.privacy.screen.empty())) k.push_back("screen");
    return k;
}
bool privacyUsed(const App& app, const std::string& k) { return !(k == "mic" ? app.privacy.mic : k == "cam" ? app.privacy.cam : app.privacy.screen).empty(); }
bool privacyVisible(const App& app) { return !privacyKinds(app).empty(); }
// The icon row/column is kQuickBox wide for one kind and grows by a step for each other kind.
double privacyExtent(const App& app) { return privacyVisible(app) ? kQuickBox + (privacyKinds(app).size() - 1) * kPrivStep : 0; }
double privacyBarW(const App& app) { return privacyVisible(app) ? privacyExtent(app) + softExtra(app, "privacy") : 0; }
double privacySideH(const App& app) { return privacyVisible(app) ? std::max(40.0, privacyExtent(app) + 14) : 0; }

void privacyAsk(App& app) {
    std::filesystem::create_directories(ccCacheDir());
    const std::string script = assetsDir() + "/../scripts/privacy_scan.sh", out = privacyFile();
    runDetached({"bash", script.c_str(), out.c_str()});
    app.privacy.nextRead = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
}

void privacyRead(App& app) {
    PrivacyBar& P = app.privacy;
    if (P.test) return;  // (a preview is on: the real state waits)
    std::string mic, cam, screen;
    std::istringstream in(ccFileRead(privacyFile()));
    for (std::string line; std::getline(in, line);) {
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string kind = line.substr(0, sp), apps = line.substr(sp + 1);
        if (kind == "mic") mic = apps;
        else if (kind == "cam") cam = apps;
        else if (kind == "screen") screen = apps;
    }
    if (mic == P.mic && cam == P.cam && screen == P.screen) return;
    P.mic = mic; P.cam = cam; P.screen = screen;
    drawAll(app);
}

void privacyTick(App& app, std::chrono::steady_clock::time_point now) {
    PrivacyBar& P = app.privacy;
    if (!privacyShown(app)) return;
    if (now >= P.nextPoll) { privacyAsk(app); P.nextPoll = now + std::chrono::seconds(kPrivPollS); }
    if (P.nextRead != std::chrono::steady_clock::time_point{} && now >= P.nextRead) { P.nextRead = {}; privacyRead(app); }
}
std::chrono::steady_clock::time_point privacyWake(const App& app) {
    auto when = std::chrono::steady_clock::time_point::max();
    if (privacyShown(app)) when = std::min(when, app.privacy.nextPoll);
    if (app.privacy.nextRead != std::chrono::steady_clock::time_point{}) when = std::min(when, app.privacy.nextRead);
    return when;
}

// One frame, whatever the style, with the icons of the kinds in use and a dot of the kind's color on each.
void privacyPaint(cairo_t* cr, App& app, double x, double y, double w, double h, bool side) {
    const Color ink = styleFrame(cr, app, widgetStyle(app, "privacy"), x, y, w, h, false, side);
    const std::vector<std::string> kinds = privacyKinds(app);
    const double span = (kinds.size() - 1) * kPrivStep;
    for (size_t i = 0; i < kinds.size(); i++) {
        const double off = -span / 2 + i * kPrivStep;
        const double cx = side ? x + w / 2 : x + w / 2 + off, cy = side ? y + h / 2 + off : y + h / 2;
        const std::string& k = kinds[i];
        const bool used = privacyUsed(app, k);
        drawIcon(cr, k == "mic" ? "microphone" : k == "cam" ? "camera" : "monitor", cx - kPrivIcon / 2, cy - kPrivIcon / 2, kPrivIcon, ink);
        if (!used) continue;  // (an idle icon, shown on purpose, has no dot)
        cairo_arc(cr, cx + kPrivIcon / 2 - 1, cy - kPrivIcon / 2 + 1, 3.2, 0, 2 * M_PI);
        setSource(cr, app.color(k == "mic" ? "tertiary" : k == "cam" ? "primary" : "error"));
        cairo_fill(cr);
    }
}
void privacyBarPaint(cairo_t* cr, App& app, const Bar& bar) {
    if (!privacyVisible(app)) return;
    privacyPaint(cr, app, slotX(bar, "privacy"), (bar.height - kQuickBox) / 2.0, privacyBarW(app), kQuickBox, false);
}
void privacySidePaint(cairo_t* cr, App& app, Rect r) {
    if (!privacyVisible(app)) return;
    const double w = kQuickBox, h = privacyExtent(app);
    privacyPaint(cr, app, r.x + (r.w - w) / 2.0, r.y + (r.h - h) / 2.0, w, h, true);
}
