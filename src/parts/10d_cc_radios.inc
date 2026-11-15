
constexpr double kCcSlideS = 0.3;
constexpr double kCcListTop = 72, kCcListBottom = kCcH - kCcPad - 24;
constexpr double kRadioRowH = 64;
constexpr int kRadioPollMs = 5000;

void netbtRun(const char* action, const char* arg = nullptr) {
    const std::string script = assetsDir() + "/../scripts/netbt.sh";
    if (arg) runDetached({"bash", script.c_str(), action, arg});
    else runDetached({"bash", script.c_str(), action});
}

void ccWritePsk(const std::string& pw) {
    const int fd = open((ccCacheDir() + "/wifi-psk").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return;
    fchmod(fd, 0600);
    (void)!write(fd, pw.data(), pw.size());
    close(fd);
}

bool ccFileChanged(const std::string& path, long long& stamp, std::string& text) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return false;
    const long long ns = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
    if (ns == stamp) return false;
    stamp = ns;
    text = ccFileRead(path);
    return true;
}

// Keeps one row per network name: the connected one, else the strongest. Connected first, then by signal.
void ccSortAps(std::vector<CcAp>& aps) {
    std::vector<CcAp> uniq;
    for (const CcAp& a : aps) {
        auto it = std::find_if(uniq.begin(), uniq.end(), [&](const CcAp& u) { return u.ssid == a.ssid; });
        if (it == uniq.end()) uniq.push_back(a);
        else if (a.active && !it->active) *it = a;
        else if (!a.active && !it->active && a.strength > it->strength) *it = a;
    }
    std::stable_sort(uniq.begin(), uniq.end(), [](const CcAp& a, const CcAp& b) {
        if (a.active != b.active) return a.active;
        return a.strength > b.strength;
    });
    aps = std::move(uniq);
}

void ccSortDevs(std::vector<CcDev>& devs) {
    auto rank = [](const std::string& s) { return s == "connected" ? 0 : s == "paired" ? 1 : 2; };
    std::stable_sort(devs.begin(), devs.end(), [&](const CcDev& a, const CcDev& b) {
        if (rank(a.state) != rank(b.state)) return rank(a.state) < rank(b.state);
        return a.name < b.name;
    });
}

void ccRadioParseLists(CcRadio& R, const std::string& text) {
    R.aps.clear();
    R.devs.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "wifi") R.wifi = v;
        else if (k == "wifiIp") R.wifiIp = v;
        else if (k == "wifiScan") R.wifiScan = v == "1";
        else if (k == "bt") R.bt = v;
        else if (k == "btScan") R.btScan = v == "1";
        else if (k == "ap") {  // active, signal, secured, saved, ssid
            const auto f = splitTabs(v);
            if (f.size() >= 5) R.aps.push_back({f[4], std::atoi(f[1].c_str()), f[0] == "1", f[2] == "1", f[3] == "1"});
        } else if (k == "dev") {
            const auto f = splitTabs(v);
            if (f.size() >= 5) R.devs.push_back({f[0], f[4], f[2], f[1], f[3].empty() ? -1 : std::atoi(f[3].c_str())});
        }
    }
    ccSortAps(R.aps);
    ccSortDevs(R.devs);
}

