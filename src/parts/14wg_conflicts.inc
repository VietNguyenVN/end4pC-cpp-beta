// Conflicting programs: other bars, notification daemons, wallpaper programs, polkit agents... running next to the shell fight with it
// (two owners for the notifications, two wallpapers, two bars). A card lists the ones that run and offers to close them.
struct CfKnown { const char* comm; const char* name; const char* kind; };  // comm: the process name as the kernel keeps it (15 letters at most)
constexpr CfKnown kCfKnown[] = {
    {"mako", "mako", "Notification daemon"}, {"dunst", "dunst", "Notification daemon"}, {"swaync", "swaync", "Notification daemon"},
    {"fnott", "fnott", "Notification daemon"}, {"wired", "wired", "Notification daemon"}, {"xfce4-notifyd", "xfce4-notifyd", "Notification daemon"},
    {"deadd-notificat", "deadd-notification-center", "Notification daemon"},
    {"waybar", "waybar", "Bar"}, {"hyprpanel", "hyprpanel", "Bar"}, {"eww", "eww", "Bar"}, {"ags", "ags", "Bar"}, {"ironbar", "ironbar", "Bar"},
    {"yambar", "yambar", "Bar"}, {"nwg-panel", "nwg-panel", "Bar"}, {"quickshell", "quickshell", "Shell"}, {"noctalia-shell", "noctalia", "Shell"},
    {"hyprpaper", "hyprpaper", "Wallpaper daemon"}, {"swww-daemon", "swww", "Wallpaper daemon"}, {"awww-daemon", "awww", "Wallpaper daemon"},
    {"swaybg", "swaybg", "Wallpaper daemon"}, {"wpaperd", "wpaperd", "Wallpaper daemon"}, {"mpvpaper", "mpvpaper", "Wallpaper daemon"},
    {"polkit-gnome-au", "polkit-gnome", "Polkit agent"}, {"polkit-kde-auth", "polkit-kde", "Polkit agent"}, {"hyprpolkitagent", "hyprpolkitagent", "Polkit agent"},
    {"lxqt-policykit-", "lxqt-policykit", "Polkit agent"}, {"swayosd-server", "swayosd", "On-screen display"}, {"avizo-service", "avizo", "On-screen display"},
    {"nwg-dock-hyprla", "nwg-dock-hyprland", "Dock"},
};

struct CfGeom { Rect later, never, close; double h; };
constexpr double kCfgW = 420, kCfgPad = 24, kCfgRow = 52, kCfgIcon = 56, kCfgHint = 34;
CfGeom cfGeom(size_t count) {
    CfGeom g;
    const double listY = kCfgPad + kCfgIcon + 12 + 30 + 14 + 40 + 12;
    const double by = listY + count * kCfgRow + 6 + kCfgHint + 16;
    g.h = by + 40 + kCfgPad;
    g.close = {kCfgW - kCfgPad - 118, by, 118, 40};
    g.never = {g.close.x - 8 - 100, by, 100, 40};
    g.later = {g.never.x - 8 - 92, by, 92, 40};
    return g;
}

std::string cfIgnorePath() { return stateDir() + "/conflicts-ignore.txt"; }
std::set<std::string> cfIgnored() {
    std::set<std::string> s;
    std::ifstream f(cfIgnorePath());
    for (std::string line; std::getline(f, line);) if (!line.empty()) s.insert(line);
    return s;
}

// The known programs that run now (the ones the user said to leave alone are skipped unless all is true).
std::vector<CfItem> cfScan(bool all) {
    std::string names;
    for (const CfKnown& k : kCfKnown) names += std::string(names.empty() ? "" : " ") + k.comm;
    const std::string out = captureArgv({"bash", "-c", "for n in " + names + "; do pgrep -x \"$n\" >/dev/null 2>&1 && echo \"$n\"; done"});
    const std::set<std::string> skip = all ? std::set<std::string>{} : cfIgnored();
    std::vector<CfItem> items;
    std::istringstream in(out);
    for (std::string line; std::getline(in, line);) {
        for (const CfKnown& k : kCfKnown) if (line == k.comm && !skip.count(line)) items.push_back({k.comm, k.name, k.kind});
    }
    return items;
}

void cfBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kCfBuffer = {cfBufferRelease};

void drawConflicts(App& app) {
    CfState& S = app.cf;
    if (!S.surface || !S.configured) return;
    const CfGeom G = cfGeom(S.items.size());
    const int W = static_cast<int>(kCfgW), H = static_cast<int>(std::ceil(G.h)), stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-conflicts", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    PangoLayout* L = pango_cairo_create_layout(cr);
    roundedRect(cr, 0, 0, kCfgW, G.h, 28);
    setSource(cr, app.color("surface_container_high"), 0.99);
    cairo_fill(cr);
    cairo_new_path(cr);
    cairo_arc(cr, kCfgW / 2, kCfgPad + kCfgIcon / 2, kCfgIcon / 2, 0, 2 * M_PI);
    setSource(cr, app.color("tertiary_container"));
    cairo_fill(cr);
    drawIcon(cr, "danger-triangle", kCfgW / 2 - 15, kCfgPad + kCfgIcon / 2 - 15, 30, app.color("on_tertiary_container"));
    const double ty = kCfgPad + kCfgIcon + 12;
    clkText(cr, L, i18n::tr("Programs that can conflict"), 22, false, kCfgPad, ty + 15, app.color("on_surface"), 1, kCfgW - 2 * kCfgPad, 1);
    PangoLayout* B = pango_cairo_create_layout(cr);
    PangoFontDescription* fd2 = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd2, 14 * PANGO_SCALE);
    pango_layout_set_font_description(B, fd2);
    pango_font_description_free(fd2);
    pango_layout_set_width(B, static_cast<int>((kCfgW - 2 * kCfgPad) * PANGO_SCALE));
    pango_layout_set_alignment(B, PANGO_ALIGN_CENTER);
    pango_layout_set_text(B, i18n::tr("These run next to end4pC and can fight with it for the notifications, the wallpaper or the bar."), -1);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, kCfgPad, ty + 40);
    pango_cairo_show_layout(cr, B);
    g_object_unref(B);
    double y = ty + 30 + 14 + 40 + 12;
    for (const CfItem& it : S.items) {
        roundedRect(cr, kCfgPad, y, kCfgW - 2 * kCfgPad, kCfgRow - 8, 16);
        setSource(cr, app.color("surface_container"));
        cairo_fill(cr);
        clkText(cr, L, it.name, 15, true, kCfgPad + 16, y + (kCfgRow - 8) / 2.0, app.color("on_surface"), 1);
        clkText(cr, L, i18n::tr(it.kind.c_str()), 13, false, kCfgPad + 16, y + (kCfgRow - 8) / 2.0, app.color("on_surface_variant"), 1, kCfgW - 2 * kCfgPad - 32, 2);
        y += kCfgRow;
    }
    PangoLayout* Hn = pango_cairo_create_layout(cr);
    PangoFontDescription* hf = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(hf, 12 * PANGO_SCALE);
    pango_layout_set_font_description(Hn, hf);
    pango_font_description_free(hf);
    pango_layout_set_width(Hn, static_cast<int>((kCfgW - 2 * kCfgPad) * PANGO_SCALE));
    pango_layout_set_alignment(Hn, PANGO_ALIGN_CENTER);
    pango_layout_set_text(Hn, i18n::tr("They can start again with your session: take them out of your startup apps."), -1);
    setSource(cr, app.color("on_surface_variant"), 0.85);
    cairo_move_to(cr, kCfgPad, y + 6);
    pango_cairo_show_layout(cr, Hn);
    g_object_unref(Hn);
    const auto button = [&](const Rect& r, const char* label, int id, bool filled) {
        const bool hot = S.hover == id;
        if (filled || hot) {
            roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            if (filled) setSource(cr, app.color("primary"), hot ? 0.88 : 1.0);
            else setSource(cr, app.color("primary"), 0.12);
            cairo_fill(cr);
        }
        const int tw = clkTextWidth(L, label, 14, true);
        clkText(cr, L, label, 14, true, r.x + (r.w - tw) / 2.0, r.y + r.h / 2.0, app.color(filled ? "on_primary" : "primary"), 1);
    };
    button(G.later, i18n::tr("Not now"), 100, false);
    button(G.never, i18n::tr("Never ask"), 101, false);
    button(G.close, i18n::tr("Close them"), 102, true);
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kCfBuffer, nullptr);
    wl_surface_attach(S.surface, buffer, 0, 0);
    wl_surface_damage_buffer(S.surface, 0, 0, W, H);
    wl_surface_commit(S.surface);
}

