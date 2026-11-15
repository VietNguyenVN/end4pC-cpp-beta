
constexpr double kRdDlgW = 336, kRdDlgPad = 24, kRdDlgInner = kRdDlgW - 2 * kRdDlgPad;

struct RdSize { double w = 0, h = 0; };

RdSize rdText(cairo_t* cr, PangoLayout* L, const std::string& t, const char* font, double x, double y, const Color& c, double a = 1, double w = 0, int align = 0, bool wrap = false) {
    PangoFontDescription* f = pango_font_description_from_string(font);
    pango_layout_set_font_description(L, f);
    pango_font_description_free(f);
    pango_layout_set_text(L, t.c_str(), -1);
    pango_layout_set_width(L, w > 0 ? static_cast<int>(w * PANGO_SCALE) : -1);
    pango_layout_set_ellipsize(L, w > 0 && !wrap ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
    pango_layout_set_wrap(L, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(L, align == 1 ? PANGO_ALIGN_CENTER : align == 2 ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(L, &tw, &th);
    if (cr) {
        setSource(cr, c, a);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, L);
    }
    return {static_cast<double>(tw), static_cast<double>(th)};
}

struct RdCtx {
    App& app;
    cairo_t* cr;
    PangoLayout* L;
    ControlCenter& C;
    CcRadio& R;
    bool wifi;
    bool hitsOn = true;
};

bool rdHit(RdCtx& g, Rect r, int kind, int id, const std::string& target, const char* tip) {
    if (!g.hitsOn) return false;
    CcHit h{r, kind, id, target, tip ? tip : ""};
    const bool hot = g.R.hover == h.key();
    g.R.hits.push_back(std::move(h));
    return hot;
}

void rdIconBtn(RdCtx& g, const Rect& r, const char* icon, bool hot) {
    cairo_new_sub_path(g.cr);
    cairo_arc(g.cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
    setSource(g.cr, g.app.color(hot ? "surface_container_highest" : "surface_container_high"));
    cairo_fill(g.cr);
    drawIcon(g.cr, icon, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, g.app.color("on_surface"), hot);
}

void rdSwitch(RdCtx& g, double x, double y, bool on, bool hot) {
    cairo_t* cr = g.cr;
    if (on) {
        roundedRect(cr, x, y, 52, 32, 16);
        setSource(cr, g.app.color("primary"));
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, 2);
        roundedRect(cr, x + 1, y + 1, 50, 30, 15);
        setSource(cr, g.app.color("surface_container_highest"));
        cairo_fill_preserve(cr);
        setSource(cr, g.app.color("outline"));
        cairo_stroke(cr);
    }
    const double r = on ? 12 : 8, cx = on ? x + 36 : x + 16, cy = y + 16;
    if (hot) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 20, 0, 2 * M_PI);
        setSource(cr, g.app.color(on ? "primary" : "on_surface"), 0.08);
        cairo_fill(cr);
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    setSource(cr, g.app.color(on ? "on_primary" : "outline"));
    cairo_fill(cr);
}

void rdSignal(cairo_t* cr, double x, double y, int strength, const Color& c) {
    const int lit = strength > 80 ? 4 : strength > 60 ? 3 : strength > 40 ? 2 : strength > 20 ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        const double bh = 7 + i * 5;
        roundedRect(cr, x + 2 + i * 5, y + 22 - bh, 4, bh, 1.5);
        setSource(cr, c, i < lit ? 1 : 0.3);
        cairo_fill(cr);
    }
}

const char* rdDeviceIcon(const std::string& bluez) {
    if (bluez.find("headset") != std::string::npos || bluez.find("headphone") != std::string::npos) return "headphones-round";
    if (bluez.find("speaker") != std::string::npos || bluez.find("audio") != std::string::npos) return "speaker";
    if (bluez == "phone") return "smartphone";
    if (bluez == "computer") return "laptop";
    if (bluez.find("mouse") != std::string::npos) return "mouse";
    if (bluez.find("keyboard") != std::string::npos) return "keyboard";
    if (bluez.find("gaming") != std::string::npos) return "gamepad";
    if (bluez == "watch") return "watch-round";
    return "bluetooth";
}

std::string rdBusyText(const std::string& action) {
    if (action == "bt-pair") return "Pairing...";
    if (action == "bt-disconnect" || action == "wifi-down") return "Disconnecting...";
    if (action == "bt-forget") return "Removing...";
    return "Connecting...";
}

