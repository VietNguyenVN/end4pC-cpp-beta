
double dockPillRadius(const App& app, const Rect& p) { return std::min<double>(app.cfg.dockRounding, p.h / 2); }
double dockHugFlare(const App& app, const Rect& p) { return std::min(dockPillRadius(app, p), 18.0); }

void dockPillPath(cairo_t* cr, const App& app, const Rect& p) {
    const double r = dockPillRadius(app, p);
    if (!app.cfg.dockHug) { roundedRect(cr, p.x, p.y, p.w, p.h, r); return; }
    const double f = dockHugFlare(app, p), bottom = p.y + p.h, right = p.x + p.w;
    cairo_new_path(cr);
    cairo_move_to(cr, p.x - f, bottom);
    cairo_arc_negative(cr, p.x - f, bottom - f, f, M_PI / 2, 0);
    cairo_arc(cr, p.x + r, p.y + r, r, M_PI, 3 * M_PI / 2);
    cairo_arc(cr, right - r, p.y + r, r, -M_PI / 2, 0);
    cairo_arc_negative(cr, right + f, bottom - f, f, M_PI, M_PI / 2);
}

// The soft shadow: a few rounded layers, each a little wider and lower, at a small alpha that adds up to the shadow's 0.3. They are
// drawn outside the pill only (the card is see-through, so the shadow must not show through it). Under the blur rule's ignore_alpha,
// so Hyprland never blurs it. None for the hug style: it sits on the edge.
// On sway (SwayFX) the layer blur follows every pixel that is not fully transparent, so a soft shadow would be blurred as a halo
// round the dock: no shadow there (the same as the popups, see makeShadowMask). Other compositors are unaffected.
void dockPaintShadow(cairo_t* cr, const App& app, const Rect& p) {
    if (app.cfg.dockHug || wmKind() == Wm::Sway) return;
    constexpr int kLayers = 6;
    const double r = dockPillRadius(app, p);
    cairo_save(cr);
    cairo_rectangle(cr, -1000, -1000, dockLW(app.dock) + 2000.0, dockLH(app.dock) + 2000.0);
    roundedRect(cr, p.x, p.y, p.w, p.h, r);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip(cr);
    cairo_matrix_t inv = dockFrame(app.dock);
    cairo_matrix_invert(&inv);
    double sx = 0, sy = 3;
    cairo_matrix_transform_distance(&inv, &sx, &sy);
    for (int k = 1; k <= kLayers; k++) {
        const double grow = 2.5 * k;
        roundedRect(cr, p.x - grow + sx, p.y - grow + sy, p.w + 2 * grow, p.h + 2 * grow, r + grow);
        setSource(cr, app.color("shadow"), 0.3 / kLayers);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void dockPaintPill(cairo_t* cr, const App& app) {
    const Rect p = app.dock.pill;
    dockPaintShadow(cr, app, p);
    dockPillPath(cr, app, p);
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
}

void dockPaintTile(cairo_t* cr, const App& app, const DockItem& it, double x, double top, double size) {
    roundedRect(cr, x, top, size, size, size * 0.28);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);
    const std::string letter = clkFirstGlyph(it.name.empty() ? it.key : it.name);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, std::max(6.0, std::round(size * 0.5)) * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, letter.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, x + (size - tw) / 2, top + (size - th) / 2);
    setSource(cr, app.color("on_surface"));
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(fd);
    g_object_unref(layout);
}

