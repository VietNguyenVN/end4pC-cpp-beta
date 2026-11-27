// Window geometry is used only for targets on the frozen monitor image. The
// picker also lists other workspaces, where only a toplevel capture is valid.
std::vector<CapWindow> capParseWindows(const std::string& rows, int monX, int monY) {
    std::vector<CapWindow> windows;
    std::istringstream in(rows);
    for (std::string line; std::getline(in, line);) {
        std::istringstream row(line);
        std::string f[9];
        bool complete = true;
        for (std::string& field : f)
            if (!std::getline(row, field, '\t')) { complete = false; break; }
        if (!complete) continue;
        const auto number = [](const std::string& s) { return std::strtod(s.c_str(), nullptr); };
        const Rect bounds{number(f[3]) - monX, number(f[4]) - monY, number(f[5]), number(f[6])};
        if (bounds.w <= 0 || bounds.h <= 0) continue;
        windows.push_back({f[0], f[1], f[2], bounds, f[7] == "true", f[8] == "true"});
    }
    return windows;
}

void capLoadWindows(Capture& c, bool enabled) {
    if (!enabled || c.record || c.autoAct || wmKind() != Wm::Hyprland) return;
    const std::string rows = captureArgv({"bash", "-c", R"sh(
        mon=$(hyprctl -j monitors | jq -c --arg name "$1" 'first(.[] | select(.name == $name))')
        [ -n "$mon" ] || exit 0
        hyprctl -j clients | jq -r --argjson m "$mon" '
            def clean: tostring | gsub("[[:cntrl:]]"; " ");
            map(select(.mapped and (.size[0] > 0) and (.size[1] > 0))
                | .special = ((.workspace.name // "") | . == "special" or startswith("special:"))
                | .visible = (.monitor == $m.id and (.hidden | not) and
                    ((.pinned and (.special | not)) or
                    (if .special then .workspace.id == $m.specialWorkspace.id
                     else .workspace.id == $m.activeWorkspace.id end))))
            | sort_by(if .visible then 0 else 1 end, if .special then 0 else 1 end,
                      -(.fullscreen // 0), if .floating then 0 else 1 end,
                      (if (.focusHistoryID // -1) < 0 then 999999 else .focusHistoryID end))
            | .[] | [(.stableId // ""), ((if .title == "" then .class else .title end) // "Window" | clean),
                     (.workspace.name // "" | clean),
                     .at[0], .at[1], .size[0], .size[1], .special, .visible] | @tsv'
    )sh", "sh", c.monName.c_str()});
    c.windows = capParseWindows(rows, c.monX, c.monY);
    for (int i = 0; i < static_cast<int>(c.windows.size()); ++i)
        c.windowGroup[c.windows[i].special].push_back(i);
}

Rect capWindowRect(const Capture& c, const CapWindow& window) {
    const Rect& r = window.bounds;
    const double left = std::clamp(r.x, 0.0, static_cast<double>(c.w));
    const double top = std::clamp(r.y, 0.0, static_cast<double>(c.h));
    const double right = std::clamp(r.x + r.w, left, static_cast<double>(c.w));
    const double bottom = std::clamp(r.y + r.h, top, static_cast<double>(c.h));
    return {left, top, right - left, bottom - top};
}

Rect capWindowsButton(const Capture& c) { return {(c.w - 144) / 2.0, 83, 144, 34}; }

struct CapWindowMenu {
    Rect body, tabs[2];
    int rows;
    Rect row(int i) const { return {body.x + 8, body.y + 56 + i * 42, body.w - 16, 40}; }
};

CapWindowMenu capWindowMenu(const Capture& c) {
    const double w = std::clamp(c.w - 24.0, 240.0, 520.0), x = (c.w - w) / 2, y = 125;
    const int count = static_cast<int>(c.windowGroup[c.specialWindows].size());
    const int rows = std::min(std::max(1, count), std::clamp((c.h - 195) / 42, 1, 8));
    return {{x, y, w, 78.0 + rows * 42}, {{x + 8, y + 8, w / 2 - 10, 36},
            {x + w / 2 + 2, y + 8, w / 2 - 10, 36}}, rows};
}

int capMenuWindowAt(const Capture& c, double x, double y) {
    const auto menu = capWindowMenu(c);
    const auto& group = c.windowGroup[c.specialWindows];
    for (int row = 0; row < menu.rows && c.windowScroll + row < static_cast<int>(group.size()); ++row)
        if (menu.row(row).contains(x, y)) return group[c.windowScroll + row];
    return -1;
}

bool capWindowTargets(const Capture& c) { return !c.record && !c.autoAct && !c.has && !c.windows.empty(); }

int capTargetAt(const Capture& c, double x, double y) {
    if (!capWindowTargets(c) || c.windowFd >= 0 || capWindowsButton(c).contains(x, y)) return -1;
    if (c.windowMenu) return capMenuWindowAt(c, x, y);
    for (int i = 0; i < static_cast<int>(c.windows.size()); ++i)
        if (c.windows[i].visible && capWindowRect(c, c.windows[i]).contains(x, y)) return i;
    return -1;
}

void capStopWindow(Capture& c) {
    if (c.windowFd >= 0) close(c.windowFd);
    if (c.windowPng >= 0) close(c.windowPng);
    c.windowFd = c.windowPng = -1;
    c.pendingWindow = -1;
}

void captureRedraw(App& app);

void capSetSelection(Capture& c, const Rect& r) {
    c.sx = r.x; c.sy = r.y; c.sw = r.w; c.sh = r.h;
    c.has = r.w >= kCapMin && r.h >= kCapMin;
    c.windowMenu = false;
    c.windowHover = -1;
}

void capWindowFailed(App& app, const CapWindow& window) {
    Capture& c = app.cap;
    if (window.visible) capSetSelection(c, capWindowRect(c, window));
    if (!c.has) {
        c.windowMenu = true;
        c.windowError = true;
    }
    captureRedraw(app);
}

void capPickWindow(App& app, int index) {
    Capture& c = app.cap;
    if (index < 0 || index >= static_cast<int>(c.windows.size()) || c.windowFd >= 0) return;
    const auto& window = c.windows[index];
    if (window.id.empty()) { capWindowFailed(app, window); return; }
    int ready[2];
    const int image = memfd_create("end4pC-window-shot", MFD_CLOEXEC);
    if (image < 0) { capWindowFailed(app, window); return; }
    if (pipe2(ready, O_CLOEXEC | O_NONBLOCK) < 0) {
        close(image);
        capWindowFailed(app, window);
        return;
    }
    const pid_t child = fork();
    if (child == 0) {
        childSignalReset();
        if (dup2(ready[1], STDOUT_FILENO) < 0 || dup2(image, STDERR_FILENO) < 0) _exit(1);
        // Keep the PNG and completion signal separate. A compositor may never
        // provide a buffer for a closed or offscreen toplevel.
        execlp("sh", "sh", "-c", R"sh(timeout -k 1s 2s grim -l 1 -T "$1" - >&2 2>/dev/null && printf y)sh",
               "sh", window.id.c_str(), nullptr);
        _exit(127);
    }
    close(ready[1]);
    if (child < 0) {
        close(ready[0]); close(image);
        capWindowFailed(app, window);
        return;
    }
    c.windowFd = ready[0];
    c.windowPng = image;
    c.pendingWindow = index;
    c.windowError = false;
    captureRedraw(app);
}

cairo_status_t capReadPng(void* fd, unsigned char* bytes, unsigned size) {
    while (size > 0) {
        const ssize_t count = read(*static_cast<int*>(fd), bytes, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return CAIRO_STATUS_READ_ERROR;
        size -= count;
        bytes += count;
    }
    return CAIRO_STATUS_SUCCESS;
}

void capReadWindow(App& app) {
    Capture& c = app.cap;
    if (c.windowFd < 0 || c.pendingWindow < 0) return;
    char ready = 0;
    const ssize_t count = read(c.windowFd, &ready, 1);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    const auto& window = c.windows[c.pendingWindow];
    cairo_surface_t* shot = count == 1 && ready == 'y' && lseek(c.windowPng, 0, SEEK_SET) >= 0
        ? cairo_image_surface_create_from_png_stream(capReadPng, &c.windowPng) : nullptr;
    capStopWindow(c);
    if (!shot || cairo_surface_status(shot) != CAIRO_STATUS_SUCCESS) {
        if (shot) cairo_surface_destroy(shot);
        capWindowFailed(app, window);
        return;
    }
    const double iw = cairo_image_surface_get_width(shot), ih = cairo_image_surface_get_height(shot);
    if (iw < 1 || ih < 1) {
        cairo_surface_destroy(shot);
        capWindowFailed(app, window);
        return;
    }
    const double scale = std::min({1.0, std::max(1.0, c.w - 48.0) / iw, std::max(1.0, c.h - 168.0) / ih});
    c.windowShot.reset(shot, cairo_surface_destroy);
    c.windowImage = {(c.w - iw * scale) / 2, (c.h - ih * scale) / 2 - 20, iw * scale, ih * scale};
    capSetSelection(c, c.windowImage);
    captureRedraw(app);
}

void capWindowScroll(App& app, int step) {
    Capture& c = app.cap;
    if (!c.windowMenu || c.windowFd >= 0) return;
    const int maxFirst = std::max(0, static_cast<int>(c.windowGroup[c.specialWindows].size()) - capWindowMenu(c).rows);
    const int scroll = std::clamp(c.windowScroll + step, 0, maxFirst);
    if (scroll == c.windowScroll) return;
    c.windowScroll = scroll;
    c.windowHover = capMenuWindowAt(c, app.pointerX, app.pointerY);
    captureRedraw(app);
}

bool capWindowClick(App& app, double x, double y) {
    Capture& c = app.cap;
    if (!capWindowTargets(c)) return false;
    if (c.windowFd >= 0) return true;
    if (capWindowsButton(c).contains(x, y)) {
        c.windowMenu = !c.windowMenu;
        if (c.windowMenu && c.windowGroup[0].empty()) c.specialWindows = true;
        c.windowHover = -1;
        c.windowError = false;
        captureRedraw(app);
        return true;
    }
    if (!c.windowMenu) return false;
    const auto menu = capWindowMenu(c);
    for (int i = 0; i < 2; ++i)
        if (menu.tabs[i].contains(x, y)) {
            c.specialWindows = i == 1;
            c.windowScroll = 0;
            c.windowHover = -1;
            c.windowError = false;
            captureRedraw(app);
            return true;
        }
    const int window = capMenuWindowAt(c, x, y);
    if (window >= 0) capPickWindow(app, window);
    else if (!menu.body.contains(x, y)) {
        c.windowMenu = false;
        c.windowHover = capTargetAt(c, x, y);
        captureRedraw(app);
    }
    return true;
}

void capWindowKey(App& app, uint32_t key) {
    Capture& c = app.cap;
    if (key == KEY_ESC) { c.windowMenu = false; c.windowHover = -1; }
    else if (key == KEY_LEFT || key == KEY_RIGHT) {
        c.specialWindows = key == KEY_RIGHT;
        c.windowScroll = 0;
        c.windowHover = -1;
        c.windowError = false;
    } else if (key == KEY_ENTER || key == KEY_KPENTER) {
        capPickWindow(app, c.windowHover);
        return;
    } else if (key == KEY_UP || key == KEY_DOWN) {
        const auto& group = c.windowGroup[c.specialWindows];
        if (group.empty()) return;
        const auto found = std::find(group.begin(), group.end(), c.windowHover);
        const int row = found == group.end() ? c.windowScroll :
            std::clamp(static_cast<int>(found - group.begin()) + (key == KEY_UP ? -1 : 1), 0, static_cast<int>(group.size()) - 1);
        c.windowHover = group[row];
        c.windowScroll = std::clamp(c.windowScroll, std::max(0, row - capWindowMenu(c).rows + 1), row);
    } else return;
    captureRedraw(app);
}

std::optional<Rect> capWindowLabel(const Capture& c, const Rect& window, const Rect& hint, const Rect& toggle) {
    constexpr double edge = 4, gap = 6, height = 28;
    const double width = std::min({330.0, std::max(100.0, window.w - 16), c.w - 2 * edge});
    if (width <= 20) return std::nullopt;
    const double x = std::clamp(window.x + 8, edge, c.w - width - edge);
    const auto overlaps = [](const Rect& a, const Rect& b) {
        return a.x < b.x + b.w + gap && a.x + a.w + gap > b.x &&
               a.y < b.y + b.h + gap && a.y + a.h + gap > b.y;
    };
    // Keep the title attached to a window edge if possible.
    for (double y : {window.y - height - gap, window.y + window.h + gap,
                     window.y + gap, window.y + window.h - height - gap,
                     std::max(hint.y + hint.h, toggle.y + toggle.h) + gap}) {
        const Rect candidate{x, y, width, height};
        if (y >= edge && y + height <= c.h - edge &&
            !overlaps(candidate, hint) && !overlaps(candidate, toggle)) return candidate;
    }
    return std::nullopt;
}

void capPaintWindowPicker(cairo_t* cr, const App& app, PangoLayout* layout, const Rect& hint) {
    const Capture& c = app.cap;
    if (!capWindowTargets(c) || c.mode != kCapIdle) return;
    auto fill = [&](const Rect& r, double radius, const char* color, double alpha = 1) {
        roundedRect(cr, r.x, r.y, r.w, r.h, radius);
        setSource(cr, app.color(color), alpha);
        cairo_fill(cr);
    };
    auto label = [&](const std::string& text, const Rect& r, int px, bool bold, bool center, const char* color) {
        auto* font = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(font, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
        pango_layout_set_width(layout, std::max(1, static_cast<int>(r.w)) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(layout, center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
        pango_layout_set_text(layout, text.c_str(), -1);
        int h = 0;
        pango_layout_get_pixel_size(layout, nullptr, &h);
        setSource(cr, app.color(color));
        cairo_move_to(cr, r.x, r.y + (r.h - h) / 2);
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        pango_layout_set_single_paragraph_mode(layout, FALSE);
    };
    const Rect toggle = capWindowsButton(c);
    if (!c.windowMenu && c.windowHover >= 0) {
        const auto& window = c.windows[c.windowHover];
        const Rect r = capWindowRect(c, window);
        const char* accent = window.special ? "tertiary" : "primary";
        // Keep the highlight out of the translucent instruction pill.
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, c.w, c.h);
        roundedRect(cr, hint.x - 2, hint.y - 2, hint.w + 4, hint.h + 4, hint.h / 2 + 2);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_clip(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
        roundedRect(cr, r.x + 1, r.y + 1, std::max(1.0, r.w - 2), std::max(1.0, r.h - 2), 6);
        setSource(cr, app.color(accent), 0.13);
        cairo_fill_preserve(cr);
        setSource(cr, app.color(accent));
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        cairo_restore(cr);
        if (const auto title = capWindowLabel(c, r, hint, toggle)) {
            fill(*title, 14, "surface_container_high");
            label(window.special ? window.workspace + " · " + window.title : window.title,
                  {title->x + 10, title->y, title->w - 20, title->h}, 12, true, false, "on_surface");
        }
    }
    const bool active = c.windowMenu || c.windowButtonHot;
    fill(toggle, 17, active ? "primary_container" : "surface_container_high");
    label(std::string(i18n::tr(c.windowFd >= 0 && !c.windowMenu ? "Capturing window…" : "Windows")) +
          (c.windowFd >= 0 || c.windowMenu ? "" : "  ·  W"),
          toggle, 13, true, true, active ? "on_primary_container" : "on_surface");
    if (!c.windowMenu) return;

    const auto menu = capWindowMenu(c);
    fill(menu.body, 20, "surface_container");
    for (int i = 0; i < 2; ++i) {
        const bool selected = c.specialWindows == (i == 1);
        fill(menu.tabs[i], 13, selected ? "primary_container" : "surface_container_high");
        label(i18n::tr(i == 0 ? "Windows" : "Special workspaces"), menu.tabs[i], 13, true, true,
              selected ? "on_primary_container" : "on_surface");
    }
    const auto& group = c.windowGroup[c.specialWindows];
    if (group.empty()) label(i18n::tr("No windows"), menu.row(0), 13, false, true, "on_surface_variant");
    for (int i = 0; i < menu.rows && c.windowScroll + i < static_cast<int>(group.size()); ++i) {
        const int index = group[c.windowScroll + i];
        const auto& window = c.windows[index];
        const Rect r = menu.row(i);
        if (index == c.windowHover || index == c.pendingWindow) fill(r, 10, "surface_container_highest");
        label(window.title, {r.x + 12, r.y + 2, r.w - 24, 20}, 14, true, false, "on_surface");
        const std::string location = window.special ? window.workspace :
            std::string(i18n::tr("Workspace")) + " " + window.workspace;
        label(location, {r.x + 12, r.y + 21, r.w - 24, 17}, 11, false, false, "on_surface_variant");
    }
    const char* status = c.windowFd >= 0 ? "Capturing window…" :
                         c.windowError ? "Could not capture the window. Open its workspace and try again." :
                         static_cast<int>(group.size()) > menu.rows ? "Scroll to see more windows" : nullptr;
    if (status) label(i18n::tr(status), {menu.body.x + 12, menu.body.y + menu.body.h - 22, menu.body.w - 24, 16},
                      11, false, true, c.windowError ? "error" : "on_surface_variant");
}
