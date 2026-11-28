// What T3xt draws. Included once by editor.cpp, inside its anonymous namespace.
#pragma once

bool hot(const Rect& r, int id, int arg = 0) {
    g.hits.push_back({r, id, arg});
    return g.hoverId == id && g.hoverArg == arg;
}
bool hotIn(const Rect& r, const Rect& visible, int id, int arg = 0) {
    const double x0 = std::max(r.x, visible.x), y0 = std::max(r.y, visible.y);
    const double x1 = std::min(r.x + r.w, visible.x + visible.w), y1 = std::min(r.y + r.h, visible.y + visible.h);
    if (x1 <= x0 || y1 <= y0) return false;
    return hot({x0, y0, x1 - x0, y1 - y0}, id, arg);
}

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

void drawField(cairo_t* cr, const Rect& r, int id, const Field& f, int which, const std::string& placeholder) {
    const bool focused = g.focus == which && (which == 0 ? false : true), hov = hot(r, id);
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
    const double tx = r.x + 16, tw = r.w - 32;
    if (f.s.empty()) text(cr, placeholder, tx, r.y + (r.h - 18) / 2, 14, false, color("on_surface_variant"), 0.7, tw);
    else text(cr, f.s, tx, r.y + (r.h - 18) / 2, 14, false, color("on_surface"), 1, tw);
    if (focused && g.caretOn) {
        const double cw = textWidth(f.s.substr(0, std::min(f.cur, f.s.size())), 14, false);
        src(cr, color("on_surface"));
        cairo_rectangle(cr, tx + std::min(cw, tw), r.y + (r.h - 20) / 2, 2, 20);
        cairo_fill(cr);
    }
}

// ---- the colours of the code: the palette of the shell -----------------------------------------------------------------------------------
// The colours of the shell are close to each other (all warm, or all cool), which is right for an interface and useless for telling a keyword from
// a string. So the colours of the code are the primary colour of the shell turned round the hue circle: same lightness feel, same family, but
// each kind of token on its own hue. The keywords are the primary colour itself, and the text, the comments and the diffs are straight from the palette.
struct Hsl { double h, s, l; };
Hsl toHsl(const Color& c) {
    const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), l = (mx + mn) / 2, d = mx - mn;
    double h = 0, s = 0;
    if (d > 1e-9) {
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == c.r) h = std::fmod((c.g - c.b) / d + 6, 6);
        else if (mx == c.g) h = (c.b - c.r) / d + 2;
        else h = (c.r - c.g) / d + 4;
        h *= 60;
    }
    return {h, s, l};
}
Color fromHsl(double h, double s, double l) {
    h = std::fmod(std::fmod(h, 360) + 360, 360);
    const double c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2) - 1)), m = l - c / 2;
    double r = 0, g_ = 0, b = 0;
    if (h < 60) { r = c; g_ = x; } else if (h < 120) { r = x; g_ = c; } else if (h < 180) { g_ = c; b = x; }
    else if (h < 240) { g_ = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return {r + m, g_ + m, b + m};
}
Color tokenColor(uint8_t t) {
    using namespace t3;
    static time_t stamp = 0;
    static Color table[TCount];
    if (stamp != g_colorsStamp || t >= TCount) {
        stamp = g_colorsStamp;
        const Color text = color("on_surface"), bg = color("background"), primary = color("primary");
        const bool dark = (0.2126 * bg.r + 0.7152 * bg.g + 0.0722 * bg.b) < 0.5;
        const Hsl p = toHsl(primary);
        const double l = dark ? std::clamp(p.l, 0.66, 0.80) : std::clamp(p.l, 0.28, 0.42), s = std::clamp(std::max(p.s, 0.5), 0.5, 0.7);
        auto turn = [&](double deg) { return fromHsl(p.h + deg, s, l); };
        for (int i = 0; i < TCount; i++) table[i] = text;
        table[TKeyword] = primary;
        table[TTag] = primary;
        table[THeading] = primary;
        table[TString] = turn(110);
        table[TNumber] = turn(-38);
        table[TType] = turn(58);
        table[TFunction] = turn(205);
        table[TAttr] = turn(165);
        table[TPreproc] = turn(-85);
        table[TEmphasis] = turn(110);
        table[TComment] = mix(color("on_surface_variant"), color("surface_container"), 0.7);
        table[TAdded] = turn(110);
        table[TRemoved] = color("error");
    }
    return table[t < TCount ? t : 0];
}

