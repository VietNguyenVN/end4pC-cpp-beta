// What Fil3s does and its Wayland window: navigation, selection, the operations, the menu, the keys, the loop.
// Included once by fil3s.cpp, inside its anonymous namespace (after ui.h).
#pragma once

// ---- the folder ------------------------------------------------------------------------------------------------------------------------
bool g_keepScroll = false;  // a refresh of the same folder keeps the scroll; going to another one starts at the top

void watchFolder() {
    if (g.inotifyFd < 0) return;
    if (g.watch >= 0) { inotify_rm_watch(g.inotifyFd, g.watch); g.watch = -1; }
    g.watch = inotify_add_watch(g.inotifyFd, g.cwd.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
}
void reload(bool keepScroll) {
    g.generation++;
    g.loading = g.entries.empty() || !keepScroll;
    g_keepScroll = keepScroll;
    g.lister.request(g.cwd, g.generation);
    g.dirty = true;
}
void navigate(const std::string& path, bool record = true) {
    std::error_code ec;
    const std::string target = fs::weakly_canonical(path, ec).string();
    if (ec || !fs::is_directory(target, ec)) { g.toast = i18n::tr("That folder cannot be opened"); g.toastAt = Clock::now(); g.dirty = true; return; }
    if (record && !g.cwd.empty() && target != g.cwd) { g.back.push_back(g.cwd); g.forward.clear(); if (g.back.size() > 200) g.back.erase(g.back.begin()); }
    g.cwd = target;
    g.entries.clear();
    g.view.clear();
    g.selected.clear();
    g.cursor = g.anchor = -1;
    g.search = {};
    g.loadError.clear();
    g.thumbs.forgetPending();
    g.waitingThumbs.clear();
    g.fadingThumbs.clear();
    watchFolder();
    reload(false);
}
void goBack() { if (g.back.empty()) return; const std::string to = g.back.back(); g.back.pop_back(); g.forward.push_back(g.cwd); navigate(to, false); }
void goForward() { if (g.forward.empty()) return; const std::string to = g.forward.back(); g.forward.pop_back(); g.back.push_back(g.cwd); navigate(to, false); }
void goUp() { if (g.cwd != "/" && !inTrash()) navigate(fs::path(g.cwd).parent_path().string()); }

void rebuildView() {
    g.view.clear();
    const std::string needle = lowerCase(g.search.s);
    for (size_t i = 0; i < g.entries.size(); i++) {
        const Entry& e = g.entries[i];
        if (e.hidden && !g.showHidden && needle.empty()) continue;
        if (!needle.empty() && e.lower.find(needle) == std::string::npos) continue;
        g.view.push_back(static_cast<int>(i));
    }
    g.cursor = std::min(g.cursor, static_cast<int>(g.view.size()) - 1);
    g.viewDirty = false;
    g.dirty = true;
}
void applyListing(Listing& l) {
    if (l.dir != g.cwd) return;
    g.entries = std::move(l.entries);
    sortEntries(g.entries, g.sort, g.descending);
    g.loading = false;
    g.loadError = l.error;
    std::set<std::string> present;
    for (const Entry& e : g.entries) present.insert(e.path);
    for (auto it = g.selected.begin(); it != g.selected.end();) it = present.count(*it) ? std::next(it) : g.selected.erase(it);
    if (!g.selectAfter.empty()) {
        g.selected.clear();
        for (const std::string& p : g.selectAfter) if (present.count(p)) g.selected.insert(p);
        g.selectAfter.clear();
    }
    if (!g_keepScroll) { g.scroll = 0; g.band.reset(); }
    rebuildView();
}
void resort() {
    sortEntries(g.entries, g.sort, g.descending);
    rebuildView();
}

// ---- selection -------------------------------------------------------------------------------------------------------------------------
void selectOnly(int i) {
    g.selected.clear();
    if (i >= 0 && i < static_cast<int>(g.view.size())) g.selected.insert(entryAt(i).path);
    g.cursor = g.anchor = i;
    g.dirty = true;
}
void selectRange(int to) {
    if (g.anchor < 0) g.anchor = 0;
    g.selected.clear();
    for (int i = std::min(g.anchor, to); i <= std::max(g.anchor, to) && i < static_cast<int>(g.view.size()); i++) g.selected.insert(entryAt(i).path);
    g.cursor = to;
    g.dirty = true;
}
void toggleOne(int i) {
    const std::string& p = entryAt(i).path;
    if (!g.selected.erase(p)) g.selected.insert(p);
    g.cursor = g.anchor = i;
    g.dirty = true;
}
void selectAll() {
    for (int i : g.view) g.selected.insert(g.entries[static_cast<size_t>(i)].path);
    g.dirty = true;
}
std::vector<const Entry*> selectedEntries() {
    std::vector<const Entry*> out;
    for (int i : g.view) { const Entry& e = g.entries[static_cast<size_t>(i)]; if (g.selected.count(e.path)) out.push_back(&e); }
    return out;
}
std::vector<std::string> selectedPaths() {
    std::vector<std::string> out;
    for (const Entry* e : selectedEntries()) out.push_back(e->path);
    return out;
}
void scrollToCursor() {
    if (g.cursor < 0 || g.cellH <= 0 || g.columns <= 0) return;
    const double headerH = g.grid ? 0 : 34, viewH = g.content.h - headerH;
    const double top = (g.cursor / g.columns) * g.cellH, bottom = top + g.cellH;
    if (top < g.scroll) g.scroll = top;
    else if (bottom > g.scroll + viewH) g.scroll = bottom - viewH;
    g.scroll = std::clamp(g.scroll, 0.0, g.scrollMax);
    g.dirty = true;
}

// ---- operations ------------------------------------------------------------------------------------------------------------------------
void say(const std::string& msg) { g.toast = msg; g.toastAt = Clock::now(); g.dirty = true; }

// A name typed in a dialog: a single path component (no slash, not "." or "..", no control characters, not longer than a file name can be).
bool validName(const std::string& n) {
    if (n.empty() || n.size() > 255 || n == "." || n == "..") return false;
    for (const unsigned char c : n) if (c == '/' || c < 0x20 || c == 0x7f) return false;
    return true;
}
void openEntry(int i) {
    const Entry& e = entryAt(i);
    if (e.dir) { navigate(e.path); return; }
    if (inTrash()) { say(i18n::tr("Restore it from the Trash to open it")); return; }
    if (!e.regular) { say(i18n::tr("That is not a regular file")); return; }
    openWithDefault(e.path);
}
void copySelection(bool cut) {
    g.clip = selectedPaths();
    g.clipCut = cut;
    if (!g.clip.empty()) say(std::to_string(g.clip.size()) + " · " + (cut ? i18n::tr("Ready to move") : i18n::tr("Ready to copy")));
}
void pasteHere() {
    if (g.clip.empty() || inTrash()) return;
    g.worker.submit({g.clipCut ? Op::Move : Op::Copy, g.clip, g.cwd, {}});
    if (g.clipCut) g.clip.clear();
}
void trashSelection() {
    const auto paths = selectedPaths();
    if (paths.empty()) return;
    if (inTrash()) { g.dialog = Dlg::ConfirmDelete; g.dirty = true; return; }
    g.worker.submit({Op::Trash, paths, "", {}});
}
void askDeleteForever() { if (!g.selected.empty()) { g.dialog = Dlg::ConfirmDelete; g.dirty = true; } }
void restoreSelection() { const auto p = selectedPaths(); if (!p.empty()) g.worker.submit({Op::Restore, p, "", {}}); }
void emptyTrash() {
    std::vector<std::string> all;
    std::error_code ec;
    for (fs::directory_iterator it(trashFiles(), ec), end; !ec && it != end; it.increment(ec)) all.push_back(it->path().string());
    if (!all.empty()) g.worker.submit({Op::Delete, all, "", {}});
}
void openDialog(Dlg d, const std::string& initial = "", const std::string& target = "") {
    g.dialog = d;
    g.dialogField = {initial, initial.size()};
    g.dialogTarget = target;
    g.focus = d == Dlg::ConfirmDelete ? 0 : 2;
    g.caretReset = Clock::now();
    g.dirty = true;
}
void startRename() {
    const auto sel = selectedEntries();
    if (sel.size() != 1 || inTrash()) return;
    const std::string& n = sel[0]->name;
    const size_t dot = n.rfind('.');
    openDialog(Dlg::Rename, n, sel[0]->path);
    if (dot != std::string::npos && dot > 0 && !sel[0]->dir) g.dialogField.cur = dot;  // the caret before the extension
}
void closeDialog() { g.dialog = Dlg::None; g.focus = 0; g.dirty = true; }
void commitDialog() {
    if (g.dialog == Dlg::ConfirmDelete) {
        const auto paths = selectedPaths();
        closeDialog();
        if (!paths.empty()) g.worker.submit({Op::Delete, paths, "", {}});
        return;
    }
    const std::string name = g.dialogField.s;
    if (!validName(name)) { say(i18n::tr("That name is not valid")); return; }
    std::error_code ec;
    const Dlg kind = g.dialog;
    if (kind == Dlg::Rename) {
        const std::string to = (fs::path(g.dialogTarget).parent_path() / name).string();
        if (to == g.dialogTarget) { closeDialog(); return; }
        if (fs::exists(fs::symlink_status(to, ec))) { say(i18n::tr("A file with that name already exists")); return; }
        std::string error;
        if (!moveAny(g.dialogTarget, to, error)) { say(error); return; }
        g.selectAfter = {to};
    } else {
        const std::string to = (fs::path(g.cwd) / name).string();
        if (fs::exists(fs::symlink_status(to, ec))) { say(i18n::tr("A file with that name already exists")); return; }
        if (kind == Dlg::NewFolder) fs::create_directory(to, ec);
        else { const int fd = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644); if (fd >= 0) close(fd); else ec = std::error_code(errno, std::generic_category()); }
        if (ec) { say(ec.message()); return; }
        g.selectAfter = {to};
    }
    closeDialog();
    reload(true);
}

