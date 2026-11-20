// The "drives" bar module: removable drives (USB sticks, external disks, cards). The bar button shows only while one is plugged in; its popup lists
// them to mount, open and eject (udisksctl). lsblk is read by a detached shell into a cache file, like the Bluetooth module does.
constexpr double kDrRowH = 64, kDrHeadH = 68, kDrHeadBox = 56, kDrBtnGap = 6, kDrMarqueeSpeed = 42, kDrMarqueePause = 0.8, kDrBtn = 36, kDrRowRadius = 20;
constexpr int kDrPollS = 5, kDrMenuPollS = 2;

std::string drivesFile() { return ccCacheDir() + "/drives.txt"; }

bool drivesModuleShown(const App& app) {
    for (const auto* l : {&app.cfg.layoutLeft, &app.cfg.layoutCenter, &app.cfg.layoutRight, &app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight})
        if (std::find(l->begin(), l->end(), "drives") != l->end()) return true;
    return false;
}
bool drivesVisible(const App& app) { return !app.drives.list.empty(); }
bool drivesShown(const App& app) { return drivesModuleShown(app); }

std::string drivesUnescape(const std::string& s) {  // lsblk -P writes \xNN for spaces and quotes
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] == 'x') { out += static_cast<char>(std::strtol(s.substr(i + 2, 2).c_str(), nullptr, 16)); i += 3; }
        else out += s[i];
    }
    return out;
}

std::string drivesSize(unsigned long long b) {
    char buf[32];
    if (b >= 1000ULL * 1000 * 1000 * 1000) std::snprintf(buf, sizeof buf, "%.1f TB", b / 1e12);
    else if (b >= 1000ULL * 1000 * 1000) std::snprintf(buf, sizeof buf, "%.1f GB", b / 1e9);
    else std::snprintf(buf, sizeof buf, "%.0f MB", b / 1e6);
    return buf;
}

std::vector<DriveInfo> drivesParse(const std::string& text) {
    struct Raw { std::map<std::string, std::string> kv; };
    std::vector<Raw> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Raw r;
        size_t i = 0;
        while (i < line.size()) {
            const size_t eq = line.find("=\"", i);
            if (eq == std::string::npos) break;
            const size_t end = line.find('"', eq + 2);
            if (end == std::string::npos) break;
            size_t ks = line.rfind(' ', eq);
            ks = ks == std::string::npos || ks < i ? i : ks + 1;
            r.kv[line.substr(ks, eq - ks)] = drivesUnescape(line.substr(eq + 2, end - eq - 2));
            i = end + 1;
        }
        rows.push_back(std::move(r));
    }
    std::map<std::string, std::string> modelOf;
    std::map<std::string, unsigned long long> sizeOf;
    for (const Raw& r : rows) {
        if (!r.kv.count("TYPE") || r.kv.at("TYPE") != "disk") continue;
        modelOf[r.kv.at("PATH")] = r.kv.count("MODEL") ? r.kv.at("MODEL") : "";
        sizeOf[r.kv.at("PATH")] = r.kv.count("SIZE") ? std::strtoull(r.kv.at("SIZE").c_str(), nullptr, 10) : 0;
    }
    std::vector<DriveInfo> out;  // one entry per disk: a stick with three partitions is one drive
    for (const Raw& r : rows) {
        const auto get = [&](const char* k) { auto it = r.kv.find(k); return it == r.kv.end() ? std::string() : it->second; };
        const std::string type = get("TYPE"), fs = get("FSTYPE");
        if ((type != "part" && type != "disk") || fs.empty() || fs == "swap" || fs == "crypto_LUKS" || fs.size() > 7 && fs.compare(fs.size() - 7, 7, "_member") == 0) continue;
        if (get("HOTPLUG") != "1" && get("RM") != "1") continue;
        if (lowerCopy(get("PARTTYPE")) == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") continue;  // the EFI system partition
        const std::string disk = type == "part" && !get("PKNAME").empty() ? get("PKNAME") : get("PATH");
        auto it = std::find_if(out.begin(), out.end(), [&](const DriveInfo& d) { return d.parent == disk; });
        if (it == out.end()) {
            DriveInfo d;
            d.dev = d.parent = disk;
            d.size = sizeOf.count(disk) ? sizeOf[disk] : 0;
            out.push_back(std::move(d));
            it = out.end() - 1;
        }
        const unsigned long long psize = std::strtoull(get("SIZE").c_str(), nullptr, 10);
        it->parts.push_back(get("PATH"));
        if (get("MOUNTPOINT").empty()) it->unmounted.push_back(get("PATH"));
        else if (it->mount.empty()) it->mount = get("MOUNTPOINT");
        if (psize >= it->biggest) {  // the biggest partition names the drive and gives its file system
            it->biggest = psize;
            it->fstype = fs;
            it->name = !get("LABEL").empty() ? get("LABEL") : trimmed(modelOf.count(disk) ? modelOf[disk] : "");
        }
    }
    for (DriveInfo& d : out) {  // what kind of drive: the model says a stick or a card reader; a big one is a hard disk or an SSD
        const std::string model = lowerCopy(modelOf.count(d.parent) ? modelOf[d.parent] : "");
        const auto has = [&](const char* w) { return model.find(w) != std::string::npos; };
        d.icon = d.parent.rfind("/dev/mmcblk", 0) == 0 || has("card") || has("sd/mmc") || has("sdxc") ? "sd-card"
               : has("flash") || has("datatraveler") || has("cruzer") || has("usb disk") || has("pendrive") ? "flash-drive"
               : has("ssd") || has("hdd") || d.size >= 100ULL * 1000 * 1000 * 1000 ? "server-minimalistic" : "flash-drive";
    }
    for (DriveInfo& d : out)
        if (d.name.empty()) { const std::string m = trimmed(modelOf.count(d.parent) ? modelOf[d.parent] : ""); d.name = m.empty() ? d.dev.substr(d.dev.rfind('/') + 1) : m; }
    return out;
}