const char* kMonoFamily = "JetBrainsMono Nerd Font Mono,JetBrains Mono,Hack,Adwaita Mono,DejaVu Sans Mono,Monospace";
PangoFontDescription* monoFont() {
    PangoFontDescription* d = pango_font_description_new();
    pango_font_description_set_family(d, kMonoFamily);
    pango_font_description_set_absolute_size(d, g.fontPx * PANGO_SCALE);
    return d;
}
void measureFont() {
    PangoFontDescription* d = monoFont();
    pango_layout_set_font_description(g_measure, d);
    pango_font_description_free(d);
    pango_layout_set_width(g_measure, -1);
    pango_layout_set_text(g_measure, "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", -1);
    PangoRectangle ink, logical;
    pango_layout_get_extents(g_measure, &ink, &logical);
    g.charW = logical.width / 40.0 / PANGO_SCALE;
    g.lineH = std::ceil(g.fontPx * 1.55);
}

// A line as the text on screen: tabs become spaces up to the next stop; map[i] is where byte i of the line starts in it.
void expandLine(const std::string& s, std::string& out, std::vector<int>& map) {
    out.clear();
    map.assign(s.size() + 1, 0);
    int vis = 0;
    for (size_t i = 0; i < s.size();) {
        map[i] = static_cast<int>(out.size());
        if (s[i] == '\t') { const int n = t3::kTabWidth - vis % t3::kTabWidth; out.append(static_cast<size_t>(n), ' '); vis += n; i++; }
        else {
            const size_t e = static_cast<size_t>(t3::nextCol(s, static_cast<int>(i)));
            for (size_t k = i + 1; k < e; k++) map[k] = static_cast<int>(out.size());
            out.append(s, i, e - i);
            vis++;
            i = e;
        }
    }
    map[s.size()] = static_cast<int>(out.size());
}

void drawCodeLine(cairo_t* cr, t3::Doc& d, int line, double x, double y) {
    const std::string& s = d.lines[static_cast<size_t>(line)];
    if (s.empty()) return;
    static std::string disp;
    static std::vector<int> map;
    static std::vector<uint8_t> toks;
    expandLine(s, disp, map);
    d.tokens(line, toks);
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = monoFont();
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, disp.c_str(), static_cast<int>(disp.size()));
    PangoAttrList* attrs = pango_attr_list_new();
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i + 1;
        while (j < s.size() && toks[j] == toks[i]) j++;
        if (toks[i] != t3::TPlain) {
            const Color c = tokenColor(toks[i]);
            PangoAttribute* a = pango_attr_foreground_new(static_cast<guint16>(c.r * 65535), static_cast<guint16>(c.g * 65535), static_cast<guint16>(c.b * 65535));
            a->start_index = static_cast<guint>(map[i]);
            a->end_index = static_cast<guint>(map[j]);
            pango_attr_list_insert(attrs, a);
            if (toks[i] == t3::TComment || toks[i] == t3::TEmphasis) {
                PangoAttribute* it = pango_attr_style_new(PANGO_STYLE_ITALIC);
                it->start_index = static_cast<guint>(map[i]);
                it->end_index = static_cast<guint>(map[j]);
                pango_attr_list_insert(attrs, it);
            }
            if (toks[i] == t3::THeading) {
                PangoAttribute* b = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                b->start_index = static_cast<guint>(map[i]);
                b->end_index = static_cast<guint>(map[j]);
                pango_attr_list_insert(attrs, b);
            }
        }
        i = j;
    }
    pango_layout_set_attributes(l, attrs);
    pango_attr_list_unref(attrs);
    src(cr, color("on_surface"));
    cairo_move_to(cr, x, y + (g.lineH - g.fontPx * 1.3) / 2);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

