

constexpr int kPanelW = 410, kPanelRoom = 20;
constexpr int kPanelSurfW = kPanelW + 2 * kPanelRoom, kPanelSurfH = 660;
constexpr int kPanelHeaderH = 68, kPanelListMax = 500, kPanelRadius = 28, kPanelItemPad = 12, kPanelItemGap = 8;
constexpr const char* kPanelTitleFont = "Sans Bold 14";

void closeNotifPanel(App& app);
void drawNotifPanel(App& app);

std::string notifAgo(time_t then) {
    const long d = static_cast<long>(std::time(nullptr) - then);
    if (d < 60) return "now";
    if (d < 3600) return std::to_string(d / 60) + " min";
    if (d < 86400) return std::to_string(d / 3600) + " h";
    return std::to_string(d / 86400) + " d";
}

struct PanelRowMetrics {
    double h = 0;
    int appH = 0, titleH = 0, bodyH = 0;
    std::vector<std::pair<std::string, std::string>> actions;
};

PanelRowMetrics measurePanelRow(PangoLayout* layout, const Notif& n, double textW) {
    PanelRowMetrics m;
    auto font = [&](const char* name) {
        PangoFontDescription* fd = pango_font_description_from_string(name);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
    };
    pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_attributes(layout, nullptr);
    int tw = 0;
    font(kNotifAppFont);
    pango_layout_set_height(layout, -1);
    pango_layout_set_text(layout, "Ag", -1);
    pango_layout_get_pixel_size(layout, &tw, &m.appH);
    font(kNotifTitleFont);
    pango_layout_set_height(layout, -1);
    pango_layout_set_text(layout, n.summary.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &m.titleH);
    if (!n.body.empty()) {
        font(kNotifBodyFont);
        pango_layout_set_height(layout, -3);
        setNotifBody(layout, n.body);
        pango_layout_get_pixel_size(layout, &tw, &m.bodyH);
    }
    for (const auto& a : n.actions)
        if (!a.second.empty()) m.actions.push_back(a);
    const double textBlock = m.appH + 2 + m.titleH + (m.bodyH ? 2 + m.bodyH : 0);
    m.h = kPanelItemPad * 2 + std::max<double>(36, textBlock) + (m.actions.empty() ? 0 : 8 + 30);
    return m;
}

void notifPanelAnchor(App& app, int& surfLeft, int& pillX, double* bellAlong = nullptr) {
    const Bar* bar = mainBar(const_cast<App&>(app));
    const int sideMargin = app.cfg.frame + (app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0);
    const double screenW = bar ? bar->width + 2.0 * sideMargin : 1920;
    double bellCx = screenW - 60;
    if (bar && hasSlot(*bar, "notifications")) bellCx = sideMargin + notifX(*bar) + notifW(app) / 2.0;
    const double left = std::clamp(bellCx - kPanelW / 2.0, double(app.cfg.frame + 12), screenW - app.cfg.frame - 12 - kPanelW);
    surfLeft = std::max(0, static_cast<int>(left) - kPanelRoom);
    pillX = static_cast<int>(left) - surfLeft;
    if (bellAlong) *bellAlong = bellCx - surfLeft;
}

bool panelAtBottom(const App& app) {
    if (sideBar(app)) return sideModuleAtBottom(app, "notifications", true);
    return app.cfg.barPosition != "top";
}

