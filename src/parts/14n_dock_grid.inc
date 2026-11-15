
constexpr int kGridCols = 5, kGridRows = 4;
constexpr double kGridSearchH = 40, kGridSearchGap = 10;
constexpr double kGridTail = 9, kGridTailGap = 10;
constexpr double kGridCellW = 90, kGridCellH = 96, kGridPad = 14, kGridIcon = 50, kGridRadius = 24;
constexpr double kGridOpenSeconds = 0.38, kGridCloseSeconds = 0.14;
constexpr double kGridIconSeconds = 0.34, kGridIconDelay = 0.022, kGridBoing = 2.2;

double dockEaseOutBack(double t, double boing) {
    t = std::clamp(t, 0.0, 1.0);
    const double c3 = boing + 1;
    return 1 + c3 * std::pow(t - 1, 3) + boing * std::pow(t - 1, 2);
}

double dockGridReveal(const DockState& D) {
    if (!D.gridOpen) return 0;
    if (D.gridClosing) return 1 - std::clamp(D.gridT / kGridCloseSeconds, 0.0, 1.0);
    return dockEaseOutBack(D.gridT / kGridOpenSeconds, 1.4);
}

void dockGridFilter(App& app) {
    DockState& D = app.dock;
    D.gridShown.clear();
    const std::string q = dockLower(D.gridText);
    if (q.empty()) { D.gridShown = D.gridApps; return; }
    std::vector<int> rest;
    for (int idx : D.gridApps) {
        const AppEntry& e = app.apps[idx];
        const std::string name = dockLower(e.name);
        if (name.rfind(q, 0) == 0) D.gridShown.push_back(idx);
        else if (name.find(q) != std::string::npos || dockLower(e.generic).find(q) != std::string::npos ||
                 dockLower(e.keywords).find(q) != std::string::npos || dockLower(e.id).find(q) != std::string::npos) rest.push_back(idx);
    }
    D.gridShown.insert(D.gridShown.end(), rest.begin(), rest.end());
}

void dockGridCollect(App& app) {
    DockState& D = app.dock;
    ensureApps(app);
    if (D.gridAppsFor == app.apps.size() && !D.gridApps.empty()) { dockGridFilter(app); return; }
    D.gridApps.clear();
    for (size_t i = 0; i < app.apps.size(); i++) D.gridApps.push_back(static_cast<int>(i));
    std::sort(D.gridApps.begin(), D.gridApps.end(), [&](int a, int b) { return dockLower(app.apps[a].name) < dockLower(app.apps[b].name); });
    D.gridAppsFor = app.apps.size();
    dockGridFilter(app);
}

int dockGridTotalRows(const DockState& D) { return (static_cast<int>(D.gridShown.size()) + kGridCols - 1) / kGridCols; }

double dockGridAnchorX(const App& app) {
    const DockState& D = app.dock;
    for (const DockItem& it : D.items) if (it.special == 3) return it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2;
    return D.pill.x + D.pill.w / 2;
}

Rect dockGridLayout(const App& app) {
    const DockState& D = app.dock;
    const double w = kGridCols * kGridCellW + 2 * kGridPad, h = kGridRows * kGridCellH + 2 * kGridPad + kGridSearchH + kGridSearchGap;
    return dockBeside(D, dockToPhys(D, dockGridAnchorX(app), D.pill.y - kGridTailGap - kGridTail - dockPopPx(app)), w, h, 6);
}

