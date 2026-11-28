// What Fil3s draws. Included once by fil3s.cpp, inside its anonymous namespace.
#pragma once

bool hot(const Rect& r, int id, int arg = 0) {
    g.hits.push_back({r, id, arg});
    return g.hoverId == id && g.hoverArg == arg;
}
// A hit area that only counts where it is visible (a row scrolled under the header must not catch the clicks aimed at the header).
bool hotIn(const Rect& r, const Rect& visible, int id, int arg = 0) {
    const double x0 = std::max(r.x, visible.x), y0 = std::max(r.y, visible.y);
    const double x1 = std::min(r.x + r.w, visible.x + visible.w), y1 = std::min(r.y + r.h, visible.y + visible.h);
    if (x1 <= x0 || y1 <= y0) return false;
    return hot({x0, y0, x1 - x0, y1 - y0}, id, arg);
}

const Entry& entryAt(int viewIndex) { return g.entries[static_cast<size_t>(g.view[static_cast<size_t>(viewIndex)])]; }
constexpr double kBarW = 14;  // the room the scroll bar takes at the right of the files
bool isSelected(const Entry& e) { return g.selected.count(e.path) > 0; }
bool inTrash() { return g.cwd == trashFiles(); }

void discButton(cairo_t* cr, const Rect& r, int id, const char* ic, bool active = false, bool danger = false, int arg = 0, bool enabled = true) {
    const bool hov = enabled && hot(r, id, arg);
    Color bg = active ? color("secondary_container") : color("surface_container_high");
    const Color fg = danger && hov ? color("on_error_container") : active ? color("on_secondary_container") : color("on_surface");
    if (danger && hov) bg = color("error_container");
    src(cr, hov ? mix(fg, bg, 0.12) : bg, enabled ? 1.0 : 0.5);
    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
    cairo_fill(cr);
    icon(cr, ic, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, fg, enabled ? 1.0 : 0.38);
}

void drawField(cairo_t* cr, const Rect& r, int id, const Field& f, int which, const std::string& placeholder, bool clearButton, bool searchIcon = true) {
    const bool focused = g.focus == which, hov = hot(r, id);
    const Color bg = color(focused ? "surface_container_highest" : "surface_container_high");
    src(cr, hov && !focused ? mix(color("on_surface"), bg, 0.06) : bg);
    rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
    cairo_fill(cr);
    if (focused) {
        src(cr, color("primary"));
        cairo_set_line_width(cr, 2);
        rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2 - 1);
        cairo_stroke(cr);
    }
    if (searchIcon) icon(cr, "magnifer", r.x + 14, r.y + (r.h - 22) / 2, 22, color("on_surface_variant"));
    const double lead = searchIcon ? 44 : 18, tx = r.x + lead, tw = r.w - lead - (clearButton && !f.s.empty() ? 44 : 16);
    if (f.s.empty()) text(cr, placeholder, tx, r.y + (r.h - 18) / 2, 15, false, color("on_surface_variant"), 0.7, tw);
    else text(cr, f.s, tx, r.y + (r.h - 18) / 2, 15, false, color("on_surface"), 1, tw);
    if (focused && g.caretOn) {
        const double cw = textWidth(f.s.substr(0, std::min(f.cur, f.s.size())), 15, false);
        src(cr, color("on_surface"));
        cairo_rectangle(cr, tx + std::min(cw, tw), r.y + (r.h - 22) / 2, 2, 22);
        cairo_fill(cr);
    }
    if (clearButton && !f.s.empty()) {
        const Rect x{r.x + r.w - 36, r.y + (r.h - 28) / 2, 28, 28};
        const bool xh = hot(x, SearchClear);
        if (xh) { src(cr, color("on_surface"), 0.1); cairo_arc(cr, x.x + 14, x.y + 14, 14, 0, 2 * M_PI); cairo_fill(cr); }
        icon(cr, "close-circle", x.x + 4, x.y + 4, 20, color("on_surface_variant"));
    }
}

