// Tools: drawing the Freeform board (src/freeform.h). Included by src/tools.cpp (inside its anonymous namespace); the page and the PNG export both use it.
// The text of a board is the user's own: it is always handed to Pango with an explicit length so the translation lookup leaves it alone.

namespace ffdraw {
constexpr double kPadX = 16, kPadY = 10, kPlainPad = 4, kMinNodeW = 72, kMinTextW = 24, kLinkPx = 15, kLinkWidth = 3;

Color rgbColor(unsigned v) { return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0}; }
double luma(const Color& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
Color inkOn(const Color& bg) { return luma(bg) > 0.55 ? Color{0.11, 0.11, 0.12} : Color{0.96, 0.96, 0.96}; }

// A layout of user text. width > 0 wraps nothing but gives the lines a box to centre in.
PangoLayout* userLayout(PangoLayout* l, const std::string& s, double px, double width, bool center, const std::string& font = "") {
    if (font.empty()) {
        setFont(l, px, false);
    } else {
        PangoFontDescription* d = pango_font_description_from_string(font.c_str());
        pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
    }
    pango_layout_set_width(l, width > 0 ? static_cast<int>(width * PANGO_SCALE) : -1);
    pango_layout_set_alignment(l, center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    pango_layout_set_text(l, s.c_str(), static_cast<int>(s.size()));
    return l;
}
void textSize(const std::string& s, double px, double& w, double& h, const std::string& font = "") {
    userLayout(g_measure, s, px, 0, false, font);
    int iw = 0, ih = 0;
    pango_layout_get_pixel_size(g_measure, &iw, &ih);
    w = iw;
    h = ih;
}

// Sets the outer size of a text or node from its text (a node has room around it and a minimum width).
void fit(ff::Item& it) {
    if (it.kind != ff::Item::Text) return;
    double w = 0, h = 0;
    textSize(it.text, it.size, w, h, it.font);
    if (it.text.empty()) { double w2 = 0; textSize("Ag", it.size, w2, h, it.font); }
    const double px = it.boxed ? kPadX : kPlainPad, py = it.boxed ? kPadY : kPlainPad;
    it.textW = std::max(w + 2 * px, it.boxed ? kMinNodeW : kMinTextW);
    it.textH = h + 2 * py;
}
void fitAll(ff::Items& items) { for (ff::Item& it : items) fit(it); }

// Where the caret of a text sits inside the box that holds it, and the byte under a point (both measured from the text's own origin).
void caretAt(const ff::Item& it, size_t idx, double& x, double& y, double& h) {
    const double px = it.boxed ? kPadX : kPlainPad;
    userLayout(g_measure, it.text, it.size, it.boxed ? it.textW - 2 * px : 0, it.boxed, it.font);
    PangoRectangle pos{};
    pango_layout_get_cursor_pos(g_measure, static_cast<int>(std::min(idx, it.text.size())), &pos, nullptr);
    x = pos.x / double(PANGO_SCALE);
    y = pos.y / double(PANGO_SCALE);
    h = pos.height / double(PANGO_SCALE);
}
size_t indexAt(const ff::Item& it, double x, double y) {
    const double px = it.boxed ? kPadX : kPlainPad;
    userLayout(g_measure, it.text, it.size, it.boxed ? it.textW - 2 * px : 0, it.boxed, it.font);
    int idx = 0, trailing = 0;
    pango_layout_xy_to_index(g_measure, static_cast<int>(x * PANGO_SCALE), static_cast<int>(y * PANGO_SCALE), &idx, &trailing);
    size_t at = static_cast<size_t>(idx);
    while (trailing-- > 0) at = trNextChar(it.text, at);
    return std::min(at, it.text.size());
}

// The path of a shape in its box: flat corners, rounded corners or an ellipse.
void outline(cairo_t* cr, int style, const ff::Box& b) {
    if (style == ff::kShapeEllipse) {
        cairo_save(cr);
        cairo_translate(cr, b.x + b.w / 2, b.y + b.h / 2);
        cairo_scale(cr, std::max(0.5, b.w / 2), std::max(0.5, b.h / 2));
        cairo_new_sub_path(cr);
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
    } else if (style == ff::kShapeRounded) {
        rrect(cr, b.x, b.y, b.w, b.h, std::min(22.0, std::min(b.w, b.h) * 0.3));
    } else {
        cairo_rectangle(cr, b.x, b.y, b.w, b.h);
    }
}

void drawStroke(cairo_t* cr, const ff::Item& it) {
    const Color c = rgbColor(it.rgb);
    src(cr, c);
    if (it.pts.size() == 1) {
        cairo_arc(cr, it.pts[0].x, it.pts[0].y, it.size / 2, 0, 2 * M_PI);
        cairo_fill(cr);
        return;
    }
    cairo_set_line_width(cr, it.size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_new_path(cr);
    cairo_move_to(cr, it.pts[0].x, it.pts[0].y);
    ff::Pt cur = it.pts[0];
    for (size_t i = 1; i + 1 < it.pts.size(); i++) {  // quadratic curves through the midpoints: smooth, whatever the mouse did
        const ff::Pt ctl = it.pts[i], mid{(it.pts[i].x + it.pts[i + 1].x) / 2, (it.pts[i].y + it.pts[i + 1].y) / 2};
        cairo_curve_to(cr, cur.x + 2.0 / 3 * (ctl.x - cur.x), cur.y + 2.0 / 3 * (ctl.y - cur.y), mid.x + 2.0 / 3 * (ctl.x - mid.x), mid.y + 2.0 / 3 * (ctl.y - mid.y), mid.x, mid.y);
        cur = mid;
    }
    cairo_line_to(cr, it.pts.back().x, it.pts.back().y);
    cairo_stroke(cr);
}

ff::Box shapeBox(const ff::Item& it) {
    const double x0 = std::min(it.pts[0].x, it.pts[1].x), y0 = std::min(it.pts[0].y, it.pts[1].y);
    return {x0, y0, std::fabs(it.pts[0].x - it.pts[1].x), std::fabs(it.pts[0].y - it.pts[1].y)};
}
void drawShape(cairo_t* cr, const ff::Item& it, const Color& bg) {
    if (it.pts.size() < 2) return;
    const Color c = rgbColor(it.rgb);
    ff::Box b = shapeBox(it);
    b = {b.x + it.size / 2, b.y + it.size / 2, std::max(1.0, b.w - it.size), std::max(1.0, b.h - it.size)};
    if (!it.mshape.empty()) {  // a Material shape fills the box: the unit-square outline is stretched to it, then the pen goes back to normal
        cairo_save(cr);
        cairo_translate(cr, b.x, b.y);
        cairo_scale(cr, b.w, b.h);
        shapePath(cr, it.mshape, 0, 0, 1);
        cairo_restore(cr);
    } else {
        outline(cr, it.shape, b);
    }
    if (it.filled) { src(cr, mix(c, bg, 0.22)); cairo_fill_preserve(cr); }
    src(cr, c);
    cairo_set_line_width(cr, it.size);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
}

void drawText(cairo_t* cr, const ff::Item& it, const Color& bg) {
    if (it.pts.empty()) return;
    const Color c = rgbColor(it.rgb);
    const double x = it.pts[0].x, y = it.pts[0].y, px = it.boxed ? kPadX : kPlainPad, py = it.boxed ? kPadY : kPlainPad;
    Color ink = c;
    if (it.boxed) {
        const Color fill = mix(c, bg, 0.2);
        outline(cr, it.shape, {x + 1, y + 1, it.textW - 2, it.textH - 2});
        src(cr, fill);
        cairo_fill_preserve(cr);
        src(cr, c);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        ink = inkOn(fill);
    }
    PangoLayout* l = pango_cairo_create_layout(cr);
    userLayout(l, it.text, it.size, it.boxed ? it.textW - 2 * px : 0, it.boxed, it.font);
    src(cr, ink);
    cairo_move_to(cr, x + px, y + py);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

// The pill with the phrase on a link, centred on the middle of the arrow.
bool linkLabelBox(const ff::Items& items, const ff::Item& link, bool editing, ff::Box& out) {
    ff::Pt a, b;
    if (!ff::linkEnds(items, link, a, b) || (link.text.empty() && !editing)) return false;
    double w = 0, h = 0;
    textSize(link.text, kLinkPx, w, h);
    if (link.text.empty()) { double w2 = 0; textSize("Ag", kLinkPx, w2, h); }
    w = std::max(w + 20, 44.0);
    h += 10;
    out = {(a.x + b.x) / 2 - w / 2, (a.y + b.y) / 2 - h / 2, w, h};
    return true;
}

void drawLink(cairo_t* cr, const ff::Items& items, const ff::Item& link, const Color& bg, bool editing) {
    ff::Pt a, b;
    if (!ff::linkEnds(items, link, a, b)) return;
    const Color c = rgbColor(link.rgb);
    const double ang = std::atan2(b.y - a.y, b.x - a.x), head = 11 + link.size * 1.5, back = head * 0.8;
    src(cr, c);
    cairo_set_line_width(cr, link.size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, a.x, a.y);
    cairo_line_to(cr, b.x - std::cos(ang) * back, b.y - std::sin(ang) * back);
    cairo_stroke(cr);
    cairo_move_to(cr, b.x, b.y);
    cairo_line_to(cr, b.x - std::cos(ang - 0.42) * head, b.y - std::sin(ang - 0.42) * head);
    cairo_line_to(cr, b.x - std::cos(ang + 0.42) * head, b.y - std::sin(ang + 0.42) * head);
    cairo_close_path(cr);
    cairo_fill(cr);
    ff::Box box;
    if (!linkLabelBox(items, link, editing, box)) return;
    rrect(cr, box.x, box.y, box.w, box.h, box.h / 2);
    src(cr, bg);
    cairo_fill_preserve(cr);
    src(cr, c);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    PangoLayout* l = pango_cairo_create_layout(cr);
    userLayout(l, link.text, kLinkPx, 0, false);
    src(cr, inkOn(bg));
    cairo_move_to(cr, box.x + 10, box.y + 5);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

void drawItem(cairo_t* cr, const ff::Items& items, const ff::Item& it, const Color& bg, bool editing = false) {
    switch (it.kind) {
        case ff::Item::Stroke: if (!it.pts.empty()) drawStroke(cr, it); break;
        case ff::Item::Shape: drawShape(cr, it, bg); break;
        case ff::Item::Text: drawText(cr, it, bg); break;
        case ff::Item::Link: drawLink(cr, items, it, bg, editing); break;
    }
}

// The whole board: the arrows underneath, the rest in the order it was drawn.
void drawBoard(cairo_t* cr, const ff::Items& items, const Color& bg, int editingId = 0) {
    for (const ff::Item& it : items) if (it.kind == ff::Item::Link) drawItem(cr, items, it, bg, it.id == editingId);
    for (const ff::Item& it : items) if (it.kind != ff::Item::Link) drawItem(cr, items, it, bg);
}

void drawSelection(cairo_t* cr, const ff::Box& b, const Color& c) {
    rrect(cr, b.x - 6, b.y - 6, b.w + 12, b.h + 12, 12);
    src(cr, c);
    cairo_set_line_width(cr, 1.5);
    const double dash[2] = {6, 4};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
}

// The picture of a board: its content plus a margin, on the colour of the canvas, at twice the size (at most 8192 pixels a side).
bool renderPng(const ff::Items& items, const Color& bg, const std::string& path) {
    ff::Box b;
    if (!ff::contentBounds(items, b)) return false;
    for (const ff::Item& it : items) {  // the phrase on an arrow may stick out past both ends
        ff::Box lb;
        if (it.kind != ff::Item::Link || !linkLabelBox(items, it, false, lb)) continue;
        const double x0 = std::min(b.x, lb.x), y0 = std::min(b.y, lb.y);
        b = {x0, y0, std::max(b.x + b.w, lb.x + lb.w) - x0, std::max(b.y + b.h, lb.y + lb.h) - y0};
    }
    constexpr double margin = 40, maxSide = 8192;
    const double w = b.w + 2 * margin, h = b.h + 2 * margin;
    const double scale = std::min(2.0, maxSide / std::max(w, h));
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(w * scale)), static_cast<int>(std::ceil(h * scale)));
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); return false; }
    cairo_t* cr = mkcr(s);
    src(cr, bg);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, margin - b.x, margin - b.y);
    drawBoard(cr, items, bg);
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}
}  // namespace ffdraw