// The conversions and archives (see convert.h): one click, the new file appears next to the original.
void submitCommands(std::vector<Cmd> cmds) {
    if (cmds.empty()) return;
    g.worker.submit({Op::Command, {}, "", std::move(cmds)});
}
void convertSelection(int targetIndex) {
    const Target& t = targets()[static_cast<size_t>(targetIndex)];
    std::vector<Cmd> cmds;
    for (const Entry* e : selectedEntries()) if (e->regular && offers(e->kind, t, extensionOf(e->name))) cmds.push_back(convertCommand(*e, t));
    submitCommands(std::move(cmds));
}
void compressSelection(int formatIndex) {
    const auto sel = selectedEntries();
    if (sel.empty()) return;
    submitCommands({archiveCommand(sel, archiveFormats()[static_cast<size_t>(formatIndex)], g.cwd)});
}
void extractSelection(bool intoFolder) {
    std::vector<Cmd> cmds;
    for (const Entry* e : selectedEntries()) if (e->kind == Kind::Archive && e->regular) cmds.push_back(extractCommand(*e, intoFolder));
    submitCommands(std::move(cmds));
}

// ---- the places ------------------------------------------------------------------------------------------------------------------------
bool placeExists(const std::string& path) {
    for (const Place& p : g.places) if (p.path == path) return true;
    return false;
}
void reindexPlaces() { for (size_t i = 0; i < g.places.size(); i++) g.places[i].user = static_cast<int>(i); }
void addPlace(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_directory(path, ec)) { say(i18n::tr("That folder cannot be opened")); return; }
    if (placeExists(path)) { say(i18n::tr("Already in Places")); return; }
    g.places.push_back(makePlace(path));
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void removePlace(int i) {
    if (i < 0 || i >= static_cast<int>(g.places.size())) return;
    g.places.erase(g.places.begin() + i);
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void movePlace(int from, int to) {
    const int n = static_cast<int>(g.places.size());
    if (from < 0 || from >= n) return;
    to = std::clamp(to, 0, n - 1);
    if (from == to) return;
    Place moved = g.places[static_cast<size_t>(from)];
    g.places.erase(g.places.begin() + from);
    g.places.insert(g.places.begin() + to, std::move(moved));
    reindexPlaces();
    savePlaces(g.places);
    g.dirty = true;
}
void updateDrop() {
    const int n = static_cast<int>(g.places.size());
    if (n == 0) return;
    const int to = std::clamp(static_cast<int>((g.py - g.placeRowsY0) / g.placeStep), 0, n - 1);
    if (to != g.dropIndex) { g.dropIndex = to; g.dirty = true; }
}
void cancelPlacePress() { g.pressedPlace = -1; g.placeDrag = false; g.dirty = true; }

// ---- the menu --------------------------------------------------------------------------------------------------------------------------
MenuItem item(const std::string& label, const char* ic, Act act, int arg = 0, bool enabled = true) {
    MenuItem m;
    m.label = label; m.icon = ic; m.act = act; m.arg = arg; m.enabled = enabled;
    return m;
}
MenuItem separatorItem() { MenuItem m; m.separator = true; return m; }

void buildMenu(bool onItem) {
    g.menu.clear();
    g.menuSub = -1;
    g.subPending = -2;
    const auto sel = selectedEntries();
    if (inTrash()) {
        if (onItem) { g.menu.push_back(item(i18n::tr("Restore"), "restart", Act::Restore)); g.menu.push_back(item(i18n::tr("Delete forever"), "trash-bin-2", Act::Delete)); g.menu.push_back(separatorItem()); }
        g.menu.push_back(item(i18n::tr("Empty the Trash"), "trash-bin-trash", Act::EmptyTrash, 0, !g.entries.empty()));
        g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
        return;
    }
    if (onItem && !sel.empty()) {
        const bool one = sel.size() == 1;
        g.menu.push_back(item(i18n::tr(sel[0]->dir && one ? "Open folder" : "Open"), "folder-open", Act::Open, 0, one));
        g.menu.push_back(separatorItem());
        g.menu.push_back(item(i18n::tr("Cut"), "scissors", Act::Cut));
        g.menu.push_back(item(i18n::tr("Copy"), "copy", Act::Copy));
        g.menu.push_back(item(i18n::tr("Rename"), "pen", Act::Rename, 0, one));
        if (one && sel[0]->dir && !placeExists(sel[0]->path)) g.menu.push_back(item(i18n::tr("Add to Places"), "bookmark", Act::AddPlace, 0));
        g.menu.push_back(item(i18n::tr("Move to Trash"), "trash-bin-minimalistic", Act::Trash));
        g.menu.push_back(separatorItem());
        // Conversions: only when every selected file is of the same family.
        bool allImage = true, allVideo = true, allAudio = true, allArchive = true;
        for (const Entry* e : sel) {
            allImage &= e->kind == Kind::Image && e->regular && extensionOf(e->name) != "svg";
            allVideo &= e->kind == Kind::Video && e->regular;
            allAudio &= e->kind == Kind::Audio && e->regular;
            allArchive &= e->kind == Kind::Archive && e->regular;
        }
        auto convertMenu = [&](const std::string& title, const char* ic, Group first, Group second, bool both) {
            MenuItem m = item(title, ic, Act::None);
            const auto& all = targets();
            const std::string ext0 = one ? extensionOf(sel[0]->name) : "";
            for (size_t i = 0; i < all.size(); i++) {
                if (all[i].group != first && !(both && all[i].group == second)) continue;
                if (ext0 == all[i].ext || (ext0 == "jpeg" && std::string(all[i].ext) == "jpg")) continue;  // not into what it already is
                if (both && all[i].group == second && !m.children.empty() && m.children.back().act == Act::Convert && all[m.children.back().arg].group == first) m.children.push_back(separatorItem());
                m.children.push_back(item(all[i].label, "", Act::Convert, static_cast<int>(i)));
            }
            return m;
        };
        if (allImage) g.menu.push_back(convertMenu(i18n::tr("Convert to"), "gallery-edit", Group::Image, Group::Image, false));
        if (allVideo) {
            g.menu.push_back(convertMenu(i18n::tr("Convert video to"), "video-frame-replace", Group::Video, Group::Video, false));
            g.menu.push_back(convertMenu(i18n::tr("Extract audio as"), "music-note", Group::Audio, Group::Audio, false));
        }
        if (allAudio) g.menu.push_back(convertMenu(i18n::tr("Convert to"), "music-note-slider", Group::Audio, Group::Audio, false));
        if (allArchive) {
            g.menu.push_back(item(i18n::tr("Extract"), "archive-down", Act::Extract));
            g.menu.push_back(item(i18n::tr("Extract here"), "archive-down-minimlistic", Act::ExtractHere));
        }
        MenuItem pack = item(i18n::tr("Compress to"), "archive-up", Act::None);
        const auto& formats = archiveFormats();
        for (size_t i = 0; i < formats.size(); i++) if (haveTool(formats[i].tool)) pack.children.push_back(item(formats[i].label, "", Act::Compress, static_cast<int>(i)));
        if (!pack.children.empty()) g.menu.push_back(pack);
        g.menu.push_back(separatorItem());
        g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
        return;
    }
    g.menu.push_back(item(i18n::tr("New folder"), "add-folder", Act::NewFolder));
    g.menu.push_back(item(i18n::tr("New file"), "document-add", Act::NewFile));
    g.menu.push_back(separatorItem());
    g.menu.push_back(item(i18n::tr("Paste"), "clipboard-add", Act::Paste, 0, !g.clip.empty()));
    if (!placeExists(g.cwd)) g.menu.push_back(item(i18n::tr("Add this folder to Places"), "bookmark", Act::AddPlace, 1));
    g.menu.push_back(item(i18n::tr("Select all"), "list-check", Act::SelectAll));
    g.menu.push_back(separatorItem());
    g.menu.push_back(item(i18n::tr(g.showHidden ? "Hide hidden files" : "Show hidden files"), "eye", Act::Hidden));
    g.menu.push_back(item(i18n::tr("Refresh"), "restart", Act::Refresh));
}
// The menu of a row of the sidebar (a place of the user, the Trash, a drive...), or of the blank part of it (side == -1).
void buildSideMenu(int side) {
    g.menu.clear();
    g.menuSub = -1;
    g.subPending = -2;
    if (side < 0 || side >= static_cast<int>(g_sideRows.size())) {
        g.menu.push_back(item(i18n::tr("Add this folder to Places"), "add-circle", Act::AddPlace, 1, !inTrash() && !placeExists(g.cwd)));
        g.menu.push_back(item(i18n::tr("Reset Places"), "restart", Act::ResetPlaces));
        return;
    }
    g.menu.push_back(item(i18n::tr("Open"), "folder-open", Act::OpenPlace, side));
    if (side < static_cast<int>(g.places.size())) g.menu.push_back(item(i18n::tr("Remove from Places"), "close-circle", Act::RemovePlace, side));
    else if (g_sideRows[static_cast<size_t>(side)].path == trashFiles()) g.menu.push_back(item(i18n::tr("Empty the Trash"), "trash-bin-trash", Act::EmptyTrash));
}
void openMenu(bool onItem, int side = -2) {
    if (side != -2) buildSideMenu(side); else buildMenu(onItem);
    g.menuOpen = true;
    g.menuX = g.px;
    g.menuY = g.py;
    g.dirty = true;
}
void closeMenu() { if (g.menuOpen) { g.menuOpen = false; g.menuSub = -1; g.subPending = -2; g.dirty = true; } }
void runAct(const MenuItem& m) {
    switch (m.act) {
        case Act::AddPlace: { const auto sel = selectedEntries(); addPlace(m.arg == 1 || sel.empty() ? g.cwd : sel[0]->path); break; }
        case Act::RemovePlace: removePlace(m.arg); break;
        case Act::ResetPlaces: resetPlaces(); g.places = loadPlaces(); g.dirty = true; break;
        case Act::OpenPlace: if (m.arg >= 0 && m.arg < static_cast<int>(g_sideRows.size())) navigate(g_sideRows[static_cast<size_t>(m.arg)].path); break;
        case Act::Open: { const auto sel = selectedEntries(); if (sel.size() == 1) for (size_t i = 0; i < g.view.size(); i++) if (entryAt(static_cast<int>(i)).path == sel[0]->path) { openEntry(static_cast<int>(i)); break; } break; }
        case Act::Cut: copySelection(true); break;
        case Act::Copy: copySelection(false); break;
        case Act::Paste: pasteHere(); break;
        case Act::Rename: startRename(); break;
        case Act::Trash: trashSelection(); break;
        case Act::Delete: askDeleteForever(); break;
        case Act::Restore: restoreSelection(); break;
        case Act::EmptyTrash: if (!g.entries.empty()) { g.selected.clear(); selectAll(); g.dialog = Dlg::ConfirmDelete; } break;
        case Act::NewFolder: openDialog(Dlg::NewFolder, i18n::tr("New folder")); break;
        case Act::NewFile: openDialog(Dlg::NewFile, i18n::tr("New file")); break;
        case Act::Convert: convertSelection(m.arg); break;
        case Act::Compress: compressSelection(m.arg); break;
        case Act::Extract: extractSelection(true); break;
        case Act::ExtractHere: extractSelection(false); break;
        case Act::SelectAll: selectAll(); break;
        case Act::Refresh: reload(true); break;
        case Act::Hidden: g.showHidden = !g.showHidden; rebuildView(); break;
        default: break;
    }
}

// ---- the keys --------------------------------------------------------------------------------------------------------------------------
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
bool modActive(const char* name) { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0; }

// A key while a text field has the keyboard. True when it can repeat.
bool fieldKey(uint32_t key, xkb_keysym_t sym, bool ctrl) {
    Field& f = g.focus == 1 ? g.search : g.dialogField;
    f.cur = std::min(f.cur, f.s.size());
    g.caretReset = Clock::now();
    g.dirty = true;
    const bool isSearch = g.focus == 1;
    switch (sym) {
        case XKB_KEY_Escape:
            if (g.dialog != Dlg::None) closeDialog();
            else if (isSearch && !g.search.s.empty()) { g.search = {}; g.viewDirty = true; }
            else g.focus = 0;
            return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter:
            if (g.dialog != Dlg::None) commitDialog();
            else { g.focus = 0; if (g.view.size() == 1) openEntry(0); }
            return false;
        case XKB_KEY_Left: f.cur = prevChar(f.s, f.cur); return true;
        case XKB_KEY_Right: f.cur = nextChar(f.s, f.cur); return true;
        case XKB_KEY_Home: f.cur = 0; return false;
        case XKB_KEY_End: f.cur = f.s.size(); return false;
        case XKB_KEY_BackSpace:
            if (f.cur > 0) { const size_t p = prevChar(f.s, f.cur); f.s.erase(p, f.cur - p); f.cur = p; if (isSearch) g.viewDirty = true; }
            return true;
        case XKB_KEY_Delete:
            if (f.cur < f.s.size()) { f.s.erase(f.cur, nextChar(f.s, f.cur) - f.cur); if (isSearch) g.viewDirty = true; }
            return true;
        case XKB_KEY_Down: if (isSearch && !g.view.empty()) { g.focus = 0; selectOnly(0); scrollToCursor(); } return false;
        default: break;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f || f.s.size() > 240) return false;
    f.s.insert(f.cur, buf, static_cast<size_t>(n));
    f.cur += static_cast<size_t>(n);
    if (isSearch) g.viewDirty = true;
    return true;
}

void moveCursor(int delta, bool extend) {
    if (g.view.empty()) return;
    const int count = static_cast<int>(g.view.size());
    int to = g.cursor < 0 ? (delta > 0 ? 0 : count - 1) : std::clamp(g.cursor + delta, 0, count - 1);
    if (extend) selectRange(to); else selectOnly(to);
    scrollToCursor();
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT), alt = modActive(XKB_MOD_NAME_ALT);
    if (g.pressedPlace != -1 && sym == XKB_KEY_Escape) { cancelPlacePress(); return false; }
    if (g.dialog == Dlg::ConfirmDelete) {
        if (sym == XKB_KEY_Escape) closeDialog();
        else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) commitDialog();
        return false;
    }
    if (g.menuOpen) { if (sym == XKB_KEY_Escape) closeMenu(); return false; }
    if (ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_w)) { g.running = false; return false; }
    if (g.focus != 0) return fieldKey(key, sym, ctrl);
    if (ctrl) {
        switch (sym) {
            case XKB_KEY_a: case XKB_KEY_A: selectAll(); return false;
            case XKB_KEY_c: case XKB_KEY_C: copySelection(false); return false;
            case XKB_KEY_x: case XKB_KEY_X: copySelection(true); return false;
            case XKB_KEY_v: case XKB_KEY_V: pasteHere(); return false;
            case XKB_KEY_h: case XKB_KEY_H: g.showHidden = !g.showHidden; rebuildView(); return false;
            case XKB_KEY_f: case XKB_KEY_F: g.focus = 1; g.search.cur = g.search.s.size(); g.caretReset = Clock::now(); g.dirty = true; return false;
            case XKB_KEY_r: case XKB_KEY_R: reload(true); return false;
            case XKB_KEY_1: g.grid = true; g.dirty = true; return false;
            case XKB_KEY_2: g.grid = false; g.dirty = true; return false;
            case XKB_KEY_Left: case XKB_KEY_Right: case XKB_KEY_Up: case XKB_KEY_Down: {  // Ctrl+arrows: the focus moves, the selection stays
                const int step = sym == XKB_KEY_Left ? -1 : sym == XKB_KEY_Right ? 1 : sym == XKB_KEY_Up ? (g.grid ? -g.columns : -1) : (g.grid ? g.columns : 1);
                if (!g.view.empty()) { g.cursor = std::clamp((g.cursor < 0 ? 0 : g.cursor) + step, 0, static_cast<int>(g.view.size()) - 1); scrollToCursor(); g.dirty = true; }
                return true;
            }
            case XKB_KEY_space: if (g.cursor >= 0 && g.cursor < static_cast<int>(g.view.size())) toggleOne(g.cursor); return false;  // Ctrl+Space: toggles the one in focus
            case XKB_KEY_N: if (shift) openDialog(Dlg::NewFolder, i18n::tr("New folder")); return false;
            case XKB_KEY_n: if (shift) openDialog(Dlg::NewFolder, i18n::tr("New folder")); return false;
            default: return false;
        }
    }
    if (alt) {
        if (sym == XKB_KEY_Left) goBack();
        else if (sym == XKB_KEY_Right) goForward();
        else if (sym == XKB_KEY_Up) goUp();
        return false;
    }
    switch (sym) {
        case XKB_KEY_Left: moveCursor(-1, shift); return true;
        case XKB_KEY_Right: moveCursor(1, shift); return true;
        case XKB_KEY_Up: moveCursor(g.grid ? -g.columns : -1, shift); return true;
        case XKB_KEY_Down: moveCursor(g.grid ? g.columns : 1, shift); return true;
        case XKB_KEY_Prior: moveCursor(-std::max(1, static_cast<int>(g.content.h / std::max(1.0, g.cellH))) * (g.grid ? g.columns : 1), shift); return false;
        case XKB_KEY_Next: moveCursor(std::max(1, static_cast<int>(g.content.h / std::max(1.0, g.cellH))) * (g.grid ? g.columns : 1), shift); return false;
        case XKB_KEY_Home: if (!g.view.empty()) { if (shift) selectRange(0); else selectOnly(0); scrollToCursor(); } return false;
        case XKB_KEY_End: if (!g.view.empty()) { const int last = static_cast<int>(g.view.size()) - 1; if (shift) selectRange(last); else selectOnly(last); scrollToCursor(); } return false;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (g.cursor >= 0 && g.cursor < static_cast<int>(g.view.size())) openEntry(g.cursor); return false;
        case XKB_KEY_BackSpace: goUp(); return false;
        case XKB_KEY_Delete: if (shift) askDeleteForever(); else trashSelection(); return false;
        case XKB_KEY_space: if (g.cursor >= 0 && g.cursor < static_cast<int>(g.view.size())) selectOnly(g.cursor); return false;
        case XKB_KEY_F2: startRename(); return false;
        case XKB_KEY_F5: reload(true); return false;
        case XKB_KEY_Escape: if (!g.selected.empty()) { g.selected.clear(); g.dirty = true; } else if (!g.search.s.empty()) { g.search = {}; g.viewDirty = true; } return false;
        default: break;
    }
    char buf[16];  // typing a letter starts the search (no need to click the field first)
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) > 0x20 && buf[0] != 0x7f) {
        g.focus = 1;
        g.search.cur = g.search.s.size();
        return fieldKey(key, sym, false);
    }
    return false;
}