void rdHeader(RdCtx& g) {
    const Color onSurf = g.app.color("on_surface"), variant = g.app.color("on_surface_variant");
    const CcRadio& R = g.R;
    const bool radioOn = g.wifi ? R.wifi == "on" : R.bt == "on";
    std::string sub;
    if (g.wifi ? R.wifiScan : R.btScan) sub = "Scanning...";
    else if (!radioOn) sub = "Off";
    else if (g.wifi) {
        const auto it = std::find_if(R.aps.begin(), R.aps.end(), [](const CcAp& a) { return a.active; });
        sub = it != R.aps.end() ? it->ssid : "Not connected";
    } else {
        const auto it = std::find_if(R.devs.begin(), R.devs.end(), [](const CcDev& d) { return d.state == "connected"; });
        sub = it != R.devs.end() ? it->name : "Not connected";
    }
    const Rect back{16, 18, 40, 40}, scan{276, 18, 40, 40};
    rdIconBtn(g, back, "arrow-left", rdHit(g, back, CcHkBack, 0, "", "Back"));
    const RdSize t = rdText(g.cr, g.L, g.wifi ? "Wi-Fi" : "Bluetooth", "Sans Bold 13", 64, 18, onSurf, 1, 204);
    rdText(g.cr, g.L, sub, "Sans 9", 64, 18 + t.h + 1, variant, 1, 204);
    rdIconBtn(g, scan, g.wifi ? "refresh" : "radar", rdHit(g, scan, CcHkScan, 0, "", g.wifi ? "Scan for networks" : "Scan for devices"));
    rdSwitch(g, 324, 22, radioOn, rdHit(g, {324, 22, 52, 32}, CcHkSwitch, 0, "", "Turn on or off"));
}

bool rdRowFrame(RdCtx& g, double y, const std::string& target, bool active, const char* tip) {
    const double top = std::max(y, kCcListTop), bottom = std::min(y + 60, kCcListBottom);
    const bool hot = rdHit(g, {16, top, 360, bottom - top}, CcHkRow, 0, target, tip);
    roundedRect(g.cr, 16, y, 360, 60, 20);
    if (active) setSource(g.cr, g.app.color("secondary_container"));
    else if (hot) setSource(g.cr, g.app.color("on_surface"), 0.08);
    else { cairo_new_path(g.cr); return hot; }
    cairo_fill(g.cr);
    return hot;
}

void rdInfoBtn(RdCtx& g, double x, double y, const Color& ink, bool hot) {
    if (hot) {
        cairo_new_sub_path(g.cr);
        cairo_arc(g.cr, x + 20, y + 20, 20, 0, 2 * M_PI);
        setSource(g.cr, ink, 0.12);
        cairo_fill(g.cr);
    }
    drawIcon(g.cr, "info-circle", x + 9, y + 9, 22, ink);
}

void rdApRow(RdCtx& g, const CcAp& a, double y, bool busy) {
    rdRowFrame(g, y, a.ssid, a.active, a.active ? "Details" : "Connect");
    const Color ink = g.app.color(a.active ? "on_secondary_container" : "on_surface"), sub = g.app.color(a.active ? "on_secondary_container" : "on_surface_variant");
    rdSignal(g.cr, 32, y + 18, a.strength, ink);
    const double trail = a.active ? 328 : 348;
    const double tw = trail - 8 - 70;
    const RdSize t = rdText(g.cr, g.L, a.ssid, "Sans Bold 11", 70, y + 11, ink, 1, tw);
    const std::string state = busy ? rdBusyText(g.R.actAction) : a.active ? "Connected" : a.secure ? "Secured" : "Open";
    rdText(g.cr, g.L, state, "Sans 9", 70, y + 11 + t.h + 1, sub, 1, tw);
    if (a.active) {
        rdInfoBtn(g, 328, y + 10, ink, rdHit(g, {328, y + 10, 40, 40}, CcHkInfo, 0, a.ssid, "Details"));
    } else if (a.secure) {
        drawIcon(g.cr, "lock-keyhole", 348, y + 22, 16, sub);
    }
}

