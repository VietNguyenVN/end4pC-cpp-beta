
void dockGridLaunch(App& app, int appIdx);
void dockActivate(App& app, int idx);

bool dockPointerOverPill(const App& app) {
    const DockState& D = app.dock;
    const DockPt p = dockPointer(app);
    return app.onDock && D.slide < 0.5 && p.y >= D.pill.y - dockPopPx(app) - 4 && p.x >= D.pill.x - 6 && p.x <= D.pill.x + D.pill.w + 6;
}

int dockHitIndex(const App& app) {
    const DockState& D = app.dock;
    const DockPt p = dockPointer(app);
    if (D.slide > 0.5 || p.y < D.pill.y - dockPopPx(app) || p.x < D.pill.x || p.x > D.pill.x + D.pill.w) return -1;
    int best = -1;
    double bestDist = 0;
    for (size_t i = 0; i < D.items.size(); i++) {
        const DockItem& it = D.items[i];
        if (it.separator) continue;
        const double dist = std::abs(p.x - (it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2));
        if (best < 0 || dist < bestDist) { best = static_cast<int>(i); bestDist = dist; }
    }
    return best;
}

void dockAim(App& app) {
    DockState& D = app.dock;
    const bool over = dockPointerOverPill(app) && !D.dragging;
    const double peak = app.cfg.dockMagnify ? app.cfg.dockMagnification / 100.0 - 1 : 0.0, unit = app.cfg.dockIconSize + app.cfg.dockSpacing;
    for (DockItem& it : D.items) {
        if (it.separator) continue;
        const double d = over ? (dockPointer(app).x - it.baseCx) / unit : 1e9;
        it.target = 1 + peak * std::exp(-(d * d) / (2 * kDockSigma * kDockSigma));
    }
    D.moving = true;
}

void dockCloseMenu(App& app) {
    DockState& D = app.dock;
    D.menuOpen = false;
    D.menuItem = -1;
    D.menuHover = -1;
    D.menuRows.clear();
    D.menuRects.clear();
}

int dockMenuRowAt(const App& app) {
    const auto& rects = app.dock.menuRects;
    for (size_t i = 0; i < rects.size(); i++)
        if (rects[i].contains(app.pointerX, app.pointerY)) return static_cast<int>(i);
    return -1;
}

void dockShow(App& app) {
    DockState& D = app.dock;
    D.hideArmed = false;
    if (D.slideTarget < 0.5) return;
    D.slideTarget = 0;
    dockRebuild(app);
}

void dockHide(App& app) {
    DockState& D = app.dock;
    if (!app.cfg.dockAutoHide || app.onDock || D.menuOpen || D.gridOpen) return;
    D.slideTarget = 1;
    D.moving = true;
}

int dockGridCellAt(const App& app) {
    for (const auto& [rect, appIdx] : app.dock.gridCells)
        if (rect.contains(app.pointerX - dockSlideOffset(app.dock).x, app.pointerY - dockSlideOffset(app.dock).y)) return appIdx;
    return -1;
}

bool dockMovable(const DockItem& it) { return !it.separator && it.special == 0 && it.pinned; }

bool dockDragMotion(App& app) {
    DockState& D = app.dock;
    const DockPt p = dockPointer(app);
    if (!D.dragging) {
        if (D.pressIdx < 0 || D.pressIdx >= static_cast<int>(D.items.size()) || !dockMovable(D.items[D.pressIdx])) { D.pressIdx = -1; return false; }
        if (std::abs(p.x - D.pressX) < 6) return true;
        D.dragging = true;
        D.dragKey = D.items[D.pressIdx].key;
        D.hover = -1;
        dockCloseMenu(app);
    }
    D.dragCx = p.x - D.dragGrab;
    int cur = -1;
    for (size_t i = 0; i < D.items.size(); i++) if (D.items[i].key == D.dragKey && dockMovable(D.items[i])) cur = static_cast<int>(i);
    if (cur < 0) { D.dragging = false; D.pressIdx = -1; return false; }
    for (;;) {
        const int n = static_cast<int>(D.items.size());
        int next = -1;
        if (cur + 1 < n && dockMovable(D.items[cur + 1]) && D.dragCx >= D.items[cur + 1].baseCx) next = cur + 1;
        else if (cur > 0 && dockMovable(D.items[cur - 1]) && D.dragCx <= D.items[cur - 1].baseCx) next = cur - 1;
        if (next < 0) break;
        const double before = D.items[next].slot.x - D.items[next].shift;
        std::swap(D.items[cur], D.items[next]);
        dockLayout(app);
        DockItem& moved = D.items[cur];
        moved.shift += before - (moved.slot.x - moved.shift);
        cur = next;
    }
    dockAim(app);
    D.moving = true;
    return true;
}