void drawNotifPanel(App& app) {
    NotifPanel& P = app.panel;
    if (!P.open() || !P.configured) return;
    const int W = kPanelSurfW, H = kPanelSurfH;
    const bool atBottom = panelAtBottom(app);
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-notif-panel", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    popGroupBegin(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto useFont = [&](const char* name) {
        PangoFontDescription* fd2 = pango_font_description_from_string(name);
        pango_layout_set_font_description(layout, fd2);
        pango_font_description_free(fd2);
    };
    const double itemW = kPanelW - 2 * kPanelItemPad, textX = kPanelItemPad + 36 + 10, textW = itemW - textX - kPanelItemPad;

    std::vector<PanelRowMetrics> metrics;
    double contentH = 0;
    for (const Notif& n : app.notifs) {
        metrics.push_back(measurePanelRow(layout, n, textW));
        contentH += metrics.back().h + kPanelItemGap;
    }
    contentH -= kPanelItemGap;
    const bool empty = app.notifs.empty();
    const double viewportH = empty ? 190 - kPanelItemPad : std::min<double>(kPanelListMax, contentH);
    const double listH = viewportH + kPanelItemPad;
    const double ph = kPanelHeaderH + listH;
    P.contentH = contentH;
    P.maxScroll = std::max(0.0, contentH - viewportH);
    P.scroll = std::clamp(P.scroll, 0.0, P.maxScroll);

    const double py = atBottom ? H - kPanelRoom - ph : kPanelRoom;
    if (!P.shadow || P.shadowH != static_cast<int>(ph)) {
        if (P.shadow) cairo_surface_destroy(P.shadow);
        P.shadow = makeShadowMask(kPanelW, ph, kPanelRadius);
        P.shadowH = static_cast<int>(ph);
    }
    setSource(cr, app.color("shadow"), 0.3);
    cairo_mask_surface(cr, P.shadow, P.pillX - kLaunShadow, py - kLaunShadow + 2);
    cairo_translate(cr, P.pillX, py);
    P.originX = P.pillX;
    P.originY = py;
    cairo_save(cr);
    roundedRect(cr, 0, 0, kPanelW, ph, kPanelRadius);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_restore(cr);
    roundedRect(cr, 0, 0, kPanelW, ph, kPanelRadius);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);

    int tw = 0, th = 0;
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_height(layout, -1);
    pango_layout_set_attributes(layout, nullptr);
    useFont(kPanelTitleFont);
    pango_layout_set_text(layout, "Notifications", -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, 22, 14);
    pango_cairo_show_layout(cr, layout);
    useFont(kNotifAppFont);
    const std::string count = app.cfg.doNotDisturb ? "Do not disturb is on"
                              : empty ? "All caught up"
                                      : std::to_string(app.notifs.size()) + (app.notifs.size() == 1 ? " notification" : " notifications");
    pango_layout_set_text(layout, count.c_str(), -1);
    setSource(cr, app.color(app.cfg.doNotDisturb ? "primary" : "on_surface_variant"));
    cairo_move_to(cr, 22, 14 + th + 1);
    pango_cairo_show_layout(cr, layout);

    P.clearAll = {};
    if (!empty) {
        useFont(kNotifBodyFont);
        pango_layout_set_text(layout, "Clear all", -1);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        const double bw = 16 + 18 + 6 + lw + 18, bh = 36;
        P.clearAll = {kPanelW - 18 - 36 - 8 - bw, 16, bw, bh};
        roundedRect(cr, P.clearAll.x, P.clearAll.y, bw, bh, bh / 2);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        if (P.hoverButton == -3) {
            roundedRect(cr, P.clearAll.x, P.clearAll.y, bw, bh, bh / 2);
            setSource(cr, app.color("on_secondary_container"), 0.1);
            cairo_fill(cr);
        }
        drawIcon(cr, "delete_sweep", P.clearAll.x + 14, P.clearAll.y + 9, 18, app.color("on_secondary_container"), true);
        setSource(cr, app.color("on_secondary_container"));
        cairo_move_to(cr, P.clearAll.x + 14 + 18 + 6, P.clearAll.y + (bh - lh) / 2.0);
        pango_cairo_show_layout(cr, layout);
    }

    {
        P.dnd = {kPanelW - 18 - 36, 16, 36, 36};
        const bool on = app.cfg.doNotDisturb != 0;
        cairo_new_sub_path(cr);
        cairo_arc(cr, P.dnd.x + 18, P.dnd.y + 18, 18, 0, 2 * M_PI);
        if (on) {
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
        } else {
            setSource(cr, app.color("outline"));
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
        if (P.hoverButton == -4) {
            cairo_new_sub_path(cr);
        cairo_arc(cr, P.dnd.x + 18, P.dnd.y + 18, 18, 0, 2 * M_PI);
            setSource(cr, app.color(on ? "on_primary" : "on_surface"), 0.12);
            cairo_fill(cr);
        }
        drawIcon(cr, "notifications_off", P.dnd.x + 9, P.dnd.y + 9, 18, app.color(on ? "on_primary" : "on_surface_variant"), on);
    }

    const double listTop = kPanelHeaderH;
    P.rows.clear();
    cairo_save(cr);
    cairo_rectangle(cr, 0, listTop, kPanelW, viewportH);
    cairo_clip(cr);
    if (empty) {
        shapePath(cr, app.cfg.notificationShape, kPanelW / 2.0 - 36, listTop + 30, 72);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        drawIcon(cr, "notifications_off", kPanelW / 2.0 - 16, listTop + 30 + 20, 32, app.color("on_secondary_container"), true);
        useFont(kNotifBodyFont);
        pango_layout_set_text(layout, "No notifications", -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface_variant"));
        cairo_move_to(cr, (kPanelW - tw) / 2.0, listTop + 120);
        pango_cairo_show_layout(cr, layout);
    }
    double y = listTop - (P.scroll + P.band.over);
    for (size_t i = 0; i < app.notifs.size(); i++) {
        const Notif& n = app.notifs[i];
        const PanelRowMetrics& m = metrics[i];
        const double x0 = kPanelItemPad, h = m.h;
        if (y + h >= listTop && y <= listTop + viewportH) {
            NotifPanel::Row row;
            row.id = n.id;
            row.rect = {x0, y, itemW, h};
            row.close = {x0 + itemW - 12 - 22, y + 10, 22, 22};
            const bool hot = P.hoverId == n.id;
            roundedRect(cr, x0, y, itemW, h, 18);
            setSource(cr, app.color(n.urgency >= 2 ? "error_container" : "surface_container_high"));
            cairo_fill(cr);
            if (hot) {
                roundedRect(cr, x0, y, itemW, h, 18);
                setSource(cr, app.color("on_surface"), 0.05);
                cairo_fill(cr);
            }
            const double ix = x0 + kPanelItemPad, iy = y + kPanelItemPad;
            const std::string shape = n.urgency >= 2 ? "SoftBurst" : app.cfg.notificationShape;
            shapePath(cr, shape, ix, iy, 36);
            setSource(cr, app.color(n.urgency >= 2 ? "primary_container" : "secondary_container"));
            cairo_fill(cr);
            if (cairo_surface_t* pic = notifPicture(app, n)) {
                cairo_save(cr);
                shapePath(cr, shape, ix, iy, 36);
                cairo_clip(cr);
                cairo_translate(cr, ix, iy);
                cairo_scale(cr, 36.0 / cairo_image_surface_get_width(pic), 36.0 / cairo_image_surface_get_height(pic));
                cairo_set_source_surface(cr, pic, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            } else if (cairo_surface_t* appIcon = app.cfg.notificationAppIcons ? notifAppIcon(app, n) : nullptr) {
                paintNotifAppIcon(cr, appIcon, shape, ix, iy, 36, 22);
            } else {
                drawIcon(cr, notifSolarIcon(n), ix + 8, iy + 8, 20, app.color(n.urgency >= 2 ? "on_primary_container" : "on_secondary_container"), true);
            }
            double ty = y + kPanelItemPad;
            const double tx = x0 + textX;
            pango_layout_set_width(layout, static_cast<int>((textW - 26) * PANGO_SCALE));
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            useFont(kNotifAppFont);
            pango_layout_set_height(layout, -1);
            pango_layout_set_attributes(layout, nullptr);
            pango_layout_set_text(layout, (notifAppLabel(n) + "  ·  " + notifAgo(n.time)).c_str(), -1);
            setSource(cr, app.color("on_surface_variant"));
            cairo_move_to(cr, tx, ty);
            pango_cairo_show_layout(cr, layout);
            ty += m.appH + 2;
            pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
            useFont(kNotifTitleFont);
            pango_layout_set_height(layout, -1);
            pango_layout_set_text(layout, n.summary.c_str(), -1);
            setSource(cr, app.color("on_surface"));
            cairo_move_to(cr, tx, ty);
            pango_cairo_show_layout(cr, layout);
            ty += m.titleH + 2;
            if (m.bodyH) {
                useFont(kNotifBodyFont);
                pango_layout_set_height(layout, -3);
                setNotifBody(layout, n.body);
                setSource(cr, app.color("on_surface_variant"));
                cairo_move_to(cr, tx, ty);
                pango_cairo_show_layout(cr, layout);
            }
            const double by = y + h - kPanelItemPad - 30;
            double bx = tx;
            {
                double row = 0;
                for (size_t b = 0; b < m.actions.size(); b++) {
                    useFont(kNotifBodyFont);
                    pango_layout_set_height(layout, -1);
                    pango_layout_set_width(layout, -1);
                    pango_layout_set_attributes(layout, nullptr);
                    pango_layout_set_text(layout, m.actions[b].second.c_str(), -1);
                    int lw = 0, lh = 0;
                    pango_layout_get_pixel_size(layout, &lw, &lh);
                    row += lw + 28 + (b ? 6 : 0);
                }
                bx = std::max(tx, x0 + itemW - kPanelItemPad - row);
            }
            for (size_t b = 0; b < m.actions.size(); b++) {
                useFont(kNotifBodyFont);
                pango_layout_set_height(layout, -1);
                pango_layout_set_width(layout, -1);
                pango_layout_set_attributes(layout, nullptr);
                pango_layout_set_text(layout, m.actions[b].second.c_str(), -1);
                int lw = 0, lh = 0;
                pango_layout_get_pixel_size(layout, &lw, &lh);
                const double bw = lw + 28;
                if (bx + bw > x0 + itemW - kPanelItemPad) break;
                const Rect r{bx, by, bw, 30};
                row.buttons.push_back({r, m.actions[b].first});
                roundedRect(cr, r.x, r.y, r.w, r.h, 15);
                setSource(cr, app.color(n.urgency >= 2 ? "primary" : "secondary_container"));
                cairo_fill(cr);
                if (P.hoverId == n.id && P.hoverButton == static_cast<int>(b)) {
                    roundedRect(cr, r.x, r.y, r.w, r.h, 15);
                    setSource(cr, app.color("on_surface"), 0.1);
                    cairo_fill(cr);
                }
                setSource(cr, app.color(n.urgency >= 2 ? "on_primary" : "on_secondary_container"));
                cairo_move_to(cr, r.x + 14, r.y + (r.h - lh) / 2.0);
                pango_cairo_show_layout(cr, layout);
                bx += bw + 6;
                pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
            }
            if (hot) {
                const bool onClose = P.hoverButton == -2;
                cairo_new_sub_path(cr);
        cairo_arc(cr, row.close.x + 11, row.close.y + 11, 11, 0, 2 * M_PI);
                setSource(cr, app.color("on_surface"), onClose ? 0.16 : 0.08);
                cairo_fill(cr);
                drawIcon(cr, "close", row.close.x + 3, row.close.y + 3, 16, app.color("on_surface_variant"), onClose);
            }
            P.rows.push_back(std::move(row));
        }
        y += h + kPanelItemGap;
    }
    cairo_restore(cr);

    if (contentH > viewportH && P.band.thumbAlpha() > 0) {  // (the mark shows while the wheel moves and fades out soon after)
        const double thumbH = std::max(28.0, viewportH * viewportH / contentH);
        const double thumbY = listTop + (viewportH - thumbH) * P.scroll / std::max(1.0, contentH - viewportH);
        roundedRect(cr, kPanelW - 7, thumbY, 3, thumbH, 1.5);
        setSource(cr, app.color("outline"), 0.7 * P.band.thumbAlpha());
        cairo_fill(cr);
    }
    P.pillHeight = ph;

    g_object_unref(layout);
    const double rev = std::max(0.0, P.anim.reveal);
    double ox = 0, oy = 0;
    popGrowOrigin(app, P.pillX, py, kPanelW, ph, P.along, ox, oy);
    cairo_identity_matrix(cr);
    if (rev > 0.001) popGroupEnd(cr, rev, ox, oy, P.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8));
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, P.surface, P.blur, W, H, [&](cairo_t* m) {
        if (rev <= 0.001) return;
        popPaintGrown(m, rev, ox, oy, 1.0, [&] {
            cairo_translate(m, P.pillX, py);
            roundedRect(m, 0, 0, kPanelW, ph, kPanelRadius);
            cairo_fill(m);
        });
    });
    wl_surface_attach(P.surface, buffer, 0, 0);
    wl_surface_damage_buffer(P.surface, 0, 0, W, H);
    wl_surface_commit(P.surface);
}