// ---- the mouse -------------------------------------------------------------------------------------------------------------------------
void updateCursor();
void setHoverImpl() {
    int id = None, arg = 0;
    if (g.inside) for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it) if (it->r.contains(g.px, g.py)) { id = it->id; arg = it->arg; break; }
    if (id != g.hoverId || arg != g.hoverArg) {
        g.hoverId = id;
        g.hoverArg = arg;
        g.dirty = true;
        if (g.menuOpen && id != MenuRow) g.subPending = -2;  // off the menu: whatever was waiting is dropped
        if (g.menuOpen && id == MenuRow) {
            if (arg >= 1000 || arg < 0) g.subPending = -2;  // in the second panel (or on the background of a panel): the open one stays
            else {
                const int sub = arg < static_cast<int>(g.menu.size()) && !g.menu[static_cast<size_t>(arg)].children.empty() ? arg : -1;
                if (sub == g.menuSub) g.subPending = -2;
                else if (g.menuSub < 0) g.menuSub = sub;  // nothing open: the panel follows the pointer at once
                else { g.subPending = sub; g.subPendingAt = Clock::now(); }  // one is open: the pointer may be on its way to it across other rows, so wait a moment
            }
        }
    }
}
void setHover() { setHoverImpl(); updateCursor(); }

// The lasso follows the pointer: whatever its rectangle touches is selected (with Ctrl held at the start, on top of what was already selected).
void updateLasso() {
    g.lassoX1 = std::clamp(g.px - g.content.x, 0.0, g.content.w);
    g.lassoY1 = std::clamp(g.py - g.content.y, 0.0, g.content.h) + g.scroll;
    const double x0 = std::min(g.lassoX0, g.lassoX1), x1 = std::max(g.lassoX0, g.lassoX1), y0 = std::min(g.lassoY0, g.lassoY1), y1 = std::max(g.lassoY0, g.lassoY1);
    g.selected = g.lassoBase;
    for (int i = 0; i < static_cast<int>(g.view.size()); i++) {
        const Rect r = itemRect(i);
        if (r.y > y1) break;  // (the rows come in order: nothing below can be touched)
        if (r.x < x1 && r.x + r.w > x0 && r.y < y1 && r.y + r.h > y0) g.selected.insert(entryAt(i).path);
    }
    g.dirty = true;
}
// The pointer: an arrow by default, a hand over what can be clicked, a text cursor over the fields. (Without this the compositor may show whatever it had.)
void updateCursor() {
    if (!g.cursorMgr || !g.pointer || !g.inside) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (g.placeDrag) {  // picked up: a closed hand
        if (g.cursorShape != WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING) { g.cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING; wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, g.cursorShape); }
        return;
    }
    switch (g.hoverId) {
        case SearchField: case DialogField: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT; break;
        case None: case Empty: case Scrim: case SideEmpty: break;
        case ScrollThumb: case ScrollTrack: break;  // (an arrow over the scroll bar, as everywhere)
        case MenuRow: if (g.hoverArg >= 0) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
        default: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER; break;
    }
    if (g.dialog != Dlg::None && g.hoverId == Scrim) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

