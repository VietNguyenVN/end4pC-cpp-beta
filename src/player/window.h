// The window of Play3r: Wayland, the buffers, the input and the loop. Included once by player.cpp, inside its anonymous namespace.
#pragma once

void onFrameDone(void*, wl_callback* cb, uint32_t) {
    wl_callback_destroy(cb);
    g.frameCallback = nullptr;
    if (g.dirty) {}  // the next turn of the loop draws
}
const wl_callback_listener kFrameListener = {onFrameDone};

void onBufferRelease(void* data, wl_buffer*) { static_cast<Buffer*>(data)->busy = false; }
const wl_buffer_listener kBufferListener = {onBufferRelease};

Buffer* acquireBuffer(int width, int height, double scale) {
    Buffer* spare = nullptr;
    for (auto& slot : g.buffers) {
        if (slot.busy) continue;
        if (slot.buffer && slot.frame.lw == width && slot.frame.lh == height && slot.frame.scale == scale) return &slot;
        if (!spare) spare = &slot;
    }
    if (!spare) return nullptr;
    spare->clear();
    auto& frame = spare->frame;
    if (!hidpi::begin(frame, width, height, scale, "end4pC-play3r")) return nullptr;
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, frame.fd, static_cast<int32_t>(frame.size));
    if (pool) {
        spare->buffer = wl_shm_pool_create_buffer(pool, 0, frame.pw, frame.ph, frame.stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }
    close(frame.fd);
    frame.fd = -1;
    if (!spare->buffer) { spare->clear(); return nullptr; }
    wl_buffer_add_listener(spare->buffer, &kBufferListener, spare);
    return spare;
}