// ---- the editor ------------------------------------------------------------------------------------------------------------------------
double colX(const t3::Doc& d, int line, int col) { return t3::visualColumn(d.lines[static_cast<size_t>(line)], col) * g.charW; }

int matchesOf(t3::Doc& d) {
    if (!g.findOpen || g.find.s.empty()) return 0;
    const size_t id = reinterpret_cast<size_t>(&d);
    if (g.matchKeyRev != d.revision || g.matchKeyDoc != id || g.matchKeyNeedle != g.find.s || g.matchKeyCase != g.matchCase) {
        g.matchCount = d.countMatches(g.find.s, g.matchCase);
        g.matchKeyRev = d.revision; g.matchKeyDoc = id; g.matchKeyNeedle = g.find.s; g.matchKeyCase = g.matchCase;
    }
    return g.matchCount;
}

void drawFindBar(cairo_t* cr, const Rect& card);

void drawEditor(cairo_t* cr, const Rect& card) {
    t3::Doc& d = doc();
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, kRadius);
    cairo_fill(cr);
    measureFont();
    const Rect inner{card.x + 8, card.y + 8, card.w - 16, card.h - 8 - kStatusH};
    const int digits = std::max(2, static_cast<int>(std::to_string(d.lineCount()).size()));
    g.gutterW = std::ceil(digits * g.charW) + 28;
    g.textRect = {inner.x + g.gutterW, inner.y, inner.w - g.gutterW, inner.h};
    g.viewLines = std::max(1, static_cast<int>(inner.h / g.lineH));
    // How far it can scroll.
    const size_t id = reinterpret_cast<size_t>(&d);
    if (g.widthRevision != d.revision || g.widthDoc != id) {
        double longest = 0;
        for (const std::string& s : d.lines) longest = std::max(longest, static_cast<double>(s.size()));
        g.longestBytes = longest; g.widthRevision = d.revision; g.widthDoc = id;
    }
    g.scrollMaxY = std::max(0.0, d.lineCount() * g.lineH - inner.h + g.lineH * 4);
    g.scrollMaxX = std::max(0.0, g.longestBytes * g.charW - g.textRect.w + 60);
    d.scrollY = std::clamp(d.scrollY, 0.0, g.scrollMaxY);
    d.scrollX = std::clamp(d.scrollX, 0.0, g.scrollMaxX);
    const double offY = d.scrollY + g.band.over;
    const int first = std::max(0, static_cast<int>(offY / g.lineH)), last = std::min(d.lineCount() - 1, static_cast<int>((offY + inner.h) / g.lineH));
    d.ensureStates(first);

    hotIn(g.textRect, g.textRect, TextArea);
    hotIn({inner.x, inner.y, g.gutterW, inner.h}, inner, Gutter);
    cairo_save(cr);
    cairo_rectangle(cr, inner.x, inner.y, inner.w, inner.h);
    cairo_clip(cr);
    const t3::Pos selA = d.selStart(), selB = d.selEnd();
    const std::string needle = g.findOpen ? g.find.s : "";
    for (int l = first; l <= last; l++) {
        const double y = inner.y + l * g.lineH - offY;
        const std::string& s = d.lines[static_cast<size_t>(l)];
        const bool caretLine = l == d.cur.line && !d.hasSelection();
        if (caretLine) { src(cr, color("on_surface"), 0.055); rrect(cr, inner.x + 6, y + 1, inner.w - 12, g.lineH - 2, 10); cairo_fill(cr); }  // (a pill inside the card, not a bar to its corners)
        // the text of this line is clipped to its own area (it scrolls sideways under the gutter)
        cairo_save(cr);
        cairo_rectangle(cr, g.textRect.x, y, g.textRect.w, g.lineH);
        cairo_clip(cr);
        const double tx = g.textRect.x + 6 - d.scrollX;
        if (!needle.empty()) {  // every match of the search on this line
            const std::string hay = g.matchCase ? s : t3::lowerStr(s), n = g.matchCase ? needle : t3::lowerStr(needle);
            for (size_t p = hay.find(n); p != std::string::npos; p = hay.find(n, p + n.size())) {
                const double x0 = tx + colX(d, l, static_cast<int>(p)), x1 = tx + colX(d, l, static_cast<int>(p + n.size()));
                src(cr, color("tertiary"), 0.35);
                rrect(cr, x0, y + 1, x1 - x0, g.lineH - 2, 4);
                cairo_fill(cr);
            }
        }
        if (d.hasSelection() && l >= selA.line && l <= selB.line) {
            const int c0 = l == selA.line ? selA.col : 0, c1 = l == selB.line ? selB.col : static_cast<int>(s.size());
            double x0 = tx + colX(d, l, c0), x1 = tx + colX(d, l, c1);
            if (l < selB.line) x1 += g.charW * 0.6;  // the line break is selected too
            src(cr, color("primary"), 0.30);
            cairo_rectangle(cr, x0, y, std::max(2.0, x1 - x0), g.lineH);
            cairo_fill(cr);
        }
        drawCodeLine(cr, d, l, tx, y);
        if (l == d.cur.line && g.focus == 0 && g.caretOn) {
            src(cr, color("primary"));
            cairo_rectangle(cr, tx + colX(d, l, d.cur.col) - 1, y + 2, 2, g.lineH - 4);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        // the line number
        const std::string num = std::to_string(l + 1);
        text(cr, num, inner.x, y + (g.lineH - g.fontPx * 1.3) / 2, g.fontPx - 1, l == d.cur.line, color("on_surface_variant"), l == d.cur.line ? 0.95 : 0.5, g.gutterW - 14, 2);
    }
    cairo_restore(cr);
    // The scroll mark: it shows while the wheel moves (or the pointer is on it), then fades; it can be dragged.
    g.scrollTrack = {};
    if (g.scrollMaxY > 0) {
        const double h = inner.h;
        g.thumbH = std::max(40.0, h * h / (h + g.scrollMaxY));
        g.thumbTop = inner.y + (h - g.thumbH) * (d.scrollY / g.scrollMaxY);
        g.scrollTrack = {inner.x + inner.w - 14, inner.y, 14, h};
        const bool over = hotIn({g.scrollTrack.x, g.thumbTop, g.scrollTrack.w, g.thumbH}, g.scrollTrack, Scrollbar);
        const double alpha = (over || g.dragScroll) ? 1.0 : g.band.thumbAlpha();
        if (alpha > 0) {
            src(cr, color(g.dragScroll ? "primary" : "on_surface_variant"), (g.dragScroll ? 0.9 : over ? 0.7 : 0.4) * alpha);
            const double bw = over || g.dragScroll ? 8 : 5;
            rrect(cr, g.scrollTrack.x + (g.scrollTrack.w - bw) / 2, g.thumbTop, bw, g.thumbH, bw / 2);
            cairo_fill(cr);
        }
    }
    // The status bar.
    const Rect bar{card.x + 16, card.y + card.h - kStatusH, card.w - 32, kStatusH};
    const int vcol = t3::visualColumn(d.lines[static_cast<size_t>(d.cur.line)], d.cur.col) + 1;
    std::string left = std::string(i18n::tr("Line")) + " " + std::to_string(d.cur.line + 1) + ", " + std::string(i18n::tr("Column")) + " " + std::to_string(vcol);
    if (d.hasSelection()) left += "  ·  " + std::string(i18n::tr("Selected")) + ": " + std::to_string(d.selectedText().size());
    text(cr, left, bar.x, bar.y + (bar.h - 16) / 2 - 2, 13, false, color("on_surface_variant"), 0.9, bar.w * 0.5);
    std::string right = d.lang->name + "  ·  UTF-8  ·  " + (d.crlf ? "CRLF" : "LF") + "  ·  " + std::string(i18n::tr("Spaces")) + ": " + std::to_string(t3::kTabWidth);
    if (d.readOnly) right = std::string(i18n::tr("Read only")) + "  ·  " + right;
    text(cr, right, bar.x + bar.w * 0.4, bar.y + (bar.h - 16) / 2 - 2, 13, false, color("on_surface_variant"), 0.9, bar.w * 0.6, 2);
    drawFindBar(cr, card);
}