void drivesNotify(const std::string& title, const std::string& body) { runDetached({"notify-send", "-a", "end4pC", "-i", "drive-removable-media", i18n::tr(title.c_str()), body.c_str()}); }

void drivesAsk(App& app) {
    const std::string f = drivesFile();
    const std::string script = "lsblk -P -p -b -o PATH,PKNAME,LABEL,FSTYPE,SIZE,MOUNTPOINT,RM,HOTPLUG,TYPE,PARTTYPE,MODEL > \"$1.tmp\" 2>/dev/null && mv \"$1.tmp\" \"$1\"";
    std::filesystem::create_directories(ccCacheDir());
    runDetached({"bash", "-c", script.c_str(), "bash", f.c_str()});
    app.drives.nextRead = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
}

void drivesLayout(App& app);
void drawDrivesMenu(App& app);

// A drive plugged in or taken out: say so; taking out one that was mounted and not ejected is a warning.
void drivesApply(App& app, std::vector<DriveInfo> now) {
    DrivesBar& D = app.drives;
    if (!D.primed) {
        D.primed = true;
    } else {
        for (const DriveInfo& d : now) {
            const bool known = std::any_of(D.list.begin(), D.list.end(), [&](const DriveInfo& o) { return o.dev == d.dev; });
            const bool sameDisk = std::any_of(D.list.begin(), D.list.end(), [&](const DriveInfo& o) { return o.parent == d.parent; });
            if (!known && !sameDisk) drivesNotify("Drive connected", d.name + " · " + drivesSize(d.size));
        }
        for (const DriveInfo& o : D.list) {
            if (std::any_of(now.begin(), now.end(), [&](const DriveInfo& d) { return d.parent == o.parent; })) continue;
            const bool ejected = D.ejecting.erase(o.parent) > 0;
            if (!ejected && !o.mount.empty()) drivesNotify("Drive removed without ejecting", o.name + " was in use: its files may be damaged");
        }
    }
    const bool changed = now.size() != D.list.size() || !std::equal(now.begin(), now.end(), D.list.begin(), [](const DriveInfo& a, const DriveInfo& b) { return a.dev == b.dev && a.mount == b.mount && a.name == b.name; });
    D.list = std::move(now);
    DrivesMenu& M = app.drivesMenu;
    if (!M.pending.empty() && (changed || std::chrono::steady_clock::now() - M.pendingAt > std::chrono::seconds(8))) {  // the action is over: the state shows it
        M.pending.clear();
        if (!changed && M.open()) { drivesLayout(app); drawDrivesMenu(app); }
    }
    if (!changed) return;
    if (app.drivesMenu.open()) {  // (the items point into the list: they are rebuilt before anything draws them again)
        drivesLayout(app);
        if (D.list.empty()) retractDrivesMenu(app); else drawDrivesMenu(app);
    }
    drawAll(app);
}