// ---- the header ------------------------------------------------------------------------------------------------------------------------
std::vector<std::string> g_crumbPaths;
void drawCrumbs(cairo_t* cr, const Rect& r) {
    src(cr, color("surface_container_high"));
    rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
    cairo_fill(cr);
    struct Seg { std::string label, path; };
    std::vector<Seg> segs;
    const std::string home = homeDir();
    if (inTrash()) segs.push_back({i18n::tr("Trash"), g.cwd});
    else {
        std::string rest = g.cwd, acc;
        if (g.cwd == home || g.cwd.rfind(home + "/", 0) == 0) { segs.push_back({i18n::tr("Home"), home}); rest = g.cwd.substr(home.size()); acc = home; }
        else segs.push_back({"/", "/"});
        std::stringstream ss(rest);
        std::string part;
        while (std::getline(ss, part, '/')) if (!part.empty()) { acc += "/" + part; segs.push_back({part, acc}); }
    }
    // From the right: as many as fit, the ones before them hidden behind "…".
    const double sep = 22, padX = 14;
    std::vector<double> widths;
    for (const Seg& s : segs) widths.push_back(textWidth(s.label, 15, true) + 20);
    double room = r.w - 2 * padX, used = 0;
    size_t first = segs.size();
    while (first > 0 && used + widths[first - 1] + (first < segs.size() ? sep : 0) <= room - (first > 1 ? 36 : 0)) { first--; used += widths[first] + (first + 1 < segs.size() ? sep : 0); }
    if (first == segs.size()) first = segs.size() - 1;  // at least the last one (cut with an ellipsis)
    double x = r.x + padX;
    g_crumbPaths.clear();
    if (first > 0) { text(cr, "…", x + 6, r.y + (r.h - 20) / 2, 16, true, color("on_surface_variant")); x += 36; }
    for (size_t i = first; i < segs.size(); i++) {
        const bool last = i + 1 == segs.size();
        const double w = std::min(widths[i], r.x + r.w - padX - x);
        const Rect pillRect{x, r.y + 6, w, r.h - 12};
        g_crumbPaths.push_back(segs[i].path);
        const bool hov = hot(pillRect, Crumb, static_cast<int>(g_crumbPaths.size()) - 1);
        if (hov) { src(cr, color("on_surface"), 0.1); rrect(cr, pillRect.x, pillRect.y, pillRect.w, pillRect.h, pillRect.h / 2); cairo_fill(cr); }
        text(cr, segs[i].label, x + 10, r.y + (r.h - textHeight(segs[i].label, 15, true)) / 2, 15, true, color(last ? "on_surface" : "on_surface_variant"), 1, w - 20);
        x += w;
        if (!last) { icon(cr, "alt-arrow-right", x + 1, r.y + (r.h - 18) / 2, 18, color("on_surface_variant"), 0.6); x += sep; }
    }
}

void drawHeader(cairo_t* cr, int W) {
    const double y = kPad, h = 44;
    double x = kPad;
    discButton(cr, {x, y, h, h}, BtnBack, "alt-arrow-left", false, false, 0, !g.back.empty());
    x += h + 6;
    discButton(cr, {x, y, h, h}, BtnForward, "alt-arrow-right", false, false, 0, !g.forward.empty());
    x += h + 6;
    discButton(cr, {x, y, h, h}, BtnUp, "alt-arrow-up", false, false, 0, g.cwd != "/" && !inTrash());
    x += h + 12;
    const double right = W - kPad;
    const double closeX = right - h, hiddenX = closeX - 6 - h, sortX = hiddenX - 6 - h, viewX = sortX - 6 - h;
    const double searchW = std::clamp(W * 0.2, 150.0, 260.0), searchX = viewX - 12 - searchW;
    drawCrumbs(cr, {x, y, std::max(120.0, searchX - 12 - x), h});
    drawField(cr, {searchX, y, searchW, h}, SearchField, g.search, 1, i18n::tr("Search"), true);
    discButton(cr, {viewX, y, h, h}, BtnView, g.grid ? "list" : "widget");
    discButton(cr, {sortX, y, h, h}, BtnSort, g.descending ? "sort-from-top-to-bottom" : "sort-from-bottom-to-top");
    discButton(cr, {hiddenX, y, h, h}, BtnHidden, g.showHidden ? "eye" : "eye-closed", g.showHidden);
    discButton(cr, {closeX, y, h, h}, BtnClose, "close-circle", false, true);
}

// ---- the sidebar -----------------------------------------------------------------------------------------------------------------------
void placeRow(cairo_t* cr, const Rect& r, const Place& p, int index, bool current) {
    const bool hov = hot(r, PlaceRow, index);
    const Color bg = current ? color("secondary_container") : color("surface_container"), fg = current ? color("on_secondary_container") : color("on_surface");
    if (current || hov) { src(cr, current ? bg : mix(color("on_surface"), bg, 0.08)); rrect(cr, r.x, r.y, r.w, r.h, r.h / 2); cairo_fill(cr); }
    icon(cr, p.icon.c_str(), r.x + 16, r.y + (r.h - 22) / 2, 22, fg);
    text(cr, p.label, r.x + 50, r.y + (r.h - textHeight(p.label, 15, current)) / 2, 15, current, fg, 1, r.w - 62);
}