void drawFindBar(cairo_t* cr, const Rect& card) {
    if (!g.findOpen) return;
    const double h = g.replaceOpen ? 108 : 56, w = std::min(card.w - 40, 560.0), x = card.x + card.w - w - 22, y = card.y + 14;
    for (int i = 5; i >= 1; i--) { src(cr, color("on_surface"), 0.03); rrect(cr, x - i, y - i + 3, w + 2 * i, h + 2 * i, 24 + i); cairo_fill(cr); }
    src(cr, color("surface_container_high"));
    rrect(cr, x, y, w, h, 24);
    cairo_fill(cr);
    hot({x, y, w, h}, Scrim, 2);  // (the bar itself: a click on it is not a click on the text under it)
    const double bw = 40;
    const Rect field{x + 8, y + 7, w - 8 - 8 - 4 * (bw + 4) - 70, 42};
    drawField(cr, field, FindField, g.find, 1, i18n::tr("Find"));
    const int count = matchesOf(doc());
    const std::string cnt = g.find.s.empty() ? "" : std::to_string(count);
    text(cr, cnt, field.x + field.w - 64, y + 18, 12, false, color(count || g.find.s.empty() ? "on_surface_variant" : "error"), 0.9, 52, 2);
    double bx = field.x + field.w + 6;
    {  // "Aa": case sensitive when it is lit
        const Rect r{bx, y + 8, bw, 40};
        const bool hov = hot(r, FindCase), on = g.matchCase;
        const Color bg = on ? color("secondary_container") : color("surface_container_highest"), fg = on ? color("on_secondary_container") : color("on_surface");
        src(cr, hov ? mix(fg, bg, 0.12) : bg);
        cairo_arc(cr, r.x + 20, r.y + 20, 20, 0, 2 * M_PI);
        cairo_fill(cr);
        text(cr, "Aa", r.x, r.y + (r.h - 18) / 2, 14, true, fg, 1, r.w, 1);
    }
    bx += bw + 4;
    discButton(cr, {bx, y + 8, bw, 40}, FindPrev, "alt-arrow-up");
    bx += bw + 4;
    discButton(cr, {bx, y + 8, bw, 40}, FindNext, "alt-arrow-down");
    bx += bw + 4;
    discButton(cr, {bx, y + 8, bw, 40}, FindClose, "close-circle", false, true);
    if (g.replaceOpen) {
        const Rect rf{x + 8, y + 57, w - 16 - 2 * 108 - 8, 42};
        drawField(cr, rf, ReplaceField, g.replace, 2, i18n::tr("Replace with"));
        double rx = rf.x + rf.w + 8;
        for (auto [label, id] : {std::pair<const char*, int>{"Replace", ReplaceOne}, {"All", ReplaceAllBtn}}) {
            const Rect b{rx, y + 59, 100, 38};
            const bool hov = hot(b, id);
            src(cr, color("secondary_container"));
            rrect(cr, b.x, b.y, b.w, b.h, 19);
            cairo_fill(cr);
            if (hov) { src(cr, color("on_secondary_container"), 0.1); rrect(cr, b.x, b.y, b.w, b.h, 19); cairo_fill(cr); }
            text(cr, i18n::tr(label), b.x, b.y + (b.h - 20) / 2, 14, true, color("on_secondary_container"), 1, b.w, 1);
            rx += 108;
        }
    }
}