void drivesRead(App& app) {
    struct stat st {};
    const std::string path = drivesFile();
    if (stat(path.c_str(), &st) != 0) return;
    const long long stamp = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
    if (stamp == app.drives.stamp) return;
    app.drives.stamp = stamp;
    drivesApply(app, drivesParse(ccFileRead(path)));
}

void drivesTick(App& app, std::chrono::steady_clock::time_point now) {
    DrivesBar& D = app.drives;
    if (!drivesShown(app)) return;
    if (now >= D.nextPoll) { drivesAsk(app); D.nextPoll = now + std::chrono::seconds(app.drivesMenu.open() ? kDrMenuPollS : kDrPollS); }
    if (D.nextRead != std::chrono::steady_clock::time_point{} && now >= D.nextRead) { D.nextRead = {}; drivesRead(app); }
}

std::chrono::steady_clock::time_point drivesWake(const App& app) {
    auto when = std::chrono::steady_clock::time_point::max();
    if (drivesShown(app)) when = std::min(when, app.drives.nextPoll);
    if (app.drives.nextRead != std::chrono::steady_clock::time_point{}) when = std::min(when, app.drives.nextRead);
    if (app.drivesMenu.open() && app.drivesMenu.marquee) when = std::min(when, std::chrono::steady_clock::now() + std::chrono::milliseconds(16));
    if (app.drivesMenu.open() && app.drivesMenu.configured) when = std::min(when, popWake(app.drivesMenu.anim.phase, app.drivesMenu.anim.settleAt, std::chrono::steady_clock::now()));
    return when;
}