// Dragging the scroll bar: the handle follows the pointer, and the folder with it.
void dragScrollTo(double py) {
    const double travel = g.scrollTrack.h - g.thumbH;
    if (travel <= 0 || g.scrollMax <= 0) return;
    const double top = std::clamp(py - g.dragScrollOffset, g.scrollTrack.y, g.scrollTrack.y + travel);
    g.scroll = std::clamp((top - g.scrollTrack.y) / travel * g.scrollMax, 0.0, g.scrollMax);
    g.band.reset();
    g.dirty = true;
}
void onMotion() {
    if (g.pressedPlace >= 0) {  // a button is down on a place: moving before the hold counts cancels it; once picked up, the pointer sets where it lands
        if (g.placeDrag) { updateDrop(); g.dirty = true; }
        else if (std::abs(g.py - g.pressY) > 8 || std::abs(g.px - g.pressX) > 8) g.pressedPlace = -2;
    }
    if (g.dragScroll) { dragScrollTo(g.py); return; }
    if (g.lasso) { updateLasso(); return; }
    setHover();
}
void onRelease() {
    if (g.pressedPlace != -1) {
        const int row = g.pressedPlace;
        const bool dragging = g.placeDrag, stillOnIt = g.hoverId == PlaceRow && g.hoverArg == row;
        const int to = g.dropIndex;
        cancelPlacePress();
        if (dragging) movePlace(row, to);
        else if (row >= 0 && stillOnIt && row < static_cast<int>(g_sideRows.size())) navigate(g_sideRows[static_cast<size_t>(row)].path);
        return;
    }
    if (g.dragScroll) { g.dragScroll = false; g.dirty = true; } if (g.lasso) { g.lasso = false; g.dirty = true; } }