void closeNotifPanel(App& app) {
    NotifPanel& P = app.panel;
    if (!P.open()) return;
    if (P.layer) zwlr_layer_surface_v1_destroy(P.layer);
    releaseBlurSurface(P.blur);
    wl_surface_destroy(P.surface);
    if (P.shadow) cairo_surface_destroy(P.shadow);
    P = NotifPanel{};
    app.onPanel = false;
    app.repeatKey = 0;
    updateCursor(app);
    malloc_trim(0);
}

void onPanelConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->panel.configured) app->panel.anim.animStart = std::chrono::steady_clock::now();
    app->panel.configured = true;
    drawNotifPanel(*app);
}
void onPanelClosed(void* d, zwlr_layer_surface_v1*) { closeNotifPanel(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kPanelListener = {onPanelConfigure, onPanelClosed};

void openNotifPanel(App& app) {
    NotifPanel& P = app.panel;
    if (P.open()) return;
    closeSelector(app);
    closeLauncher(app);
    closeMediaPanel(app); closeClockPanel(app);
    P = NotifPanel{};
    app.notifCount = 0;
    {
        std::vector<uint32_t> up;
        for (const auto& n : app.notifs)
            if (n.popup) up.push_back(n.id);
        for (uint32_t id : up) dismissNotif(app, id, 2);
    }
    int surfLeft = 0;
    const bool vert = sideBar(app);
    if (vert) P.pillX = kPanelRoom;
    else notifPanelAnchor(app, surfLeft, P.pillX, &P.along);
    popStart(P.anim);
    P.surface = wl_compositor_create_surface(app.compositor);
    P.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, P.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-notif-panel");
    const bool atBottom = panelAtBottom(app);
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, kR = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(P.layer, vert ? ((atBottom ? kB : kT) | (app.cfg.barPosition == "right" ? kR : kL)) : ((atBottom ? kB : kT) | kL));
    if (vert) {
        const int side = std::max(0, sideCardEdge(app) - kPanelRoom);
        const int bottom = app.cfg.frame + 10 - kPanelRoom;
        zwlr_layer_surface_v1_set_margin(P.layer, atBottom ? 0 : bottom, app.cfg.barPosition == "right" ? side : 0, atBottom ? bottom : 0, app.cfg.barPosition == "left" ? side : 0);
    } else {
        int edge = app.cfg.frame + kBarHeight + 12;
        if (app.cfg.barStyle == "float" || app.cfg.barStyle == "island") edge += kBarGap;
        const int margin = std::max(0, edge - kPanelRoom);
        zwlr_layer_surface_v1_set_margin(P.layer, atBottom ? 0 : margin, 0, atBottom ? margin : 0, surfLeft);
    }
    zwlr_layer_surface_v1_set_size(P.layer, kPanelSurfW, kPanelSurfH);
    zwlr_layer_surface_v1_set_exclusive_zone(P.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(P.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(P.layer, &kPanelListener, &app);
    wl_surface_commit(P.surface);
    drawAll(app);
}

void retractNotifPanel(App& app) {
    NotifPanel& P = app.panel;
    if (!P.open() || popIsOut(P.anim)) return;
    if (!P.configured) { closeNotifPanel(app); return; }
    popRetract(P.anim, std::chrono::steady_clock::now());
    P.hoverId = 0;
    P.hoverButton = -1;
    P.overList = false;
    drawNotifPanel(app);
}
void toggleNotifPanel(App& app) {
    NotifPanel& P = app.panel;
    if (!P.open()) { openNotifPanel(app); return; }
    if (popIsOut(P.anim)) {
        popReverse(P.anim, std::chrono::steady_clock::now());
        drawNotifPanel(app);
    } else retractNotifPanel(app);
}
void tickNotifPanel(App& app, std::chrono::steady_clock::time_point now) {
    NotifPanel& P = app.panel;
    if (!P.open() || !P.configured) return;
    bool moving = false;
    if (popTick(P.anim, now, moving)) { closeNotifPanel(app); return; }
    if (moving || P.band.step()) drawNotifPanel(app);
}
std::chrono::steady_clock::time_point notifPanelWake(const App& app) {
    if (!app.panel.open() || !app.panel.configured) return std::chrono::steady_clock::time_point::max();
    const auto now = std::chrono::steady_clock::now(), wake = popWake(app.panel.anim.phase, app.panel.anim.settleAt, now);
    return app.panel.band.busy() ? std::min(wake, now + std::chrono::milliseconds(16)) : wake;
}

void alarmStop();  // (14u_timers.inc)
void deleteNotif(App& app, uint32_t id) {
    if (const Notif* n = findNotif(app, id); n && n->desktopEntry == "end4pC-alarm") alarmStop();  // (closing the notification of an alarm silences it)
    dismissNotif(app, id, 2);
    for (auto it = app.notifs.begin(); it != app.notifs.end(); ++it)
        if (it->id == id) {
            if (!it->closedSent && app.bus) notifyd_emit_closed(app.bus, id, 2);
            if (it->image) cairo_surface_destroy(it->image);
            app.notifs.erase(it);
            break;
        }
}

double ph_bottom(const NotifPanel& P) { return P.pillHeight - kPanelItemPad; }

void notifPanelMotion(App& app) {
    NotifPanel& P = app.panel;
    if (popIsOut(P.anim)) return;
    const double lx = app.pointerX - P.originX, ly = app.pointerY - P.originY;
    uint32_t hoverId = 0;
    int hoverButton = -1;
    if (P.clearAll.w > 0 && P.clearAll.contains(lx, ly)) hoverButton = -3;
    if (P.dnd.w > 0 && P.dnd.contains(lx, ly)) hoverButton = -4;
    for (const auto& r : P.rows) {
        if (!r.rect.contains(lx, ly) || ly < kPanelHeaderH || ly > ph_bottom(P)) continue;
        hoverId = r.id;
        if (r.close.contains(lx, ly)) hoverButton = -2;
        for (size_t i = 0; i < r.buttons.size(); i++)
            if (r.buttons[i].first.contains(lx, ly)) hoverButton = static_cast<int>(i);
    }
    const bool overList = lx >= kPanelW - 26 && lx < kPanelW && ly >= kPanelHeaderH && ly <= ph_bottom(P);
    if (hoverId == P.hoverId && hoverButton == P.hoverButton && overList == P.overList) return;
    P.overList = overList;
    P.hoverId = hoverId;
    P.hoverButton = hoverButton;
    drawNotifPanel(app);
    updateCursor(app);
}

void notifPanelClick(App& app) {
    NotifPanel& P = app.panel;
    if (popIsOut(P.anim)) return;
    const double lx = app.pointerX - P.originX, ly = app.pointerY - P.originY;
    if (!Rect{0, 0, double(kPanelW), P.pillHeight}.contains(lx, ly)) { retractNotifPanel(app); return; }
    if (P.hoverButton == -4) {
        setDoNotDisturb(app, !app.cfg.doNotDisturb);
        return;
    }
    if (P.hoverButton == -3) {
        std::vector<uint32_t> ids;
        for (const auto& n : app.notifs) ids.push_back(n.id);
        for (uint32_t id : ids) deleteNotif(app, id);
        P.scroll = 0;
        P.hoverId = 0;
        P.hoverButton = -1;
        drawNotifPanel(app);
        return;
    }
    for (const auto& r : P.rows) {
        if (!r.rect.contains(lx, ly)) continue;
        const uint32_t id = r.id;
        if (P.hoverButton == -2) {
            deleteNotif(app, id);
        } else if (P.hoverButton >= 0 && P.hoverButton < static_cast<int>(r.buttons.size())) {
            const std::string key = r.buttons[P.hoverButton].second;
            notifInvoke(app, id, key);
            deleteNotif(app, id);
        } else if (Notif* n = findNotif(app, id)) {
            bool had = false;
            for (const auto& a : n->actions)
                if (a.first == "default") { notifInvoke(app, id, "default"); had = true; }
            if (had) deleteNotif(app, id);
        }
        P.hoverId = 0;
        P.hoverButton = -1;
        drawNotifPanel(app);
        return;
    }
}

void notifPanelScroll(App& app, int rows) {
    NotifPanel& P = app.panel;
    if (popIsOut(P.anim)) return;
    elasticWheel(P.band, P.scroll, P.maxScroll, rows * 48.0);
    drawNotifPanel(app);
    notifPanelMotion(app);
}

// evdev key codes: what wl_keyboard sends, whatever the layout.
constexpr uint32_t kKeyEsc = 1, kKeyEnter = 28, kKeyKpEnter = 96, kKeyHome = 102, kKeyUp = 103, kKeyPageUp = 104,
                   kKeyLeft = 105, kKeyRight = 106, kKeyEnd = 107, kKeyDown = 108, kKeyPageDown = 109;

constexpr uint32_t kKeyBackspace = 14;
bool repeatable(uint32_t k) { return k == kKeyBackspace || k == kKeyLeft || k == kKeyRight || k == kKeyUp || k == kKeyDown || k == kKeyPageUp || k == kKeyPageDown; }

void focusTile(App& app, int idx) {
    Selector& sel = app.sel;
    sel.focusIdx = std::clamp(idx, 0, static_cast<int>(sel.files.size()) - 1);
    sel.kbFocus = true;
    const int row = sel.focusIdx / kCols;
    if (row < sel.scrollRow) sel.scrollRow = row;
    else if (row >= sel.scrollRow + kVisRows) sel.scrollRow = row - kVisRows + 1;
    sel.scrollRow = std::clamp(sel.scrollRow, 0, maxScrollRow(sel));
    sel.hoverTile = -1;
    drawSelector(app);
    onlineMaybeMore(app);
}

bool selectorKey(App& app, uint32_t key) {
    Selector& sel = app.sel;
    if (!sel.open()) return false;
    if (popIsOut(sel.anim)) return true;
    if (!sel.confirmPath.empty()) {
        if (key == kKeyEnter || key == kKeyKpEnter) { doTrash(app); drawSelector(app); }
        return true;
    }
    if (key == 111 && sel.onlineProvider.empty() && !sel.keyPanel) {
        const int idx = sel.kbFocus ? sel.focusIdx : sel.hoverTile;
        if (idx >= 0 && idx < static_cast<int>(sel.files.size())) { askTrash(app, sel.files[idx]); drawSelector(app); }
        return true;
    }
    if (app.xkbState) {
        const xkb_keycode_t code = key + 8;  // evdev -> xkb
        const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
        std::string& target = sel.keyPanel ? sel.keyInput : sel.query;
        const bool online = !sel.onlineProvider.empty();
        auto changed = [&] {
            sel.keyMsg.clear();
            if (!sel.keyPanel && !online) rebuildSelectorList(app);
            drawSelector(app);
        };
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code);
        if (!sel.keyPanel) {
            const bool shift = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
            const xkb_keysym_t low = xkb_keysym_to_lower(sym);
            textsel::Key k = textsel::Key::None;
            if (key == kKeyBackspace) k = textsel::Key::Backspace;
            else if ((shift || ctrl) && key == kKeyLeft) k = textsel::Key::Left;
            else if ((shift || ctrl) && key == kKeyRight) k = textsel::Key::Right;
            else if ((shift || ctrl) && key == kKeyHome) k = textsel::Key::Home;
            else if ((shift || ctrl) && key == kKeyEnd) k = textsel::Key::End;
            else if (ctrl && low == XKB_KEY_a) k = textsel::Key::SelectAll;
            else if (ctrl && low == XKB_KEY_c) k = textsel::Key::Copy;
            else if (ctrl && low == XKB_KEY_x) k = textsel::Key::Cut;
            else if (ctrl && low == XKB_KEY_v) k = textsel::Key::Paste;
            if (k != textsel::Key::None && (k != textsel::Key::Backspace || !sel.query.empty())) {
                textsel::Field f(sel.query, sel.qCur, sel.qAnc);
                textsel::Result r = textsel::handleKey(f, k, ctrl, shift);
                if (r.wantCopy) clipboard::copy(r.copyText);
                if (r.wantPaste) {
                    std::string clip;
                    if (clipboard::paste(clip) && sel.query.size() + clip.size() - (f.hi() - f.lo()) <= 300) r.changed = f.insert(clip);
                    std::fill(clip.begin(), clip.end(), '\0');
                }
                if (r.changed) changed();
                else drawSelector(app);
                return true;
            }
        } else if (key == kKeyBackspace && !target.empty()) {
            size_t n = target.size() - 1;
            while (n > 0 && (static_cast<unsigned char>(target[n]) & 0xC0) == 0x80) n--;  // a whole UTF-8 letter
            target.resize(n);
            changed();
            return true;
        }
        if (sel.keyPanel && ctrl && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
            std::string clip = captureArgv({"wl-paste", "-n"});
            std::string clean;
            for (char c : clip) if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f && !(sel.keyPanel && c == ' ')) clean += c;
            if (target.size() + clean.size() <= 300) target += clean;
            std::fill(clip.begin(), clip.end(), '\0');
            changed();
            return true;
        }
        char buf[16];
        const int n = ctrl ? 0 : xkb_state_key_get_utf8(app.xkbState, code, buf, sizeof buf);
        if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f && key != kKeyEnter && key != kKeyKpEnter) {
            if (!(sel.keyPanel && buf[0] == ' ') && target.size() < 300) {
                if (sel.keyPanel) target.append(buf, n);
                else { textsel::Field f(sel.query, sel.qCur, sel.qAnc); f.insert(std::string(buf, static_cast<size_t>(n))); }
            }
            changed();
            return true;
        }
        if (key == kKeyEnter || key == kKeyKpEnter) {
            if (sel.keyPanel) { onlineSaveKey(app); drawSelector(app); return true; }
            if (online && sel.query != sel.onlineQuery) { onlineSearch(app); drawSelector(app); return true; }
        }
    }
    if (sel.files.empty()) return false;
    const int cur = sel.focusIdx < 0 ? 0 : sel.focusIdx;
    switch (key) {
        case kKeyLeft: focusTile(app, cur - 1); return true;
        case kKeyRight: focusTile(app, cur + 1); return true;
        case kKeyUp: focusTile(app, cur - kCols); return true;
        case kKeyDown: focusTile(app, cur + kCols); return true;
        case kKeyPageUp: focusTile(app, cur - kCols * kVisRows); return true;
        case kKeyPageDown: focusTile(app, cur + kCols * kVisRows); return true;
        case kKeyHome: focusTile(app, 0); return true;
        case kKeyEnd: focusTile(app, static_cast<int>(sel.files.size()) - 1); return true;
        case kKeyEnter:
        case kKeyKpEnter:
            if (isOnlineTile(sel.files[cur])) { if (const OnlineItem* oi = onlineItemFor(sel, sel.files[cur])) onlineDownload(app, *oi, true); }
            else applyTheme(app, sel.files[cur]);
            sel.kbFocus = true;
            drawSelector(app);
            return true;
        default: return false;
    }
}

int cornerForSurface(App& app, wl_surface* surface) {
    for (auto& b : app.bars)
        for (auto& c : b->corners)
            if (c.surface == surface) return c.corner;
    return -1;
}

Bar* barForSurface(App& app, wl_surface* surface) {
    for (auto& b : app.bars)
        if (b->surface == surface) return b.get();
    return nullptr;
}