void drivesIconPaint(cairo_t* cr, App& app, double x, double y, double w, double h, bool hot) {
    const Color framed = styleFrame(cr, app, widgetStyle(app, "drives"), x, y, w, h, hot);
    const bool inUse = std::any_of(app.drives.list.begin(), app.drives.list.end(), [](const DriveInfo& d) { return !d.mount.empty(); });
    drawIcon(cr, "flash-drive", x + (w - 18) / 2.0, y + (h - 18) / 2.0, 18, inUse ? app.color("primary") : framed, hot);
    if (inUse) {
        cairo_arc(cr, x + w - 7, y + 7, 3.2, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
}
void drivesBarPaint(cairo_t* cr, App& app, const Bar& bar) {
    if (!drivesVisible(app)) return;
    const double x = slotX(bar, "drives"), y = (bar.height - kQuickBox) / 2.0;
    const bool hot = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitDrives;
    drivesIconPaint(cr, app, x, y, kQuickBox + softExtra(app, "drives"), kQuickBox, hot);
}
void drivesSidePaint(cairo_t* cr, App& app, Rect r, bool hot) {
    if (!drivesVisible(app)) return;
    drivesIconPaint(cr, app, r.x + (r.w - kQuickBox) / 2.0, r.y + (r.h - kQuickBox) / 2.0, kQuickBox, kQuickBox, hot);
}

void drivesLayout(App& app) {
    DrivesMenu& M = app.drivesMenu;
    using K = DrivesMenu::Item::Kind;
    M.items.clear();
    M.hover = -1;
    const double x = kClkPad, w = kClkW - 2 * kClkPad;
    double y = kClkPad + kDrHeadH;
    const auto& list = app.drives.list;
    for (size_t i = 0; i < list.size(); i++) {
        M.items.push_back({K::Row, static_cast<int>(i), {x, y, w, kDrRowH}});
        M.items.push_back({K::Eject, static_cast<int>(i), {x + w - 12 - kDrBtn, y + (kDrRowH - kDrBtn) / 2, kDrBtn, kDrBtn}});
        if (!list[i].mount.empty()) M.items.push_back({K::Copy, static_cast<int>(i), {x + w - 12 - 2 * kDrBtn - kDrBtnGap, y + (kDrRowH - kDrBtn) / 2, kDrBtn, kDrBtn}});
        y += kDrRowH + kClkGap;
    }
    if (!M.note.empty()) { M.items.push_back({K::Note, -1, {x, y, w, kDrRowH}}); y += kDrRowH + kClkGap; }
    M.cardW = static_cast<int>(kClkW);
    M.cardH = static_cast<int>(y - kClkGap + kClkPad);
}

bool drivesInteractive(DrivesMenu::Item::Kind k) { return k != DrivesMenu::Item::Kind::Note; }
int drivesItemAt(const DrivesMenu& M, double x, double y) {
    int hit = -1;
    for (size_t k = 0; k < M.items.size(); k++)  // (the buttons lie over their row: they win)
        if (drivesInteractive(M.items[k].kind) && M.items[k].r.contains(x, y) && (hit < 0 || M.items[k].kind != DrivesMenu::Item::Kind::Row)) hit = static_cast<int>(k);
    return hit;
}

void drivesDrawItem(App& app, cairo_t* cr, PangoLayout* L, size_t k) {
    const DrivesMenu& M = app.drivesMenu;
    const DrivesMenu::Item& it = M.items[k];
    const Rect r = it.r;
    const bool hot = M.hover == static_cast<int>(k), pressed = M.pressed == static_cast<int>(k);
    if (it.kind == DrivesMenu::Item::Kind::Note) {
        roundedRect(cr, r.x, r.y, r.w, r.h, kDrRowRadius);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        clkText(cr, L, M.note, 13, false, r.x + 16, r.y + r.h / 2, app.color("error"), 1, r.w - 32);
        return;
    }
    if (it.dev < 0 || it.dev >= static_cast<int>(app.drives.list.size())) return;
    const DriveInfo& d = app.drives.list[static_cast<size_t>(it.dev)];
    const bool busy = M.pending == d.parent || M.pending == d.dev;
    if (it.kind == DrivesMenu::Item::Kind::Row) {
        const bool rowHot = M.hover == static_cast<int>(k) || (M.hover >= 0 && M.items[static_cast<size_t>(M.hover)].dev == it.dev);
        roundedRect(cr, r.x, r.y, r.w, r.h, kDrRowRadius);
        setSource(cr, app.color(rowHot || pressed ? "surface_container_highest" : "surface_container_high"), busy ? 0.6 : 1);
        cairo_fill(cr);
        const double cy = r.y + r.h / 2;
        roundedRect(cr, r.x + 10, cy - 22, 44, 44, 22);
        setSource(cr, app.color(d.mount.empty() ? "surface_container_highest" : "primary_container"));
        cairo_fill(cr);
        drawIcon(cr, d.icon.empty() ? "flash-drive" : d.icon, r.x + 20, cy - 12, 24, app.color(d.mount.empty() ? "on_surface_variant" : "primary"), false);
        const bool mounted = !d.mount.empty();
        const double tx = r.x + 10 + 44 + 14, textW = r.x + r.w - 12 - (mounted ? 2 * kDrBtn + kDrBtnGap : kDrBtn) - 12 - tx;
        clkText(cr, L, d.name, 15, true, tx, cy - 10, app.color("on_surface"), 1, textW);
        const std::string sub = busy ? std::string(i18n::tr("Working…")) : d.mount.empty() ? std::string(i18n::tr("Not mounted")) + " · " + drivesSize(d.size) : drivesSize(d.size) + " · " + d.fstype + " · " + d.mount;
        const int sw = clkTextWidth(L, sub, 12, false);
        if (sw <= textW) {
            clkText(cr, L, sub, 12, false, tx, cy + 12, app.color("on_surface_variant"), 1, textW);
        } else {  // too long: it ends in an ellipsis, and slides back and forth while the pointer is on the row
            double off = 0;
            if (rowHot) {
                const double over = sw - textW, leg = over / kDrMarqueeSpeed, cycle = 2 * (leg + kDrMarqueePause);
                const double t = std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now() - M.hoverAt).count(), cycle);
                off = t < kDrMarqueePause ? 0 : t < kDrMarqueePause + leg ? (t - kDrMarqueePause) * kDrMarqueeSpeed : t < 2 * kDrMarqueePause + leg ? over : over - (t - 2 * kDrMarqueePause - leg) * kDrMarqueeSpeed;
                const_cast<DrivesMenu&>(M).marquee = true;
            }
            cairo_save(cr);
            cairo_rectangle(cr, tx, cy + 2, textW, 20);
            cairo_clip(cr);
            if (rowHot) clkText(cr, L, sub, 12, false, tx - off, cy + 12, app.color("on_surface_variant"), 1);
            else {
                std::string cut = sub;
                while (cut.size() > 4 && clkTextWidth(L, cut + "…", 12, false) > textW) {
                    size_t c = cut.size() - 1;
                    while (c > 0 && (static_cast<unsigned char>(cut[c]) & 0xC0) == 0x80) c--;
                    cut.resize(c);
                }
                clkText(cr, L, cut + "…", 12, false, tx, cy + 12, app.color("on_surface_variant"), 1);
            }
            cairo_restore(cr);
        }
        return;
    }
    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
    setSource(cr, app.color(hot || pressed ? "surface_container_high" : "surface_container"), busy ? 0.5 : 1);
    cairo_fill(cr);
    const bool isCopy = it.kind == DrivesMenu::Item::Kind::Copy;
    drawIcon(cr, isCopy ? (M.copied == it.dev ? "check-circle" : "copy") : "export", r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, app.color(isCopy && M.copied == it.dev ? "primary" : "on_surface"), false);
}