void activateMenuRow(int arg) {
    const MenuItem* m = nullptr;
    if (arg >= 1000) {
        if (g.menuSub >= 0 && g.menuSub < static_cast<int>(g.menu.size())) {
            const auto& kids = g.menu[static_cast<size_t>(g.menuSub)].children;
            if (arg - 1000 < static_cast<int>(kids.size())) m = &kids[static_cast<size_t>(arg - 1000)];
        }
    } else if (arg >= 0 && arg < static_cast<int>(g.menu.size())) m = &g.menu[static_cast<size_t>(arg)];
    if (!m || !m->enabled) return;
    if (!m->children.empty()) { g.menuSub = arg; g.dirty = true; return; }  // (a click on a row with a second panel opens it)
    const MenuItem copy = *m;
    closeMenu();
    runAct(copy);
}

void onClick(uint32_t button) {
    setHover();
    const bool right = button == 0x111;
    if (g.dialog != Dlg::None) {
        if (g.hoverId == DialogOk) commitDialog();
        else if (g.hoverId == DialogCancel || (g.hoverId == Scrim && g.hoverArg == 0)) closeDialog();
        else if (g.hoverId == DialogField) { g.focus = 2; g.dialogField.cur = g.dialogField.s.size(); g.dirty = true; }
        return;
    }
    if (g.menuOpen) {
        if (g.hoverId == MenuRow && !right) { activateMenuRow(g.hoverArg); return; }
        if (g.hoverId == MenuRow) return;
        closeMenu();  // a click anywhere else only closes it (a right click goes on to open a new one)
        if (!right) return;
    }
    if (g.hoverId != SearchField && g.hoverId != SearchClear && g.focus != 0) { g.focus = 0; g.dirty = true; }
    if (button == 0x113) { goBack(); return; }     // the "back" button of the mouse
    if (button == 0x114) { goForward(); return; }  // and the "forward" one
    if (right) {
        if (g.hoverId == Item) { if (!g.selected.count(entryAt(g.hoverArg).path)) selectOnly(g.hoverArg); openMenu(true); }
        else if (g.hoverId == PlaceRow) openMenu(false, g.hoverArg);
        else if (g.hoverId == SideEmpty) openMenu(false, -1);
        else if (g.hoverId == Empty || g.hoverId == None) { g.selected.clear(); openMenu(false); }
        return;
    }
    if (button != 0x110) return;
    switch (g.hoverId) {
        case BtnBack: goBack(); break;
        case BtnForward: goForward(); break;
        case BtnUp: goUp(); break;
        case BtnClose: g.running = false; break;
        case BtnView: g.grid = !g.grid; g.dirty = true; break;
        case BtnHidden: g.showHidden = !g.showHidden; rebuildView(); break;
        case BtnSort:
            if (g.hoverArg >= 10) {  // a column of the list
                const Sort by = static_cast<Sort>(g.hoverArg - 10);
                if (g.sort == by) g.descending = !g.descending; else { g.sort = by; g.descending = by != Sort::Name; }
            } else g.descending = !g.descending;
            resort();
            break;
        case SearchField: g.focus = 1; g.search.cur = g.search.s.size(); g.dirty = true; break;
        case SearchClear: g.search = {}; g.viewDirty = true; g.dirty = true; break;
        case Crumb: if (g.hoverArg >= 0 && g.hoverArg < static_cast<int>(g_crumbPaths.size())) navigate(g_crumbPaths[static_cast<size_t>(g.hoverArg)]); break;
        case PlaceRow:  // it opens when the button is let go; held for a moment, it is picked up instead (see tick)
            g.pressedPlace = g.hoverArg;
            g.pressAt = Clock::now();
            g.pressX = g.px;
            g.pressY = g.py;
            g.placeDrag = false;
            break;
        case ScrollThumb: g.dragScroll = true; g.dragScrollOffset = g.py - g.thumbTop; g.dirty = true; break;
        case ScrollTrack:  // a click on the track: the handle jumps to the pointer and can be dragged from there
            g.dragScroll = true;
            g.dragScrollOffset = g.thumbH / 2;
            dragScrollTo(g.py);
            break;
        case StatusCancel: g.worker.cancelAll(); break;
        case Empty:
            g.lassoBase = modActive(XKB_MOD_NAME_CTRL) || modActive(XKB_MOD_NAME_SHIFT) ? g.selected : std::set<std::string>{};
            g.selected = g.lassoBase;
            g.lasso = true;
            g.lassoX0 = std::clamp(g.px - g.content.x, 0.0, g.content.w);
            g.lassoY0 = std::clamp(g.py - g.content.y, 0.0, g.content.h) + g.scroll;
            g.lassoX1 = g.lassoX0;
            g.lassoY1 = g.lassoY0;
            g.dirty = true;
            break;
        case Item: {
            const int i = g.hoverArg;
            const auto now = Clock::now();
            const bool ctrl = modActive(XKB_MOD_NAME_CTRL), shift = modActive(XKB_MOD_NAME_SHIFT);
            if (ctrl && shift) { const auto base = g.selected; selectRange(i); g.selected.insert(base.begin(), base.end()); }
            else if (ctrl) toggleOne(i);
            else if (shift) selectRange(i);
            else if (i == g.lastClickIndex && now - g.lastClick < std::chrono::milliseconds(400)) { openEntry(i); g.lastClickIndex = -1; return; }
            else selectOnly(i);
            g.lastClickIndex = i;
            g.lastClick = now;
            break;
        }
        default: break;
    }
}
void onWheel(double v) {
    if (g.menuOpen || g.dialog != Dlg::None) return;
    if (g.px > g.content.x) { elasticWheel(g.band, g.scroll, g.scrollMax, v * 3); g.dirty = true; }
}