std::vector<Place> g_sideRows;  // what the rows of the sidebar stand for (the index of a PlaceRow hit)
void drawSidebar(cairo_t* cr, const Rect& card) {
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, kRadius);
    cairo_fill(cr);
    g_sideRows.clear();
    hotIn(card, card, SideEmpty);  // (the blank part of the sidebar: it has a menu of its own; the rows are added after it, so they win)
    double y = card.y + 14;
    const double x = card.x + 10, w = card.w - 20, rowH = 44, bottomEdge = card.y + card.h - 10;
    auto sectionTitle = [&](const std::string& title) { text(cr, title, x + 14, y + 4, 13, true, color("on_surface_variant"), 0.85); y += 28; };
    auto fixedRow = [&](const Place& p) {
        if (y + rowH > bottomEdge) return;
        g_sideRows.push_back(p);
        placeRow(cr, {x, y, w, rowH}, p, static_cast<int>(g_sideRows.size()) - 1, g.cwd == p.path);
        y += rowH + 2;
    };

    // Places: the ones the user keeps (add with the menu of a folder, remove with the menu of a row, reorder with a long press), then the Trash.
    text(cr, i18n::tr("Places"), x + 14, y + 4, 13, true, color("on_surface_variant"), 0.85);
    y += 28;
    const int n = static_cast<int>(g.places.size());
    for (const Place& p : g.places) { Place q = p; g_sideRows.push_back(q); }  // (in order: the index of a row is the position of the place)
    g.placeRowsY0 = y;
    g.placeStep = rowH + 2;
    const int dragged = g.placeDrag && g.pressedPlace >= 0 && g.pressedPlace < n ? g.pressedPlace : -1;
    std::vector<int> order;
    for (int i = 0; i < n; i++) if (i != dragged) order.push_back(i);
    if (dragged >= 0) order.insert(order.begin() + std::clamp(g.dropIndex, 0, static_cast<int>(order.size())), dragged);  // the gap where it would land
    for (int slot = 0; slot < n; slot++) {
        const double ry = y + slot * (rowH + 2);
        if (ry + rowH > bottomEdge) break;
        if (order[static_cast<size_t>(slot)] == dragged) continue;
        placeRow(cr, {x, ry, w, rowH}, g.places[static_cast<size_t>(order[static_cast<size_t>(slot)])], order[static_cast<size_t>(slot)], g.cwd == g.places[static_cast<size_t>(order[static_cast<size_t>(slot)])].path);
    }
    y += n * (rowH + 2);
    if (n == 0) { text(cr, i18n::tr("Right-click a folder to add it here"), x + 14, y, 13, false, color("on_surface_variant"), 0.7, w - 28, 0, true); y += 44; }
    fixedRow({i18n::tr("Trash"), trashFiles(), "trash-bin-minimalistic"});
    y += 8;
    if (!g.drives.empty()) { sectionTitle(i18n::tr("Drives")); for (const Place& p : g.drives) fixedRow(p); y += 8; }
    sectionTitle(i18n::tr("System"));
    fixedRow({i18n::tr("Computer"), "/", "server-minimalistic"});

    // The mark of the program at the bottom, so the sidebar is never an empty column (it gives way when the rows need the room).
    {
        const double size = 48, ly = card.y + card.h - 14 - size;
        if (y + 12 < ly) {
            shapePath(cr, "Cookie12Sided", x + 8, ly, size);
            src(cr, color("primary_container"));
            cairo_fill(cr);
            icon(cr, "folder-open", x + 8 + 12, ly + 12, 24, color("on_primary_container"));
            text(cr, "Fil3s", x + 8 + size + 14, ly + (size - textHeight("Fil3s", 24, true)) / 2, 24, true, color("on_surface"));
        }
    }

    if (dragged >= 0) {  // the one in the hand: it follows the pointer
        const Place& p = g.places[static_cast<size_t>(dragged)];
        const double top = g.placeRowsY0, last = g.placeRowsY0 + std::max(0, n - 1) * (rowH + 2);
        const Rect fr{x, std::clamp(g.py - rowH / 2, top, last), w, rowH};
        for (int i = 4; i >= 1; i--) { src(cr, color("on_surface"), 0.035); rrect(cr, fr.x - i, fr.y - i + 3, fr.w + 2 * i, fr.h + 2 * i, rowH / 2 + i); cairo_fill(cr); }
        src(cr, color("surface_container_highest"));
        rrect(cr, fr.x, fr.y, fr.w, fr.h, fr.h / 2);
        cairo_fill(cr);
        src(cr, color("primary"), 0.8);
        cairo_set_line_width(cr, 1.5);
        rrect(cr, fr.x + 0.75, fr.y + 0.75, fr.w - 1.5, fr.h - 1.5, fr.h / 2 - 0.75);
        cairo_stroke(cr);
        icon(cr, p.icon.c_str(), fr.x + 16, fr.y + (fr.h - 22) / 2, 22, color("on_surface"));
        text(cr, p.label, fr.x + 50, fr.y + (fr.h - textHeight(p.label, 15, false)) / 2, 15, false, color("on_surface"), 1, fr.w - 62);
    }
}