void render() {
    if (!g.configured || !g.surface || g.frameCallback) return;
    Buffer* slot = acquireBuffer(g.width, g.height, g.scale.factor());
    if (!slot) return;
    g_scale = g.scale.factor();
    hidpi::Frame& f = slot->frame;
    const auto t0 = Clock::now();
    const bool fast = !g.dirty && g.waveDirty && !g.base.empty() && g.baseW == g.width && g.baseH == g.height && g.baseScale == g_scale && g.base.size() == f.size;
    if (fast) {  // only the progress moved: the rest of the window is the picture saved at the last full draw
        std::memcpy(f.data, g.base.data(), f.size);
        cairo_surface_mark_dirty(f.surface);
        cairo_t* cr = mkcr(f.surface);
        drawProgress(cr);
        cairo_destroy(cr);
    } else {
        cairo_t* cr = mkcr(f.surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        drawWindow(cr, g.width, g.height);
        cairo_destroy(cr);
        cairo_surface_flush(f.surface);
        g.base.assign(static_cast<unsigned char*>(f.data), static_cast<unsigned char*>(f.data) + f.size);
        g.baseW = g.width;
        g.baseH = g.height;
        g.baseScale = g_scale;
        cr = mkcr(f.surface);
        drawProgress(cr);
        cairo_destroy(cr);
    }
    if (std::getenv("PLAY3R_STATS")) {
        static double total = 0;
        static int frames = 0, full = 0;
        static auto since = Clock::now();
        total += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        frames++;
        if (!fast) full++;
        if (Clock::now() - since > std::chrono::seconds(2)) {
            std::fprintf(stderr, "play3r: %.1f frames/s (%d full), %.2f ms per frame\n", frames / 2.0, full, total / frames);
            total = 0; frames = 0; full = 0; since = Clock::now();
        }
    }
    cairo_surface_flush(f.surface);
    g.frameCallback = wl_surface_frame(g.surface);
    if (g.frameCallback) wl_callback_add_listener(g.frameCallback, &kFrameListener, nullptr);
    slot->busy = true;
    hidpi::present(g.surface, slot->buffer, f, g.scale);
    g.dirty = false;
    g.waveDirty = false;
    g.lastRender = Clock::now();
}

// ---- input ------------------------------------------------------------------------------------------------------------------------------
// The pointer: an arrow by default, a hand over what can be clicked, a text cursor over the fields.
void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.hoverId == SearchField || g.hoverId == NameField) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    else if (g.hoverId != None) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}
void setHoverImpl() {
    int id = None, arg = 0;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; arg = it->arg; break; }
    if (id != g.hoverId || arg != g.hoverArg) { g.hoverId = id; g.hoverArg = arg; g.dirty = true; }
}
// The bar the pointer is on (the progress or the volume), as a position from 0 to 1.
void setHover() { setHoverImpl(); updateCursor(); }
double fractionOn(int id, double x) {
    for (const Hit& h : g.hits) if (h.id == id) return std::clamp((x - h.r.x) / std::max(1.0, h.r.w), 0.0, 1.0);
    return 0;
}
void applyVolume(double frac) {
    g.engine->setVolume(static_cast<float>(frac));
    g.saved.volume = g.engine->volume();
    g.dirty = true;
}
void onClick() {
    setHover();
    if (g.hoverId != SearchField && g.hoverId != NameField && g.hoverId != SearchClear && g.focus != 0) { g.focus = 0; g.dirty = true; }
    switch (g.hoverId) {
        case SearchField: g.focus = 1; g.search.cur = g.search.s.size(); g.dirty = true; break;
        case NameField: g.focus = 2; g.plName.cur = g.plName.s.size(); g.dirty = true; break;
        case SearchClear: g.search = {}; g.viewDirty = true; g.dirty = true; break;
        case TabPlaylists: g.tab = 2; g.playlistsDirty = true; g.plNote.clear(); g.dirty = true; break;
        case SaveList: saveQueueAsPlaylist(); break;
        case PlaylistRow:
            if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(g.playlists.size())) { loadPlaylistFile(g.playlists[static_cast<size_t>(g.hoverArg)].path, true); g.tab = 1; g.search = {}; }
            break;
        case PlaylistDelete:
            if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(g.playlists.size())) deletePlaylist(g.playlists[static_cast<size_t>(g.hoverArg)].path);
            break;
        case BtnClose: g.running = false; break;
        case BtnOpen: chooseFolder(); break;
        case TabLyrics: g.tab = 0; g.dirty = true; break;
        case TabQueue: g.tab = 1; g.queueToCurrent = true; g.dirty = true; break;
        case BtnShuffle: g.saved.shuffle = !g.saved.shuffle; g.nextIndex = pickNext(); saveSaved(g.saved); g.dirty = true; break;
        case BtnRepeat: g.saved.repeat = (g.saved.repeat + 1) % 3; saveSaved(g.saved); g.dirty = true; break;
        case BtnPrev: previousTrack(); break;
        case BtnNext: nextTrack(); break;
        case BtnPlay: togglePlay(); break;
        case QueueRow: playIndex(g.hoverArg); break;
        case UpNext: nextTrack(); break;
        case LyricRow:
            if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(g.lyrics.size())) seekAbs(g.lyrics[static_cast<size_t>(g.hoverArg)].time);
            break;
        case Progress:
            g.drag = Drag::Seek;
            g.dragFrac = fractionOn(Progress, g.px);
            g.dirty = true;
            break;
        case VolumeBar:
            g.drag = Drag::Volume;
            applyVolume(fractionOn(VolumeBar, g.px));
            break;
        default: break;
    }
}
void onRelease() {
    if (g.drag == Drag::Seek) seekAbs(g.dragFrac * g.engine->duration());
    if (g.drag == Drag::Volume) saveSaved(g.saved);
    g.drag = Drag::None;
    g.dirty = true;
}
void onMotion() {
    if (g.drag == Drag::Seek) { g.dragFrac = fractionOn(Progress, g.px); g.dirty = true; }
    else if (g.drag == Drag::Volume) applyVolume(fractionOn(VolumeBar, g.px));
    setHover();
}
void onWheel(double v) {
    const bool onList = g.px > kEdge + kLeftW + kGap && g.py > kTop;
    if (onList && g.tab == 1) { elasticWheel(g.queueBand, g.queuePos, g.queueMax, v * 3); g.dirty = true; }
    else if (onList && g.tab == 2) { elasticWheel(g.plBand, g.plPos, g.plMax, v * 3); g.dirty = true; }
    else if (!onList && g.py > kTop) { applyVolume(std::clamp(g.engine->volume() - v * 0.0015, 0.0, 1.0)); saveSaved(g.saved); }
}