// ---- the tabs --------------------------------------------------------------------------------------------------------------------------
void drawTabs(cairo_t* cr, double x0, double x1) {
    const double y = kPad + 2, h = kTabH;
    const int n = static_cast<int>(g.docs.size());
    std::vector<double> w(static_cast<size_t>(n));
    double total = 0;
    for (int i = 0; i < n; i++) { w[static_cast<size_t>(i)] = std::clamp(textWidth(g.docs[static_cast<size_t>(i)]->title(), 14, false) + 62, 96.0, 220.0); total += w[static_cast<size_t>(i)] + 6; }
    const double avail = x1 - x0;
    if (total > avail) {  // too many: they get narrower (not under 96), then the strip scrolls
        const double each = std::max(96.0, (avail - 6.0 * n) / n);
        total = 0;
        for (int i = 0; i < n; i++) { w[static_cast<size_t>(i)] = std::min(w[static_cast<size_t>(i)], each); total += w[static_cast<size_t>(i)] + 6; }
    }
    g.tabScroll = std::clamp(g.tabScroll, 0.0, std::max(0.0, total - avail));
    cairo_save(cr);
    cairo_rectangle(cr, x0, y - 2, avail, h + 4);
    cairo_clip(cr);
    double x = x0 - g.tabScroll;
    for (int i = 0; i < n; i++) {
        t3::Doc& d = *g.docs[static_cast<size_t>(i)];
        const Rect r{x, y, w[static_cast<size_t>(i)], h};
        x += r.w + 6;
        if (r.x + r.w < x0 || r.x > x1) continue;
        const bool on = i == g.active, hov = hotIn(r, {x0, y, avail, h}, TabItem, i);
        const Color bg = on ? color("secondary_container") : color("surface_container_high"), fg = on ? color("on_secondary_container") : color("on_surface");
        src(cr, hov && !on ? mix(fg, bg, 0.1) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, h / 2);
        cairo_fill(cr);
        const Rect closeR{r.x + r.w - 32, r.y + (h - 24) / 2, 24, 24};
        const bool closeHot = hotIn(closeR, {x0, y, avail, h}, TabClose, i);
        if (closeHot) { src(cr, color("on_surface"), 0.12); cairo_arc(cr, closeR.x + 12, closeR.y + 12, 12, 0, 2 * M_PI); cairo_fill(cr); }
        if (d.modified && !closeHot && !hov) { src(cr, color("primary")); cairo_arc(cr, closeR.x + 12, closeR.y + 12, 4.5, 0, 2 * M_PI); cairo_fill(cr); }
        else if (hov || closeHot || on) icon(cr, "close-circle", closeR.x + 4, closeR.y + 4, 16, fg, closeHot ? 1.0 : 0.7);
        text(cr, d.title(), r.x + 16, r.y + (h - textHeight(d.title(), 14, on)) / 2, 14, on, fg, 1, r.w - 16 - 36);
    }
    cairo_restore(cr);
}

