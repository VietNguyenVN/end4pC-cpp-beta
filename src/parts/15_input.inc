

void catcherConfigure(void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (!app->catSurface || w == 0 || h == 0) return;
    const int stride = static_cast<int>(w) * 4, size = stride * static_cast<int>(h);
    const int fd = memfd_create("end4pC-catcher", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    wl_shm_pool* pool = wl_shm_create_pool(app->shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(app->catSurface, buffer, 0, 0);
    wl_surface_damage_buffer(app->catSurface, 0, 0, w, h);
    wl_surface_commit(app->catSurface);
}
void catcherClosed(void*, zwlr_layer_surface_v1*) {}
const zwlr_layer_surface_v1_listener kCatcherListener = {catcherConfigure, catcherClosed};

bool anyPopupOpen(const App& app) {
    return app.sel.open() || app.launcher.open() || app.power.open() || app.cc.open() || app.panel.open() || app.media.open() || app.clockP.open() || app.tmenu.open() || app.amenu.open() || app.btMenu.open() || app.drivesMenu.open() || app.dmenu.open() || (app.ic.open() && g_icPowerMode);
}

void syncCatcher(App& app) {
    const bool pinnedMediaOnly = app.mediaPinned && app.media.open() && !(app.sel.open() || app.launcher.open() || app.power.open() || app.cc.open() || app.panel.open() || app.clockP.open() || app.tmenu.open() || app.amenu.open() || app.btMenu.open() || app.dmenu.open());
    const bool want = anyPopupOpen(app) && !pinnedMediaOnly;
    if (want && !app.catSurface && app.compositor && app.layerShell && app.shm) {
        app.catSurface = wl_compositor_create_surface(app.compositor);
        app.catLayer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, app.catSurface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "end4pC-catcher");
        zwlr_layer_surface_v1_set_anchor(app.catLayer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_size(app.catLayer, 0, 0);
        zwlr_layer_surface_v1_set_exclusive_zone(app.catLayer, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(app.catLayer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_add_listener(app.catLayer, &kCatcherListener, &app);
        wl_surface_commit(app.catSurface);
    } else if (!want && app.catSurface) {
        zwlr_layer_surface_v1_destroy(app.catLayer);
        wl_surface_destroy(app.catSurface);
        app.catLayer = nullptr;
        app.catSurface = nullptr;
        app.onCatcher = false;
    }
}

void closeAllPopups(App& app) {
    if (app.sel.open()) retractSelector(app);
    if (app.launcher.open()) retractLauncher(app);
    if (app.power.open()) retractPowerMenu(app);
    if (app.cc.open()) retractControlCenter(app);
    if (app.panel.open()) retractNotifPanel(app);
    if (app.media.open()) retractMediaPanel(app);
    if (app.clockP.open()) retractClockPanel(app);
    if (app.tmenu.open()) closeTrayMenu(app);
    if (app.amenu.open()) retractAudioMenu(app);
    if (app.btMenu.open()) retractBtMenu(app);
    if (app.drivesMenu.open()) retractDrivesMenu(app);
    if (app.dmenu.open()) retractDesktopMenu(app);
    if (app.ic.open()) retractIslandCard(app);
}

void onPointerEnter(void* d, wl_pointer*, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
    auto* app = static_cast<App*>(d);
    app->enterSerial = serial;
    app->cursorShape = -1;
    app->hoverBar = barForSurface(*app, surface);
    app->hotCorner = cornerForSurface(*app, surface);
    app->onCap = app->cap.surface && surface == app->cap.surface;
    app->onSw = app->sw.surface && surface == app->sw.surface;
    app->onPk = app->pk.surface && surface == app->pk.surface;
    app->onSelector = app->sel.surface && surface == app->sel.surface;
    app->onLauncher = app->launcher.surface && surface == app->launcher.surface;
    app->onPower = app->power.surface && surface == app->power.surface;
    app->onCc = app->cc.surface && surface == app->cc.surface;
    app->onCatcher = app->catSurface && surface == app->catSurface;
    app->onNotif = app->np.surface && surface == app->np.surface;
    app->onPanel = app->panel.surface && surface == app->panel.surface;
    app->onMedia = app->media.surface && surface == app->media.surface;
    app->onClockP = app->clockP.surface && surface == app->clockP.surface;
    app->onTmenu = app->tmenu.surface && surface == app->tmenu.surface;
    app->onAmenu = app->amenu.surface && surface == app->amenu.surface;
    app->onBtMenu = app->btMenu.surface && surface == app->btMenu.surface;
    app->onDrives = app->drivesMenu.surface && surface == app->drivesMenu.surface;
    app->onOsk = app->osk.surface && surface == app->osk.surface;
    app->onQr = app->qr.surface && surface == app->qr.surface;
    app->onIsland = app->ic.surface && surface == app->ic.surface;
    app->onDmenu = app->dmenu.surface && surface == app->dmenu.surface;
    app->onDock = app->dock.surface && surface == app->dock.surface;
    app->onDesk = deskFromSurface(*app, surface);
    app->wallBar = nullptr;
    for (auto& b : app->bars) if (b->wpSurface && surface == b->wpSurface) app->wallBar = b.get();
    app->pointerX = wl_fixed_to_double(x);
    app->pointerY = wl_fixed_to_double(y);
    if (app->hoverBar) toBarCoords(*app->hoverBar, app->pointerX, app->pointerY);
    if (app->hoverBar) draw(*app->hoverBar);
    if (app->onCap) captureMotion(*app);
    if (app->onSw) switcherMotion(*app);
    if (app->onPk) polkitMotion(*app);
    if (app->onSelector) selectorMotion(*app);
    if (app->onLauncher) launcherMotion(*app);
    if (app->onPower) powerMotion(*app);
    if (app->onCc) ccMotion(*app);
    if (app->onNotif) notifMotion(*app);
    if (app->onPanel) notifPanelMotion(*app);
    if (app->onMedia) mediaPanelMotion(*app);
    if (app->onClockP) clockPanelMotion(*app);
    if (app->onTmenu) trayMenuMotion(*app);
    if (app->onAmenu) audioMenuMotion(*app);
    if (app->onBtMenu) btMenuMotion(*app);
    if (app->onDrives) drivesMenuMotion(*app);
    if (app->onIsland) islandCardMotion(*app);
    islandHoverCheck(*app);
    if (app->onDmenu) dmenuMotion(*app);
    if (app->onDesk) deskMotion(*app, *app->onDesk);
    if (app->onDock) dockEnter(*app);
    updateCursor(*app);
}

void onPointerLeave(void* d, wl_pointer*, uint32_t, wl_surface*) {
    auto* app = static_cast<App*>(d);
    Bar* bar = app->hoverBar;
    app->hoverBar = nullptr;
    app->hotCorner = -1;
    app->dtp.pressIdx = -1;
    app->dtp.dragging = false;
    if (bar) draw(*bar);
    if (app->onSw) { app->onSw = false; app->sw.hover = -1; drawSwitcher(*app); }
    if (app->onPk) { app->onPk = false; app->pk.hover = -1; drawPolkit(*app); }
    if (app->onCap) { app->onCap = false; app->cap.hoverBtn = -1; if (app->cap.mode == 0) captureRedraw(*app); }
    if (app->onSelector) {
        app->onSelector = false;
        app->sel.hoverTile = app->sel.hoverChip = app->sel.hoverMode = app->sel.hoverNav = app->sel.hoverBtn = -1;
        app->sel.overGrid = false;
        drawSelector(*app);
    }
    if (app->onLauncher) {
        app->onLauncher = false;
        app->launcher.hover = -1;
        drawLauncher(*app);
    }
    if (app->onPower) {
        app->onPower = false;
        app->power.hover = -1;
        drawPowerMenu(*app);
    }
    app->onCatcher = false;
    if (app->onCc && app->cc.drag == 0) {
        app->onCc = false;
        app->cc.hover = 0;
        app->cc.radio.hover.clear();
        drawControlCenter(*app);
    }
    if (app->onMedia && app->media.drag == 0) {
        app->onMedia = false;
        app->media.hover = 0;
        drawMediaPanel(*app);
    }
    if (app->onClockP) {
        app->onClockP = false;
        app->clockP.hover = 0;
        drawClockPanel(*app);
    }
    if (app->onTmenu) {
        app->onTmenu = false;
        app->tmenu.hover = -1;
        drawTrayMenu(*app);
    }
    if (app->onAmenu && app->amenu.drag < 0) {
        app->onAmenu = false;
        app->amenu.hover = -1;
        drawAudioMenu(*app);
    }
    if (app->onIsland) { app->onIsland = false; app->ic.hover = -1; drawIslandCard(*app); }
    if (app->onQr) { app->onQr = false; app->qr.hover = -1; drawQrCard(*app); }
    if (app->onOsk && !app->osk.dragging) { app->onOsk = false; app->osk.hover = -1; drawOsk(*app); }
    if (app->onBtMenu) {
        app->onBtMenu = false;
        app->btMenu.hover = -1;
        drawBtMenu(*app);
    }
    if (app->onDrives) { app->onDrives = false; app->drivesMenu.hover = -1; drawDrivesMenu(*app); }
    if (app->onDock) dockLeave(*app);
    app->wallBar = nullptr;
    if (app->onDesk && !app->onDesk->drag) deskLeave(*app, *app->onDesk);
    if (app->onDmenu) {
        app->onDmenu = false;
        app->dmenu.hovered = app->dmenu.hoverRow = app->dmenu.hoverSub = -1;
        app->dmenu.animating = true;
        drawDesktopMenu(*app);
    }
    if (app->onPanel) {
        app->onPanel = false;
        app->panel.hoverId = 0;
        app->panel.hoverButton = -1;
        app->panel.overList = false;
        drawNotifPanel(*app);
    }
    if (app->onNotif) {
        app->onNotif = false;
        app->np.hoverId = 0;
        app->np.hoverButton = -1;
        drawNotifPopup(*app);
    }
}

void onPointerMotion(void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    auto* app = static_cast<App*>(d);
    if (app->onCap) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        captureMotion(*app);
        return;
    }
    if (app->onSw) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        switcherMotion(*app);
        return;
    }
    if (app->onPk) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        polkitMotion(*app);
        return;
    }
    if (app->onSelector) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        selectorMotion(*app);
        return;
    }
    if (app->onPower) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        powerMotion(*app);
        return;
    }
    if (app->onCc) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        ccMotion(*app);
        return;
    }
    if (app->onLauncher) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        launcherMotion(*app);
        return;
    }
    if (app->onNotif) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        notifMotion(*app);
        return;
    }
    if (app->onPanel) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        notifPanelMotion(*app);
        return;
    }
    if (app->onMedia) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        mediaPanelMotion(*app);
        return;
    }
    if (app->onClockP) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        clockPanelMotion(*app);
        return;
    }
    if (app->onDock) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        dockMotion(*app);
        return;
    }
    if (app->onTmenu) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        trayMenuMotion(*app);
        return;
    }
    if (app->onAmenu) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        audioMenuMotion(*app);
        return;
    }
    if (app->onIsland) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        islandCardMotion(*app);
        return;
    }
    if (app->onQr) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        qrMotion(*app);
        return;
    }
    if (app->onOsk) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        oskMotion(*app);
        return;
    }
    if (app->onBtMenu) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        btMenuMotion(*app);
        return;
    }
    if (app->onDrives) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        drivesMenuMotion(*app);
        return;
    }
    if (app->onDmenu) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        dmenuMotion(*app);
        return;
    }
    if (app->onDesk) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        deskMotion(*app, *app->onDesk);
        return;
    }
    if (app->wallBar) {
        app->pointerX = wl_fixed_to_double(x);
        app->pointerY = wl_fixed_to_double(y);
        return;
    }
    if (!app->hoverBar) return;
    const Hit before = hitTest(*app->hoverBar, app->pointerX, app->pointerY);
    app->pointerX = wl_fixed_to_double(x);
    app->pointerY = wl_fixed_to_double(y);
    toBarCoords(*app->hoverBar, app->pointerX, app->pointerY);
    if (app->dtp.pressIdx >= 0) { dtpMotion(*app, *app->hoverBar); drawAll(*app); return; }
    if (before != hitTest(*app->hoverBar, app->pointerX, app->pointerY)) { drawAll(*app); updateCursor(*app); islandHoverCheck(*app); }
}

