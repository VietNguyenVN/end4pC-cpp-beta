
void resWSync(App& app) {
    ResW& X = app.resW;
    const App::Resources& R = app.res;
    X.target[0] = std::clamp(R.cpu, 0.0, 1.0);
    X.target[1] = std::clamp(R.ram, 0.0, 1.0);
    X.target[2] = std::clamp(R.disk, 0.0, 1.0);
    X.ramUsedGb = R.ramUsedGb;
    X.ramTotalGb = R.ramTotalGb;
    X.diskUsedGb = R.diskUsedGb;
    X.diskTotalGb = R.diskTotalGb;
    X.cpuTempC = R.tempC;
    X.primed = true;
}

const char* resWDeviceIcon(const std::string& kind) {
    if (kind == "mouse" || kind == "touchpad") return "mouse";
    if (kind == "keyboard") return "keyboard";
    if (kind == "headset" || kind == "headphones") return "headphones-round";
    if (kind == "gaming-input") return "gamepad";
    if (kind == "phone") return "smartphone";
    if (kind == "tablet") return "tablet";
    if (kind == "speakers" || kind == "speaker") return "speaker";
    return "battery-full";
}

constexpr int kResWTurnSeconds = 6;

struct ResWFourth {
    std::string label, sub, icon, unit = "%", big;
    double level = 0;
    bool ok = true, low = false, flat = false;
};

void resWNetSample(App& app, std::chrono::steady_clock::time_point now) {
    ResW& X = app.resW;
    unsigned long long rx = 0, tx = 0;
    readNetBytes(rx, tx);
    if (X.netAt != std::chrono::steady_clock::time_point{}) {
        const double dt = std::chrono::duration<double>(now - X.netAt).count();
        if (dt > 0.2) {
            X.netDown = rx >= X.netRx ? (rx - X.netRx) / dt : 0;
            X.netUp = tx >= X.netTx ? (tx - X.netTx) / dt : 0;
        }
    }
    X.netRx = rx; X.netTx = tx; X.netAt = now;
    X.netPeak = std::max({256e3, X.netPeak * 0.98, X.netDown});
}
bool resWHasNetwork() {
    unsigned long long rx = 0, tx = 0;
    readNetBytes(rx, tx);
    return rx + tx > 0 || std::filesystem::exists("/sys/class/net") && [] {
        for (const auto& e : std::filesystem::directory_iterator("/sys/class/net")) if (e.path().filename() != "lo") return true;
        return false;
    }();
}
void resWRate(double bps, std::string& number, std::string& unit) {
    static const char* kUnits[4] = {"B/s", "KB/s", "MB/s", "GB/s"};
    double v = std::max(0.0, bps);
    int u = 0;
    while (v >= 1024 && u < 3) { v /= 1024; u++; }
    char b[32];
    std::snprintf(b, sizeof b, u > 0 && v < 10 ? "%.1f" : "%.0f", v);
    number = b; unit = kUnits[u];
}

void resWTurns(App& app) {
    ResW& X = app.resW;
    std::vector<const App::Periph*> v;
    for (const auto& d : app.periph) if (d.pct > 0 || d.state == "charging" || d.state == "fully-charged") v.push_back(&d);
    std::sort(v.begin(), v.end(), [](const App::Periph* a, const App::Periph* b) { return a->pct != b->pct ? a->pct < b->pct : a->name < b->name; });
    std::vector<std::string> names;
    for (const auto* d : v) names.push_back(d->name);
    if (names != X.turnNames) {
        const std::string was = X.turn < static_cast<int>(X.turnNames.size()) ? X.turnNames[X.turn] : "";
        X.turnNames = names;
        X.turn = 0;
        for (size_t i = 0; i < names.size(); i++) if (names[i] == was) X.turn = static_cast<int>(i);
    }
    if (X.turn >= static_cast<int>(X.turnNames.size())) X.turn = 0;
}