void closeConflicts(App& app) {
    CfState& S = app.cf;
    if (!S.surface) return;
    if (S.layer) zwlr_layer_surface_v1_destroy(S.layer);
    wl_surface_destroy(S.surface);
    S.surface = nullptr;
    S.layer = nullptr;
    S.configured = false;
    S.hover = -1;
    S.items.clear();
    app.onCf = false;
}

void onCfConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    app->cf.configured = true;
    drawConflicts(*app);
}
void onCfClosed(void* d, zwlr_layer_surface_v1*) { closeConflicts(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kCfListener = {onCfConfigure, onCfClosed};

// Opens the card for what runs now; returns how many programs it lists (nothing is opened when there are none).
size_t openConflicts(App& app, bool all) {
    CfState& S = app.cf;
    if (!app.compositor || !app.layerShell || !app.shm) return 0;
    closeConflicts(app);
    S.items = cfScan(all);
    if (S.items.empty()) return 0;
    S.hover = -1;
    const CfGeom G = cfGeom(S.items.size());
    const Bar* bar = mainBar(app);
    S.surface = wl_compositor_create_surface(app.compositor);
    S.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, S.surface, bar ? bar->output : nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-conflicts");
    zwlr_layer_surface_v1_set_anchor(S.layer, 0);
    zwlr_layer_surface_v1_set_size(S.layer, static_cast<uint32_t>(kCfgW), static_cast<uint32_t>(std::ceil(G.h)));
    zwlr_layer_surface_v1_set_exclusive_zone(S.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(S.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(S.layer, &kCfListener, &app);
    wl_surface_commit(S.surface);
    return S.items.size();
}

// At the first start of a session: look once, and say nothing when nothing runs.
void conflictsStartupCheck(App& app) {
    if (!app.cfg.conflictCheck) return;
    const std::string marker = runtimeDir() + "/end4pC-conflicts-checked";
    if (access(marker.c_str(), F_OK) == 0) return;
    if (FILE* m = std::fopen(marker.c_str(), "w")) std::fclose(m);
    openConflicts(app, false);
}

void cfCloseThem(App& app) {
    for (const CfItem& it : app.cf.items) runDetached({"pkill", "-x", it.comm.c_str()});
    runDetached({"notify-send", "-a", "end4pC", "-i", "dialog-warning", i18n::tr("Programs closed"), i18n::tr("If they come back, take them out of your startup apps.")});
    closeConflicts(app);
}

void cfNeverAsk(App& app) {
    mkdir(stateDir().c_str(), 0755);
    std::ofstream f(cfIgnorePath(), std::ios::app);
    for (const CfItem& it : app.cf.items) f << it.comm << "\n";
    closeConflicts(app);
}

int cfHit(const App& app, double x, double y) {
    const CfGeom G = cfGeom(app.cf.items.size());
    return G.later.contains(x, y) ? 100 : G.never.contains(x, y) ? 101 : G.close.contains(x, y) ? 102 : -1;
}

void cfMotion(App& app) {
    const int over = cfHit(app, app.pointerX, app.pointerY);
    if (over != app.cf.hover) { app.cf.hover = over; drawConflicts(app); }
}

void cfButton(App& app, uint32_t button, bool pressed) {
    if (button != 0x110 || !pressed) return;
    const int hit = cfHit(app, app.pointerX, app.pointerY);
    if (hit == 100) closeConflicts(app);
    else if (hit == 101) cfNeverAsk(app);
    else if (hit == 102) cfCloseThem(app);
}