size_t prevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    do i--; while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
size_t nextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do i++; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
// A key while a text field has the keyboard. True when it can repeat (letters, Backspace, the arrows).
bool fieldKey(uint32_t key, xkb_keysym_t sym, bool ctrl) {
    Field& f = g.focus == 1 ? g.search : g.plName;
    f.cur = std::min(f.cur, f.s.size());
    g.caretReset = Clock::now();
    g.dirty = true;
    switch (sym) {
        case XKB_KEY_Escape:
            if (g.focus == 1 && !g.search.s.empty()) { g.search = {}; g.viewDirty = true; }
            else g.focus = 0;
            return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter:
            if (g.focus == 2) saveQueueAsPlaylist();
            else { if (g.viewDirty) rebuildView(); if (!g.view.empty()) playIndex(g.view[static_cast<size_t>(std::min<int>(g.selPos, static_cast<int>(g.view.size()) - 1))]); g.focus = 0; }
            return false;
        case XKB_KEY_Left: f.cur = prevChar(f.s, f.cur); return true;
        case XKB_KEY_Right: f.cur = nextChar(f.s, f.cur); return true;
        case XKB_KEY_Home: f.cur = 0; return false;
        case XKB_KEY_End: f.cur = f.s.size(); return false;
        case XKB_KEY_BackSpace:
            if (f.cur > 0) { const size_t p = prevChar(f.s, f.cur); f.s.erase(p, f.cur - p); f.cur = p; g.viewDirty = true; }
            return true;
        case XKB_KEY_Delete:
            if (f.cur < f.s.size()) { f.s.erase(f.cur, nextChar(f.s, f.cur) - f.cur); g.viewDirty = true; }
            return true;
        case XKB_KEY_Up: case XKB_KEY_Down:  // through the results: Enter plays the marked one
            if (g.focus == 1 && !g.view.empty()) {
                g.selPos = std::clamp(g.selPos + (sym == XKB_KEY_Up ? -1 : 1), 0, static_cast<int>(g.view.size()) - 1);
                constexpr double rowH = 56, gap = 4, areaH = 360;
                const double top = g.selPos * (rowH + gap);
                if (top < g.queuePos) g.queuePos = top;
                else if (top + rowH > g.queuePos + areaH) g.queuePos = top + rowH - areaH;
            }
            return true;
        case XKB_KEY_Prior: case XKB_KEY_Next:
            if (g.focus == 1 && !g.view.empty()) g.selPos = std::clamp(g.selPos + (sym == XKB_KEY_Prior ? -6 : 6), 0, static_cast<int>(g.view.size()) - 1);
            return true;
        default: break;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f || f.s.size() > 120) return false;
    f.s.insert(f.cur, buf, static_cast<size_t>(n));
    f.cur += static_cast<size_t>(n);
    g.viewDirty = true;
    return true;
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (g.focus != 0) {
        if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_w)) { g.running = false; return false; }
        return fieldKey(key, sym, ctrl);
    }
    if (g.tab == 1 && !ctrl) {  // in the queue, typing a letter starts the search (no need to click the field first)
        char buf[16];
        const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
        if (n > 0 && static_cast<unsigned char>(buf[0]) > 0x20 && buf[0] != 0x7f) {
            g.focus = 1;
            g.search.cur = g.search.s.size();
            return fieldKey(key, sym, ctrl);
        }
    }
    if (ctrl && (sym == XKB_KEY_f || sym == XKB_KEY_F)) { g.tab = 1; g.focus = 1; g.search.cur = g.search.s.size(); g.caretReset = Clock::now(); g.dirty = true; return false; }
    if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_w)) { g.running = false; return false; }
    if (ctrl && sym == XKB_KEY_o) { chooseFolder(); return false; }
    switch (sym) {
        case XKB_KEY_space: togglePlay(); return false;
        case XKB_KEY_Left: seekAbs(g.engine->position() - 5); return true;
        case XKB_KEY_Right: seekAbs(g.engine->position() + 5); return true;
        case XKB_KEY_Up: applyVolume(std::clamp(g.engine->volume() + 0.05, 0.0, 1.0)); saveSaved(g.saved); return true;
        case XKB_KEY_Down: applyVolume(std::clamp(g.engine->volume() - 0.05, 0.0, 1.0)); saveSaved(g.saved); return true;
        case XKB_KEY_n: case XKB_KEY_N: nextTrack(); return false;
        case XKB_KEY_p: case XKB_KEY_P: previousTrack(); return false;
        case XKB_KEY_s: case XKB_KEY_S: g.saved.shuffle = !g.saved.shuffle; g.nextIndex = pickNext(); saveSaved(g.saved); g.dirty = true; return false;
        case XKB_KEY_r: case XKB_KEY_R: g.saved.repeat = (g.saved.repeat + 1) % 3; saveSaved(g.saved); g.dirty = true; return false;
        case XKB_KEY_l: case XKB_KEY_L: g.tab = 0; g.dirty = true; return false;
        case XKB_KEY_q: case XKB_KEY_Q: g.tab = 1; g.queueToCurrent = true; g.dirty = true; return false;
        default: return false;
    }
}

