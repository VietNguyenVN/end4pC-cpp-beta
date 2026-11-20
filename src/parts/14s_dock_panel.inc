// The "docktopanel" bar module: the Dock's apps (pinned ones, a separator, then the running ones that are not pinned) as icons in the bar.
// Left click: launch it, or bring its windows forward one after the other; middle click: a new window; right click: pin or unpin it;
// dragging a pinned one reorders the pinned apps. Under each icon, one dot per window (three at most), the accent colour while one of them has the focus.
constexpr double kDtpIcon = 23, kDtpBtn = 28, kDtpGap = 2, kDtpSep = 1 + 3 * kDtpGap, kDtpPad = 2, kDtpRadius = 8, kDtpDragStart = 5;

std::shared_ptr<cairo_surface_t> dtpIcon(App& app, const std::string& name) {
    if (name.empty()) return nullptr;
    if (auto hit = app.dtp.icons.find(name); hit != app.dtp.icons.end()) return hit->second;
    constexpr int px = static_cast<int>(2 * kDtpIcon);  // twice the size: it stays sharp on a scaled screen
    std::shared_ptr<cairo_surface_t> surf;
    const std::string path = lookupIcon(name, px);
    if (!path.empty())
        if (cairo_surface_t* raw = loadIconScaled(path, px)) surf = std::shared_ptr<cairo_surface_t>(raw, cairo_surface_destroy);
    app.dtp.icons[name] = surf;
    return surf;
}

std::vector<DockItem>& dtpItems(const App& constApp) {
    App& app = const_cast<App&>(constApp);
    DockPanelState& P = app.dtp;
    if (!P.dirty || P.dragging) return P.items;
    P.dirty = false;
    ensureApps(app);
    std::vector<DockItem> pinned, loose;
    dockPinnedAndLoose(app, dockWindows(), pinned, loose);
    std::vector<DockItem> items = std::move(pinned);
    if (!items.empty() && !loose.empty()) { DockItem sep; sep.separator = true; items.push_back(sep); }
    for (DockItem& it : loose) items.push_back(std::move(it));
    for (DockItem& it : items) if (!it.separator) it.icon = dtpIcon(app, it.iconName);
    P.items = std::move(items);
    return P.items;
}

double dtpWidth(const App& app) {
    const auto& items = dtpItems(app);
    if (items.empty()) return 0;
    double w = 2 * kDtpPad - kDtpGap;
    for (const DockItem& it : items) w += (it.separator ? kDtpSep : kDtpBtn) + kDtpGap;
    return w;
}

double dtpItemOffset(const App& app, size_t index) {
    const auto& items = dtpItems(app);
    double x = kDtpPad;
    for (size_t i = 0; i < index && i < items.size(); i++) x += (items[i].separator ? kDtpSep : kDtpBtn) + kDtpGap;
    return x;
}

// Where the item the pointer is over sits while one is being dragged: the others make room for it.
double dtpShifted(const App& app, size_t index) {
    const DockPanelState& P = app.dtp;
    double x = dtpItemOffset(app, index);
    if (!P.dragging || P.dragFrom < 0 || P.dragTo < 0) return x;
    const double slot = kDtpBtn + kDtpGap;
    const int from = P.dragFrom, to = P.dragTo, i = static_cast<int>(index);
    if (i == from) return x;
    if (from < to && i > from && i <= to) return x - slot;
    if (from > to && i >= to && i < from) return x + slot;
    return x;
}

Hit dtpHit(const App& app, const Bar& bar, double x) {
    if (!hasSlot(bar, "docktopanel")) return HitNone;
    const auto& items = dtpItems(app);
    const double left = slotX(bar, "docktopanel");
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].separator) continue;
        const double ix = left + dtpItemOffset(app, i);
        if (x >= ix - kDtpGap / 2 && x < ix + kDtpBtn + kDtpGap / 2) return kHitDtp + static_cast<int>(i);
    }
    return HitNone;
}