// ---- the files -------------------------------------------------------------------------------------------------------------------------
// The picture of a file in a square: its thumbnail (images), or a coloured tile with the icon of its kind.
void drawTile(cairo_t* cr, const Entry& e, double x, double y, double size, double radius, bool selectedNow) {
    const auto [bgName, fgName] = tileColors(e.kind);
    const bool isPicture = e.kind == Kind::Image && e.regular && e.size < 80u * 1024 * 1024;
    cairo_surface_t* th = isPicture ? g.thumbs.get(e.path) : nullptr;
    double alpha = 1;
    if (th) {  // the picture is there: if the tile was drawn without it a moment ago, it fades in
        auto fade = g.fadingThumbs.find(e.path);
        if (fade == g.fadingThumbs.end() && g.waitingThumbs.erase(e.path)) fade = g.fadingThumbs.emplace(e.path, Clock::now()).first;
        if (fade != g.fadingThumbs.end()) {
            alpha = std::chrono::duration<double>(Clock::now() - fade->second).count() / 0.22;
            if (alpha >= 1) { alpha = 1; g.fadingThumbs.erase(fade); }
            else g.animating = true;
        }
    } else if (isPicture) {
        if (g.waitingThumbs.size() > 2000) g.waitingThumbs.clear();
        g.waitingThumbs.insert(e.path);
    }
    // The tile of the kind: for a picture still being made it is a quiet neutral square (not the loud colour of the kinds), so the picture arriving is gentle.
    if (!th || alpha < 1) {
        src(cr, color(isPicture ? "surface_container_highest" : selectedNow && e.kind != Kind::Folder ? "surface_container_highest" : bgName));
        rrect(cr, x, y, size, size, radius);
        cairo_fill(cr);
        const int is = static_cast<int>(size * 0.5);
        icon(cr, iconFor(e.kind), x + (size - is) / 2.0, y + (size - is) / 2.0, is, color(isPicture ? "on_surface_variant" : fgName), isPicture ? 0.35 : 1.0);
    }
    if (!th) return;
    cairo_save(cr);
    rrect(cr, x, y, size, size, radius);
    cairo_clip(cr);
    src(cr, color("surface_container_highest"));
    cairo_paint_with_alpha(cr, alpha);
    const double w = cairo_image_surface_get_width(th), h = cairo_image_surface_get_height(th), sc = std::max(size / w, size / h);
    cairo_translate(cr, x + size / 2, y + size / 2);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, th, -w / 2, -h / 2);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

// The pictures a little way above and below what is on screen are asked for first, so they are ready by the time the wheel brings them in
// (the thumbnailer does the last asked first: the far ones go in before the near ones, and the ones on screen are asked for as they are drawn).
void prefetchThumbs(double itemH, int perRow, double off, double viewH) {
    const int total = static_cast<int>(g.view.size());
    const int firstVisible = std::max(0, static_cast<int>(off / itemH)) * perRow, lastVisible = std::min(total, (static_cast<int>((off + viewH) / itemH) + 1) * perRow);
    const int reach = std::max(1, static_cast<int>(viewH / itemH * 1.5)) * perRow;
    auto ask = [&](int i) { const Entry& e = entryAt(i); if (e.kind == Kind::Image && e.regular && e.size < 80u * 1024 * 1024) g.thumbs.get(e.path); };
    for (int d = reach; d > 0; d -= 1) {  // farthest first
        if (lastVisible - 1 + d < total) ask(lastVisible - 1 + d);
        if (firstVisible - d >= 0) ask(firstVisible - d);
    }
}