void rdDevRow(RdCtx& g, const CcDev& d, double y, bool busy) {
    const bool active = d.state == "connected";
    rdRowFrame(g, y, d.mac, active, d.state == "new" ? "Pair" : active ? "Disconnect" : "Connect");
    const Color ink = g.app.color(active ? "on_secondary_container" : "on_surface"), sub = g.app.color(active ? "on_secondary_container" : "on_surface_variant");
    drawIcon(g.cr, rdDeviceIcon(d.icon), 32, y + 18, 24, ink);
    const bool batt = d.battery >= 0;
    const double trail = batt ? 282 : 328, tw = trail - 8 - 70;
    const RdSize t = rdText(g.cr, g.L, d.name, "Sans Bold 11", 70, y + 11, ink, 1, tw);
    const std::string state = busy ? rdBusyText(g.R.actAction) : active ? "Connected" : d.state == "paired" ? "Paired" : "Not paired";
    rdText(g.cr, g.L, state, "Sans 9", 70, y + 11 + t.h + 1, sub, 1, tw);
    if (batt) rdText(g.cr, g.L, std::to_string(d.battery) + "%", "Sans 9", 282, y + 24, sub, 1, 40, 2);
    rdInfoBtn(g, 328, y + 10, ink, rdHit(g, {328, y + 10, 40, 40}, CcHkInfo, 0, d.mac, "Details"));
}

void rdEmpty(RdCtx& g, const char* title, const char* sub) {
    const double mid = (kCcListTop + kCcListBottom) / 2 - 24;
    const RdSize t = rdText(g.cr, g.L, title, "Sans Bold 11", 16, mid, g.app.color("on_surface"), 1, 360, 1);
    rdText(g.cr, g.L, sub, "Sans 9", 16, mid + t.h + 4, g.app.color("on_surface_variant"), 1, 360, 1, true);
}

void rdList(RdCtx& g) {
    const CcRadio& R = g.R;
    const bool radioOn = g.wifi ? R.wifi == "on" : R.bt == "on";
    const bool scanning = g.wifi ? R.wifiScan : R.btScan;
    const size_t count = g.wifi ? R.aps.size() : R.devs.size();
    if (!radioOn) {
        if (g.wifi) rdEmpty(g, "Wi-Fi is off", "Turn it on with the switch above.");
        else rdEmpty(g, "Bluetooth is off", "Turn it on with the switch above.");
    } else if (count == 0) {
        if (g.wifi) rdEmpty(g, "No networks found", scanning ? "Scanning..." : "Scan again with the button above.");
        else rdEmpty(g, "No devices found", scanning ? "Scanning..." : "Scan for new devices with the button above.");
    } else {
        cairo_save(g.cr);
        cairo_rectangle(g.cr, 0, kCcListTop, kCcW, kCcListBottom - kCcListTop);
        cairo_clip(g.cr);
        for (size_t i = 0; i < count; i++) {
            const double y = kCcListTop + i * kRadioRowH - R.scroll;
            if (y + 60 <= kCcListTop || y >= kCcListBottom) continue;
            if (g.wifi) {
                const CcAp& a = R.aps[i];
                rdApRow(g, a, y, R.actStatus == "busy" && R.actTarget == a.ssid);
            } else {
                const CcDev& d = R.devs[i];
                rdDevRow(g, d, y, R.actStatus == "busy" && R.actTarget == d.mac);
            }
        }
        cairo_restore(g.cr);
    }
    if (!R.note.empty()) {
        rdText(g.cr, g.L, R.note, "Sans 9", 16, kCcListBottom + 6, g.app.color(R.noteErr ? "error" : "on_surface_variant"), 1, 360, 1);
    }
}

void ccRadioBubble(App& app, cairo_t* cr) {
    ControlCenter& C = app.cc;
    CcRadio& R = C.radio;
    const CcHit* under = nullptr;
    if (C.pageOpen && C.slide >= 1 && R.dialog == CcDlgNone && !R.hover.empty())
        for (const CcHit& h : R.hits) if (h.key() == R.hover && !h.tip.empty()) under = &h;
    if (under) bubbleShow(R.bubble, under->key(), under->tip, under->r.x, under->r.y, under->r.w, under->r.h, kCcW, kCcH);
    else bubbleHide(R.bubble);
    const Color bg = app.color("inverse_surface"), fg = app.color("inverse_on_surface");
    R.bubbleMoving = bubbleDraw(cr, R.bubble, {bg.r, bg.g, bg.b}, {fg.r, fg.g, fg.b});
}

struct RdButton { std::string label; int id; bool primary = false, danger = false, enabled = true, left = false; };
struct RdDialog {
    std::string title, support, bold, error;
    std::vector<std::pair<std::string, std::string>> rows;
    bool field = false;
    std::vector<RdButton> buttons;
};