// ---- Wayland ----------------------------------------------------------------------------------------------------------------------------
void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};
void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) { xdg_surface_ack_configure(s, serial); g.configured = true; g.dirty = true; }
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(kMinW, w); g.height = std::max(kMinH, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { g.running = false; }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) { g.enterSerial = serial; g.cursorShape = -1; g.inside = true; g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); setHover(); }
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) { g.inside = false; setHover(); }
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { g.px = wl_fixed_to_double(x); g.py = wl_fixed_to_double(y); onMotion(); }
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    if (button != 0x110) return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) onClick(); else onRelease();
}
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) { if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) onWheel(wl_fixed_to_double(value)); }
const wl_pointer_listener kPointerListener = {onPEnter, onPLeave, onPMotion, onPButton, onPAxis, [](void*, wl_pointer*) {}, [](void*, wl_pointer*, uint32_t) {},
                                              [](void*, wl_pointer*, uint32_t, uint32_t) {}, [](void*, wl_pointer*, uint32_t, int32_t) {}, nullptr, nullptr, nullptr};

void onKKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!g.xkbCtx) g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (km) {
                if (g.xkbState) xkb_state_unref(g.xkbState);
                if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
                g.xkbMap = km;
                g.xkbState = xkb_state_new(km);
            }
        }
    }
    close(fd);
}
void onKKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) { if (key == g.repeatKey) g.repeatKey = 0; return; }
    if (handleKey(key) && g.kbRate > 0) { g.repeatKey = key; g.repeatAt = Clock::now() + std::chrono::milliseconds(g.kbDelay); }
    else g.repeatKey = 0;
}
void onKMods(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) { if (g.xkbState) xkb_state_update_mask(g.xkbState, dep, lat, lock, 0, 0, group); }
void onKRepeat(void*, wl_keyboard*, int32_t rate, int32_t delay) { g.kbRate = rate; g.kbDelay = delay; }
const wl_keyboard_listener kKeyboardListener = {onKKeymap, [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}, [](void*, wl_keyboard*, uint32_t, wl_surface*) { g.repeatKey = 0; },
                                                onKKey, onKMods, onKRepeat};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) { g.pointer = wl_seat_get_pointer(seat); wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr); }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) { g.keyboard = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr); }
}
const wl_seat_listener kSeatListener = {onSeatCaps, [](void*, wl_seat*, const char*) {}};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wp_viewporter_interface.name) && !hidpi::disabled()) g.viewporter = static_cast<wp_viewporter*>(wl_registry_bind(reg, name, &wp_viewporter_interface, 1));
    else if (!std::strcmp(iface, wp_fractional_scale_manager_v1_interface.name) && !hidpi::disabled())
        g.fscaleMgr = static_cast<wp_fractional_scale_manager_v1*>(wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wp_cursor_shape_manager_v1_interface.name)) g.cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
        g.wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(g.wm, &kWmListener, nullptr);
    }
}
const wl_registry_listener kRegistryListener = {onGlobal, [](void*, wl_registry*, uint32_t) {}};

bool connectWayland() {
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-play3r: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-play3r: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Play3r");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-play3r");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

// Once per turn of the loop: the clock of the track, the end of a track, the lyrics that arrived, the colours that changed.
void tick() {
    const auto now = Clock::now();
    const double pos = g.engine->position();
    const bool playing = g.engine->state() == player::State::Playing;
    const bool wave = now - g.lastWave >= std::chrono::milliseconds(50);  // the wave moves at 20 frames a second
    if (std::abs(pos - g.shownPosition) > 0.04 && g.drag != Drag::Seek) {
        g.shownPosition = pos;
        g.waveDirty = true;
        if (static_cast<int>(pos) != g.timeDrawn) g.dirty = true;  // the time label shows another second
        if (g.tab == 0 && !g.lyrics.empty() && currentLyric(pos) != g.lyCurrentDrawn) g.dirty = true;  // another line is the current one
    }
    if (playing && wave) { g.waveDirty = true; g.lastWave = now; }  // the wave moves while it plays
    if (g.lyBusy && g.tab == 0) g.dirty = true;                       // the lyrics are gliding
    if (g.engine->takeEnded()) {
        if (g.saved.repeat == 2 && g.current >= 0) playIndex(g.current, false);
        else nextTrack();
    }
    if (g_lyrics.version.load() != g.lyricsSeen) {
        g.lyricsSeen = g_lyrics.version.load();
        std::lock_guard<std::mutex> lock(g_lyrics.mutex);
        g.lyrics = g_lyrics.lines;
        g.lyricsStatus = g_lyrics.status;
        g.lyHeights.clear();
        g.dirty = true;
    }
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (g.queueBand.step() | g.plBand.step()) g.dirty = true;
    if (g.viewDirty) { rebuildView(); g.queuePos = 0; g.dirty = true; }
    if (g.focus != 0) {  // the caret blinks
        const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(now - g.caretReset).count() / 530 % 2 == 0;
        if (on != g.caretOn) { g.caretOn = on; g.dirty = true; }
    }
    g.lastTick = now;
}