void drawDrivesMenu(App& app) {
    DrivesMenu& M = app.drivesMenu;
    if (!M.open() || !M.configured) return;
    PopupFrame frame;
    if (!popupCanvasBegin(frame, "end4pC-drives-menu", M.cardW, M.cardH, M.scale)) return;
    const double rev = std::max(0.0, M.anim.reveal);
    if (rev > 0.001) {
        cairo_identity_matrix(frame.cr);
        const double room = kTmRoom;
        popPaintGrown(frame.cr, rev, M.ox + room, M.oy + room, M.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8), [&] {
            cairo_save(frame.cr);
            cairo_translate(frame.cr, room, room);
            popupCardPaint(app, frame.cr, M.cardW, M.cardH, kClkRadius);
            PangoLayout* L = pango_cairo_create_layout(frame.cr);
            {
                const double bx = kClkPad, by = kClkPad;
                shapePath(frame.cr, kM3IconShape, bx, by, kDrHeadBox);
                setSource(frame.cr, app.color("primary_container"));
                cairo_fill(frame.cr);
                drawIcon(frame.cr, "flash-drive", bx + (kDrHeadBox - 26) / 2, by + (kDrHeadBox - 26) / 2, 26, app.color("on_primary_container"));
                const double tx = bx + kDrHeadBox + 14, tw = kClkW - kClkPad - tx;
                clkText(frame.cr, L, i18n::tr("Removable drives"), 16, true, tx, by + kDrHeadBox / 2.0 - 11, app.color("on_surface"), 1, tw);
                clkText(frame.cr, L, std::string(i18n::tr("Connected")) + " " + std::to_string(app.drives.list.size()), 13, false, tx, by + kDrHeadBox / 2.0 + 12, app.color("on_surface_variant"), 1, tw);
            }
            cairo_save(frame.cr);
            roundedRect(frame.cr, 0, 0, M.cardW, M.cardH, kClkRadius);
            cairo_clip(frame.cr);
            M.marquee = false;
            for (size_t k = 0; k < M.items.size(); k++) drivesDrawItem(app, frame.cr, L, k);
            cairo_restore(frame.cr);
            g_object_unref(L);
            if (!M.bubble.key.empty()) M.bubbleMoving = bubbleDraw(frame.cr, M.bubble, {app.color("surface").r, app.color("surface").g, app.color("surface").b}, {app.color("on_surface").r, app.color("on_surface").g, app.color("on_surface").b});
            cairo_restore(frame.cr);
        });
    }
    popupFrameCommit(app, frame, M.surface, M.blur, M.cardW, M.cardH, kClkRadius);
}