// ---- Wayland ---------------------------------------------------------------------------------------------------------------------------
void onFrameDone(void*, wl_callback* cb, uint32_t) { wl_callback_destroy(cb); g.frameCallback = nullptr; }
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
    if (!hidpi::begin(frame, width, height, scale, "end4pC-fil3s")) return nullptr;
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
    cairo_t* cr = mkcr(f.surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    drawWindow(cr, g.width, g.height);
    cairo_destroy(cr);
    cairo_surface_flush(f.surface);
    g.frameCallback = wl_surface_frame(g.surface);
    if (g.frameCallback) wl_callback_add_listener(g.frameCallback, &kFrameListener, nullptr);
    slot->busy = true;
    hidpi::present(g.surface, slot->buffer, f, g.scale);
    g.dirty = false;
    setHover();  // the hit areas changed with the drawing
}

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
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) { if (state == WL_POINTER_BUTTON_STATE_PRESSED) onClick(button); else if (button == 0x110) onRelease(); }
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
    if (!g.display) { std::fprintf(stderr, "end4pC-fil3s: no Wayland display\n"); return false; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    if (wl_display_roundtrip(g.display) < 0 || !g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-fil3s: missing Wayland globals\n"); return false; }
    wl_display_roundtrip(g.display);
    g.surface = wl_compositor_create_surface(g.compositor);
    hidpi::attach(g.scale, g.surface, g.viewporter, g.fscaleMgr, [] { g.dirty = true; });
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Fil3s");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-fil3s");
    xdg_toplevel_set_min_size(g.toplevel, kMinW, kMinH);
    wl_surface_commit(g.surface);
    return true;
}