RdDialog rdDialogOf(const ControlCenter& C) {
    const CcRadio& R = C.radio;
    const bool busy = R.dlgBusy;
    RdDialog d;
    d.error = R.dlgError;
    if (R.dialog == CcDlgWifiPassword) {
        d.title = "Wi-Fi password";
        d.support = "Password for the network:";
        d.bold = R.dlgTarget;
        d.field = true;
        d.buttons = {{"Cancel", CcDbCancel}, {busy ? "Connecting..." : "Connect", CcDbSubmit, true, false, !busy}};
    } else if (R.dialog == CcDlgWifiDetails) {
        const auto it = std::find_if(R.aps.begin(), R.aps.end(), [&](const CcAp& a) { return a.ssid == R.dlgTarget; });
        const bool secure = it != R.aps.end() && it->secure;
        d.title = R.dlgTarget;
        d.rows = {{"Signal", (it != R.aps.end() ? std::to_string(it->strength) : std::string("-")) + "%"},
                  {"Security", secure ? "Secured" : "Open"},
                  {"Address", R.wifiIp.empty() ? "-" : R.wifiIp}};
        d.buttons = {{"Settings", CcDbSettings, false, false, true, true}, {"Disconnect", CcDbDisconnect, false, false, !busy}, {"Done", CcDbDone}};
    } else if (R.dialog == CcDlgBtPair) {
        const auto it = std::find_if(R.devs.begin(), R.devs.end(), [&](const CcDev& x) { return x.mac == R.dlgTarget; });
        d.title = "Pair device";
        d.support = "Put the device in pairing mode, then pair with:";
        d.bold = it != R.devs.end() ? it->name : R.dlgTarget;
        d.buttons = {{"Cancel", CcDbCancel}, {busy ? rdBusyText(R.actAction) : "Pair", CcDbSubmit, true, false, !busy}};
    } else if (R.dialog == CcDlgBtDetails) {
        const auto it = std::find_if(R.devs.begin(), R.devs.end(), [&](const CcDev& x) { return x.mac == R.dlgTarget; });
        const CcDev dev = it != R.devs.end() ? *it : CcDev{R.dlgTarget, R.dlgTarget, "", "new", -1};
        const bool paired = dev.state != "new";
        d.title = dev.name;
        d.rows.push_back({"Status", dev.state == "connected" ? "Connected" : paired ? "Paired" : "Not paired"});
        if (dev.battery >= 0) d.rows.push_back({"Battery", std::to_string(dev.battery) + "%"});
        d.rows.push_back({"Address", dev.mac});
        const char* submit = dev.state == "connected" ? "Disconnect" : paired ? "Connect" : "Pair";
        if (paired) d.buttons.push_back({"Forget", CcDbForget, false, true, !busy, true});
        d.buttons.push_back({busy ? rdBusyText(R.actAction) : submit, CcDbSubmit, true, false, !busy});
        d.buttons.push_back({"Done", CcDbDone});
    }
    return d;
}

void rdDialogField(RdCtx& g, double x, double y, double w);
void rdDialogButtons(RdCtx& g, const RdDialog& d, double X, double y);

double rdDialogPass(RdCtx& g, const RdDialog& d, double X, double top, bool draw) {
    cairo_t* cr = draw ? g.cr : nullptr;
    const double x = X + kRdDlgPad, inner = kRdDlgInner;
    const Color onSurf = g.app.color("on_surface"), variant = g.app.color("on_surface_variant");
    double y = top + kRdDlgPad;
    y += rdText(cr, g.L, d.title, "Sans Bold 15", x, y, onSurf, 1, inner).h;
    if (!d.support.empty()) y += 8 + rdText(cr, g.L, d.support, "Sans 10", x, y + 8, variant, 1, inner, 0, true).h;
    if (!d.bold.empty()) y += 6 + rdText(cr, g.L, d.bold, "Sans Bold 11", x, y + 6, onSurf, 1, inner).h;
    if (d.field) {
        y += 14;
        if (draw) rdDialogField(g, x, y, inner);
        y += 52;
    }
    if (!d.rows.empty()) {
        y += 12;
        for (const auto& row : d.rows) {
            if (draw) {
                rdText(cr, g.L, row.first, "Sans 10", x, y, variant, 1, inner);
                rdText(cr, g.L, row.second, "Sans 10", x, y, onSurf, 1, inner, 2);
            }
            y += 22;
        }
    }
    if (!d.error.empty()) y += 8 + rdText(cr, g.L, d.error, "Sans 9", x, y + 8, g.app.color("error"), 1, inner, 0, true).h;
    y += kRdDlgPad;
    if (draw) rdDialogButtons(g, d, X, y);
    y += 40 + kRdDlgPad;
    return y - top;
}