// ---- the sidebar -----------------------------------------------------------------------------------------------------------------------
void drawSidebar(cairo_t* cr, const Rect& card) {
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, kRadius);
    cairo_fill(cr);
    const double x = card.x + 10, w = card.w - 20, rowH = 38;
    double y = card.y + 16;
    // Open files.
    text(cr, i18n::tr("Open files"), x + 14, y + 2, 13, true, color("on_surface_variant"), 0.85);
    y += 28;
    for (int i = 0; i < static_cast<int>(g.docs.size()); i++) {
        t3::Doc& d = *g.docs[static_cast<size_t>(i)];
        const Rect r{x, y, w, rowH};
        const bool on = i == g.active, hov = hot(r, SideDoc, i);
        const Color fg = color(on ? "on_secondary_container" : "on_surface");
        if (on || hov) { src(cr, on ? color("secondary_container") : mix(color("on_surface"), color("surface_container"), 0.08)); rrect(cr, r.x, r.y, r.w, r.h, rowH / 2); cairo_fill(cr); }
        icon(cr, "file-text", r.x + 14, r.y + (rowH - 20) / 2, 20, fg, 0.9);
        text(cr, d.title(), r.x + 44, r.y + (rowH - textHeight(d.title(), 14, false)) / 2, 14, false, fg, 1, r.w - 44 - 40);
        const Rect cr_{r.x + r.w - 34, r.y + (rowH - 26) / 2, 26, 26};
        if (d.modified && !hov) { src(cr, color("primary")); cairo_arc(cr, cr_.x + 13, cr_.y + 13, 4.5, 0, 2 * M_PI); cairo_fill(cr); }
        else if (hov) {
            const bool ch = hot(cr_, SideDocClose, i);
            if (ch) { src(cr, color("on_surface"), 0.12); cairo_arc(cr, cr_.x + 13, cr_.y + 13, 13, 0, 2 * M_PI); cairo_fill(cr); }
            icon(cr, "close-circle", cr_.x + 4, cr_.y + 4, 18, fg, 0.8);
        }
        y += rowH + 2;
        if (y > card.y + card.h * 0.4) break;
    }
    // The folder: where the active file is.
    y += 10;
    src(cr, color("outline_variant"), 0.5);
    cairo_rectangle(cr, x + 14, y, w - 28, 1);
    cairo_fill(cr);
    y += 12;
    const std::string folderName = g.sideDir.empty() ? "" : (fs::path(g.sideDir).filename().empty() ? g.sideDir : fs::path(g.sideDir).filename().string());
    text(cr, folderName, x + 14, y + 2, 13, true, color("on_surface_variant"), 0.85, w - 28);
    y += 28;
    const Rect area{x, y, w, card.y + card.h - y - 10};
    if (area.h < rowH) return;
    int rows = static_cast<int>(g.sideEntries.size()) + (g.sideDir != "/" && !g.sideDir.empty() ? 1 : 0);
    const double content = rows * (rowH + 2);
    g.sideMax = std::max(0.0, content - area.h);
    g.sidePos = std::clamp(g.sidePos, 0.0, g.sideMax);
    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);
    const double off = g.sidePos + g.sideBand.over;
    int idx = 0;
    auto row = [&](const std::string& label, const char* ic, int id, int arg, bool current) {
        const Rect r{area.x, area.y + idx * (rowH + 2) - off, area.w - (g.sideMax > 0 ? 8 : 0), rowH};
        idx++;
        if (r.y + r.h < area.y || r.y > area.y + area.h) return;
        const bool hov = hotIn(r, area, id, arg);
        const Color fg = color(current ? "on_secondary_container" : "on_surface");
        if (current || hov) { src(cr, current ? color("secondary_container") : mix(color("on_surface"), color("surface_container"), 0.08)); rrect(cr, r.x, r.y, r.w, r.h, rowH / 2); cairo_fill(cr); }
        icon(cr, ic, r.x + 14, r.y + (rowH - 20) / 2, 20, fg, 0.85);
        text(cr, label, r.x + 44, r.y + (rowH - textHeight(label, 14, false)) / 2, 14, false, fg, 1, r.w - 56);
    };
    if (g.sideDir != "/" && !g.sideDir.empty()) row("..", "alt-arrow-up", SideUp, 0, false);
    for (size_t i = 0; i < g.sideEntries.size(); i++) {
        const SideEntry& e = g.sideEntries[i];
        const bool open = !e.dir && std::any_of(g.docs.begin(), g.docs.end(), [&](const auto& d) { return d->path == e.path; });
        row(e.name, e.dir ? "folder" : "file", SideFile, static_cast<int>(i), open);
    }
    cairo_restore(cr);
    if (g.sideMax > 0 && g.sideBand.thumbAlpha() > 0) {
        const double th = std::max(36.0, area.h * area.h / (area.h + g.sideMax)), ty = area.y + (area.h - th) * (g.sidePos / g.sideMax);
        src(cr, color("on_surface_variant"), 0.35 * g.sideBand.thumbAlpha());
        rrect(cr, area.x + area.w - 4, ty, 4, th, 2);
        cairo_fill(cr);
    }
}