std::map<std::string, std::string> ccParseKv(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

std::string ccActError(const std::string& action, const std::string& status) {
    if (action == "bt-pair") return "Could not pair.";
    if (action == "wifi-connect" || action == "wifi-password") return status == "secrets" ? "Wrong password or the network is out of range." : "Could not connect.";
    if (action == "wifi-down" || action == "bt-disconnect") return "Could not disconnect.";
    if (action == "bt-forget") return "Could not forget the device.";
    return "Could not connect.";
}

void ccDlgOpen(CcRadio& R, int kind, const std::string& target) {
    R.dialog = kind;
    R.dlgTarget = target;
    R.dlgError.clear();
    R.text.clear();
    R.dlgBusy = false;
    R.hover.clear();
}
void ccDlgClose(CcRadio& R) {
    R.dialog = CcDlgNone;
    R.dlgTarget.clear();
    R.dlgError.clear();
    R.text.clear();
    R.dlgBusy = false;
    R.hover.clear();
}

void ccRadioOnAct(App& app) {
    CcRadio& R = app.cc.radio;
    if (R.actStatus == "busy") return;
    const bool ok = R.actStatus == "ok";
    if (R.dialog != CcDlgNone && R.dlgBusy && R.actTarget == R.dlgTarget) {
        R.dlgBusy = false;
        if (ok) { ccDlgClose(R); return; }
        R.dlgError = R.dialog == CcDlgWifiPassword ? "Wrong password or the network is out of range." : ccActError(R.actAction, R.actStatus);
        R.text.clear();
        return;
    }
    if (ok) { R.note.clear(); return; }
    if (R.actAction == "wifi-connect" && R.actStatus == "secrets" && R.dialog == CcDlgNone) {
        ccDlgOpen(R, CcDlgWifiPassword, R.actTarget);
        return;
    }
    R.note = ccActError(R.actAction, R.actStatus);
    R.noteErr = true;
}

bool ccRadioRead(App& app) {
    CcRadio& R = app.cc.radio;
    std::string text;
    bool changed = false;
    if (ccFileChanged(ccCacheDir() + "/netbt.txt", R.listStamp, text)) { ccRadioParseLists(R, text); changed = true; }
    if (ccFileChanged(ccCacheDir() + "/netbt-act.txt", R.actStamp, text)) {
        const auto kv = ccParseKv(text);
        const std::string id = kv.count("id") ? kv.at("id") : "";
        if (!id.empty() && id != R.actId) {
            const bool first = R.actId.empty();
            R.actId = id;
            R.actAction = kv.count("action") ? kv.at("action") : "";
            R.actTarget = kv.count("target") ? kv.at("target") : "";
            R.actStatus = kv.count("status") ? kv.at("status") : "";
            if (!first) ccRadioOnAct(app);
        }
        changed = true;
    }
    return changed;
}

void ccOpenPage(App& app, int page) {
    if (!app.cc.open()) openControlCenter(app);
    ccOpenRadio(app, page);
}

void ccOpenRadio(App& app, int page) {
    ControlCenter& C = app.cc;
    if (C.pageOpen && C.page == page) return;
    if (C.page != page) { C.radio.dialog = CcDlgNone; C.radio.scroll = 0; C.radio.note.clear(); }
    C.page = page;
    C.pageOpen = true;
    C.hover = 0;
    C.radio.hover.clear();
    C.radio.nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRadioPollMs);
    if (page == CcPageWifi) netbtRun("scan", "wifi");
    else netbtRun("list");
    ccRadioRead(app);
    drawControlCenter(app);
}
void ccCloseRadio(App& app) {
    ControlCenter& C = app.cc;
    C.pageOpen = false;
    C.radio.hover.clear();
    ccDlgClose(C.radio);
}

void ccRadioTick(App& app, std::chrono::steady_clock::time_point before, int& timeoutMs) {
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    const double target = C.pageOpen ? 1.0 : 0.0;
    if (C.slide != target) {
        const double dt = C.slideAt == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(before - C.slideAt).count());
        C.slide = target > C.slide ? std::min(target, C.slide + dt / kCcSlideS) : std::max(target, C.slide - dt / kCcSlideS);
        C.slideAt = before;
        if (C.slide == 0 && !C.pageOpen) { C.page = CcPageNone; C.slideAt = {}; }
        timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        drawControlCenter(app);
    } else {
        C.slideAt = {};
    }
    if (R.bubbleMoving) { drawControlCenter(app); timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16); }
    if (C.pageOpen && C.page != CcPageNone) {
        if (before >= R.nextPoll) { netbtRun("list"); R.nextPoll = before + std::chrono::milliseconds(kRadioPollMs); }
        if (ccRadioRead(app)) drawControlCenter(app);
        timeoutMs = timeoutMs < 0 ? 400 : std::min(timeoutMs, 400);
    }
}

int ccHitIndex(const CcRadio& R, double x, double y) {
    for (size_t i = R.hits.size(); i-- > 0;)
        if (R.hits[i].r.contains(x, y)) return static_cast<int>(i);
    return -1;
}

void ccRadioRow(App& app, const std::string& target, bool details) {
    CcRadio& R = app.cc.radio;
    if (app.cc.page == CcPageWifi) {
        const auto it = std::find_if(R.aps.begin(), R.aps.end(), [&](const CcAp& a) { return a.ssid == target; });
        if (it == R.aps.end()) return;
        if (details || it->active) { ccDlgOpen(R, CcDlgWifiDetails, it->ssid); return; }
        if (it->secure && !it->saved) { ccDlgOpen(R, CcDlgWifiPassword, it->ssid); return; }
        R.note.clear();
        netbtRun("wifi-connect", it->ssid.c_str());
        return;
    }
    const auto it = std::find_if(R.devs.begin(), R.devs.end(), [&](const CcDev& d) { return d.mac == target; });
    if (it == R.devs.end()) return;
    if (details) { ccDlgOpen(R, CcDlgBtDetails, it->mac); return; }
    if (it->state == "new") { ccDlgOpen(R, CcDlgBtPair, it->mac); return; }
    R.note.clear();
    netbtRun(it->state == "connected" ? "bt-disconnect" : "bt-connect", it->mac.c_str());
}