void dockGridOpen(App& app) {
    DockState& D = app.dock;
    dockGridCollect(app);
    if (!app.avatarTried) loadAvatar(app);
    D.gridOpen = true;
    D.gridClosing = false;
    D.gridStart = std::chrono::steady_clock::now();
    D.gridT = 0;
    D.gridScroll = 0;
    D.gridHover = -1;
    D.gridText.clear();
    dockGridFilter(app);
    zwlr_layer_surface_v1_set_keyboard_interactivity(D.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    D.moving = true;
}

void dockGridClose(App& app) {
    DockState& D = app.dock;
    if (!D.gridOpen || D.gridClosing) return;
    D.gridClosing = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(D.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    D.gridStart = std::chrono::steady_clock::now();
    D.gridT = 0;
    D.gridHover = -1;
    D.moving = true;
}

bool dockGridTick(App& app, std::chrono::steady_clock::time_point now) {
    DockState& D = app.dock;
    if (!D.gridOpen) return false;
    D.gridT = std::chrono::duration<double>(now - D.gridStart).count();
    if (D.gridClosing) {
        if (D.gridT < kGridCloseSeconds) return true;
        D.gridOpen = D.gridClosing = false;
        D.gridCells.clear();
        return true;
    }
    const double settle = kGridOpenSeconds + kGridIconDelay * (kGridCols + kGridRows) + kGridIconSeconds;
    return D.gridT < settle;
}

void dockPaintGrid(cairo_t* cr, App& app) {
    DockState& D = app.dock;
    D.gridCells.clear();
    if (!D.gridOpen) return;
    const double reveal = dockGridReveal(D);
    D.gridCard = dockGridLayout(app);
    const Rect c = D.gridCard;
    const double R = kGridRadius;
    const bool side = dockVertical(D);
    const double lw = side ? c.h : c.w, lh = side ? c.w : c.h;
    const DockPt button = dockToPhys(D, dockGridAnchorX(app), D.pill.y);
    const double tipU = std::clamp(side ? button.y - c.y : button.x - c.x, R + kGridTail, lw - R - kGridTail);
    cairo_matrix_t T;
    switch (D.pos) {
        case kDockTop: cairo_matrix_init(&T, 1, 0, 0, -1, c.x, c.y + c.h); break;
        case kDockLeft: cairo_matrix_init(&T, 0, 1, -1, 0, c.x + c.w, c.y); break;
        case kDockRight: cairo_matrix_init(&T, 0, 1, 1, 0, c.x, c.y); break;
        default: cairo_matrix_init(&T, 1, 0, 0, 1, c.x, c.y); break;
    }
    double ox = tipU, oy = lh + kGridTail;
    cairo_matrix_transform_point(&T, &ox, &oy);
    const double scale = 0.35 + 0.65 * reveal;
    cairo_save(cr);
    popGroupBegin(cr);
    cairo_save(cr);
    cairo_transform(cr, &T);
    cairo_new_path(cr);
    cairo_arc(cr, R, R, R, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, lw - R, R, R, -M_PI / 2, 0);
    {
        cairo_arc(cr, lw - R, lh - R, R, 0, M_PI / 2);
        cairo_line_to(cr, tipU + kGridTail, lh);
        cairo_line_to(cr, tipU, lh + kGridTail);
        cairo_line_to(cr, tipU - kGridTail, lh);
        cairo_arc(cr, R, lh - R, R, M_PI / 2, M_PI);
    }
    cairo_close_path(cr);
    cairo_restore(cr);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    if (app.cfg.dockBorder > 0) {
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, app.cfg.dockBorder);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        setSource(cr, app.color(app.cfg.dockBorderColor.c_str()));
        cairo_stroke(cr);
    } else {
        cairo_fill(cr);
    }
    const int total = static_cast<int>(D.gridShown.size()), maxScroll = std::max(0, dockGridTotalRows(D) - kGridRows);
    D.gridScroll = std::clamp(D.gridScroll, 0, maxScroll);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    {
        constexpr double kAvatar = kGridSearchH, kAvatarGap = 10;
        const bool hasAvatar = app.avatar != nullptr;
        const Rect f{c.x + kGridPad, c.y + c.h - kGridPad - kGridSearchH, c.w - 2 * kGridPad - (hasAvatar ? kAvatar + kAvatarGap : 0), kGridSearchH};
        roundedRect(cr, f.x, f.y, f.w, f.h, f.h / 2);
        setSource(cr, app.color("surface_container_highest"), 0.9);
        cairo_fill(cr);
        drawIcon(cr, "search", f.x + 14, f.y + (f.h - 18) / 2, 18, app.color("on_surface_variant"));
        PangoFontDescription* sf = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, sf);
        pango_font_description_free(sf);
        pango_layout_set_width(layout, static_cast<int>((f.w - 62) * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_START);
        pango_layout_set_text(layout, D.gridText.empty() ? "Search apps" : D.gridText.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, f.x + 42, f.y + (f.h - th) / 2.0);
        setSource(cr, app.color(D.gridText.empty() ? "on_surface_variant" : "on_surface"), D.gridText.empty() ? 0.7 : 1.0);
        pango_cairo_show_layout(cr, layout);
        if (!D.gridClosing) {
            const double cx = f.x + 42 + (D.gridText.empty() ? 0 : tw) + 1;
            cairo_rectangle(cr, cx, f.y + (f.h - 18) / 2.0, 1.5, 18);
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
        }
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        if (hasAvatar) {
            const double ax = f.x + f.w + kAvatarGap, ay = f.y, sc = kAvatar / cairo_image_surface_get_width(app.avatar);
            cairo_save(cr);
            cairo_arc(cr, ax + kAvatar / 2, ay + kAvatar / 2, kAvatar / 2, 0, 2 * M_PI);
            cairo_clip(cr);
            cairo_translate(cr, ax, ay);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, app.avatar, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }
    PangoFontDescription* fd = pango_font_description_from_string("Sans 9");
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_width(layout, static_cast<int>((kGridCellW - 8) * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    for (int vr = 0; vr < kGridRows; vr++) {
        for (int col = 0; col < kGridCols; col++) {
            const int slot = (D.gridScroll + vr) * kGridCols + col;
            if (slot >= total) break;
            const int appIdx = D.gridShown[slot];
            const AppEntry& e = app.apps[appIdx];
            const Rect cell{c.x + kGridPad + col * kGridCellW, c.y + kGridPad + vr * kGridCellH, kGridCellW, kGridCellH};
            D.gridCells.emplace_back(cell, appIdx);
            if (D.gridHover == appIdx) {
                roundedRect(cr, cell.x + 3, cell.y + 3, cell.w - 6, cell.h - 6, 16);
                setSource(cr, app.color("surface_container_highest"));
                cairo_fill(cr);
            }
            const double u = D.gridClosing ? 1.0 : std::clamp((D.gridT - 0.04 - kGridIconDelay * (vr + col)) / kGridIconSeconds, 0.0, 1.0);
            if (u <= 0) continue;
            const double pop = D.gridClosing ? 1.0 : dockEaseOutBack(u, kGridBoing), size = kGridIcon * pop;
            DockItem it;
            it.key = e.id;
            it.name = e.name;
            it.icon = dockIcon(app, e.icon.empty() ? e.id : e.icon);
            const double cx = cell.x + cell.w / 2, cy = cell.y + 8 + kGridIcon / 2;
            dockPaintIcon(cr, app, it, cx - size / 2, cy - size / 2, size);
            pango_layout_set_text(layout, e.name.c_str(), -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr, cell.x + 4, cell.y + 8 + kGridIcon + 6);
            setSource(cr, app.color("on_surface"), std::min(1.0, u * 1.5));
            pango_cairo_show_layout(cr, layout);
        }
    }
    if (total == 0) {
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, "No apps found", -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, c.x + (c.w - tw) / 2, c.y + kGridPad + 2 * kGridCellH - th / 2);
        setSource(cr, app.color("on_surface_variant"));
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(fd);
    g_object_unref(layout);
    popGroupEnd(cr, scale, ox, oy, std::clamp(reveal * 1.6, 0.0, 1.0));
    cairo_restore(cr);
}