void drawList(cairo_t* cr, const Rect& area) {
    constexpr double rowH = 48, gap = 2, headH = 34;
    const bool wide = area.w > 620;
    const double sizeW = wide ? 90 : 0, dateW = wide ? 130 : 0, rightPad = 20 + (g.scrollMax > 0 ? kBarW : 0);
    // The header: click a column to sort by it.
    const double hy = area.y;
    const double sizeX = area.x + area.w - rightPad - dateW - sizeW, dateX = area.x + area.w - rightPad - dateW;
    struct Col { const char* label; Sort by; double x, w; int align; };
    const Col cols[] = {{"Name", Sort::Name, area.x + 72, sizeX - area.x - 84, 0}, {"Size", Sort::Size, sizeX, sizeW, 2}, {"Modified", Sort::Date, dateX, dateW, 2}};
    for (int i = 0; i < 3; i++) {
        if (i > 0 && !wide) continue;
        const Col& c = cols[i];
        const bool hov = hot({c.x - 6, hy, c.w + 12, headH}, BtnSort, 10 + i);
        const bool on = g.sort == c.by;
        const std::string label = i18n::tr(c.label);
        text(cr, label, c.x, hy + (headH - 18) / 2, 13, true, color(on ? "primary" : "on_surface_variant"), hov ? 1.0 : 0.9, c.w - (on ? 18 : 0), c.align);
        if (on) {
            const double tw = std::min(c.w - 18, textWidth(label, 13, true));
            const double ix = c.align == 2 ? c.x + c.w - tw - 18 : c.x + tw + 2;
            icon(cr, g.descending ? "alt-arrow-down" : "alt-arrow-up", ix, hy + (headH - 16) / 2, 16, color("primary"));
        }
    }
    const Rect rows{area.x, area.y + headH, area.w, area.h - headH};
    const double content = g.view.size() * (rowH + gap);
    g.scrollMax = std::max(0.0, content - rows.h);
    g.scroll = std::clamp(g.scroll, 0.0, g.scrollMax);
    g.columns = 1;
    g.cellH = rowH + gap;
    cairo_save(cr);
    cairo_rectangle(cr, rows.x, rows.y, rows.w, rows.h);
    cairo_clip(cr);
    const double off = g.scroll + g.band.over, w = rows.w - (g.scrollMax > 0 ? kBarW : 0);
    prefetchThumbs(rowH + gap, 1, off, rows.h);
    const int firstRow = std::max(0, static_cast<int>(off / (rowH + gap)) - 1), lastRow = std::min(static_cast<int>(g.view.size()), static_cast<int>((off + rows.h) / (rowH + gap)) + 2);
    for (int i = firstRow; i < lastRow; i++) {
        const Entry& e = entryAt(i);
        const Rect row{rows.x, rows.y + i * (rowH + gap) - off, w, rowH};
        const bool sel = isSelected(e), hov = hotIn(row, rows, Item, i), cur = g.cursor == i && g.focus == 0;
        const Color base = color("surface_container"), fg = color(sel ? "on_secondary_container" : "on_surface");
        if (sel || hov) { src(cr, sel ? mix(color("on_surface"), color("secondary_container"), hov ? 0.06 : 0.0) : mix(color("on_surface"), base, 0.07)); rrect(cr, row.x, row.y, row.w, row.h, 16); cairo_fill(cr); }
        if (cur && !sel) { src(cr, color("outline_variant")); cairo_set_line_width(cr, 1.5); rrect(cr, row.x + 0.75, row.y + 0.75, row.w - 1.5, row.h - 1.5, 15); cairo_stroke(cr); }
        drawTile(cr, e, row.x + 14, row.y + 6, 36, 12, sel);
        text(cr, e.name, row.x + 62, row.y + (rowH - textHeight(e.name, 15, false)) / 2, 15, false, fg, e.hidden ? 0.6 : 1, (wide ? sizeX - 12 : row.x + row.w - 20) - (row.x + 62), 0);
        if (wide) {
            if (!e.dir) text(cr, formatSize(e.size), sizeX, row.y + (rowH - 18) / 2, 13, false, fg, 0.75, sizeW, 2);
            text(cr, formatDate(e.mtime), dateX, row.y + (rowH - 18) / 2, 13, false, fg, 0.75, dateW, 2);
        }
    }
    cairo_restore(cr);
}

void drawGrid(cairo_t* cr, const Rect& area) {
    constexpr double tile = 96, cw = 128, ch = 148, gap = 6;
    const double usable = area.w - (g.scrollMax > 0 ? kBarW : 0);
    g.columns = std::max(1, static_cast<int>(usable / (cw + gap)));
    const double spread = usable / g.columns;  // the cells share the width, so the right edge is tidy
    g.cellW = spread;
    g.cellH = ch + gap;
    const int rowsCount = (static_cast<int>(g.view.size()) + g.columns - 1) / g.columns;
    const double content = rowsCount * (ch + gap);
    g.scrollMax = std::max(0.0, content - area.h);
    g.scroll = std::clamp(g.scroll, 0.0, g.scrollMax);
    const double off = g.scroll + g.band.over;
    prefetchThumbs(ch + gap, g.columns, off, area.h);
    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);
    const int firstRow = std::max(0, static_cast<int>(off / (ch + gap)) - 1), lastRow = std::min(rowsCount, static_cast<int>((off + area.h) / (ch + gap)) + 2);
    for (int r = firstRow; r < lastRow; r++)
        for (int c = 0; c < g.columns; c++) {
            const int i = r * g.columns + c;
            if (i >= static_cast<int>(g.view.size())) break;
            const Entry& e = entryAt(i);
            const Rect cell{area.x + c * spread + 2, area.y + r * (ch + gap) - off, spread - 4, ch};
            const bool sel = isSelected(e), hov = hotIn(cell, area, Item, i), cur = g.cursor == i && g.focus == 0;
            const Color base = color("surface_container");
            if (sel || hov) { src(cr, sel ? color("secondary_container") : mix(color("on_surface"), base, 0.07)); rrect(cr, cell.x, cell.y, cell.w, cell.h, 24); cairo_fill(cr); }
            if (cur && !sel) { src(cr, color("outline_variant")); cairo_set_line_width(cr, 1.5); rrect(cr, cell.x + 0.75, cell.y + 0.75, cell.w - 1.5, cell.h - 1.5, 23); cairo_stroke(cr); }
            drawTile(cr, e, cell.x + (cell.w - tile) / 2, cell.y + 10, tile, 26, sel);
            const Color fg = color(sel ? "on_secondary_container" : "on_surface");
            text(cr, e.name, cell.x + 6, cell.y + 10 + tile + 8, 13, false, fg, e.hidden ? 0.6 : 1, cell.w - 12, 1, true, 2);
        }
    cairo_restore(cr);
}

