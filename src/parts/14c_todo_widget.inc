// To-do card: a task list with round check boxes, a pill text field and a round add button.
// Tasks are stored in ~/.local/state/end4pC/todo.txt, one per line: "d<TAB>text" (done) or "p<TAB>text" (pending).
// Included by main.cpp inside its anonymous namespace; include order matters.

constexpr int kTodoField = 5000, kTodoAdd = 5001;

int todoPending(const Todo& T) {
    int n = 0;
    for (const auto& it : T.items) if (!it.done) n++;
    return n;
}

struct TodoGeo { double pad, headerH, rowH, listTop, listBottom, fieldX, fieldY, fieldW, fieldH, fabX, fab; };
TodoGeo todoGeo(const Todo& T) {
    TodoGeo g{};
    g.pad = 20;
    g.headerH = 66;
    g.rowH = 50;
    g.fab = 52;
    g.fieldH = 52;
    g.fieldY = T.h - 24 - g.fieldH;
    g.fieldX = g.pad;
    g.fabX = T.w - g.pad - g.fab;
    g.fieldW = g.fabX - 10 - g.fieldX;
    g.listTop = g.headerH + 6;
    g.listBottom = g.fieldY - 12;
    return g;
}
double todoMaxScroll(const Todo& T) {
    const TodoGeo g = todoGeo(T);
    return std::max(0.0, T.items.size() * g.rowH - (g.listBottom - g.listTop));
}

constexpr const char* kTodoFieldFont = "Sans 11";
constexpr double kTodoFieldPad = 22;

void todoPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    Todo& T = static_cast<Todo&>(d);
    const TodoGeo g = todoGeo(T);
    T.scrollY = std::clamp(T.scrollY, 0.0, todoMaxScroll(T));
    T.list = {0, g.listTop, static_cast<double>(W), g.listBottom - g.listTop};

    roundedRect(cr, 0, 0, W, H, 32);
    setSource(cr, app.color("surface_container"), 0.9);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](const char* f) {
        PangoFontDescription* fd = pango_font_description_from_string(f);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
    };

    drawIcon(cr, "checklist", g.pad, 22, 26, app.color("primary"), true);
    font("Sans Bold 16");
    pango_layout_set_text(layout, "To-Do", -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, g.pad + 36, 35 - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    {
        const int pending = todoPending(T);
        const std::string label = pending == 0 ? "All clear" : std::to_string(pending) + " pending";
        font("Sans Bold 10");
        pango_layout_set_text(layout, label.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        const double pw = tw + 28, ph = 30, px = W - g.pad - pw, py = 35 - ph / 2;
        roundedRect(cr, px, py, pw, ph, ph / 2);
        setSource(cr, app.color(pending == 0 ? "secondary_container" : "primary_container"));
        cairo_fill(cr);
        setSource(cr, app.color(pending == 0 ? "on_secondary_container" : "on_primary_container"));
        cairo_move_to(cr, px + 14, py + (ph - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    }

    cairo_save(cr);
    cairo_rectangle(cr, 0, g.listTop, W, g.listBottom - g.listTop);
    cairo_clip(cr);
    if (T.items.empty()) {
        drawIcon(cr, "check-circle", (W - 44) / 2.0, g.listTop + (g.listBottom - g.listTop) / 2 - 40, 44, app.color("outline"), false);
        font("Sans 11");
        pango_layout_set_text(layout, "Nothing to do", -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface_variant"));
        cairo_move_to(cr, (W - tw) / 2.0, g.listTop + (g.listBottom - g.listTop) / 2 + 14);
        pango_cairo_show_layout(cr, layout);
    }
    for (size_t i = 0; i < T.items.size(); i++) {
        const TodoItem& it = T.items[i];
        const double y = g.listTop + i * g.rowH - T.scrollY;
        if (y + g.rowH < g.listTop || y > g.listBottom) continue;
        const Rect row{g.pad - 8, y + 2, W - 2.0 * (g.pad - 8), g.rowH - 4};
        const bool hot = T.hover == static_cast<int>(i) || T.hover == 1000 + static_cast<int>(i);
        if (hot) {
            roundedRect(cr, row.x, row.y, row.w, row.h, 18);
            setSource(cr, app.color("surface_container_high"), 0.9);
            cairo_fill(cr);
        }
        const Rect rowHit = rectIntersect(row, T.list);
        if (rowHit.h > 0) T.hits.push_back({static_cast<int>(i), rowHit});
        const double cx = g.pad + 13, cy = y + g.rowH / 2;
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, 11, 0, 2 * M_PI);
        if (it.done) {
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
            drawIcon(cr, "check-read", cx - 8, cy - 8, 16, app.color("on_primary"), true);
        } else {
            setSource(cr, app.color(hot ? "primary" : "outline"));
            cairo_set_line_width(cr, 2);
            cairo_stroke(cr);
        }
        const double rightRoom = hot ? 40 : 8;
        font("Sans 11");
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        const double textX = g.pad + 36, textW = W - g.pad - rightRoom - textX;
        pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
        pango_layout_set_text(layout, it.text.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color(it.done ? "outline" : "on_surface"));
        cairo_move_to(cr, textX, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        if (it.done) {
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, textX, cy + 1);
            cairo_line_to(cr, textX + tw, cy + 1);
            cairo_stroke(cr);
        }
        if (hot) {
            const Rect del{W - g.pad - 30.0, cy - 15, 30, 30};
            const bool onDel = T.hover == 1000 + static_cast<int>(i);
            if (onDel) {
                cairo_new_path(cr);
                cairo_arc(cr, del.x + 15, del.y + 15, 15, 0, 2 * M_PI);
                setSource(cr, app.color("error_container"));
                cairo_fill(cr);
            }
            drawIcon(cr, "close-circle", del.x + 4, del.y + 4, 22, app.color(onDel ? "on_error_container" : "on_surface_variant"), false);
            const Rect delHit = rectIntersect(del, T.list);
            if (delHit.h > 0) T.hits.push_back({1000 + static_cast<int>(i), delHit});
        }
    }
    cairo_restore(cr);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_width(layout, -1);

    const Rect fr{g.fieldX, g.fieldY, g.fieldW, g.fieldH};
    roundedRect(cr, fr.x, fr.y, fr.w, fr.h, fr.h / 2);
    setSource(cr, app.color("surface_container_highest"), 0.95);
    cairo_fill_preserve(cr);
    if (T.fieldActive) {
        setSource(cr, app.color("primary"));
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    } else cairo_new_path(cr);
    T.hits.push_back({kTodoField, fr});

    font("Sans 11");
    pango_layout_set_text(layout, T.text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    PangoRectangle caret{};
    pango_layout_index_to_pos(layout, static_cast<int>(std::min(T.cursor, T.text.size())), &caret);
    const double caretX = caret.x / static_cast<double>(PANGO_SCALE);
    const double inner = fr.w - 2 * kTodoFieldPad;
    const double shift = std::min(0.0, inner - caretX - 2);
    T.fieldShift = shift;
    cairo_save(cr);
    cairo_rectangle(cr, fr.x + kTodoFieldPad, fr.y, inner, fr.h);
    cairo_clip(cr);
    if (T.cursor != T.anchor && !T.text.empty()) {
        const double x0 = textsel::xAtIndex(layout, std::min(T.cursor, T.anchor)), x1 = textsel::xAtIndex(layout, std::max(T.cursor, T.anchor));
        roundedRect(cr, fr.x + kTodoFieldPad + shift + x0 - 1, fr.y + (fr.h - 28) / 2.0, x1 - x0 + 2, 28, 7);
        setSource(cr, app.color("primary"), 0.35);
        cairo_fill(cr);
    }
    if (T.text.empty()) {
        pango_layout_set_text(layout, "Add a task", -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface_variant"), 0.8);
    } else setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, fr.x + 22 + shift, fr.y + (fr.h - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    if (T.fieldActive) {
        setSource(cr, app.color("primary"));
        cairo_rectangle(cr, fr.x + 22 + shift + caretX, fr.y + (fr.h - 22) / 2.0, 2, 22);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    const Rect fab{g.fabX, g.fieldY, g.fab, g.fab};
    bool canAdd = false;
    for (char c : T.text) if (c != ' ') { canAdd = true; break; }
    const bool fhot = T.hover == kTodoAdd && canAdd;
    roundedRect(cr, fab.x, fab.y, fab.w, fab.h, fhot ? 20 : fab.h / 2);
    if (canAdd) setSource(cr, app.color(fhot ? "primary_fixed_dim" : "primary"));
    else setSource(cr, app.color("surface_container_highest"), 0.95);
    cairo_fill(cr);
    setSource(cr, app.color(canAdd ? "on_primary" : "outline"));
    cairo_set_line_width(cr, 2.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, fab.x + fab.w / 2 - 9, fab.y + fab.h / 2);
    cairo_line_to(cr, fab.x + fab.w / 2 + 9, fab.y + fab.h / 2);
    cairo_move_to(cr, fab.x + fab.w / 2, fab.y + fab.h / 2 - 9);
    cairo_line_to(cr, fab.x + fab.w / 2, fab.y + fab.h / 2 + 9);
    cairo_stroke(cr);
    T.hits.push_back({kTodoAdd, fab});

    g_object_unref(layout);
}

void todoAdd(App& app) {
    Todo& T = app.todo;
    std::string t = T.text;
    for (char& c : t) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    const size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
    if (a == std::string::npos) return;
    T.items.push_back({false, t.substr(a, b - a + 1)});
    T.text.clear();
    T.cursor = T.anchor = 0;
    T.scrollY = todoMaxScroll(T);
    deskSave(T);
    deskDraw(app, T);
}

void todoClick(App& app, DeskWidget& d, int id) {
    Todo& T = static_cast<Todo&>(d);
    T.fieldActive = id == kTodoField;
    if (id != kTodoField) T.anchor = T.cursor;
    if (id == kTodoField) {
        const TodoGeo g = todoGeo(T);
        textsel::Field f(T.text, T.cursor, T.anchor);
        const bool shift = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
        const int n = T.clicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), app.pointerX, app.pointerY);
        const double x = app.pointerX - kDeskRoom - (g.fieldX + kTodoFieldPad + T.fieldShift);
        textsel::applyClick(f, textsel::indexAtXInFont(T.text, kTodoFieldFont, x), n, shift);
        T.dragInfo = textsel::beginDrag(f, n);
        T.selDrag = true;
    }
    if (id == kTodoAdd) todoAdd(app);
    else if (id >= 1000 && id < 2000) {
        const size_t i = id - 1000;
        if (i < T.items.size()) { T.items.erase(T.items.begin() + i); T.hover = -1; deskSave(T); }
    } else if (id >= 0 && id < 1000) {
        if (static_cast<size_t>(id) < T.items.size()) { T.items[id].done = !T.items[id].done; deskSave(T); }
    }
    deskDraw(app, T);
}

void todoScrollBy(App& app, DeskWidget& d, int dir) {
    Todo& T = static_cast<Todo&>(d);
    const double before = T.scrollY;
    T.scrollY = std::clamp(T.scrollY + dir * 40.0, 0.0, todoMaxScroll(T));
    if (T.scrollY != before) { deskDraw(app, T); deskMotion(app, T); }
}

int todoCursor(const DeskWidget&, int id) {
    if (id == kTodoField) return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    return id >= 0 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : -1;
}

void todoSaveExtra(const DeskWidget& d, std::ostream& out) {
    for (const auto& it : static_cast<const Todo&>(d).items) out << (it.done ? "d" : "p") << "\t" << it.text << "\n";
}
void todoLoadLine(DeskWidget& d, const std::string& line) {
    if (line.size() > 2 && line[1] == '\t' && (line[0] == 'd' || line[0] == 'p')) static_cast<Todo&>(d).items.push_back({line[0] == 'd', line.substr(2)});
}

bool todoKey(App& app, uint32_t key) {
    Todo& T = app.todo;
    if (!app.xkbState) return false;
    const xkb_keycode_t code = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code);
    const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    const bool shift = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    switch (sym) {
        case XKB_KEY_Escape: T.fieldActive = false; T.anchor = T.cursor; deskDraw(app, T); return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: todoAdd(app); return true;
        default: break;
    }
    textsel::Key k = textsel::Key::None;
    const xkb_keysym_t low = xkb_keysym_to_lower(sym);
    if (sym == XKB_KEY_Left) k = textsel::Key::Left;
    else if (sym == XKB_KEY_Right) k = textsel::Key::Right;
    else if (sym == XKB_KEY_Home) k = textsel::Key::Home;
    else if (sym == XKB_KEY_End) k = textsel::Key::End;
    else if (sym == XKB_KEY_BackSpace) k = textsel::Key::Backspace;
    else if (sym == XKB_KEY_Delete) k = textsel::Key::Delete;
    else if (ctrl && low == XKB_KEY_a) k = textsel::Key::SelectAll;
    else if (ctrl && low == XKB_KEY_c) k = textsel::Key::Copy;
    else if (ctrl && low == XKB_KEY_x) k = textsel::Key::Cut;
    else if (ctrl && low == XKB_KEY_v) k = textsel::Key::Paste;
    if (k != textsel::Key::None) {
        textsel::Field f(T.text, T.cursor, T.anchor);
        textsel::Result r = textsel::handleKey(f, k, ctrl, shift);
        if (r.wantCopy) clipboard::copy(r.copyText);
        if (r.wantPaste) {
            std::string clip;
            if (clipboard::paste(clip)) {
                if (clip.size() > 1024) {  // cut at 1024 bytes, but not in the middle of a UTF-8 character
                    clip.resize(1024);
                    while (!clip.empty() && textsel::isContinuation(clip.back())) clip.pop_back();
                    if (!clip.empty() && (static_cast<unsigned char>(clip.back()) & 0xC0) == 0xC0) clip.pop_back();
                }
                f.insert(clip);
            }
        }
        deskDraw(app, T);
        return true;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(app.xkbState, code, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return false;
    if (T.text.size() > 300 && T.cursor == T.anchor) return true;
    textsel::Field typed(T.text, T.cursor, T.anchor);
    typed.insert(std::string(buf, static_cast<size_t>(n)));
    deskDraw(app, T);
    return true;
}

bool todoMotion(App& app, DeskWidget& d) {
    Todo& T = static_cast<Todo&>(d);
    if (!T.selDrag) return false;
    const TodoGeo g = todoGeo(T);
    const double left = g.fieldX + kTodoFieldPad, right = left + g.fieldW - 2 * kTodoFieldPad;
    const double px = app.pointerX - kDeskRoom;
    textsel::Field f(T.text, T.cursor, T.anchor);
    size_t at = textsel::indexAtXInFont(T.text, kTodoFieldFont, std::clamp(px, left, right) - (left + T.fieldShift));
    if (px < left - 4) at = std::min(at, textsel::prevChar(T.text, f.caret));
    else if (px > right + 4) at = std::max(at, textsel::nextChar(T.text, f.caret));
    textsel::drag(f, at, T.dragInfo);
    deskDraw(app, T);
    return true;
}
void todoRelease(App&, DeskWidget& d) { static_cast<Todo&>(d).selDrag = false; }

bool todoTyping(const DeskWidget& d) { return static_cast<const Todo&>(d).fieldActive; }
bool todoKeyHook(App& app, DeskWidget&, uint32_t key) { return todoKey(app, key); }
void todoBlur(App& app, DeskWidget& d) {
    Todo& T = static_cast<Todo&>(d);
    T.fieldActive = false;
    T.anchor = T.cursor;
    T.selDrag = false;
    deskDraw(app, T);
}

void todoInit(Todo& T) {
    T.typing = todoTyping;
    T.key = todoKeyHook;
    T.blur = todoBlur;
    T.motion = todoMotion;
    T.release = todoRelease;
    T.name = "todo";
    T.wantsKeys = true;
    T.moveZone = 66;
    T.radius = 32;
    T.blurCard = true;
    T.paint = todoPaint;
    T.click = todoClick;
    T.DeskWidget::scroll = todoScrollBy;
    T.DeskWidget::cursor = todoCursor;
    T.saveExtra = todoSaveExtra;
    T.loadLine = todoLoadLine;
}