// ---- the window ------------------------------------------------------------------------------------------------------------------------
void drawDialog(cairo_t* cr, int W, int H) {
    if (g.dialog == Dlg::None) return;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.32);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    hot({0, 0, static_cast<double>(W), static_cast<double>(H)}, Scrim);
    const double w = std::min(440.0, W - 48.0), h = 190, x = (W - w) / 2, y = (H - h) / 2 - 20;
    src(cr, color("surface_container_high"));
    rrect(cr, x, y, w, h, 28);
    cairo_fill(cr);
    hot({x, y, w, h}, Scrim, 1);
    std::string name = g.dialogDoc >= 0 && g.dialogDoc < static_cast<int>(g.docs.size()) ? g.docs[static_cast<size_t>(g.dialogDoc)]->title() : "";
    text(cr, i18n::tr("Save changes?"), x + 24, y + 22, 24, true, color("on_surface"));
    text(cr, name, x + 24, y + 62, 15, true, color("on_surface"), 1, w - 48);
    text(cr, i18n::tr("This file has changes that are not saved."), x + 24, y + 88, 15, false, color("on_surface_variant"), 1, w - 48, 0, true);
    const double bh = 40, by = y + h - 24 - bh;
    double rx = x + w - 24;
    for (auto [label, id, primary] : {std::tuple<const char*, int, bool>{"Save", DlgSave, true}, {"Discard", DlgDiscard, false}, {"Cancel", DlgCancel, false}}) {
        const double bwid = textWidth(i18n::tr(label), 15, true) + (primary ? 48 : 32);
        const Rect b{rx - bwid, by, bwid, bh};
        rx -= bwid + 8;
        const bool hov = hot(b, id);
        if (primary) { src(cr, color("primary")); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); if (hov) { src(cr, color("on_primary"), 0.1); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); } }
        else if (hov) { src(cr, color("primary"), 0.08); rrect(cr, b.x, b.y, b.w, b.h, 20); cairo_fill(cr); }
        text(cr, i18n::tr(label), b.x, b.y + (bh - 20) / 2, 15, true, color(primary ? "on_primary" : "primary"), 1, b.w, 1);
    }
}