void drawStatus(cairo_t* cr, const Rect& bar) {
    std::string left;
    if (g.loading) left = i18n::tr("Loading…");
    else if (!g.selected.empty()) {
        uint64_t total = 0;
        for (int i : g.view) if (g.selected.count(g.entries[static_cast<size_t>(i)].path)) total += g.entries[static_cast<size_t>(i)].size;
        left = std::string(i18n::tr("Selected")) + ": " + std::to_string(g.selected.size()) + (total > 0 ? " · " + formatSize(total) : "");
    } else left = std::string(i18n::tr("Items")) + ": " + std::to_string(g.view.size());
    bool busy;
    std::string label;
    double fraction;
    int done, total;
    {
        std::lock_guard<std::mutex> lock(g.worker.mutex);
        busy = g.worker.busy || g.worker.running;
        label = g.worker.label;
        fraction = g.worker.fraction;
        done = g.worker.done;
        total = g.worker.total;
    }
    text(cr, left, bar.x + 12, bar.y + (bar.h - 18) / 2, 13, false, color("on_surface_variant"), 0.9, busy ? bar.w * 0.28 : bar.w - 24);
    if (!busy) return;
    // The progress: a real one when ffmpeg says how far it is, a bar that moves otherwise.
    const double bx = bar.x + bar.w * 0.34, bw = bar.w * 0.66 - 56, by = bar.y + bar.h / 2;
    text(cr, label + (total > 1 ? "  " + std::to_string(std::min(done + 1, total)) + "/" + std::to_string(total) : ""), bx, bar.y + 2, 12, false, color("on_surface_variant"), 0.95, bw);
    src(cr, color("secondary_container"));
    rrect(cr, bx, by + 6, bw, 6, 3);
    cairo_fill(cr);
    src(cr, color("primary"));
    if (fraction >= 0) rrect(cr, bx, by + 6, std::max(6.0, bw * fraction), 6, 3);
    else {
        const double t = std::chrono::duration<double>(Clock::now() - g.start).count(), seg = bw * 0.3, pos = (std::sin(t * 3) * 0.5 + 0.5) * (bw - seg);
        rrect(cr, bx + pos, by + 6, seg, 6, 3);
    }
    cairo_fill(cr);
    discButton(cr, {bar.x + bar.w - 44, bar.y + (bar.h - 36) / 2, 36, 36}, StatusCancel, "close-circle", false, true);
}

// The rectangle of an entry in the content, before the scroll (the lasso asks for it): a cell of the grid or a row of the list.
Rect itemRect(int i) {
    if (g.grid) {
        const int c = i % g.columns, r = i / g.columns;
        return {c * g.cellW + 2, r * g.cellH, g.cellW - 4, g.cellH - 6};
    }
    return {0, 34 + i * g.cellH, g.content.w, g.cellH - 2};
}
void drawLasso(cairo_t* cr) {
    if (!g.lasso) return;
    const double x0 = std::min(g.lassoX0, g.lassoX1), x1 = std::max(g.lassoX0, g.lassoX1);
    const double y0 = std::min(g.lassoY0, g.lassoY1) - g.scroll, y1 = std::max(g.lassoY0, g.lassoY1) - g.scroll;
    cairo_save(cr);
    cairo_rectangle(cr, g.content.x, g.content.y, g.content.w, g.content.h);
    cairo_clip(cr);
    const Rect r{g.content.x + x0, g.content.y + y0, x1 - x0, y1 - y0};
    src(cr, color("primary"), 0.14);
    rrect(cr, r.x, r.y, r.w, r.h, 8);
    cairo_fill(cr);
    src(cr, color("primary"), 0.75);
    cairo_set_line_width(cr, 1.5);
    rrect(cr, r.x + 0.75, r.y + 0.75, std::max(0.0, r.w - 1.5), std::max(0.0, r.h - 1.5), 8);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void drawContent(cairo_t* cr, const Rect& card) {
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, kRadius);
    cairo_fill(cr);
    const Rect statusBar{card.x + 12, card.y + card.h - 44, card.w - 24, 40};
    const Rect area{card.x + 12, card.y + 12, card.w - 24, card.h - 12 - 44};
    g.content = area;
    hotIn(area, area, Empty);  // clicking the empty part clears the selection (the rows are added after this one, so they win)
    if (g.view.empty()) {
        const std::string msg = g.loading ? i18n::tr("Loading…") : !g.loadError.empty() ? g.loadError : !g.search.s.empty() ? i18n::tr("No file matches your search") : i18n::tr("This folder is empty");
        const double tw = textWidth(msg, 16, false);
        text(cr, msg, area.x + std::max(0.0, (area.w - tw) / 2), area.y + area.h / 2 - 10, 16, false, color("on_surface_variant"), 0.8, area.w, tw < area.w ? 0 : 1);
    } else if (g.grid) drawGrid(cr, area);
    else drawList(cr, area);
    g.scrollTrack = {};
    if (g.scrollMax > 0) {  // the scroll bar: always there (it is also a handle: drag it, or click the track to jump)
        const double top = g.grid ? area.y : area.y + 34, h = area.h - (g.grid ? 0 : 34);
        g.thumbH = std::max(40.0, h * h / (h + g.scrollMax));
        g.thumbTop = top + (h - g.thumbH) * (g.scroll / g.scrollMax);
        g.scrollTrack = {area.x + area.w - kBarW + 2, top, kBarW - 2, h};
        hotIn(g.scrollTrack, g.scrollTrack, ScrollTrack);
        const Rect thumbRect{g.scrollTrack.x, g.thumbTop, g.scrollTrack.w, g.thumbH};
        const bool hov = hotIn(thumbRect, g.scrollTrack, ScrollThumb) || g.dragScroll;
        const double bw = hov ? 8 : 5;
        src(cr, color("on_surface_variant"), hov ? 0.22 : 0.10);  // the track, faint
        rrect(cr, g.scrollTrack.x + (g.scrollTrack.w - 5) / 2, g.scrollTrack.y, 5, g.scrollTrack.h, 2.5);
        cairo_fill(cr);
        src(cr, color(g.dragScroll ? "primary" : "on_surface_variant"), g.dragScroll ? 0.9 : hov ? 0.75 : 0.5);
        rrect(cr, g.scrollTrack.x + (g.scrollTrack.w - bw) / 2, g.thumbTop, bw, g.thumbH, bw / 2);
        cairo_fill(cr);
    }
    drawLasso(cr);
    drawStatus(cr, statusBar);
}