void closeDrivesMenu(App& app) {
    DrivesMenu& M = app.drivesMenu;
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
    M.hover = M.pressed = -1;
    M.anim = PopAnim{};
    bubbleHide(M.bubble);
    M.bubbleMoving = M.marquee = false;
    M.copied = -1;
    app.onDrives = false;
    updateCursor(app);
    malloc_trim(0);
}

void onDrivesConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->drivesMenu.configured) app->drivesMenu.anim.animStart = std::chrono::steady_clock::now();
    app->drivesMenu.configured = true;
    drawDrivesMenu(*app);
}
void onDrivesClosed(void* d, zwlr_layer_surface_v1*) { closeDrivesMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kDrivesListener = {onDrivesConfigure, onDrivesClosed};

void openDrivesMenu(App& app, Bar* bar) {
    if (!bar || !app.layerShell || !app.compositor || !app.shm || app.drives.list.empty()) return;
    closeAllPopups(app);
    DrivesMenu& M = app.drivesMenu;
    M.bar = bar;
    M.ax = app.pointerX;
    M.ay = app.pointerY;
    M.hover = M.pressed = -1;
    M.configured = false;
    M.note.clear();
    M.pending.clear();
    drivesLayout(app);
    M.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, M.scale, M.surface, [&app] { drawDrivesMenu(app); });
    M.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, M.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-drives-menu");
    const PopOrigin origin = placePopupBeside(app, M.layer, M.ax, M.ay, M.cardW, M.cardH, bar);
    M.ox = origin.x;
    M.oy = origin.y;
    popIconCentre(app, M.ax, M.ay, M.ix, M.iy);
    popStart(M.anim);
    zwlr_layer_surface_v1_set_exclusive_zone(M.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(M.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(M.layer, &kDrivesListener, &app);
    wl_surface_commit(M.surface);
    drivesAsk(app);
}

void retractDrivesMenu(App& app) {
    DrivesMenu& M = app.drivesMenu;
    if (!M.open() || popIsOut(M.anim)) return;
    if (!M.configured) { closeDrivesMenu(app); return; }
    popRetract(M.anim, std::chrono::steady_clock::now());
    M.hover = M.pressed = -1;
    bubbleHide(M.bubble);
    drawDrivesMenu(app);
}
void toggleDrivesMenu(App& app, Bar* bar) {
    DrivesMenu& M = app.drivesMenu;
    if (!M.open()) { openDrivesMenu(app, bar); return; }
    if (popIsOut(M.anim)) { popReverse(M.anim, std::chrono::steady_clock::now()); drawDrivesMenu(app); return; }
    retractDrivesMenu(app);
}
bool drivesIconPressed(const App& app) {
    return app.drivesMenu.open() && std::fabs(app.pointerX - app.drivesMenu.ix) <= 20 && std::fabs(app.pointerY - app.drivesMenu.iy) <= 20;
}
void tickDrivesMenu(App& app, std::chrono::steady_clock::time_point now) {
    DrivesMenu& M = app.drivesMenu;
    if (!M.open() || !M.configured) return;
    bool moving = false;
    if (popTick(M.anim, now, moving)) { closeDrivesMenu(app); return; }
    if (moving || M.bubbleMoving || M.marquee) drawDrivesMenu(app);
}

void drivesMenuMotion(App& app) {
    DrivesMenu& M = app.drivesMenu;
    const int hover = drivesItemAt(M, app.pointerX - kTmRoom, app.pointerY - kTmRoom);
    if (hover == M.hover) return;
    const int oldDev = M.hover >= 0 ? M.items[static_cast<size_t>(M.hover)].dev : -1, newDev = hover >= 0 ? M.items[static_cast<size_t>(hover)].dev : -1;
    M.hover = hover;
    if (newDev != oldDev) { M.hoverAt = std::chrono::steady_clock::now(); M.copied = -1; }  // (the text starts from its beginning on each row)
    if (hover < 0) bubbleHide(M.bubble);
    else {
        const DrivesMenu::Item& it = M.items[static_cast<size_t>(hover)];
        const char* tip = it.kind == DrivesMenu::Item::Kind::Eject ? "Eject" : it.kind == DrivesMenu::Item::Kind::Copy ? "Copy the mount point" : nullptr;
        if (tip) bubbleShow(M.bubble, "dr" + std::to_string(hover), tip, it.r.x, it.r.y, it.r.w, it.r.h, M.cardW, M.cardH);
        else bubbleHide(M.bubble);
    }
    drawDrivesMenu(app);
    updateCursor(app);
}

// Mount, or unmount everything on the disk and power it off (the notification says when it is safe to pull out).
void drivesAction(App& app, const DriveInfo& d, bool eject) {
    DrivesMenu& M = app.drivesMenu;
    M.pending = d.parent;
    M.pendingAt = std::chrono::steady_clock::now();
    M.note.clear();
    if (!eject && !d.mount.empty()) { runDetached({"xdg-open", d.mount.c_str()}); retractDrivesMenu(app); return; }
    static const char* kMount =
        "ok=0; err=''; for p in \"${@:3}\"; do if out=$(udisksctl mount -b \"$p\" 2>&1); then ok=1; else err=\"$out\"; fi; done; "
        "if [ $ok = 1 ]; then notify-send -a end4pC -i drive-removable-media 'Drive mounted' \"$2\"; "
        "else notify-send -a end4pC -i dialog-error 'Could not mount the drive' \"$(printf %s \"$err\" | head -c 200)\"; fi";
    static const char* kEject =
        "fail=''; for p in $(lsblk -nrp -o PATH,MOUNTPOINT \"$1\" | awk 'NF>1{print $1}'); do "
        "out=$(udisksctl unmount -b \"$p\" 2>&1) || { fail=\"$out\"; break; }; done; "
        "if [ -n \"$fail\" ]; then notify-send -a end4pC -i dialog-error 'Could not eject the drive' \"$(printf %s \"$fail\" | head -c 200)\"; "
        "else udisksctl power-off -b \"$1\" >/dev/null 2>&1; notify-send -a end4pC -i drive-removable-media 'Safe to remove' \"$2\"; fi";
    if (eject) app.drives.ejecting.insert(d.parent);
    std::vector<const char*> args = {"bash", "-c", eject ? kEject : kMount, "bash", d.parent.c_str(), d.name.c_str()};
    if (!eject) for (const std::string& p : d.unmounted) args.push_back(p.c_str());
    runDetachedArgs(args);
    app.drives.nextPoll = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    app.drives.nextRead = {};
}

void drivesMenuButton(App& app, uint32_t button, bool pressed) {
    DrivesMenu& M = app.drivesMenu;
    constexpr uint32_t kBtnLeft = 0x110;
    if (button != kBtnLeft || popIsOut(M.anim)) return;
    const double x = app.pointerX - kTmRoom, y = app.pointerY - kTmRoom;
    if (!pressed) {
        if (M.pressed < 0) return;
        const int held = M.pressed;
        M.pressed = -1;
        if (drivesItemAt(M, x, y) == held) {
            const DrivesMenu::Item it = M.items[static_cast<size_t>(held)];
            if (it.dev >= 0 && it.dev < static_cast<int>(app.drives.list.size())) {
                const DriveInfo d = app.drives.list[static_cast<size_t>(it.dev)];
                if (it.kind == DrivesMenu::Item::Kind::Copy) { copyToClipboard(d.mount); M.copied = it.dev; }
                else drivesAction(app, d, it.kind == DrivesMenu::Item::Kind::Eject);
            }
        }
        if (M.open()) { drivesLayout(app); drawDrivesMenu(app); }
        return;
    }
    if (!Rect{0, 0, double(M.cardW), double(M.cardH)}.contains(x, y)) { retractDrivesMenu(app); return; }
    const int k = drivesItemAt(M, x, y);
    if (k < 0) return;
    M.pressed = k;
    drawDrivesMenu(app);
}