void drawToast(cairo_t* cr, int W, int H) {
    if (g.toast.empty()) return;
    if (std::chrono::duration<double>(Clock::now() - g.toastAt).count() > 5) { g.toast.clear(); return; }
    const double w = std::min(W - 48.0, textWidth(g.toast, 15, false) + 48), x = (W - w) / 2, y = H - kPad - kStatusH - 60;
    src(cr, color("inverse_surface"));
    rrect(cr, x, y, w, 48, 14);
    cairo_fill(cr);
    text(cr, g.toast, x + 24, y + 14, 15, false, color("inverse_on_surface"), 1, w - 48);
}

void drawWindow(cairo_t* cr, int W, int H) {
    g.hits.clear();
    cairo_rectangle(cr, 0, 0, W, H);  // (square: the compositor rounds the corners of its windows)
    src(cr, color("background"));
    cairo_fill(cr);
    // The header: the buttons, the tabs, search and close.
    const double y = kPad, h = 44;
    double x = kPad;
    discButton(cr, {x, y, h, h}, BtnSidebar, "sidebar-minimalistic", g.sidebar);
    x += h + 6;
    discButton(cr, {x, y, h, h}, BtnNew, "document-add");
    x += h + 6;
    discButton(cr, {x, y, h, h}, BtnOpen, "folder-open");
    x += h + 6;
    discButton(cr, {x, y, h, h}, BtnSave, "diskette", false, false, 0, !doc().readOnly);
    x += h + 14;
    const double closeX = W - kPad - h, findX = closeX - 6 - h;
    drawTabs(cr, x, findX - 12);
    discButton(cr, {findX, y, h, h}, BtnFind, "magnifer", g.findOpen);
    discButton(cr, {closeX, y, h, h}, BtnClose, "close-circle", false, true);
    const double top = kPad + h + 12, bottom = H - kPad;
    double left = kPad;
    if (g.sidebar) { g.sideRect = {kPad, top, kSideW, bottom - top}; drawSidebar(cr, g.sideRect); left += kSideW + 12; }
    else g.sideRect = {};
    drawEditor(cr, {left, top, W - kPad - left, bottom - top});
    drawToast(cr, W, H);
    drawDialog(cr, W, H);
}