void ccDlgButton(App& app, int id) {
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    if (id == CcDbCancel || id == CcDbDone) { ccDlgClose(R); return; }
    if (id == CcDbSettings) { closeControlCenter(app); runDetached({"nm-connection-editor"}); return; }
    const std::string target = R.dlgTarget;
    const auto dev = std::find_if(R.devs.begin(), R.devs.end(), [&](const CcDev& d) { return d.mac == target; });
    if (id == CcDbSubmit) {
        if (R.dialog == CcDlgWifiPassword) {
            if (R.text.empty()) return;
            ccWritePsk(R.text);
            netbtRun("wifi-password", target.c_str());
        } else if (R.dialog == CcDlgBtPair) {
            netbtRun("bt-pair", target.c_str());
        } else if (R.dialog == CcDlgBtDetails && dev != R.devs.end()) {
            netbtRun(dev->state == "connected" ? "bt-disconnect" : dev->state == "paired" ? "bt-connect" : "bt-pair", target.c_str());
        } else {
            return;
        }
    } else if (id == CcDbDisconnect) {
        netbtRun("wifi-down", target.c_str());
    } else if (id == CcDbForget) {
        netbtRun("bt-forget", target.c_str());
    } else {
        return;
    }
    R.dlgBusy = true;
    R.dlgError.clear();
}

void ccRadioMotion(App& app) {
    ControlCenter& C = app.cc;
    if (!C.pageOpen || C.slide < 1) return;
    const int h = ccHitIndex(C.radio, app.pointerX - kCcRoom, app.pointerY - kCcRoom);
    const std::string key = h >= 0 ? C.radio.hits[h].key() : std::string();
    if (key == C.radio.hover) return;
    C.radio.hover = key;
    drawControlCenter(app);
    updateCursor(app);
}

void ccRadioClick(App& app) {
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    if (!C.pageOpen || C.slide < 1) return;
    const double x = app.pointerX - kCcRoom, y = app.pointerY - kCcRoom;
    const int h = ccHitIndex(R, x, y);
    if (h < 0) {
        if (R.dialog != CcDlgNone && !R.dlgBox.contains(x, y)) ccDlgClose(R);
    } else {
        const CcHit hit = R.hits[h];
        const bool wifi = C.page == CcPageWifi;
        switch (hit.kind) {
            case CcHkBack: ccCloseRadio(app); break;
            case CcHkScan: netbtRun("scan", wifi ? "wifi" : "bt"); break;
            case CcHkSwitch: ccRunScript("toggle", wifi ? "wifi" : "bt"); break;
            case CcHkRow: ccRadioRow(app, hit.target, false); break;
            case CcHkInfo: ccRadioRow(app, hit.target, true); break;
            case CcHkButton: ccDlgButton(app, hit.id); break;
            default: break;
        }
    }
    drawControlCenter(app);
}

void ccRadioScroll(App& app, int dir) {
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    if (!C.pageOpen || C.slide < 1 || R.dialog != CcDlgNone) return;
    const double count = static_cast<double>(C.page == CcPageWifi ? R.aps.size() : R.devs.size());
    const double content = count * kRadioRowH, view = kCcListBottom - kCcListTop;
    R.scroll = std::clamp(R.scroll + dir * 48.0, 0.0, std::max(0.0, content - view));
    R.hover.clear();
    drawControlCenter(app);
}

bool ccRadioKey(App& app, uint32_t key) {
    constexpr uint32_t kEsc = 1, kBackspace = 14, kEnter = 28, kKpEnter = 96;
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    if (!C.pageOpen || C.slide < 1) return C.pageOpen;
    if (key == kEsc) {
        if (R.dialog != CcDlgNone) ccDlgClose(R);
        else ccCloseRadio(app);
        drawControlCenter(app);
        return true;
    }
    if (R.dialog == CcDlgNone) return true;
    if (key == kEnter || key == kKpEnter) {
        if (!R.dlgBusy) ccDlgButton(app, CcDbSubmit);
        drawControlCenter(app);
        return true;
    }
    if (R.dialog != CcDlgWifiPassword || R.dlgBusy || !app.xkbState) return true;
    if (key == kBackspace) {
        if (!R.text.empty()) {
            size_t n = R.text.size() - 1;
            while (n > 0 && (static_cast<unsigned char>(R.text[n]) & 0xC0) == 0x80) n--;  // a whole UTF-8 letter
            R.text.resize(n);
            drawControlCenter(app);
        }
        return true;
    }
    if (xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) return true;
    char buf[16];
    const int n = xkb_state_key_get_utf8(app.xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && R.text.size() < 256) {
        R.text.append(buf, n);
        R.dlgError.clear();
        drawControlCenter(app);
    }
    return true;
}