// ---- the menu, the dialog, the toast ---------------------------------------------------------------------------------------------------
constexpr double kMenuRowH = 44, kMenuW = 236;
double panelHeight(const std::vector<MenuItem>& items) {
    double h = 16;
    for (const MenuItem& m : items) h += m.separator ? 13 : kMenuRowH;
    return h;
}
// Draws one panel; the rows are hit areas (arg: index, 1000 + index inside the second panel).
void drawPanel(cairo_t* cr, const Rect& r, const std::vector<MenuItem>& items, int argBase) {
    hot(r, MenuRow, -1);  // (the panel itself: a click on its background must not reach what is under it)
    // soft shadow
    for (int i = 4; i >= 1; i--) { src(cr, color("on_surface"), 0.03); rrect(cr, r.x - i, r.y - i + 3, r.w + 2 * i, r.h + 2 * i, 18 + i); cairo_fill(cr); }
    src(cr, color("surface_container_high"));
    rrect(cr, r.x, r.y, r.w, r.h, 18);
    cairo_fill(cr);
    double y = r.y + 8;
    for (size_t i = 0; i < items.size(); i++) {
        const MenuItem& m = items[i];
        if (m.separator) { src(cr, color("outline_variant"), 0.7); cairo_rectangle(cr, r.x + 16, y + 6, r.w - 32, 1); cairo_fill(cr); y += 13; continue; }
        const Rect row{r.x + 6, y, r.w - 12, kMenuRowH};
        const int arg = argBase + static_cast<int>(i);
        const bool hov = m.enabled && hot(row, MenuRow, arg);
        const bool open = !m.children.empty() && argBase == 0 && g.menuSub == static_cast<int>(i);
        if (hov || open) { src(cr, color("on_surface"), 0.08); rrect(cr, row.x, row.y, row.w, row.h, 14); cairo_fill(cr); }
        const Color fg = color("on_surface");
        const double a = m.enabled ? 1.0 : 0.38;
        if (m.icon[0]) icon(cr, m.icon, row.x + 12, row.y + (row.h - 22) / 2, 22, color("on_surface_variant"), a);
        text(cr, m.label, row.x + (m.icon[0] ? 46 : 16), row.y + (row.h - textHeight(m.label, 15, false)) / 2, 15, false, fg, a, row.w - (m.icon[0] ? 46 : 16) - (m.children.empty() ? 12 : 36));
        if (!m.children.empty()) icon(cr, "alt-arrow-right", row.x + row.w - 30, row.y + (row.h - 20) / 2, 20, color("on_surface_variant"), a);
        y += kMenuRowH;
    }
}

void drawMenu(cairo_t* cr, int W, int H) {
    if (!g.menuOpen) return;
    const double h = panelHeight(g.menu);
    g.menuRect = {std::min(g.menuX, W - kMenuW - 8.0), std::min(g.menuY, H - h - 8.0), kMenuW, h};
    g.menuRect.x = std::max(8.0, g.menuRect.x);
    g.menuRect.y = std::max(8.0, g.menuRect.y);
    g.subRect = {};
    // The first panel, then the second one over it (so it is in front where they meet, and its hit areas win there).
    drawPanel(cr, g.menuRect, g.menu, 0);
    if (g.menuSub >= 0 && g.menuSub < static_cast<int>(g.menu.size()) && !g.menu[static_cast<size_t>(g.menuSub)].children.empty()) {
        const auto& kids = g.menu[static_cast<size_t>(g.menuSub)].children;
        double rowY = g.menuRect.y + 8;
        for (int i = 0; i < g.menuSub; i++) rowY += g.menu[static_cast<size_t>(i)].separator ? 13 : kMenuRowH;
        const double sh = panelHeight(kids), sw = 220;
        double sx = g.menuRect.x + g.menuRect.w - 4;
        if (sx + sw > W - 8) sx = g.menuRect.x - sw + 4;
        g.subRect = {sx, std::clamp(rowY - 8, 8.0, H - sh - 8.0), sw, sh};
        drawPanel(cr, g.subRect, kids, 1000);
    }
}