const char* hitModule(Hit h) {
    switch (h) {
    case HitClock: return "clock";
    case HitVolume: return "volume";
    case HitNetwork: return "network";
    case HitNotif: return "notifications";
    case HitMedia: case HitPrev: case HitPlay: case HitNext: return "media";
    case HitSettings: return "settings";
    case HitAvatar: return "avatar";
    case HitLauncher: return "launcher";
    case HitPower: return "power";
    case HitWeather: return "weather";
    case HitUpdates: return "updates";
    case HitBluetooth: return "bluetooth";
    case HitDrives: return "drives";
    case HitTools: return "tools";
    case HitKeyboard: return "keyboard";
    case HitActiveWindow: return "activewindow";
    case kHitStatus + 3: return "battery";
    default: break;
    }
    if (h >= kHitAi && h < kHitAi + 2) return "aiusage";
    if (h >= kHitRes && h < kHitRes + 5) return "resources";
    if (h >= kHitViz && h < kHitViz + 2) return "visualizer";
    if (h >= kHitQuick && h < kHitTray) return "quickactions";
    if (h >= kHitWs && h < kHitQuick) return "workspaces";
    return nullptr;
}

void onPointerButton(void* d, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    auto* app = static_cast<App*>(d);
    constexpr uint32_t kBtnLeft = 0x110, kBtnRight = 0x111;
    if (app->onPk) { polkitButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onSw) { switcherButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onCap) { captureButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onAmenu) { audioMenuButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onIsland) { islandCardButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onQr) { qrButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onOsk) { oskButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onDrives) { drivesMenuButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (app->onBtMenu) { btMenuButton(*app, button, state == WL_POINTER_BUTTON_STATE_PRESSED); return; }
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
        if (button == kBtnLeft && app->launcher.selDrag) app->launcher.selDrag = false;
        if (button == kBtnLeft && app->sel.qDrag) app->sel.qDrag = false;
        if (button == kBtnLeft && app->media.drag != 0) mediaPanelRelease(*app);
        if (button == kBtnLeft && app->cc.drag != 0) ccRelease(*app);
        if (button == kBtnLeft) deskRelease(*app);
        if (button == kBtnLeft) dockRelease(*app);
        if (button == kBtnLeft && app->dtp.pressIdx >= 0) { dtpRelease(*app); drawAll(*app); }
        return;
    }
    if (app->onCatcher && audioIconPressed(*app)) { toggleAudioMenu(*app, app->amenu.bar); return; }
    if (app->onCatcher && drivesIconPressed(*app)) { toggleDrivesMenu(*app, app->drivesMenu.bar); return; }
    if (app->onCatcher && btIconPressed(*app)) { toggleBtMenu(*app, app->btMenu.bar); return; }
    if (app->onCatcher) { closeAllPopups(*app); syncCatcher(*app); drawAll(*app); return; }
    if (app->onDock) { dockButton(*app, button); return; }
    if (app->onTmenu) { if (button == kBtnLeft) trayMenuClick(*app); return; }
    for (DeskWidget* w : deskList(*app)) if (w != app->onDesk && w->open() && w->typing && w->typing(*w) && w->blur) w->blur(*app, *w);
    if (app->onDesk) {
        if (button == kBtnLeft) deskClick(*app, *app->onDesk);
        else if (button == kBtnRight) deskToggleEdit(*app, *app->onDesk);
        return;
    }
    for (DeskWidget* w : deskList(*app)) if (w->open() && w->typing && w->typing(*w) && w->blur) w->blur(*app, *w);
    if (app->onDmenu) { if (button == kBtnLeft) dmenuClick(*app); return; }
    if (button == kBtnRight && app->wallBar) { openDesktopMenu(*app, app->wallBar); return; }
    if (app->hoverBar) {  // an app of the bar's dock panel
        const Hit dockHit = hitTest(*app->hoverBar, app->pointerX, app->pointerY);
        if (dockHit >= kHitDtp && dockHit < kHitDtp + 100) { dtpPress(*app, dockHit - kHitDtp, button); drawAll(*app); return; }
    }
    if (button == kBtnRight) {
        const Hit rh = app->hoverBar ? hitTest(*app->hoverBar, app->pointerX, app->pointerY) : HitNone;
        if (rh >= kHitTray && rh < kHitTray + 100) trayClick(*app, rh - kHitTray, true);
        else if (const char* mod = hitModule(rh)) toggleStylePicker(*app, app->hoverBar, mod);
        return;
    }
    if (button == 0x112) {
        const Hit mh = app->hoverBar ? hitTest(*app->hoverBar, app->pointerX, app->pointerY) : HitNone;
        if (mh == HitNotif) setDoNotDisturb(*app, !app->cfg.doNotDisturb);
        else if (mh == kHitQuick) pickAccent();
        else if (mh == HitUpdates) startUpdatesNow(*app);
        else if (mh == HitVolume) runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
        else if (mh == HitBluetooth) btTogglePower(*app);
        else if (mh >= kHitTray && mh < kHitTray + 100 && mh - kHitTray < static_cast<int>(app->tray.size())) trayTogglePin(*app, app->tray[mh - kHitTray].id);
        return;
    }
    if (button != kBtnLeft) return;
    if (app->hotCorner >= 0) {
        if (!app->cfg.hotCorners) return;
        const std::string& act = app->hotCorner == 0 ? app->cfg.cornerTL : app->hotCorner == 1 ? app->cfg.cornerTR : app->hotCorner == 2 ? app->cfg.cornerBL : app->cfg.cornerBR;
        if (act == "controlcenter") toggleControlCenter(*app);
        else if (act == "media") toggleMediaPanel(*app);
        else if (act == "wallpapers") toggleSelector(*app);
        else if (act == "launcher") toggleLauncher(*app);
        else if (act == "notifications") toggleNotifPanel(*app);
        else if (act == "power") togglePowerMenu(*app);
        else if (act == "lock") startLock();
        else if (act == "settings") toggleSettings();
        return;
    }
    if (app->onSelector) { selectorClick(*app); return; }
    if (app->onPower) { powerClick(*app); return; }
    if (app->onCc) { ccClick(*app); return; }
    if (app->onLauncher) { launcherClick(*app); return; }
    if (app->onNotif) { notifClick(*app); return; }
    if (app->onPanel) { notifPanelClick(*app); return; }
    if (app->onMedia) { mediaPanelClick(*app); return; }
    if (app->onClockP) { clockPanelClick(*app); return; }
    if (!app->hoverBar) return;
    const Hit hit = hitTest(*app->hoverBar, app->pointerX, app->pointerY);
    if (hit == HitSettings) {
        toggleSettings();
    } else if (hit == HitLauncher) {
        toggleLauncher(*app);
    } else if (hit == HitPower) {
        togglePowerMenu(*app);
    } else if (hit == HitTools) {
        toggleTools();
    } else if (hit == HitClock) {
        toggleClockPanel(*app, app->hoverBar, 0);
    } else if (hit == HitKeyboard) {
        toggleClockPanel(*app, app->hoverBar, 5);
    } else if (hit == HitUpdates) {
        runUpdatesTerminal(*app);
    } else if (hit == HitWeather) {
        toggleClockPanel(*app, app->hoverBar, 1);
    } else if (hit >= kHitRes && hit < kHitRes + 5) {
        toggleClockPanel(*app, app->hoverBar, 3, hit - kHitRes);
    } else if (hit >= kHitAi && hit < kHitAi + 2) {
        toggleClockPanel(*app, app->hoverBar, 2, hit - kHitAi);
    } else if (hit == HitMedia) {
        toggleMediaPanel(*app, app->hoverBar);
    } else if (hit == HitNotif) {
        toggleNotifPanel(*app);
    } else if (hit == HitPrev) {
        mediaSend(*app, {"previous"});
    } else if (hit == HitPlay) {
        mediaSend(*app, {"play-pause"});
    } else if (hit == HitNext) {
        mediaSend(*app, {"next"});
    } else if (hit == HitAvatar) {
        toggleControlCenter(*app, true);
    } else if (hit == kHitStatus) {
        quickAction(*app, 1);
        drawAll(*app);
    } else if (hit == kHitStatus + 3) {
        toggleClockPanel(*app, app->hoverBar, 4);
    } else if (hit == kHitStatus + 1) {
        runDetached({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
    } else if (hit == HitTray) {
        app->trayOpen = !app->trayOpen;
        drawAll(*app);
    } else if (hit >= kHitTray && hit < kHitTray + 100) {
        trayClick(*app, hit - kHitTray, false);
    } else if (hit >= kHitQuick) {
        quickAction(*app, hit - kHitQuick);
        drawAll(*app);
    } else if (hit >= kHitWs) {
        focusWorkspace(hit - kHitWs);
    } else if (hit == HitVolume) {
        toggleAudioMenu(*app, app->hoverBar);
    } else if (hit == HitBluetooth) {
        toggleBtMenu(*app, app->hoverBar);
    } else if (hit == HitDrives) {
        toggleDrivesMenu(*app, app->hoverBar);
    }
}

void onPointerAxis(void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    auto* app = static_cast<App*>(d);
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    if (app->hotCorner >= 0) {
        if (!app->cfg.hotCorners) return;
        if (app->hotCorner == 0) changeBrightness(*app, value > 0 ? -3 : 3);
        else if (app->hotCorner == 1) runDetached({"wpctl", "set-volume", "-l", volLimit(*app).c_str(), "@DEFAULT_AUDIO_SINK@", volStep(*app, value > 0).c_str()});
        return;
    }
    if (app->onAmenu) { audioMenuScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onBtMenu) return;
    if (app->onIsland) { islandToggleView(*app); return; }
    if (app->onDock && app->dock.gridOpen) { dockGridScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onCc && app->cc.page != CcPageNone) { ccRadioScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onSelector) { selectorScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onLauncher) { launcherScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onPanel) { notifPanelScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onMedia) { mediaPanelScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onClockP) { clockPanelScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onDmenu) { dmenuScroll(*app, value > 0 ? 1 : -1); return; }
    if (app->onDesk) { deskScroll(*app, *app->onDesk, value > 0 ? 1 : -1); return; }
    if (!app->hoverBar) return;
    const Hit overHit = hitTest(*app->hoverBar, app->pointerX, app->pointerY);
    if (overHit == HitIsland) { islandToggleView(*app); return; }
    if (overHit != HitVolume) return;
    runDetached({"wpctl", "set-volume", "-l", volLimit(*app).c_str(), "@DEFAULT_AUDIO_SINK@", volStep(*app, value > 0).c_str()});
}
void onPointerFrame(void*, wl_pointer*) {}
void onPointerAxisSource(void*, wl_pointer*, uint32_t) {}
void onPointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

const wl_pointer_listener kPointerListener = {
    onPointerEnter, onPointerLeave, onPointerMotion, onPointerButton, onPointerAxis,
    onPointerFrame, onPointerAxisSource, onPointerAxisStop, onPointerAxisDiscrete};

// Keyboard: only the selector ever has focus (its layer grabs the keyboard while open). We never parse the
// keymap; Esc is evdev key 1 whatever the layout.
void onKbKeymap(void* d, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    auto* app = static_cast<App*>(d);
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!app->xkbCtx) app->xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* keymap = xkb_keymap_new_from_string(app->xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (keymap) {
                if (app->xkbState) xkb_state_unref(app->xkbState);
                if (app->xkbMap) xkb_keymap_unref(app->xkbMap);
                app->xkbMap = keymap;
                app->xkbState = xkb_state_new(keymap);
            }
        }
    }
    close(fd);
}
void onKbEnter(void* d, wl_keyboard*, uint32_t, wl_surface* surface, wl_array*) {
    auto* app = static_cast<App*>(d);
    for (DeskWidget* w : deskList(*app)) w->kbFocus = w->surface && surface == w->surface;
}
void onKbLeave(void* d, wl_keyboard*, uint32_t, wl_surface*) {
    auto* app = static_cast<App*>(d);
    app->repeatKey = 0;
    for (DeskWidget* w : deskList(*app)) w->kbFocus = false;
}
void onKbKey(void* d, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    auto* app = static_cast<App*>(d);
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (key == app->repeatKey) app->repeatKey = 0;
        return;
    }
    if (app->pk.open()) {
        if (polkitKey(*app, key) && app->kbRate > 0) {
            app->repeatKey = key;
            app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
        }
        return;
    }
    if (app->sw.open()) { switcherKey(*app, key); return; }
    if (app->cap.open()) { captureKey(*app, key); return; }
    if (app->dock.gridOpen && !app->dock.gridClosing) {
        if (dockGridKey(*app, key) && launcherRepeatable(key) && app->kbRate > 0) {
            app->repeatKey = key;
            app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
        }
        return;
    }
    for (DeskWidget* w : deskList(*app)) {
        if (!w->open() || !w->kbFocus || !w->typing || !w->typing(*w)) continue;
        if (w->key && w->key(*app, *w, key) && launcherRepeatable(key) && app->kbRate > 0) {
            app->repeatKey = key;
            app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
        }
        return;
    }
    if (app->clockP.open() && app->clockP.adding) {
        if (clockPanelKey(*app, key) && app->kbRate > 0) {
            app->repeatKey = key;
            app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
        }
        return;
    }
    if (app->cc.open() && app->cc.page != CcPageNone && ccRadioKey(*app, key)) return;
    if (key == kKeyEsc && app->sel.open() && !app->sel.confirmPath.empty()) { app->sel.confirmPath.clear(); selectorMotion(*app); drawSelector(*app); return; }
    if (app->ic.open() && g_icPowerMode) { islandPowerKey(*app, key); return; }
    if (key == kKeyEsc && app->qr.surface) { closeQrCard(*app); return; }
    if (key == kKeyEsc) { retractControlCenter(*app); retractPowerMenu(*app); retractSelector(*app); retractLauncher(*app); retractNotifPanel(*app); retractMediaPanel(*app); retractClockPanel(*app); closeTrayMenu(*app); retractAudioMenu(*app); retractBtMenu(*app); retractDesktopMenu(*app); return; }
    if (app->power.open()) { powerKey(*app, key); return; }
    if (app->launcher.open()) {
        if (launcherKey(*app, key) && launcherRepeatable(key) && app->kbRate > 0) {
            app->repeatKey = key;
            app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
        }
        return;
    }
    if (selectorKey(*app, key) && repeatable(key) && app->kbRate > 0) {
        app->repeatKey = key;
        app->repeatAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(app->kbDelay);
    }
}
void onKbModifiers(void* d, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    auto* app = static_cast<App*>(d);
    if (app->xkbState) xkb_state_update_mask(app->xkbState, depressed, latched, locked, 0, 0, group);
    if (app->sw.open()) switcherModifiers(*app);
}
void onKbRepeat(void* d, wl_keyboard*, int32_t rate, int32_t delay) {
    auto* app = static_cast<App*>(d);
    app->kbRate = rate;
    app->kbDelay = delay;
}
const wl_keyboard_listener kKeyboardListener = {onKbKeymap, onKbEnter, onKbLeave, onKbKey, onKbModifiers, onKbRepeat};

void onSeatCapabilities(void* d, wl_seat* seat, uint32_t caps) {
    auto* app = static_cast<App*>(d);
    const bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (hasPointer && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &kPointerListener, app);
    } else if (!hasPointer && app->pointer) {
        wl_pointer_release(app->pointer);
        app->pointer = nullptr;
    }
    const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    if (hasKeyboard && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &kKeyboardListener, app);
    } else if (!hasKeyboard && app->keyboard) {
        wl_keyboard_release(app->keyboard);
        app->keyboard = nullptr;
    }
}
void onSeatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {onSeatCapabilities, onSeatName};