// Once per turn of the loop: what the threads finished, what changed on disk, the colours, the repeat of a key.
void tick() {
    const auto now = Clock::now();
    Listing l;
    if (g.lister.take(l)) applyListing(l);
    bool wasRunning = false;
    {
        std::lock_guard<std::mutex> lock(g.worker.mutex);
        if (g.worker.changed) {
            g.worker.changed = false;
            g.dirty = true;
            if (!g.worker.failure.empty()) { say(g.worker.failure); g.worker.failure.clear(); }
        }
        if (g.status == "running" && !g.worker.running) {
            wasRunning = true;
            for (const std::string& o : g.worker.outputs) g.selectAfter.insert(o);
            g.worker.outputs.clear();
        }
        g.status = g.worker.running ? "running" : "";
    }
    if (wasRunning) reload(true);  // the job is done: the folder is read again (the inotify watch usually got there first)
    if (g.worker.busy && g.worker.fraction < 0) g.dirty = true;  // the bar that moves
    if (g.repeatKey && now >= g.repeatAt) {
        if (!handleKey(g.repeatKey)) g.repeatKey = 0;
        g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
    }
    if (mtimeOf(colorsPath()) != g_colorsStamp) { loadColors(); g.dirty = true; }
    if (g.lasso) {  // near the top or bottom edge the folder scrolls by itself, so the lasso can reach what is out of sight
        const double edge = 36, speed = 14;
        const double above = g.content.y + edge - g.py, below = g.py - (g.content.y + g.content.h - edge);
        if (above > 0) g.scroll = std::max(0.0, g.scroll - speed * std::min(1.0, above / edge));
        else if (below > 0) g.scroll = std::min(g.scrollMax, g.scroll + speed * std::min(1.0, below / edge));
        updateLasso();
    }
    if (g.menuOpen && g.subPending != -2 && now - g.subPendingAt > std::chrono::milliseconds(320)) {  // the pointer stayed on another row: its panel (or none) replaces the open one
        g.menuSub = g.subPending;
        g.subPending = -2;
        g.dirty = true;
    }
    if (g.pressedPlace >= 0 && !g.placeDrag && g.pressedPlace < static_cast<int>(g.places.size()) && now - g.pressAt > std::chrono::milliseconds(450)) {
        g.placeDrag = true;  // held long enough: the place is picked up
        g.dropIndex = g.pressedPlace;
        g.dirty = true;
        updateCursor();
    }
    if (g.animating) g.dirty = true;  // a fade is running
    if (g.band.step()) g.dirty = true;
    if (g.viewDirty) rebuildView();
    if (now - g.drivesAt > std::chrono::seconds(4)) {
        g.drivesAt = now;
        auto d = driveList();
        bool same = d.size() == g.drives.size();
        for (size_t i = 0; same && i < d.size(); i++) same = d[i].path == g.drives[i].path;
        if (!same) { g.drives = std::move(d); g.dirty = true; }
    }
    if (!g.toast.empty()) g.dirty = true;
    if (g.focus != 0) {
        const bool on = std::chrono::duration_cast<std::chrono::milliseconds>(now - g.caretReset).count() / 530 % 2 == 0;
        if (on != g.caretOn) { g.caretOn = on; g.dirty = true; }
    }
    g.lastTick = now;
}

