
constexpr double kBtRowH = 64, kBtRowGap = kClkGap, kBtRadius = kClkRadius, kBtRowRadius = 20, kBtDisconnectD = 36, kBtScreenMargin = 200;
constexpr double kBtFootH = 52;
constexpr int kBtStatusPollS = 10, kBtListPollS = 3;

bool btShown(const App& app) {
    for (const auto* l : {&app.cfg.layoutLeft, &app.cfg.layoutCenter, &app.cfg.layoutRight, &app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight})
        if (std::find(l->begin(), l->end(), "bluetooth") != l->end()) return true;
    return false;
}

void btReadStatus(App& app) {
    BtBar& B = app.btBar;
    const std::string path = ccCacheDir() + "/cc.txt";
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || st.st_mtime == B.stamp) return;
    B.stamp = st.st_mtime;
    std::istringstream in(ccFileRead(path));
    std::string line, bt = "absent", name;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "bt") bt = v;
        else if (k == "btName") name = v;
    }
    const bool changed = bt != B.bt || name != B.name;
    B.bt = bt;
    B.name = bt == "on" ? name : "";
    if (changed) {
        if (app.btMenu.open()) app.btMenu.radio.bt = bt;
        drawAll(app);
    }
}

void btAskStatus(App& app, std::chrono::steady_clock::time_point now) {
    runDetached({"bash", (assetsDir() + "/../scripts/cc.sh").c_str(), "status"});
    app.btBar.nextRead = now + std::chrono::milliseconds(500);
}

void btTogglePower(App& app) {
    const auto now = std::chrono::steady_clock::now();
    runDetached({"bash", (assetsDir() + "/../scripts/cc.sh").c_str(), "toggle", "bt"});
    app.btBar.nextRead = now + std::chrono::milliseconds(1200);
    app.btMenu.nextList = now + std::chrono::milliseconds(1200);
}

std::chrono::steady_clock::time_point btWake(const App& app) {
    const auto never = std::chrono::steady_clock::time_point::max();
    auto when = never;
    const BtBar& B = app.btBar;
    if (btShown(app)) when = std::min(when, B.nextPoll);
    if (B.nextRead != std::chrono::steady_clock::time_point{}) when = std::min(when, B.nextRead);
    if (app.btMenu.open()) {
        when = std::min(when, app.btMenu.nextList);
        if (app.btMenu.configured) when = std::min(when, popWake(app.btMenu.anim.phase, app.btMenu.anim.settleAt, std::chrono::steady_clock::now()));
    }
    return when;
}