ResWFourth resWFourth(App& app, int which = -1) {
    ResWFourth F;
    if (app.batPresent) {
        F.label = i18n::tr("Battery");
        F.level = app.batPct / 100.0;
        F.big = std::to_string(app.batPct);
        F.sub = app.batCharging ? i18n::tr("Charging") : "";
        F.icon = app.batCharging ? "battery-charge" : "battery-full";
        F.low = !app.batCharging && app.batPct <= app.cfg.batteryLow;
        return F;
    }
    ResW& X = app.resW;
    if (!X.turnNames.empty()) {
        const std::string& name = X.turnNames[which >= 0 && which < static_cast<int>(X.turnNames.size()) ? which : X.turn];
        for (const auto& p : app.periph) if (p.name == name) {
            F.label = p.name;
            F.icon = resWDeviceIcon(p.kind);
            F.level = p.pct / 100.0;
            F.big = std::to_string(p.pct);
            F.low = p.state != "charging" && p.state != "fully-charged" && p.pct <= app.cfg.peripheralLow;
            if (X.turnNames.size() > 1) F.sub = std::to_string(X.turn + 1) + "/" + std::to_string(X.turnNames.size());
            return F;
        }
    }
    static const bool hasNet = resWHasNetwork();
    if (hasNet) {
        F.label = i18n::tr("Network");
        F.icon = "transfer-horizontal";
        resWRate(X.netDown, F.big, F.unit);
        std::string un, uu;
        resWRate(X.netUp, un, uu);
        F.sub = "↓ " + F.big + " " + F.unit + "  ·  ↑ " + un + " " + uu;
        F.level = X.netPeak > 0 ? std::clamp(X.netDown / X.netPeak, 0.0, 1.0) : 0;
        return F;
    }
    F.label = i18n::tr("Uptime");
    F.icon = "clock-circle";
    F.flat = true;
    std::ifstream up("/proc/uptime");
    double secs = 0; up >> secs;
    const int h = static_cast<int>(secs / 3600), m = static_cast<int>(secs / 60) % 60, d = h / 24;
    if (d > 0) { F.big = std::to_string(d); F.unit = "d"; F.sub = std::to_string(h % 24) + " h " + std::to_string(m) + " min"; }
    else { F.big = std::to_string(h); F.unit = "h"; F.sub = std::to_string(m) + " min"; }
    return F;
}

void resWGrid(int W, int H, double gap, double pad, int& cols, int& rows) {
    double best = 1e9;
    cols = 2;
    rows = 2;
    for (int c : {2, 4, 1}) {
        const int r = (4 + c - 1) / c;
        const double tw = (W - 2 * pad - (c - 1) * gap) / c, th = (H - 2 * pad - (r - 1) * gap) / r;
        if (tw < 60 || th < 60) continue;
        const double score = std::fabs(std::log(tw / th));
        if (score < best - 1e-9) { best = score; cols = c; rows = r; }
    }
}

void resWPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    ResW& X = static_cast<ResW&>(d);
    roundedRect(cr, 0, 0, W, H, 32);
    setSource(cr, app.color("surface_container"), 0.94);
    cairo_fill(cr);

    const double pad = 10, gap = 8;
    int cols = 2, rows = 2;
    resWGrid(W, H, gap, pad, cols, rows);
    const double tw = (W - 2 * pad - (cols - 1) * gap) / cols, th = (H - 2 * pad - (rows - 1) * gap) / rows;

    const ResWFourth F = resWFourth(app);
    X.target[3] = F.flat ? 0 : F.level;
    const bool turning = X.turnAnim.fading;
    const std::string batLabel = F.label, batSub = F.sub, batIcon = F.icon;
    const bool batOk = F.ok, batLow = F.low;

    struct Tile { const char* label; const char* icon; const char* base; const char* on; bool warn; bool ok; std::string sub; };
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f of %.1f GB", X.ramUsedGb, X.ramTotalGb);
    const std::string ramSub = buf;
    std::snprintf(buf, sizeof buf, "%.0f of %.0f GB", X.diskUsedGb, X.diskTotalGb);
    const std::string diskSub = buf;
    if (X.cpuTempC >= 0) std::snprintf(buf, sizeof buf, "%.0f °C", X.cpuTempC);
    else std::snprintf(buf, sizeof buf, "%u cores", std::max(1u, std::thread::hardware_concurrency()));
    const std::string cpuSub = buf;
    const Tile tiles[4] = {
        {"CPU", "cpu", "primary_container", "on_primary_container", X.shown[0] * 100 >= app.cfg.resWarnCpu, true, cpuSub},
        {"Memory", "server-square", "secondary_container", "on_secondary_container", X.shown[1] * 100 >= app.cfg.resWarnMem, true, ramSub},
        {"Disk", "database", "tertiary_container", "on_tertiary_container", X.shown[2] >= 0.95, true, diskSub},
        {batLabel.c_str(), batIcon.c_str(), "primary_fixed", "on_primary_fixed", batLow, batOk, batSub},
    };
    const Tile prevTile = {X.prevName.c_str(), X.prevIcon.c_str(), "primary_fixed", "on_primary_fixed", false, X.prevOk, X.prevSub};

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](double px, bool bold) {
        PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
    };
    for (int i = 0; i < 4; i++) {
        const Tile& T = tiles[i];
        const double tx = pad + (i % cols) * (tw + gap), ty = pad + (i / cols) * (th + gap);
        const int passes = i == 3 && turning ? 2 : 1;
        for (int pass = 0; pass < passes; pass++) {
        const Tile& C = i == 3 && turning && pass == 0 ? prevTile : T;
        const double dx = i == 3 && turning ? (pass == 0 ? X.turnAnim.outDx() : X.turnAnim.inDx()) : 0;
        const double alphaMul = i == 3 && turning ? (pass == 0 ? X.turnAnim.outAlpha() : X.turnAnim.inAlpha()) : 1;
        cairo_save(cr);
        if (passes == 2) { roundedRect(cr, tx, ty, tw, th, 24); cairo_clip(cr); }
        cairo_translate(cr, dx, 0);
        const Color base = app.color(T.warn ? "error_container" : T.base), on = app.color(T.warn ? "on_error_container" : T.on);
        const double level = std::clamp(X.shown[i], 0.0, 1.0);
        if (pass == 0) {
            cairo_save(cr);
            cairo_translate(cr, -dx, 0);
            roundedRect(cr, tx, ty, tw, th, 24);
            ClipGroup cg1;
            beginClipGroup(cr, cg1);
            setSource(cr, base, 0.34);
            cairo_paint(cr);
            if (T.ok && level > 0) {
                setSource(cr, base);
                cairo_rectangle(cr, tx, ty + th * (1 - level), tw, th * level + 1);
                cairo_fill(cr);
            }
            endClipGroup(cr, cg1);
            cairo_restore(cr);
        }
        const double levelY = T.ok && level > 0 ? ty + th * (1 - level) : ty + th;
        const Color emptyInk = app.color("on_surface");
        auto inkText = [&](double x, double y, double alpha) {
            alpha *= alphaMul;
            cairo_save(cr);
            cairo_rectangle(cr, tx, ty, tw, levelY - ty);
            cairo_clip(cr);
            setSource(cr, emptyInk, alpha);
            cairo_move_to(cr, x, y);
            pango_cairo_show_layout(cr, layout);
            cairo_restore(cr);
            cairo_save(cr);
            cairo_rectangle(cr, tx, levelY, tw, ty + th - levelY);
            cairo_clip(cr);
            setSource(cr, on, alpha);
            cairo_move_to(cr, x, y);
            pango_cairo_show_layout(cr, layout);
            cairo_restore(cr);
        };

        const double ic = std::clamp(th * 0.2, 28.0, 40.0), p = std::clamp(th * 0.085, 10.0, 16.0);
        if (pass == 0) {
            cairo_save(cr); cairo_translate(cr, -dx, 0);
            cairo_new_path(cr);
            cairo_arc(cr, tx + p + ic / 2, ty + p + ic / 2, ic / 2, 0, 2 * M_PI);
            setSource(cr, app.color("surface_container_lowest"), 0.9);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        cairo_push_group(cr);
        drawIcon(cr, C.icon, tx + p + ic * 0.22, ty + p + ic * 0.22, static_cast<int>(ic * 0.56), app.color("on_surface"), true);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alphaMul);

        const double labelPx = std::clamp(th * 0.1, 12.0, 16.0), valPx = std::clamp(th * 0.27, 20.0, 46.0);
        const double textW = tw - 2 * p;
        font(labelPx, true);
        pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_text(layout, C.label, -1);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        const double labelY = ty + p + ic + std::clamp(th * 0.05, 6.0, 12.0);
        inkText(tx + p, labelY, 1.0);
        if (!C.sub.empty() && th > 120) {
            font(labelPx * 0.82, false);
            pango_layout_set_text(layout, C.sub.c_str(), -1);
            inkText(tx + p, labelY + lh + 1, 0.72);
        }
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        const bool show = C.ok;
        const int pct = static_cast<int>(std::lround(X.target[i] * 100));
        const std::string big = i == 3 ? (pass == 0 && turning ? (X.prevOk ? std::to_string(static_cast<int>(std::lround(X.prevVal * 100))) : "") : F.big) : std::to_string(pct);
        const std::string unit = i == 3 ? (pass == 0 && turning ? "%" : F.unit) : "%";
        font(valPx, true);
        pango_layout_set_text(layout, show ? big.c_str() : "", -1);
        int vw = 0, vh = 0;
        pango_layout_get_pixel_size(layout, &vw, &vh);
        const double vy = ty + th - p - vh + 2;
        inkText(tx + p, vy, 1.0);
        if (show) {
            font(valPx * 0.52, true);
            pango_layout_set_text(layout, unit.c_str(), -1);
            int pw = 0, ph = 0;
            pango_layout_get_pixel_size(layout, &pw, &ph);
            inkText(tx + p + vw + 2, vy + vh - ph - valPx * 0.12, 1.0);
        }
        cairo_restore(cr);
        }
    }
    g_object_unref(layout);
}

void resWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    ResW& X = app.resW;
    if (!X.open() || !X.configured) return;
    if (!X.primed || now > app.resLast + 2 * std::chrono::milliseconds(app.cfg.resInterval)) {
        updateResources(app);
        app.resLast = now - std::chrono::milliseconds(app.cfg.resInterval) + std::chrono::milliseconds(600);
        resWSync(app);
    }
    resWTurns(app);
    const bool rotating = !app.batPresent && X.turnNames.size() > 1;
    if (rotating) {
        if (X.turnAt == std::chrono::steady_clock::time_point{}) X.turnAt = now;
        if (now >= X.turnAt + std::chrono::seconds(kResWTurnSeconds) && !X.turnAnim.fading) {
            const ResWFourth was = resWFourth(app);
            X.prevName = was.label; X.prevIcon = was.icon; X.prevSub = was.sub; X.prevVal = was.level; X.prevOk = was.ok;
            X.turn = (X.turn + 1) % static_cast<int>(X.turnNames.size());
            X.turnAt = now;
            X.turnAnim.select(X.turnAnim.tab + 1, now);
            X.dirty = true;
        }
        if (X.turnAnim.fading) {
            X.turnAnim.stepNow(now);
            X.dirty = true;
            timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        } else {
            const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(X.turnAt + std::chrono::seconds(kResWTurnSeconds) - now).count());
            if (ms > 0) timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
        }
    } else { X.turnAt = {}; if (X.turnAnim.fading) { X.turnAnim.endFade(); X.dirty = true; } }
    if (!app.batPresent && X.turnNames.empty() && now >= X.netAt + std::chrono::milliseconds(std::max(1000, app.cfg.resInterval))) resWNetSample(app, now);
    const ResWFourth F = resWFourth(app);
    X.target[3] = F.flat ? 0 : F.level;
    char buf[240];
    std::snprintf(buf, sizeof buf, "%.0f|%.0f|%.0f|%.0f|%.0f|%.1f|%.0f|%s|%s|%s", X.target[0] * 100, X.target[1] * 100, X.target[2] * 100, X.target[3] * 100, X.cpuTempC, X.ramUsedGb, X.diskUsedGb, F.label.c_str(), F.big.c_str(), F.sub.c_str());
    if (X.key != buf || X.dirty) {
        X.key = buf;
        for (int i = 0; i < 4; i++) X.shown[i] = std::round(X.target[i] * 100) / 100;
        X.dirty = false;
        deskDraw(app, X);
    }
}

void resWToggled(App&, bool open) { (void)open; }

void resWInit(ResW& X) {
    X.name = "resources";
    X.minW = 200; X.maxW = 640;
    X.minH = 200; X.maxH = 640;
    X.w = 330; X.h = 330;
    X.radius = 32;
    X.blurCard = true;
    X.paint = resWPaint;
    X.onToggle = resWToggled;
}