void drawDialog(cairo_t* cr, int W, int H) {
    if (g.dialog == Dlg::None) return;
    src(cr, color("on_surface"), 0.0);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.32);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    hot({0, 0, static_cast<double>(W), static_cast<double>(H)}, Scrim);
    const bool field = g.dialog != Dlg::ConfirmDelete;
    const double w = std::min(420.0, W - 48.0), h = field ? 210 : 190, x = (W - w) / 2, y = (H - h) / 2 - 20;
    src(cr, color("surface_container_high"));
    rrect(cr, x, y, w, h, 28);
    cairo_fill(cr);
    hot({x, y, w, h}, Scrim, 1);  // (a click inside the dialog is not a click on the scrim)
    std::string title, okLabel = i18n::tr("OK");
    switch (g.dialog) {
        case Dlg::NewFolder: title = i18n::tr("New folder"); okLabel = i18n::tr("Create"); break;
        case Dlg::NewFile: title = i18n::tr("New file"); okLabel = i18n::tr("Create"); break;
        case Dlg::Rename: title = i18n::tr("Rename"); okLabel = i18n::tr("Rename"); break;
        default: title = i18n::tr("Delete forever?"); okLabel = i18n::tr("Delete"); break;
    }
    text(cr, title, x + 24, y + 22, 24, true, color("on_surface"));
    if (field) drawField(cr, {x + 24, y + 76, w - 48, 52}, DialogField, g.dialogField, 2, "", false, false);
    else text(cr, i18n::tr("The selected items will be deleted for good. This cannot be undone."), x + 24, y + 70, 15, false, color("on_surface_variant"), 1, w - 48, 0, true);
    const double bh = 40, by = y + h - 24 - bh;
    const double okW = textWidth(okLabel, 15, true) + 48, cancelW = textWidth(i18n::tr("Cancel"), 15, true) + 32;
    const Rect ok{x + w - 24 - okW, by, okW, bh}, cancel{ok.x - 8 - cancelW, by, cancelW, bh};
    const bool ch = hot(cancel, DialogCancel), oh = hot(ok, DialogOk);
    if (ch) { src(cr, color("primary"), 0.08); rrect(cr, cancel.x, cancel.y, cancel.w, cancel.h, 20); cairo_fill(cr); }
    text(cr, i18n::tr("Cancel"), cancel.x, cancel.y + (bh - 20) / 2, 15, true, color("primary"), 1, cancel.w, 1);
    const bool danger = g.dialog == Dlg::ConfirmDelete;
    src(cr, danger ? color("error") : color("primary"));
    rrect(cr, ok.x, ok.y, ok.w, ok.h, 20);
    cairo_fill(cr);
    if (oh) { src(cr, color(danger ? "on_error" : "on_primary"), 0.1); rrect(cr, ok.x, ok.y, ok.w, ok.h, 20); cairo_fill(cr); }
    text(cr, okLabel, ok.x, ok.y + (bh - 20) / 2, 15, true, color(danger ? "on_error" : "on_primary"), 1, ok.w, 1);
}

void drawToast(cairo_t* cr, int W, int H) {
    if (g.toast.empty()) return;
    const double age = std::chrono::duration<double>(Clock::now() - g.toastAt).count();
    if (age > 5) { g.toast.clear(); return; }
    const double w = std::min(W - 48.0, textWidth(g.toast, 15, false) + 48), x = (W - w) / 2, y = H - kPad - 44 - 56;
    src(cr, color("inverse_surface"));
    rrect(cr, x, y, w, 48, 14);
    cairo_fill(cr);
    text(cr, g.toast, x + 24, y + 14, 15, false, color("inverse_on_surface"), 1, w - 48);
}

void drawWindow(cairo_t* cr, int W, int H) {
    g.hits.clear();
    g.animating = false;
    cairo_rectangle(cr, 0, 0, W, H);  // (square: the compositor rounds the corners of its windows)
    src(cr, color("background"));
    cairo_fill(cr);
    drawHeader(cr, W);
    const double top = kPad + 44 + 12, bottom = H - kPad;
    g.sidebar = {kPad, top, kSideW, bottom - top};
    drawSidebar(cr, g.sidebar);
    drawContent(cr, {kPad + kSideW + 12, top, W - 2 * kPad - kSideW - 12, bottom - top});
    drawToast(cr, W, H);
    drawMenu(cr, W, H);
    drawDialog(cr, W, H);
}