void dockRelease(App& app) {
    DockState& D = app.dock;
    if (D.pressIdx < 0 && !D.dragging) return;
    if (!D.dragging) {
        const int idx = D.pressIdx;
        D.pressIdx = -1;
        if (idx >= 0 && idx < static_cast<int>(D.items.size()) && dockHitIndex(app) == idx) dockActivate(app, idx);
        return;
    }
    D.dragging = false;
    D.pressIdx = -1;
    std::string joined;
    for (DockItem& it : D.items) {
        if (it.key == D.dragKey && dockMovable(it)) it.shift += D.dragCx - (it.slot.x - it.shift + app.cfg.dockIconSize * it.scale * D.squeeze / 2);
        if (dockMovable(it)) joined += (joined.empty() ? "" : ",") + it.key;
    }
    D.dragKey.clear();
    if (!joined.empty() && joined != app.cfg.dockPinned && setConfig(app, "dockPinned", joined).empty()) applyConfig(app);
    D.moving = true;
    dockAim(app);
}

void dockMotion(App& app) {
    DockState& D = app.dock;
    if ((D.pressIdx >= 0 || D.dragging) && dockDragMotion(app)) return;
    if (D.gridOpen && !D.gridClosing) {
        const int cell = dockGridCellAt(app);
        if (cell != D.gridHover) { D.gridHover = cell; D.moving = true; }
    }
    if (D.menuOpen) {
        const int row = dockMenuRowAt(app);
        if (row != D.menuHover) {
            D.menuHover = row;
            dockRedraw(app);
        }
        return;
    }
    if (D.slideTarget > 0.5) dockShow(app);
    D.hover = dockHitIndex(app);
    dockAim(app);
}

void dockEnter(App& app) {
    dockShow(app);
    dockMotion(app);
}

void dockLeave(App& app) {
    DockState& D = app.dock;
    app.onDock = false;
    if (D.pressIdx >= 0 || D.dragging) { D.pressIdx = -1; D.dragging = false; D.dragKey.clear(); dockRebuild(app); }
    D.hover = -1;
    dockCloseMenu(app);
    dockGridClose(app);
    if (app.cfg.dockAutoHide) {
        D.hideArmed = true;
        D.hideAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long>(kDockHideDelay * 1000));
    }
    dockAim(app);
}

void dockFocus(const std::string& addr) { wmFocusWindow(addr); }

void dockLaunch(const DockItem& it) {
    runDetached({"gtk-launch", (it.entryId.empty() ? it.key : it.entryId).c_str()});
}

void dockNewInstance(App& app, const DockItem& it) {
    if (it.exec.empty()) { dockLaunch(it); return; }
    const std::string cmd = stripFieldCodes(it.exec);
    if (it.terminal) runDetached({app.cfg.terminal.c_str(), "-e", "sh", "-c", cmd.c_str()});
    else runDetached({"sh", "-c", cmd.c_str()});
}

void dockActivate(App& app, int idx) {
    const DockItem it = app.dock.items[idx];
    if (it.special == 3) {
        if (app.dock.gridOpen && !app.dock.gridClosing) dockGridClose(app); else dockGridOpen(app);
        return;
    }
    if (it.special == 2) {
        const DockPt lp = dockPointer(app);
        const double py = lp.y - app.dock.slide * dockLH(app.dock);
        const char* cmd = app.dock.mediaNext.contains(lp.x, py) ? "next" : "play-pause";
        runDetached({"playerctl", "-p", playerSelector(app.cfg.preferredPlayer).c_str(), cmd});
        return;
    }
    if (it.special == 1) {
        if (setConfig(app, "dockAutoHide", app.cfg.dockAutoHide ? "0" : "1").empty()) applyConfig(app);
        return;
    }
    if (it.addresses.empty()) { dockLaunch(it); return; }
    const bool next = it.focused && it.addresses.size() > 1;
    dockFocus(next ? it.addresses[1] : it.addresses[0]);
}