int pollTimeout() {
    if (g.band.busy() || g.lasso || g.animating || g.placeDrag) return 16;
    if (g.pressedPlace >= 0) return 33;
    if (g.menuOpen && g.subPending != -2) return 33;
    if (g.worker.busy || !g.toast.empty()) return 33;
    if (g.focus != 0) return 120;
    return 500;
}

void teardown() {
    if (g.cursorDev) wp_cursor_shape_device_v1_destroy(g.cursorDev);
    g.worker.shutdown();
    g.thumbs.shutdown();
    if (g.inotifyFd >= 0) close(g.inotifyFd);
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
}

volatile sig_atomic_t g_term = 0;
void onTermSignal(int) { g_term = 1; }

int run(int argc, char** argv) {
    std::string lang;
    const std::string cfg = readFile(configPath());
    jsonValue(cfg, "language", lang);
    std::string mat, style, scale;
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = jsonValue(cfg, "materialIcons", mat) && mat == "1";
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
    g.lister.wakeFd = wake[1];
    g.thumbs.start(wake[1]);
    g.worker.start(wake[1]);
    g.inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    g.places = loadPlaces();
    g.drives = driveList();

    std::string first = homeDir();
    if (argc > 1) {
        std::error_code ec;
        const fs::path given = fs::weakly_canonical(argv[1], ec);
        first = !ec && fs::is_directory(given, ec) ? given.string() : !ec && fs::exists(given, ec) ? given.parent_path().string() : first;
    }
    navigate(first, false);

    std::signal(SIGTERM, onTermSignal);
    std::signal(SIGINT, onTermSignal);
    std::signal(SIGHUP, onTermSignal);
    std::signal(SIGPIPE, SIG_IGN);
    Clock::time_point reloadAt{};
    while (g.running && !g_term) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);
        pollfd fds[] = {{wl_display_get_fd(g.display), POLLIN, 0}, {wake[0], POLLIN, 0}, {g.inotifyFd, POLLIN, 0}};
        if (poll(fds, 3, pollTimeout()) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (fds[0].revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
        if (fds[1].revents & POLLIN) {
            char junk[64];
            while (read(wake[0], junk, sizeof junk) > 0) {}
            g.dirty = true;
        }
        if (fds[2].revents & POLLIN) {  // something changed in the folder: read it again a moment later (a copy makes many events)
            char junk[4096];
            while (read(g.inotifyFd, junk, sizeof junk) > 0) {}
            reloadAt = Clock::now() + std::chrono::milliseconds(200);
        }
        if (reloadAt != Clock::time_point{} && Clock::now() >= reloadAt) { reloadAt = {}; reload(true); }
        tick();
        if (g.dirty) render();
    }
    teardown();
    g_object_unref(g_measure);
    cairo_destroy(dcr);
    cairo_surface_destroy(dummy);
    return 0;
}