void dtpDrawItem(cairo_t* cr, App& app, const DockItem& it, double bx, double by, bool hot, double alpha = 1) {
    if (hot) {
        roundedRect(cr, bx, by, kDtpBtn, kDtpBtn, kDtpRadius);
        setSource(cr, app.color("on_surface"), 0.1 * alpha);
        cairo_fill(cr);
    }
    const double ix = std::round(bx + (kDtpBtn - kDtpIcon) / 2), iy = std::round(by + (kDtpBtn - kDtpIcon) / 2);
    if (it.icon) {
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        cairo_scale(cr, kDtpIcon / cairo_image_surface_get_width(it.icon.get()), kDtpIcon / cairo_image_surface_get_height(it.icon.get()));
        cairo_set_source_surface(cr, it.icon.get(), 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint_with_alpha(cr, alpha);
        cairo_restore(cr);
    } else {
        drawIcon(cr, "window-frame", ix, iy, static_cast<int>(kDtpIcon), app.color("on_surface_variant"));
    }
    const int windows = std::min<int>(static_cast<int>(it.addresses.size()), 3);
    if (windows > 0) {
        const double dotW = it.addresses.size() <= 3 ? 4 : 2, total = windows * dotW + (windows - 1) * 2;
        double dx = bx + (kDtpBtn - total) / 2;
        const double dy = iy + kDtpIcon + 1;
        for (int k = 0; k < windows; k++, dx += dotW + 2) {
            roundedRect(cr, dx, dy, dotW, 2, 1);
            if (it.focused) setSource(cr, app.color("primary"), alpha);
            else setSource(cr, app.color("on_surface"), 0.6 * alpha);
            cairo_fill(cr);
        }
    }
}

void dtpBarPaint(cairo_t* cr, App& app, const Bar& bar, Hit hot) {
    if (!hasSlot(bar, "docktopanel")) return;
    const auto& items = dtpItems(app);
    const DockPanelState& P = app.dtp;
    const double left = slotX(bar, "docktopanel"), top = (bar.height - kDtpBtn) / 2.0;
    int dragged = -1;
    for (size_t i = 0; i < items.size(); i++) {
        const DockItem& it = items[i];
        const double bx = left + dtpShifted(app, i);
        if (it.separator) {
            const double h = std::round(kDtpBtn * 0.6);
            cairo_new_path(cr);
            cairo_rectangle(cr, std::round(bx + kDtpSep / 2.0 - 0.5), (bar.height - h) / 2.0, 1, h);
            setSource(cr, app.color("outline_variant"));
            cairo_fill(cr);
            continue;
        }
        if (P.dragging && static_cast<int>(i) == P.dragFrom) { dragged = static_cast<int>(i); continue; }
        dtpDrawItem(cr, app, it, bx, top, hot == kHitDtp + static_cast<int>(i) && !P.dragging);
    }
    if (dragged >= 0) {  // the dragged one follows the pointer, a little see-through, over the others
        const double bx = std::clamp(app.pointerX - kDtpBtn / 2.0, left, left + dtpWidth(app) - kDtpBtn);
        dtpDrawItem(cr, app, items[dragged], bx, top, true, 0.85);
    }
}

std::vector<std::pair<int, Rect>> dtpSideRects(const App& app, double x, double y) {
    std::vector<std::pair<int, Rect>> out;
    const auto& items = dtpItems(app);
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].separator) out.push_back({kHitDtp + static_cast<int>(i), {x, y, 40, kDtpBtn}});
        y += (items[i].separator ? kDtpSep : kDtpBtn) + kDtpGap;
    }
    return out;
}

double dtpSideHeight(const App& app) {
    const auto& items = dtpItems(app);
    if (items.empty()) return 0;
    double h = -kDtpGap;
    for (const DockItem& it : items) h += (it.separator ? kDtpSep : kDtpBtn) + kDtpGap;
    return h;
}

void dtpMarkDirty(App& app) { app.dtp.dirty = true; }

// The pinned apps in their new order (what dragging one gives).
void dtpCommitOrder(App& app, int from, int to) {
    const auto& items = dtpItems(app);
    std::vector<std::string> keys;
    for (const DockItem& it : items) {
        if (it.separator) break;
        if (it.pinned) keys.push_back(it.key);
    }
    if (from < 0 || to < 0 || from >= static_cast<int>(keys.size()) || to >= static_cast<int>(keys.size()) || from == to) return;
    const std::string moved = keys[from];
    keys.erase(keys.begin() + from);
    keys.insert(keys.begin() + to, moved);
    std::string joined;
    for (const std::string& k : keys) joined += (joined.empty() ? "" : ",") + k;
    if (setConfig(app, "dockPinned", joined.empty() ? "-" : joined).empty()) applyConfig(app);
}

int dtpIndexAt(const App& app, double x, double left) {
    const auto& items = dtpItems(app);
    int best = -1;
    double bestD = 1e9;
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].separator || !items[i].pinned) continue;
        const double d = std::fabs(left + dtpItemOffset(app, i) + kDtpBtn / 2 - x);
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

void dtpActivate(App& app, const DockItem& it) {
    if (it.addresses.empty()) { dockLaunch(it); return; }
    const bool next = it.focused && it.addresses.size() > 1;
    dockFocus(next ? it.addresses[1] : it.addresses[0]);
}

// A press on an icon: left starts a click that can become a drag, the others act at once.
void dtpPress(App& app, int index, uint32_t button) {
    const auto& items = dtpItems(app);
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    const DockItem it = items[index];
    if (button == 0x110) {
        app.dtp.pressIdx = index;
        app.dtp.pressX = app.pointerX;
    } else if (button == 0x112) {
        dockNewInstance(app, it);
    } else if (button == 0x111) {
        dockSetPinned(app, it.pinned ? it.key : (it.entryId.empty() ? it.key : it.entryId), !it.pinned);
    }
}

void dtpMotion(App& app, const Bar& bar) {
    DockPanelState& P = app.dtp;
    if (P.pressIdx < 0) return;
    const auto& items = dtpItems(app);
    if (!P.dragging) {
        if (std::fabs(app.pointerX - P.pressX) < kDtpDragStart || P.pressIdx >= static_cast<int>(items.size()) || !items[P.pressIdx].pinned) return;
        P.dragging = true;
        P.dragFrom = P.dragTo = P.pressIdx;
    }
    const int over = dtpIndexAt(app, app.pointerX, slotX(bar, "docktopanel"));
    if (over >= 0) P.dragTo = over;
}

// The button was let go: a click if it never became a drag, else the new order.
void dtpRelease(App& app) {
    DockPanelState& P = app.dtp;
    if (P.pressIdx < 0) return;
    const int index = P.pressIdx, from = P.dragFrom, to = P.dragTo;
    const bool wasDrag = P.dragging;
    P.pressIdx = -1;
    P.dragging = false;
    P.dragFrom = P.dragTo = -1;
    if (wasDrag) {
        dtpCommitOrder(app, from, to);
        P.dirty = true;
    } else {
        const auto& items = dtpItems(app);
        if (index < static_cast<int>(items.size())) dtpActivate(app, items[index]);
    }
}