void dockSetPinned(App& app, const std::string& key, bool pin) {
    std::vector<std::string> list = splitList(app.cfg.dockPinned);
    if (pin) {
        if (std::find(list.begin(), list.end(), key) == list.end()) list.push_back(key);
    } else {
        std::erase(list, key);
    }
    std::string joined;
    for (const std::string& k : list) joined += (joined.empty() ? "" : ",") + k;
    if (setConfig(app, "dockPinned", joined.empty() ? "-" : joined).empty()) applyConfig(app);
}

void dockClose(const std::string& addr) { wmCloseWindow(addr); }

void dockMenuAction(App& app, const DockItem& it, DockMenuKind kind) {
    switch (kind) {
        case DockMenuPin: dockSetPinned(app, it.entryId.empty() ? it.key : it.entryId, true); break;
        case DockMenuUnpin: dockSetPinned(app, it.key, false); break;
        case DockMenuNew: dockNewInstance(app, it); break;
        case DockMenuPrev: runDetached({"playerctl", "-p", playerSelector(app.cfg.preferredPlayer).c_str(), "previous"}); break;
        case DockMenuNext: runDetached({"playerctl", "-p", playerSelector(app.cfg.preferredPlayer).c_str(), "next"}); break;
        case DockMenuPlayer: toggleMediaPanel(app); break;
        case DockMenuClose:
            if (!it.addresses.empty()) dockClose(it.addresses[0]);
            break;
    }
}

void dockOpenMenu(App& app, int idx) {
    DockState& D = app.dock;
    const DockItem& it = D.items[idx];
    std::vector<std::pair<DockMenuKind, std::string>> rows;
    if (it.special == 2) {
        rows = {{DockMenuPrev, "Previous"}, {DockMenuNext, "Next"}, {DockMenuPlayer, "Open the player"}};
        D.menuRows = std::move(rows);
        D.menuOpen = true;
        D.menuItem = idx;
        D.menuHover = -1;
        D.menuX = it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2;
        dockRedraw(app);
        return;
    }
    rows.push_back(it.pinned ? std::make_pair(DockMenuUnpin, std::string("Unpin from the Dock")) : std::make_pair(DockMenuPin, std::string("Pin to the Dock")));
    if (!it.exec.empty()) rows.emplace_back(DockMenuNew, "New window");
    if (it.running) rows.emplace_back(DockMenuClose, "Close");
    D.menuRows = std::move(rows);
    D.menuOpen = true;
    D.menuItem = idx;
    D.menuHover = -1;
    D.menuX = it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2;
    dockRedraw(app);
}

void dockButton(App& app, uint32_t button) {
    constexpr uint32_t kBtnLeft = 0x110, kBtnRight = 0x111, kBtnMiddle = 0x112;
    DockState& D = app.dock;
    if (D.gridOpen && !D.gridClosing) {
        const int cell = dockGridCellAt(app);
        const DockPt slid = dockSlideOffset(D);
        if (D.gridCard.contains(app.pointerX - slid.x, app.pointerY - slid.y) || cell >= 0) {
            if (button == 0x110 && cell >= 0) dockGridLaunch(app, cell);
            return;
        }
        const bool onButton = D.hover >= 0 && D.hover < static_cast<int>(D.items.size()) && D.items[D.hover].special == 3;
        dockGridClose(app);
        if (onButton) return;
    }
    if (D.menuOpen) {
        const int row = dockMenuRowAt(app), item = D.menuItem;
        const bool chosen = row >= 0 && button == kBtnLeft && item >= 0 && item < static_cast<int>(D.items.size());
        const DockMenuKind kind = chosen ? D.menuRows[row].first : DockMenuPin;
        const DockItem it = chosen ? D.items[item] : DockItem{};
        dockCloseMenu(app);
        if (chosen) dockMenuAction(app, it, kind);
        dockRedraw(app);
        return;
    }
    if (D.hover < 0) return;
    if (button == kBtnLeft && dockMovable(D.items[D.hover])) {
        const DockItem& it = D.items[D.hover];
        D.pressIdx = D.hover;
        D.pressX = dockPointer(app).x;
        D.dragGrab = D.pressX - (it.slot.x + app.cfg.dockIconSize * it.scale * D.squeeze / 2);
        return;
    }
    if (button == kBtnLeft) dockActivate(app, D.hover);
    else if (D.items[D.hover].special == 1 || D.items[D.hover].special == 3) return;
    else if (D.items[D.hover].special == 2) {
        if (button == kBtnMiddle) dockMenuAction(app, D.items[D.hover], DockMenuNext);
        else if (button == kBtnRight) dockOpenMenu(app, D.hover);
    }
    else if (button == kBtnMiddle) dockNewInstance(app, D.items[D.hover]);
    else if (button == kBtnRight) dockOpenMenu(app, D.hover);
}