void rdDialogField(RdCtx& g, double x, double y, double w) {
    cairo_t* cr = g.cr;
    roundedRectCorners(cr, x, y, w, 52, 4, 4, 0, 0);
    setSource(cr, g.app.color("surface_container_highest"));
    cairo_fill(cr);
    cairo_rectangle(cr, x, y + 50, w, 2);
    setSource(cr, g.app.color("primary"));
    cairo_fill(cr);
    const std::string& pw = g.R.text;
    size_t letters = 0;
    for (unsigned char c : pw) if ((c & 0xC0) != 0x80) letters++;
    const size_t dots = std::min(letters, static_cast<size_t>((w - 32) / 14));
    if (pw.empty()) {
        rdText(cr, g.L, "Password", "Sans 11", x + 20, y + 17, g.app.color("on_surface_variant"), 0.8, w - 36);
    } else {
        setSource(cr, g.app.color("on_surface"));
        for (size_t i = 0; i < dots; i++) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + 20 + i * 14.0, y + 26, 4, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    cairo_rectangle(cr, x + 16 + dots * 14.0, y + 16, 2, 20);
    setSource(cr, g.app.color("primary"));
    cairo_fill(cr);
}

void rdDialogButton(RdCtx& g, const RdButton& b, double x, double y, double w, double h) {
    cairo_t* cr = g.cr;
    const bool hot = b.enabled && rdHit(g, {x, y, w, h}, CcHkButton, b.id, "", nullptr);
    if (b.primary) {
        roundedRect(cr, x, y, w, h, h / 2);
        setSource(cr, g.app.color(b.enabled ? "primary" : "on_surface"), b.enabled ? 1 : 0.12);
        cairo_fill(cr);
    } else if (hot) {
        roundedRect(cr, x, y, w, h, h / 2);
        setSource(cr, g.app.color("primary"), 0.12);
        cairo_fill(cr);
    }
    const Color ink = g.app.color(b.primary ? (b.enabled ? "on_primary" : "on_surface") : b.danger ? "error" : "primary");
    const RdSize t = rdText(nullptr, g.L, b.label, "Sans Bold 11", 0, 0, ink);
    rdText(cr, g.L, b.label, "Sans Bold 11", x + (w - t.w) / 2, y + (h - t.h) / 2, ink, b.enabled ? 1 : 0.38);
}

void rdDialogButtons(RdCtx& g, const RdDialog& d, double X, double y) {
    constexpr double kBtnH = 40, kGap = 8, kBtnPad = 14;
    std::vector<double> widths;
    double rightTotal = 0;
    int rightCount = 0;
    for (const RdButton& b : d.buttons) {
        widths.push_back(rdText(nullptr, g.L, b.label, "Sans Bold 11", 0, 0, g.app.color("on_surface")).w + 2 * kBtnPad);
        if (!b.left) { rightTotal += widths.back(); rightCount++; }
    }
    double leftX = X + kRdDlgPad, rightX = X + kRdDlgW - kRdDlgPad - rightTotal - kGap * std::max(0, rightCount - 1);
    for (size_t i = 0; i < d.buttons.size(); i++) {
        const RdButton& b = d.buttons[i];
        double& cursor = b.left ? leftX : rightX;
        rdDialogButton(g, b, cursor, y, widths[i], kBtnH);
        cursor += widths[i] + kGap;
    }
}

void rdDialogDraw(RdCtx& g) {
    const RdDialog d = rdDialogOf(g.C);
    const double X = (kCcW - kRdDlgW) / 2;
    g.hitsOn = true;
    cairo_set_source_rgba(g.cr, 0, 0, 0, 0.32);
    cairo_rectangle(g.cr, 0, 0, kCcW, kCcH);
    cairo_fill(g.cr);
    const double H = rdDialogPass(g, d, X, 0, false);
    const double top = (kCcH - H) / 2;
    g.R.dlgBox = {X, top, kRdDlgW, H};
    roundedRect(g.cr, X, top, kRdDlgW, H, 28);
    setSource(g.cr, g.app.color("surface_container_high"));
    cairo_fill(g.cr);
    rdDialogPass(g, d, X, top, true);
}

void ccDrawRadioPage(App& app, cairo_t* cr, PangoLayout* layout, double x0) {
    ControlCenter& C = app.cc;
    const bool dlg = C.radio.dialog != CcDlgNone;
    C.radio.hits.clear();
    RdCtx g{app, cr, layout, C, C.radio, C.page == CcPageWifi, !dlg};
    cairo_save(cr);
    cairo_translate(cr, x0, 0);
    rdHeader(g);
    rdList(g);
    if (dlg) rdDialogDraw(g);
    cairo_restore(cr);
}