// The M3 look: a pill with the Bluetooth icon on a disc and, with a device connected, its name beside it (as the updates and the keyboard widgets).
constexpr double kBtM3NameMax = 110, kBtM3Gap = 6;
double btM3NameW(const App& app) {
    const BtBar& B = app.btBar;
    if (B.bt != "on" || B.name.empty()) return 0;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* f = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, B.name.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    pango_font_description_free(f);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return std::min<double>(tw, kBtM3NameMax);
}
double btBarW(const App& app) {
    if (widgetStyle(app, "bluetooth") != "m3" || sideBar(app)) return kQuickBox + softExtra(app, "bluetooth");
    const double name = btM3NameW(app);
    return name > 0 ? kM3Inset + kM3Disc + kBtM3Gap + name + kModulePad : kQuickBox;
}
void btM3Paint(cairo_t* cr, App& app, double x, double cy, bool hot) {
    const BtBar& B = app.btBar;
    const bool connected = B.bt == "on" && !B.name.empty(), off = B.bt == "off", absent = B.bt == "absent";
    const double w = btBarW(app), name = btM3NameW(app);
    roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
    setSource(cr, app.color(off || absent ? "surface_container_low" : "primary_container"));
    cairo_fill(cr);
    if (hot) {
        roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
        setSource(cr, app.color("on_primary_container"), 0.08);
        cairo_fill(cr);
    }
    const double dx = x + (connected ? kM3Inset : (w - kM3Disc) / 2.0);
    constexpr double kDiscIcon = 16;
    if (connected) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, dx + kM3Disc / 2.0, cy, kM3Disc / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
    const Color ink = connected ? app.color("on_primary") : off || absent ? app.color("outline") : app.color("on_primary_container");
    drawIcon(cr, "bluetooth", dx + (kM3Disc - kDiscIcon) / 2.0, cy - kDiscIcon / 2.0, kDiscIcon, ink, hot);
    if (off) {
        cairo_move_to(cr, dx + kM3Disc / 2.0 - 6, cy + 6);
        cairo_line_to(cr, dx + kM3Disc / 2.0 + 6, cy - 6);
        setSource(cr, app.color("outline"));
        cairo_set_line_width(cr, 1.6);
        cairo_stroke(cr);
    }
    if (connected) {
        PangoLayout* layout = pango_cairo_create_layout(cr);
        PangoFontDescription* f = pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
        pango_layout_set_font_description(layout, f);
        pango_layout_set_width(layout, static_cast<int>(name * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_text(layout, B.name.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_primary_container"));
        cairo_move_to(cr, x + kM3Inset + kM3Disc + kBtM3Gap, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(f);
        g_object_unref(layout);
    }
}

void btIconPaint(cairo_t* cr, App& app, double x, double y, double w, double h, bool hot) {
    const BtBar& B = app.btBar;
    const std::string style = widgetStyle(app, "bluetooth");
    const Color framed = styleFrame(cr, app, style, x, y, w, h, hot);
    const bool connected = B.bt == "on" && !B.name.empty(), dim = B.bt == "absent" || B.bt == "off";
    const Color ink = dim ? app.color("outline") : connected ? app.color("primary") : framed;
    drawIcon(cr, "bluetooth", x + (w - 18) / 2.0, y + (h - 18) / 2.0, 18, ink, hot);
    if (connected) {
        cairo_arc(cr, x + w - 7, y + 7, 3.2, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
    if (B.bt == "off") {
        cairo_move_to(cr, x + w / 2 - 7, y + h / 2 + 7);
        cairo_line_to(cr, x + w / 2 + 7, y + h / 2 - 7);
        setSource(cr, app.color("outline"));
        cairo_set_line_width(cr, 1.6);
        cairo_stroke(cr);
    }
}
void btBarPaint(cairo_t* cr, App& app, const Bar& bar) {
    const double x = slotX(bar, "bluetooth"), y = (bar.height - kQuickBox) / 2.0;
    const bool hot = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitBluetooth;
    if (widgetStyle(app, "bluetooth") == "m3" && !sideBar(app)) { btM3Paint(cr, app, x, bar.height / 2.0, hot); return; }
    btIconPaint(cr, app, x, y, kQuickBox + softExtra(app, "bluetooth"), kQuickBox, hot);
}
void btSidePaint(cairo_t* cr, App& app, Rect r, bool hot) {
    btIconPaint(cr, app, r.x + (r.w - kQuickBox) / 2.0, r.y + (r.h - kQuickBox) / 2.0, kQuickBox, kQuickBox, hot);
}

void btLayout(App& app) {
    BtMenu& M = app.btMenu;
    const CcRadio& R = M.radio;
    using K = BtMenu::Item::Kind;
    M.items.clear();
    M.hover = -1;
    bubbleHide(M.bubble);
    const double x = kClkPad, w = kClkW - 2 * kClkPad;
    M.items.push_back({K::Power, -1, {x + w - 18 - 52, kClkPad + 18, 52, 32}});
    double y = kClkPad + kClkHeroH + kClkGap;
    int connected = 0;
    if (R.bt != "on") {
        M.items.push_back({K::Note, -1, {x, y, w, kBtRowH}});
        y += kBtRowH + kBtRowGap;
    } else {
        for (size_t i = 0; i < R.devs.size(); i++) {
            if (R.devs[i].state != "connected") continue;
            connected++;
            M.items.push_back({K::Row, static_cast<int>(i), {x, y, w, kBtRowH}});
            M.items.push_back({K::Disconnect, static_cast<int>(i), {x + w - 12 - kBtDisconnectD, y + (kBtRowH - kBtDisconnectD) / 2, kBtDisconnectD, kBtDisconnectD}});
            y += kBtRowH + kBtRowGap;
        }
        if (connected == 0) {
            M.items.push_back({K::Note, -1, {x, y, w, kBtRowH}});
            y += kBtRowH + kBtRowGap;
        }
    }
    M.items.push_back({K::Footer, -1, {x, y, w, kBtFootH}});
    y += kBtFootH;
    if (!M.note.empty()) {
        M.items.push_back({K::Note, -2, {x, y + kClkGap, w, kBtRowH}});
        y += kClkGap + kBtRowH;
    }
    const double contentH = y + kClkPad;
    const int screenH = app.screenH > 0 ? app.screenH : 1080;
    M.cardW = static_cast<int>(kClkW);
    M.cardH = static_cast<int>(std::min(contentH, screenH - kBtScreenMargin));
}

bool btInteractive(BtMenu::Item::Kind k) { return k == BtMenu::Item::Kind::Power || k == BtMenu::Item::Kind::Disconnect || k == BtMenu::Item::Kind::Footer; }
int btItemAt(const BtMenu& M, double x, double y) {
    for (size_t k = 0; k < M.items.size(); k++)
        if (btInteractive(M.items[k].kind) && M.items[k].r.contains(x, y)) return static_cast<int>(k);
    return -1;
}

void btDrawHero(App& app, cairo_t* cr, PangoLayout* L) {
    const CcRadio& R = app.btMenu.radio;
    const double x = kClkPad, y = kClkPad, w = kClkW - 2 * kClkPad, h = kClkHeroH;
    cairo_save(cr);
    roundedRect(cr, x, y, w, h, 24);
    setSource(cr, app.color("primary_container"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    clockCookiePath(cr, 9, x + w - 150, y - 26, 230);
    setSource(cr, app.color("primary"), 0.2);
    cairo_fill(cr);
    cairo_restore(cr);
    const Color ink = app.color("on_primary_container");
    const bool on = R.bt == "on";
    const CcDev* first = nullptr;
    int connected = 0;
    for (const CcDev& d : R.devs) if (d.state == "connected") { if (!first) first = &d; connected++; }
    clkText(cr, L, R.bt == "absent" ? "No adapter" : on ? "On" : "Off", 52, true, x + 18, y + 54, ink);
    clkText(cr, L, on && first ? (first->name.empty() ? first->mac : first->name) : "Bluetooth", 15, false, x + 20, y + 108, ink, 0.88);
    double cx = x + 18;
    std::vector<std::string> chips;
    if (on && connected > 0) chips.push_back(std::string(i18n::tr("Connected")) + " " + std::to_string(connected));
    if (on && first && first->battery >= 0) chips.push_back(std::string(i18n::tr("Battery")) + " " + std::to_string(first->battery) + "%");
    for (const std::string& label : chips) {
        const int tw = clkTextWidth(L, label, 12, true);
        roundedRect(cr, cx, y + h - 46, tw + 20, 28, 10);
        setSource(cr, ink, 0.14);
        cairo_fill(cr);
        clkText(cr, L, label, 12, true, cx + 10, y + h - 32, ink, 0.95);
        cx += tw + 28;
    }
}

void btDrawRow(App& app, cairo_t* cr, PangoLayout* L, Rect r, const CcDev& d, bool offOrAbsent) {
    const double a = offOrAbsent ? 0.5 : 1;
    roundedRect(cr, r.x, r.y, r.w, r.h, kBtRowRadius);
    setSource(cr, app.color("surface_container_high"), a);
    cairo_fill(cr);
    const double cy = r.y + r.h / 2;
    roundedRect(cr, r.x + 10, cy - 22, 44, 44, 22);
    setSource(cr, app.color("primary_container"), a);
    cairo_fill(cr);
    drawIcon(cr, rdDeviceIcon(d.icon), r.x + 20, cy - 12, 24, app.color("primary"), false);
    const double tx = r.x + 10 + 44 + 14, textW = r.x + r.w - 12 - kBtDisconnectD - 12 - tx;
    clkText(cr, L, d.name.empty() ? d.mac : d.name, 15, true, tx, cy - 10, app.color("on_surface"), a, textW);
    const std::string state = d.battery >= 0 ? std::string(i18n::tr("Connected")) + " · " + std::to_string(d.battery) + "%" : std::string(i18n::tr("Connected"));
    clkText(cr, L, state, 12, false, tx, cy + 12, app.color("on_surface_variant"), a, textW);
}

void btDrawItem(App& app, cairo_t* cr, PangoLayout* L, size_t k) {
    const BtMenu& M = app.btMenu;
    const BtMenu::Item& it = M.items[k];
    const Rect r = it.r;
    const bool hot = M.hover == static_cast<int>(k), pressed = M.pressed == static_cast<int>(k);
    const CcRadio& R = M.radio;
    const bool offOrAbsent = R.bt != "on";
    switch (it.kind) {
    case BtMenu::Item::Kind::Power: {
        const bool on = R.bt == "on";
        const double a = R.bt == "absent" ? 0.38 : 1;
        const Color ink = app.color("on_primary_container");
        roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        setSource(cr, on ? app.color("primary") : ink, on ? a : 0.25 * a);
        cairo_fill(cr);
        cairo_arc(cr, on ? r.x + r.w - 16 : r.x + 16, r.y + r.h / 2, on ? 12 : 8, 0, 2 * M_PI);
        setSource(cr, on ? app.color("on_primary") : app.color("on_primary_container"), a);
        cairo_fill(cr);
        break;
    }
    case BtMenu::Item::Kind::Note: {
        const bool err = it.dev == -2;
        const std::string text = err ? M.note : R.bt == "absent" ? "Bluetooth is not available" : R.bt != "on" ? "Bluetooth is off" : "No devices connected";
        roundedRect(cr, r.x, r.y, r.w, r.h, kBtRowRadius);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        clkText(cr, L, text, 13, false, r.x, r.y + r.h / 2, app.color(err ? "error" : "on_surface_variant"), 1, r.w, 1);
        break;
    }
    case BtMenu::Item::Kind::Row:
        btDrawRow(app, cr, L, r, R.devs[static_cast<size_t>(it.dev)], offOrAbsent);
        break;
    case BtMenu::Item::Kind::Disconnect: {
        const bool busy = M.pending == R.devs[static_cast<size_t>(it.dev)].mac;
        cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
        setSource(cr, app.color(hot || pressed ? "surface_container_highest" : "surface_container"), busy ? 0.5 : 1);
        cairo_fill(cr);
        drawIcon(cr, "link-broken", r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, app.color("on_surface"), false);
        break;
    }
    case BtMenu::Item::Kind::Footer: {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        if (hot || pressed) {
            roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            setSource(cr, app.color("on_primary_container"), pressed ? 0.12 : 0.08);
            cairo_fill(cr);
        }
        clkText(cr, L, i18n::tr("Manage devices"), 15, true, r.x, r.y + r.h / 2, app.color("on_primary_container"), 1, r.w, 1);
        drawIcon(cr, "alt-arrow-right", r.x + r.w - 20 - 20, r.y + (r.h - 20) / 2, 20, app.color("on_primary_container"), false);
        break;
    }
    }
}

void drawBtMenu(App& app) {
    BtMenu& M = app.btMenu;
    if (!M.open() || !M.configured) return;
    PopupFrame frame;
    if (!popupCanvasBegin(frame, "end4pC-bt-menu", M.cardW, M.cardH, M.scale)) return;
    const double rev = std::max(0.0, M.anim.reveal);
    if (rev > 0.001) {
        cairo_identity_matrix(frame.cr);
        const double room = kTmRoom;
        popPaintGrown(frame.cr, rev, M.ox + room, M.oy + room, M.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8), [&] {
            cairo_save(frame.cr);
            cairo_translate(frame.cr, room, room);
            popupCardPaint(app, frame.cr, M.cardW, M.cardH, kBtRadius);
            PangoLayout* L = pango_cairo_create_layout(frame.cr);
            btDrawHero(app, frame.cr, L);
            cairo_save(frame.cr);
            roundedRect(frame.cr, 0, 0, M.cardW, M.cardH, kBtRadius);
            cairo_clip(frame.cr);
            for (size_t k = 0; k < M.items.size(); k++) btDrawItem(app, frame.cr, L, k);
            cairo_restore(frame.cr);
            g_object_unref(L);
            if (!M.bubble.key.empty()) M.bubbleMoving = bubbleDraw(frame.cr, M.bubble, {app.color("surface").r, app.color("surface").g, app.color("surface").b}, {app.color("on_surface").r, app.color("on_surface").g, app.color("on_surface").b});
            cairo_restore(frame.cr);
        });
    }
    popupFrameCommit(app, frame, M.surface, M.blur, M.cardW, M.cardH, kBtRadius);
}

void closeBtMenu(App& app) {
    BtMenu& M = app.btMenu;
    if (!M.open()) return;
    if (M.layer) zwlr_layer_surface_v1_destroy(M.layer);
    releaseBlurSurface(M.blur);
    hidpi::detach(M.scale);
    wl_surface_destroy(M.surface);
    M.surface = nullptr;
    M.layer = nullptr;
    M.configured = false;
    M.bar = nullptr;
    M.items.clear();
    M.cardW = M.cardH = 0;
    M.hover = -1;
    M.pressed = -1;
    M.anim = PopAnim{};
    bubbleHide(M.bubble);
    M.bubbleMoving = false;
    app.onBtMenu = false;
    updateCursor(app);
    malloc_trim(0);
}

void onBtConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->btMenu.configured) app->btMenu.anim.animStart = std::chrono::steady_clock::now();
    app->btMenu.configured = true;
    drawBtMenu(*app);
}
void onBtClosed(void* d, zwlr_layer_surface_v1*) { closeBtMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kBtListener = {onBtConfigure, onBtClosed};

void btMenuRead(App& app) {
    BtMenu& M = app.btMenu;
    std::string text;
    if (ccFileChanged(ccCacheDir() + "/netbt.txt", M.listStamp, text)) {
        ccRadioParseLists(M.radio, text);
        btLayout(app);
        drawBtMenu(app);
    }
    if (ccFileChanged(ccCacheDir() + "/netbt-act.txt", M.actStamp, text)) {
        const auto kv = ccParseKv(text);
        const std::string id = kv.count("id") ? kv.at("id") : "";
        const std::string status = kv.count("status") ? kv.at("status") : "";
        if (!id.empty() && id != M.actId && status != "busy") {
            M.actId = id;
            M.pending.clear();
            M.note = status == "ok" ? "" : ccActError(kv.count("action") ? kv.at("action") : "", status);
            btLayout(app);
            drawBtMenu(app);
        }
    }
}

void openBtMenu(App& app, Bar* bar) {
    if (!bar || !app.layerShell || !app.compositor || !app.shm) return;
    closeAllPopups(app);
    BtMenu& M = app.btMenu;
    M.bar = bar;
    M.ax = app.pointerX;
    M.ay = app.pointerY;
    M.hover = -1;
    M.pressed = -1;
    M.configured = false;
    M.note.clear();
    M.radio.bt = app.btBar.bt;
    btLayout(app);
    M.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, M.scale, M.surface, [&app] { drawBtMenu(app); });
    M.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, M.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-bt-menu");
    const PopOrigin origin = placePopupBeside(app, M.layer, M.ax, M.ay, M.cardW, M.cardH, bar);
    M.ox = origin.x;
    M.oy = origin.y;
    popIconCentre(app, M.ax, M.ay, M.ix, M.iy);
    popStart(M.anim);
    zwlr_layer_surface_v1_set_exclusive_zone(M.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(M.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(M.layer, &kBtListener, &app);
    wl_surface_commit(M.surface);
    runDetached({"bash", (assetsDir() + "/../scripts/netbt.sh").c_str(), "list"});
    M.nextList = std::chrono::steady_clock::now() + std::chrono::seconds(kBtListPollS);
    btReadStatus(app);
}

void retractBtMenu(App& app) {
    BtMenu& M = app.btMenu;
    if (!M.open() || popIsOut(M.anim)) return;
    if (!M.configured) { closeBtMenu(app); return; }
    popRetract(M.anim, std::chrono::steady_clock::now());
    M.hover = -1;
    M.pressed = -1;
    bubbleHide(M.bubble);
    drawBtMenu(app);
}
void toggleBtMenu(App& app, Bar* bar) {
    BtMenu& M = app.btMenu;
    if (!M.open()) { openBtMenu(app, bar); return; }
    if (popIsOut(M.anim)) { popReverse(M.anim, std::chrono::steady_clock::now()); drawBtMenu(app); return; }
    retractBtMenu(app);
}
bool btIconPressed(const App& app) {
    return app.btMenu.open() && std::fabs(app.pointerX - app.btMenu.ix) <= 20 && std::fabs(app.pointerY - app.btMenu.iy) <= 20;
}
void tickBtMenu(App& app, std::chrono::steady_clock::time_point now) {
    BtMenu& M = app.btMenu;
    if (!M.open() || !M.configured) return;
    bool moving = false;
    if (popTick(M.anim, now, moving)) { closeBtMenu(app); return; }
    if (moving || M.bubbleMoving) drawBtMenu(app);
}

void btHoverBubble(BtMenu& M, int k) {
    if (k < 0) { bubbleHide(M.bubble); return; }
    const BtMenu::Item& it = M.items[static_cast<size_t>(k)];
    const char* tip = it.kind == BtMenu::Item::Kind::Disconnect ? "Disconnect" : it.kind == BtMenu::Item::Kind::Power ? "Bluetooth on or off" : nullptr;
    if (!tip) { bubbleHide(M.bubble); return; }
    bubbleShow(M.bubble, "bt" + std::to_string(k), tip, it.r.x, it.r.y, it.r.w, it.r.h, M.cardW, M.cardH);
}

void btMenuMotion(App& app) {
    BtMenu& M = app.btMenu;
    const double x = app.pointerX - kTmRoom, y = app.pointerY - kTmRoom;
    const int hover = btItemAt(M, x, y);
    if (hover == M.hover) return;
    M.hover = hover;
    btHoverBubble(M, hover);
    drawBtMenu(app);
    updateCursor(app);
}

void btMenuButton(App& app, uint32_t button, bool pressed) {
    BtMenu& M = app.btMenu;
    constexpr uint32_t kBtnLeft = 0x110;
    if (button != kBtnLeft) return;
    if (popIsOut(M.anim)) return;
    const double x = app.pointerX - kTmRoom, y = app.pointerY - kTmRoom;
    if (!pressed) {
        if (M.pressed < 0) return;
        const int held = M.pressed;
        M.pressed = -1;
        if (btItemAt(M, x, y) == held) {
            const BtMenu::Item it = M.items[static_cast<size_t>(held)];
            if (it.kind == BtMenu::Item::Kind::Power) btTogglePower(app);
            else if (it.kind == BtMenu::Item::Kind::Footer) {
                closeBtMenu(app);
                ccOpenPage(app, CcPageBt);
                return;
            } else if (it.kind == BtMenu::Item::Kind::Disconnect && M.pending.empty()) {
                const CcDev& d = M.radio.devs[static_cast<size_t>(it.dev)];
                M.pending = d.mac;
                M.note.clear();
                netbtRun("bt-disconnect", d.mac.c_str());
                M.nextList = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
            }
        }
        btLayout(app);
        drawBtMenu(app);
        return;
    }
    if (!Rect{0, 0, double(M.cardW), double(M.cardH)}.contains(x, y)) { retractBtMenu(app); return; }
    const int k = btItemAt(M, x, y);
    if (k < 0) return;
    M.pressed = k;
    drawBtMenu(app);
}

void btTick(App& app, std::chrono::steady_clock::time_point now) {
    BtBar& B = app.btBar;
    BtMenu& M = app.btMenu;
    if (btShown(app) && now >= B.nextPoll) { btAskStatus(app, now); B.nextPoll = now + std::chrono::seconds(kBtStatusPollS); }
    if (B.nextRead != std::chrono::steady_clock::time_point{} && now >= B.nextRead) { B.nextRead = {}; btReadStatus(app); }
    if (M.open()) {
        if (now >= M.nextList) {
            runDetached({"bash", (assetsDir() + "/../scripts/netbt.sh").c_str(), "list"});
            M.nextList = now + std::chrono::seconds(kBtListPollS);
        }
        btMenuRead(app);
    }
}