void dockGridLaunch(App& app, int appIdx) {
    DockItem it;
    it.key = it.entryId = app.apps[appIdx].id;
    dockLaunch(it);
    dockGridClose(app);
}

bool dockGridKey(App& app, uint32_t key) {
    DockState& D = app.dock;
    if (!app.xkbState || !D.gridOpen || D.gridClosing) return false;
    const xkb_keycode_t code = key + 8;  // evdev -> xkb
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code);
    const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    const int total = static_cast<int>(D.gridShown.size());
    auto textChanged = [&] {
        dockGridFilter(app);
        D.gridScroll = 0;
        D.gridHover = -1;
        D.moving = true;
        dockRedraw(app);
    };
    auto moveBy = [&](int delta) {
        if (total == 0) return;
        const auto at = std::find(D.gridShown.begin(), D.gridShown.end(), D.gridHover);
        const int from = at == D.gridShown.end() ? (delta > 0 ? -delta : 0) : static_cast<int>(at - D.gridShown.begin());
        const int pos = std::clamp(from + delta, 0, total - 1);
        D.gridHover = D.gridShown[pos];
        const int row = pos / kGridCols;
        if (row < D.gridScroll) D.gridScroll = row;
        else if (row >= D.gridScroll + kGridRows) D.gridScroll = row - kGridRows + 1;
        D.moving = true;
        dockRedraw(app);
    };
    switch (sym) {
        case XKB_KEY_Escape: dockGridClose(app); dockRedraw(app); return false;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            if (D.gridHover >= 0 && std::find(D.gridShown.begin(), D.gridShown.end(), D.gridHover) != D.gridShown.end()) dockGridLaunch(app, D.gridHover);
            else if (total > 0) dockGridLaunch(app, D.gridShown[0]);
            dockRedraw(app);
            return false;
        case XKB_KEY_Left: moveBy(-1); return true;
        case XKB_KEY_Right: moveBy(1); return true;
        case XKB_KEY_Up: moveBy(-kGridCols); return true;
        case XKB_KEY_Down: moveBy(kGridCols); return true;
        case XKB_KEY_BackSpace: {
            if (D.gridText.empty()) return true;
            size_t from = prevChar(D.gridText, D.gridText.size());
            if (ctrl) {
                size_t j = D.gridText.size();
                while (j > 0 && D.gridText[j - 1] == ' ') j--;
                while (j > 0 && D.gridText[j - 1] != ' ') j--;
                from = j;
            }
            D.gridText.erase(from);
            textChanged();
            return true;
        }
        default: break;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(app.xkbState, code, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return false;
    if (D.gridText.size() + n > 64) return true;
    D.gridText.append(buf, n);
    textChanged();
    return true;
}

void dockMediaChanged(App& app) {
    for (DockItem& it : app.dock.items) if (it.special == 2) it.extra = dockMediaExtra(app);
    dockFitWidth(app);
    dockRedraw(app);
}

void dockGridScroll(App& app, int dir) {
    DockState& D = app.dock;
    const int maxScroll = std::max(0, dockGridTotalRows(D) - kGridRows);
    const int next = std::clamp(D.gridScroll + dir, 0, maxScroll);
    if (next == D.gridScroll) return;
    D.gridScroll = next;
    D.gridHover = dockGridCellAt(app);
    D.moving = true;
}

void dockOnWindowsChanged(App& app) {
    if (!app.dock.open() || !app.dock.configured) return;
    dockRebuild(app);
    dockRedraw(app);
}

void dockWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    DockState& D = app.dock;
    if (!D.open() || !D.configured) return;
    auto wakeAt = [&](std::chrono::steady_clock::time_point when) {
        const int ms = std::max<int>(1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(when - now).count()));
        timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
    };
    if (D.hideArmed) {
        if (now >= D.hideAt) { D.hideArmed = false; dockHide(app); }
        else wakeAt(D.hideAt);
    }
    if (D.slideTarget < 0.5 && !app.onDock) {
        if (now >= D.nextRefresh) { dockRebuild(app); dockRedraw(app); }
        wakeAt(D.nextRefresh);
    }
    if (D.moving) {
        constexpr auto kFrame = std::chrono::milliseconds(14);
        if (now - D.lastStep >= kFrame) dockStep(app, now);
        if (D.moving) wakeAt(D.lastStep + kFrame);
    }
}
