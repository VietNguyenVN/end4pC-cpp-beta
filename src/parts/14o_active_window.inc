
bool awinRead(App& app) {
    const std::string out = wmActiveWindowTsv();
    std::string line = out.substr(0, out.find('\n'));
    std::string cls, title;
    if (const size_t tab = line.find('\t'); tab != std::string::npos) { cls = line.substr(0, tab); title = line.substr(tab + 1); }
    if (cls.empty()) {
        cls = i18n::tr("Desktop");
        title = std::string(i18n::tr("Workspace")) + " " + std::to_string(app.workspace);
    }
    std::string iconFor;
    if (!line.empty() && line.front() != '\t') {
        ensureApps(app);
        const AppEntry* e = dockEntryFor(app, cls);
        iconFor = e && !e->icon.empty() ? e->icon : cls;
    }
    if (iconFor != app.awin.iconFor) {
        if (app.awin.icon) { cairo_surface_destroy(app.awin.icon); app.awin.icon = nullptr; }
        if (!iconFor.empty()) {
            const std::string path = lookupIcon(iconFor, 44);
            if (!path.empty()) app.awin.icon = loadIconScaled(path, 44);
        }
        app.awin.iconFor = iconFor;
    }
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    int widest = 0, widths[2] = {0, 0}, idx = 0;
    for (const auto& [font, text] : {std::pair<const char*, const std::string*>{"Sans 8", &cls}, {"Sans 10", &title}}) {
        PangoFontDescription* fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, text->c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        widest = std::max(widest, tw);
        widths[idx++] = std::min(tw, 280);
        pango_font_description_free(fd);
    }
    int m3Widest = 0;
    for (const auto& [font, text] : {std::pair<const char*, const std::string*>{"Sans 9", &cls}, {"Sans 11", &title}}) {
        PangoFontDescription* fd = pango_font_description_from_string(font);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_text(layout, text->c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        m3Widest = std::max(m3Widest, tw);
        pango_font_description_free(fd);
    }
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    const int width = std::min(widest + 12, 280), m3 = std::min(m3Widest + 12, 280);
    const bool changed = cls != app.awin.cls || title != app.awin.title || width != app.awin.barW || m3 != app.awin.m3W;
    app.awin.cls = cls;
    app.awin.title = title;
    app.awin.barW = width;
    app.awin.m3W = m3;
    app.awin.clsW = widths[0];
    app.awin.titleW = widths[1];
    return changed;
}

bool refreshWsApps(App& app) {
    const bool wanted = app.cfg.workspaceApps && inBarLayout(app, "workspaces");
    if (!wanted) {
        const bool had = !app.wsMainClass.empty();
        app.wsMainClass.clear();
        return had;
    }
    const std::string out = wmWorkspaceAppsTsv();
    std::map<int, std::string> biggest;
    std::map<int, double> biggestArea;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        if (end == std::string::npos) end = out.size();
        const std::string line = out.substr(start, end - start);
        start = end + 1;
        const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        const int id = std::atoi(line.c_str());
        const std::string cls = line.substr(t1 + 1, t2 - t1 - 1);
        const double area = std::strtod(line.c_str() + t2 + 1, nullptr);
        if (cls.empty() || id <= 0) continue;
        const auto seen = biggestArea.find(id);
        if (seen == biggestArea.end() || area > seen->second) {
            biggestArea[id] = area;
            biggest[id] = cls;
        }
    }
    const bool changed = biggest != app.wsMainClass;
    app.wsMainClass = std::move(biggest);
    if (!changed) return false;
    std::set<std::string> classes;
    for (const auto& entry : app.wsMainClass) classes.insert(entry.second);
    ensureApps(app);
    for (const std::string& cls : classes) {
        if (app.wsIcons.count(cls)) continue;
        const AppEntry* e = dockEntryFor(app, cls);
        const std::string name = e && !e->icon.empty() ? e->icon : cls;
        const std::string path = lookupIcon(name, kWsAppIconPx);
        app.wsIcons[cls] = path.empty() ? nullptr : loadIconScaled(path, kWsAppIconPx);
    }
    for (auto it = app.wsIcons.begin(); it != app.wsIcons.end();) {
        if (classes.count(it->first)) { ++it; continue; }
        if (it->second) cairo_surface_destroy(it->second);
        it = app.wsIcons.erase(it);
    }
    return true;
}