void dockPaintIcon(cairo_t* cr, const App& app, const DockItem& it, double x, double top, double size) {
    if (it.special == 2 && app.musicArt && !app.musicStatus.empty()) {
        const double px = cairo_image_surface_get_width(app.musicArt);
        cairo_save(cr);
        roundedRect(cr, x, top, size, size, size * 0.28);
        cairo_clip(cr);
        cairo_translate(cr, x, top);
        cairo_scale(cr, size / px, size / px);
        cairo_set_source_surface(cr, app.musicArt, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        if (app.musicStatus == "Paused") {
            roundedRect(cr, x, top, size, size, size * 0.28);
            setSource(cr, app.color("scrim"), 0.55);
            cairo_fill(cr);
            const int glyph = std::max(8, static_cast<int>(size * 0.5));
            drawIcon(cr, "pause", x + (size - glyph) / 2.0, top + (size - glyph) / 2.0, glyph, Color{1, 1, 1}, true);
        }
        return;
    }
    if (it.special == 2) {
        const DockState& D = app.dock;
        if (D.hover >= 0 && D.hover < static_cast<int>(D.items.size()) && &D.items[D.hover] == &it) {
            roundedRect(cr, x, top, size, size, size * 0.28);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
        }
        const int glyph = std::max(8, static_cast<int>(size * 0.56));
        drawIcon(cr, "music_note", x + (size - glyph) / 2.0, top + (size - glyph) / 2.0, glyph, app.color(app.musicStatus.empty() ? "on_surface_variant" : "primary"), !app.musicStatus.empty());
        return;
    }
    if (it.special) {
        const DockState& D = app.dock;
        if (D.hover >= 0 && D.hover < static_cast<int>(D.items.size()) && &D.items[D.hover] == &it) {
            roundedRect(cr, x, top, size, size, size * 0.28);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
        }
        const int glyph = std::max(8, static_cast<int>(size * 0.56));
        if (it.special == 3) drawIcon(cr, "apps", x + (size - glyph) / 2.0, top + (size - glyph) / 2.0, glyph, app.color(D.gridOpen ? "primary" : "on_surface"), D.gridOpen);
        else drawIcon(cr, "push_pin", x + (size - glyph) / 2.0, top + (size - glyph) / 2.0, glyph, app.color(app.cfg.dockAutoHide ? "on_surface_variant" : "primary"), !app.cfg.dockAutoHide);
        return;
    }
    if (!it.icon) { dockPaintTile(cr, app, it, x, top, size); return; }
    const double px = cairo_image_surface_get_width(it.icon.get());
    cairo_save(cr);
    cairo_translate(cr, x, top);
    cairo_scale(cr, size / px, size / px);
    cairo_set_source_surface(cr, it.icon.get(), 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

void dockPaintTrack(cairo_t* cr, App& app, double x, double midY, double width) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* small = pango_font_description_from_string(kArtistFont);
    PangoFontDescription* bold = pango_font_description_from_string(kTitleFont);
    pango_layout_set_width(layout, static_cast<int>(width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_font_description(layout, small);
    pango_layout_set_text(layout, app.musicArtist.empty() ? " " : app.musicArtist.c_str(), -1);
    int tw = 0, ah = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &ah);
    pango_layout_set_font_description(layout, bold);
    pango_layout_set_text(layout, app.musicTitle.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double top = midY - (ah + th + kMusicLineSpacing) / 2.0;
    pango_layout_set_font_description(layout, small);
    pango_layout_set_text(layout, app.musicArtist.c_str(), -1);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, x, top);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_font_description(layout, bold);
    pango_layout_set_text(layout, app.musicTitle.c_str(), -1);
    setSource(cr, app.color(app.musicStatus == "Playing" ? "on_surface" : "on_surface_variant"));
    cairo_move_to(cr, x, top + ah + kMusicLineSpacing);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(small);
    pango_font_description_free(bold);
    g_object_unref(layout);
    DockState& D = app.dock;
    const DockPt lp = dockPointer(app);
    const double bx = x + width + 8, by = midY - kDockCtlSize / 2, py = lp.y - D.slide * dockSlideDist(D);
    D.mediaPlay = Rect{bx, by, kDockCtlSize, kDockCtlSize};
    D.mediaNext = Rect{bx + kDockCtlSize + kDockCtlGap, by, kDockCtlSize, kDockCtlSize};
    const bool playing = app.musicStatus == "Playing";
    const std::pair<const Rect*, const char*> buttons[2] = {{&D.mediaPlay, playing ? "pause" : "play_arrow"}, {&D.mediaNext, "skip_next"}};
    for (const auto& [r, icon] : buttons) {
        if (app.onDock && r->contains(lp.x, py)) {
            roundedRect(cr, r->x, r->y, r->w, r->h, r->h / 2);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
        }
        const int glyph = static_cast<int>(kDockCtlSize * 0.62);
        drawIcon(cr, icon, r->x + (r->w - glyph) / 2.0, r->y + (r->h - glyph) / 2.0, glyph, app.color("on_surface"), true);
    }
}

void dockPaintItems(cairo_t* cr, App& app) {
    app.dock.mediaPlay = app.dock.mediaNext = Rect{};
    const DockState& D = app.dock;
    const double floor = D.pill.y + D.pill.h - kDockDotRoom, px = app.cfg.dockIconSize;
    for (const DockItem& it : D.items) {
        const double cx = it.separator ? it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2 : it.slot.x + px * it.scale * D.squeeze / 2;
        if (it.separator) {
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, cx, floor - px * 0.85);
            cairo_line_to(cr, cx, floor - px * 0.15);
            setSource(cr, app.color("outline_variant"));
            cairo_stroke(cr);
            continue;
        }
        const double size = px * it.scale * D.squeeze;
        if (D.dragging && it.key == D.dragKey) continue;
        dockUpright(cr, D, cx, floor - size / 2, [&] { dockPaintIcon(cr, app, it, cx - size / 2, floor - size, size); });
        if (it.special == 2 && it.extra > 0)
            dockUpright(cr, D, cx + size / 2 + 10, floor - size / 2, [&] { dockPaintTrack(cr, app, cx + size / 2 + 10, floor - size / 2, std::min(app.musicTextW, kDockTrackMax)); });
        if (!it.running) continue;
        cairo_arc(cr, cx, floor + kDockDotRoom / 2.0, 2.5, 0, 2 * M_PI);
        setSource(cr, app.color(it.focused ? "primary" : "on_surface_variant"), it.focused ? 1.0 : 0.7);
        cairo_fill(cr);
    }
    if (D.dragging) {
        for (const DockItem& it : D.items) {
            if (it.key != D.dragKey) continue;
            const double size = px * 1.15, cx = std::clamp(D.dragCx, D.pill.x + kDockPadX, D.pill.x + D.pill.w - kDockPadX), top = floor - size - 6;
            cairo_save(cr);
            cairo_translate(cr, cx, floor - 2);
            cairo_scale(cr, 1, 0.22);
            cairo_pattern_t* sh = cairo_pattern_create_radial(0, 0, 0, 0, 0, size * 0.45);
            cairo_pattern_add_color_stop_rgba(sh, 0, 0, 0, 0, 0.35);
            cairo_pattern_add_color_stop_rgba(sh, 1, 0, 0, 0, 0);
            cairo_set_source(cr, sh);
            cairo_arc(cr, 0, 0, size * 0.45, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(sh);
            cairo_restore(cr);
            dockUpright(cr, D, cx, top + size / 2, [&] { dockPaintIcon(cr, app, it, cx - size / 2, top, size); });
        }
    }
}

void dockPaintLabel(cairo_t* cr, const App& app) {
    const DockState& D = app.dock;
    const DockItem& it = D.items[D.hover];
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, it.name.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double bw = tw + 22, bh = th + 12, cx = it.slot.x + (it.slot.w - app.cfg.dockSpacing) / 2;
    const double lift = std::max(0.0, app.cfg.dockIconSize * (it.scale * D.squeeze - 1));
    const Rect box = dockBeside(D, dockToPhys(D, cx, D.pill.y - lift - 10), bw, bh, 4);
    const double x = box.x, y = box.y;
    roundedRect(cr, x, y, bw, bh, bh / 2);
    setSource(cr, app.color("surface_container_highest"), 0.95);
    cairo_fill(cr);
    cairo_move_to(cr, x + 11, y + 6);
    setSource(cr, app.color("on_surface"));
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(fd);
    g_object_unref(layout);
}

constexpr double kDockMenuW = 200, kDockMenuRowH = 30;

void dockPaintMenu(cairo_t* cr, App& app) {
    const DockState& D = app.dock;
    const double rows = static_cast<double>(D.menuRows.size());
    const double lift = D.menuItem >= 0 && D.menuItem < static_cast<int>(D.items.size()) ? std::max(0.0, app.cfg.dockIconSize * (D.items[D.menuItem].scale * D.squeeze - 1)) : 0.0;
    const Rect box = dockBeside(D, dockToPhys(D, D.menuX, D.pill.y - 10 - lift), kDockMenuW, rows * kDockMenuRowH + 12, 6);
    const double x = box.x, top = box.y;
    roundedRect(cr, x, top, kDockMenuW, rows * kDockMenuRowH + 12, 16);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(layout, fd);
    std::vector<Rect> rects;
    for (size_t i = 0; i < D.menuRows.size(); i++) {
        const double rowTop = top + 6 + static_cast<double>(i) * kDockMenuRowH;
        rects.push_back(Rect{x, rowTop, kDockMenuW, kDockMenuRowH});
        if (static_cast<int>(i) == D.menuHover) {
            roundedRect(cr, x + 6, rowTop + 2, kDockMenuW - 12, kDockMenuRowH - 4, 10);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
        }
        pango_layout_set_text(layout, D.menuRows[i].second.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, x + 16, rowTop + (kDockMenuRowH - th) / 2);
        setSource(cr, app.color("on_surface"));
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(fd);
    g_object_unref(layout);
    app.dock.menuRects = rects;
}

void dockPaint(App& app, cairo_t* cr) {
    dockLayout(app);
    const DockState& D = app.dock;
    cairo_save(cr);
    const cairo_matrix_t frame = dockFrame(D);
    cairo_transform(cr, &frame);
    cairo_translate(cr, 0, D.slide * dockSlideDist(D));
    dockPaintPill(cr, app);
    dockPaintItems(cr, app);
    cairo_restore(cr);
    const DockPt slid = dockSlideOffset(D);
    cairo_translate(cr, slid.x, slid.y);
    if (D.menuOpen) dockPaintMenu(cr, app);
    else if (D.hover >= 0 && D.hover < static_cast<int>(D.items.size()) && !D.gridOpen && D.items[D.hover].special != 2) dockPaintLabel(cr, app);
    dockPaintGrid(cr, app);
}

Rect dockRectToLogical(const DockState& D, const Rect& r) {
    const DockPt a = dockToLogical(D, r.x, r.y), b = dockToLogical(D, r.x + r.w, r.y + r.h);
    return Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

void dockSetInput(App& app) {
    DockState& D = app.dock;
    wl_region* region = wl_compositor_create_region(app.compositor);
    auto add = [&](const Rect& logical) {
        const Rect r = dockRectToPhys(D, logical);
        wl_region_add(region, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w), static_cast<int>(r.h));
    };
    const int LW = dockLW(D), LH = dockLH(D);
    add(Rect{0, static_cast<double>(LH - kDockStrip), static_cast<double>(LW), static_cast<double>(kDockStrip)});
    const double dy = D.slide * dockSlideDist(D), top = D.pill.y + dy;
    const int pad = app.cfg.dockHug ? 22 : 6;
    if (top < LH) add(Rect{static_cast<double>(static_cast<int>(D.pill.x) - pad), static_cast<double>(static_cast<int>(top) - dockPopPx(app) - 6), static_cast<double>(static_cast<int>(D.pill.w) + 2 * pad), static_cast<double>(LH - static_cast<int>(top) + dockPopPx(app) + 6)});
    if (!D.menuRects.empty()) {
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const Rect& r : D.menuRects) { x0 = std::min(x0, r.x); y0 = std::min(y0, r.y); x1 = std::max(x1, r.x + r.w); y1 = std::max(y1, r.y + r.h); }
        const Rect card = dockRectToLogical(D, Rect{x0, y0, x1 - x0, y1 - y0});
        const double menuTop = card.y - 6, bridgeBottom = top + 6;
        add(Rect{static_cast<double>(static_cast<int>(card.x)), static_cast<double>(static_cast<int>(menuTop)), static_cast<double>(static_cast<int>(card.w)), static_cast<double>(static_cast<int>(std::max(0.0, bridgeBottom - menuTop)))});
    }
    if (D.gridOpen) {
        const Rect c = dockRectToLogical(D, D.gridCard);
        add(Rect{static_cast<double>(static_cast<int>(c.x) - 6), static_cast<double>(static_cast<int>(c.y) - 6), static_cast<double>(static_cast<int>(c.w) + 12), static_cast<double>(static_cast<int>(std::max(0.0, top + 6 - c.y + 6)))});
    }
    wl_surface_set_input_region(D.surface, region);
    wl_region_destroy(region);
}

void dockRedraw(App& app) {
    DockState& D = app.dock;
    if (!D.open() || !D.configured || D.w <= 0 || D.h <= 0) return;
    const int W = D.w, H = D.h, stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-dock", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    dockPaint(app, cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(D.surface, buffer, 0, 0);
    wl_surface_damage_buffer(D.surface, 0, 0, W, H);
    dockSetInput(app);
    updateBlurRegion(app, D.surface, D.blur, W, H, [&](cairo_t* m) {
        cairo_save(m);
        const cairo_matrix_t frame = dockFrame(D);
        cairo_transform(m, &frame);
        cairo_translate(m, 0, D.slide * dockSlideDist(D));
        dockPillPath(m, app, D.pill);
        cairo_fill(m);
        cairo_restore(m);
        const DockPt slid = dockSlideOffset(D);
        cairo_translate(m, slid.x, slid.y);
        if (D.menuOpen) dockPaintMenu(m, app);
        dockPaintGrid(m, app);
    });
    wl_surface_commit(D.surface);
}