int pollTimeoutBase() {
    int t = g.engine && g.engine->state() == player::State::Playing ? 33 : 250;
    if (g.queueBand.busy() || g.plBand.busy() || g.lyBusy) t = 16;
    else if (g.focus != 0) t = std::min(t, 120);
    return t;
}

// A key held down repeats from the loop, so the loop must wake when the next repeat is due (otherwise the repeat only runs when something else wakes it).
int pollTimeout() {
    int t = pollTimeoutBase();
    if (g.repeatKey) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - Clock::now()).count();
        t = std::min<int>(t, static_cast<int>(std::max<long long>(1, left)));
    }
    return t;
}

void teardown() {
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    if (g.frameCallback) wl_callback_destroy(g.frameCallback);
    if (g.toplevel) xdg_toplevel_destroy(g.toplevel);
    if (g.xsurface) xdg_surface_destroy(g.xsurface);
    hidpi::detach(g.scale);
    if (g.surface) wl_surface_destroy(g.surface);
    for (auto& b : g.buffers) b.clear();
    if (g.display) { wl_display_flush(g.display); wl_display_disconnect(g.display); }
    if (g.xkbState) xkb_state_unref(g.xkbState);
    if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
    if (g.xkbCtx) xkb_context_unref(g.xkbCtx);
    if (g.cover) cairo_surface_destroy(g.cover);
}

volatile sig_atomic_t g_term = 0;
void onTermSignal(int) { g_term = 1; }

int run(int argc, char** argv) {
    std::string lang;
    const std::string cfg = readFile(configPath());
    jsonValue(cfg, "language", lang);
    std::string mat, style;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(cfg, "materialIcons", mat) && mat == "1";
    std::string scale;
    if (jsonValue(cfg, "iconScale", scale) && !scale.empty()) g_iconScale = std::clamp(std::atoi(scale.c_str()), 60, 150);
    if (jsonValue(cfg, "iconStyle", style) && !style.empty()) g_iconStyle = style;
    i18n::load(lang, assetsDir(), dataDir() + "/i18n");
    applyShellFont(fontFamilyFromConfig(dataDir() + "/config.json"), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(dataDir() + "/config.json", "fontWeightTitle", 550));
    loadColors();
    if (!connectWayland()) return 1;
    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);

    int wake[2];
    if (pipe2(wake, O_CLOEXEC | O_NONBLOCK) < 0) return 1;
    g_wakeFd = wake[1];
    g_scanner.start(wake[1]);

    g.saved = loadSaved();
    g.engine = std::make_unique<player::Engine>();
    g.engine->setVolume(g.saved.volume);
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; i++) inputs.push_back(argv[i]);
    const bool autoplay = !inputs.empty();
    if (inputs.empty()) {
        // The queue of the last time (paused, on the song that was loaded), or the music folder the first time.
        std::vector<Track> last;
        for (const std::string& p : readM3u(lastQueuePath())) { std::error_code ec; if (std::filesystem::is_regular_file(p, ec)) last.push_back({p}); }
        if (!last.empty()) {
            setQueue(std::move(last), false);
            if (g.saved.current >= 0 && g.saved.current < static_cast<int>(g.queue.size())) playIndex(g.saved.current, false, false);
        } else openMusic({homeDir() + "/Music"}, false);
    } else openMusic(inputs, autoplay);
    const bool haveMpris = g_mpris.start(mprisControl());
    if (!haveMpris) std::fprintf(stderr, "end4pC-play3r: no D-Bus session: the bar and the media keys will not see the player\n");

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wake[0], POLLIN, 0}, {haveMpris ? g_mpris.fd() : -1, haveMpris ? g_mpris.events() : static_cast<short>(0), 0}};
        int timeout = pollTimeout();
        if (haveMpris && g_mpris.timeoutMs() >= 0) timeout = std::min(timeout, g_mpris.timeoutMs());
        if (poll(fds, 3, timeout) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wake[0], junk, sizeof junk) > 0) {}
            g.dirty = true;
            if (!g.search.s.empty()) g.viewDirty = true;  // tags arrived: the search can find more
            std::string chosen;
            {
                std::lock_guard<std::mutex> lock(g_chosenMutex);
                chosen.swap(g_chosen);
            }
            if (!chosen.empty()) openMusic({chosen}, true);
        }
        if (haveMpris) g_mpris.process();
        tick();
        if (haveMpris) g_mpris.publish();
        if (g.dirty || g.waveDirty) render();
    }
    saveLastQueue();
    g.engine.reset();
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
