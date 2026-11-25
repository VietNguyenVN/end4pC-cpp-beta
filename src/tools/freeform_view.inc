// The look of the Freeform page: the board and its toolbar. Included inside FreeformPage (page_freeform.inc).

    std::vector<Entry> toolbarEntries(bool labels, double copyW, double saveW) const {
        std::vector<Entry> e;
        for (int i = 0; i < kToolCount; i++) e.push_back({ToolBtn, i, 34});
        e.push_back({None, 0, 17});
        for (int i = 0; i < 3; i++) e.push_back({StyleBtn, i, 30});
        e.push_back({ShapeBtn, 0, 30});
        e.push_back({FillBtn, 0, 30});
        e.push_back({None, 0, 17});
        for (int i = 0; i < 8; i++) e.push_back({ColorBtn, i, 24});
        e.push_back({None, 0, 17});
        for (int i = 0; i < 3; i++) e.push_back({SizeBtn, i, 28});
        e.push_back({FontBtn, 0, 40});
        e.push_back({None, 0, 17});
        e.push_back({UndoBtn, 0, 36});
        e.push_back({RedoBtn, 0, 36});
        e.push_back({ClearBtn, 0, 36});
        e.push_back({None, 0, 17});
        e.push_back({CopyBtn, 0, labels ? copyW : 40});
        e.push_back({SaveBtn, 0, labels ? saveW : 40});
        return e;
    }

    // A small picture of a shape style, centred on (cx, cy).
    static void styleGlyph(cairo_t* cr, int shape, double cx, double cy, const Color& c, bool solid) {
        ffdraw::outline(cr, shape, {cx - 8, cy - 6, 16, 12});
        src(cr, c);
        if (solid) cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }

    void drawButton(cairo_t* cr, const Hit& h, const Color& bg, bool labels) {
        const Color fg = g.color("on_surface"), muted = g.color("on_surface_variant"), primary = g.color("primary");
        const bool hot = hover.id == h.id && hover.arg == h.arg;
        const double cx = h.r.x + h.r.w / 2, cy = h.r.y + h.r.h / 2;
        auto pill = [&](const Color& base, bool on) {
            if (!on && !hot) return;
            rrect(cr, h.r.x, h.r.y, h.r.w, h.r.h, h.r.h / 2);
            src(cr, on ? g.color("primary_container") : mix(fg, base, 0.1));
            cairo_fill(cr);
        };
        const Color onFg = g.color("on_primary_container");
        switch (h.id) {
            case ToolBtn: pill(bg, tool == h.arg); ::icon(cr, kToolIcons[h.arg], cx - 10, cy - 10, 20, tool == h.arg ? onFg : fg); break;
            case StyleBtn: pill(bg, style == h.arg); styleGlyph(cr, h.arg, cx, cy, style == h.arg ? onFg : fg, false); break;
            case ShapeBtn: {  // the Material shape the tool would draw
                pill(bg, shapesOpen || !mshape.empty());
                shapePath(cr, mshape.empty() ? "Cookie12Sided" : mshape, cx - 10, cy - 10, 20);
                src(cr, shapesOpen || !mshape.empty() ? onFg : fg);
                cairo_fill(cr);
                break;
            }
            case FillBtn: {
                pill(bg, fillShapes);
                ffdraw::outline(cr, style, {cx - 8, cy - 7, 16, 14});
                src(cr, fillShapes ? onFg : fg, fillShapes ? 0.5 : 0);
                cairo_fill_preserve(cr);
                src(cr, fillShapes ? onFg : fg);
                cairo_set_line_width(cr, 2);
                const double dash[2] = {3, 2.5};
                cairo_set_dash(cr, fillShapes ? nullptr : dash, fillShapes ? 0 : 2, 0);
                cairo_stroke(cr);
                cairo_set_dash(cr, nullptr, 0, 0);
                break;
            }
            case ColorBtn: {
                const bool on = color == kColors[h.arg];
                if (on || hot) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cx, cy, 12, 0, 2 * M_PI);
                    src(cr, on ? primary : mix(fg, bg, 0.3));
                    cairo_set_line_width(cr, 2);
                    cairo_stroke(cr);
                }
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx, cy, 8.5, 0, 2 * M_PI);
                src(cr, ffdraw::rgbColor(kColors[h.arg]));
                cairo_fill_preserve(cr);
                src(cr, g.color("outline_variant"));
                cairo_set_line_width(cr, 1);
                cairo_stroke(cr);
                break;
            }
            case SizeBtn: {
                pill(bg, false);
                const double d = h.arg == 0 ? 6 : h.arg == 1 ? 10 : 15;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx, cy, d / 2, 0, 2 * M_PI);
                src(cr, sizeIdx == h.arg ? primary : muted);
                cairo_fill(cr);
                break;
            }
            case FontBtn: {
                pill(bg, fontOpen);
                PangoLayout* l = pango_cairo_create_layout(cr);
                ffdraw::userLayout(l, "Aa", 18, 0, false, font);  // the button shows the font it would give
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(l, &tw, &th);
                src(cr, fontOpen ? onFg : fg);
                cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
                pango_cairo_show_layout(cr, l);
                g_object_unref(l);
                break;
            }
            case UndoBtn: case RedoBtn: case ClearBtn: {
                const bool on = h.id == UndoBtn ? !hist.past.empty() : h.id == RedoBtn ? !hist.future.empty() : !items.empty();
                if (on) pill(bg, false);
                ::icon(cr, h.id == UndoBtn ? "undo-left-round" : h.id == RedoBtn ? "undo-right-round" : "trash-bin-minimalistic", cx - 10, cy - 10, 20, mix(fg, bg, on ? 1.0 : 0.35));
                break;
            }
            case CopyBtn: case SaveBtn: {
                const bool main = h.id == SaveBtn;
                const Color base = g.color(main ? "primary" : "secondary_container"), ink = g.color(main ? "on_primary" : "on_secondary_container");
                rrect(cr, h.r.x, h.r.y, h.r.w, h.r.h, h.r.h / 2);
                src(cr, hot ? mix(ink, base, 0.1) : base);
                cairo_fill(cr);
                const char* label = main ? "Save" : "Copy";
                double lw = 0, lh = 0;
                measure(label, 15, true, 0, lw, lh);
                const double total = 20 + (labels ? 6 + lw : 0), x0 = cx - total / 2;
                ::icon(cr, main ? "diskette" : "copy", x0, cy - 10, 20, ink);
                if (labels) text(cr, label, x0 + 26, cy - lh / 2, 15, true, ink);
                break;
            }
        }
    }

    void drawBars(cairo_t* cr, Rect r) {
        const Color bg = g.color("surface_container_high");
        double copyW = 0, saveW = 0, lh = 0;
        measure("Copy", 15, true, 0, copyW, lh);
        measure("Save", 15, true, 0, saveW, lh);
        copyW += 20 + 6 + 24;
        saveW += 20 + 6 + 24;
        constexpr double gap = 3, pad = 14;
        bool labels = true;
        std::vector<Entry> e = toolbarEntries(true, copyW, saveW);
        auto widthOf = [&](const std::vector<Entry>& v) { double w = 2 * pad; for (const Entry& x : v) w += x.w; return w + gap * (v.size() - 1); };
        if (widthOf(e) > r.w) { labels = false; e = toolbarEntries(false, copyW, saveW); }
        // still too wide (a narrow window): every button and gap shrinks together, down to about 60 %
        const double natural = widthOf(e), fit = std::clamp((r.w - 2 * pad) / std::max(1.0, natural - 2 * pad), 0.6, 1.0);
        if (fit < 1) for (Entry& en : e) en.w *= fit;
        const double step = gap * fit, total = std::min(widthOf(e) - gap * (e.size() - 1) + step * (e.size() - 1), r.w);
        bar = {(r.w - total) / 2, r.h - kToolbarH, total, kToolbarH};
        rrect(cr, bar.x, bar.y, bar.w, bar.h, kToolbarH / 2);
        src(cr, bg);
        cairo_fill(cr);
        hits.clear();
        double x = bar.x + pad;
        for (const Entry& en : e) {
            if (en.id == None) {
                src(cr, g.color("outline_variant"), 0.7);
                cairo_rectangle(cr, x + en.w / 2 - 0.5, bar.y + 16, 1, kToolbarH - 32);
                cairo_fill(cr);
            } else {
                const double bh = en.id == CopyBtn || en.id == SaveBtn ? 40 : en.id == ColorBtn || en.id == SizeBtn ? 32 : 36;
                Hit h;
                h.id = en.id;
                h.arg = en.arg;
                h.r = {x, bar.y + (kToolbarH - bh) / 2, en.w, bh};
                hits.push_back(h);
                drawButton(cr, h, bg, labels);
            }
            x += en.w + step;
        }
    }

    static const char* tipOf(const Hit& h) {
        switch (h.id) {
            case ToolBtn: return kToolTips[h.arg];
            case StyleBtn: return h.arg == ff::kShapeFlat ? "Flat corners" : h.arg == ff::kShapeRounded ? "Rounded corners" : "Ellipse";
            case FillBtn: return "Fill shapes";
            case ColorBtn: return "Color";
            case SizeBtn: return h.arg == 0 ? "Thin" : h.arg == 1 ? "Medium" : "Thick";
            case FontBtn: return "Font";
            case ShapeBtn: return "Material shapes";
            case UndoBtn: return "Undo (Ctrl+Z)";
            case RedoBtn: return "Redo (Ctrl+Y)";
            case ClearBtn: return "Clear the board";
            case CopyBtn: return "Copy as a picture (Ctrl+C)";
            case SaveBtn: return "Save as a picture (Ctrl+S)";
        }
        return nullptr;
    }
    // A label that floats above the toolbar and, for a moment, the message of what Copy or Save just did.
    void pillLabel(cairo_t* cr, const std::string& s, double cx, double bottom, double maxRight) {
        double w = 0, h = 0;
        measure(s, 13, false, 0, w, h);
        const double pw = w + 24, ph = h + 12, x = std::clamp(cx - pw / 2, 8.0, std::max(8.0, maxRight - pw - 8));
        rrect(cr, x, bottom - ph, pw, ph, ph / 2);
        src(cr, g.color("inverse_surface"));
        cairo_fill(cr);
        text(cr, s, x + 12, bottom - ph + 6, 13, false, g.color("inverse_on_surface"));
    }

    void drawCaret(cairo_t* cr) {
        const ff::Item* e = find(editId);
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        lastCaret = caretOn;
        if (!e || !caretOn) return;
        double x = 0, y = 0, h = 0;
        if (e->kind == ff::Item::Text) {
            double cx = 0, cy = 0;
            ffdraw::caretAt(*e, caret, cx, cy, h);
            x = e->pts[0].x + (e->boxed ? ffdraw::kPadX : ffdraw::kPlainPad) + cx;
            y = e->pts[0].y + (e->boxed ? ffdraw::kPadY : ffdraw::kPlainPad) + cy;
        } else {
            ff::Box box;
            if (!ffdraw::linkLabelBox(items, *e, true, box)) return;
            double w = 0;
            ffdraw::textSize(e->text.substr(0, caret), ffdraw::kLinkPx, w, h);
            if (e->text.empty()) { double w2 = 0; ffdraw::textSize("Ag", ffdraw::kLinkPx, w2, h); }
            x = box.x + 10 + w;
            y = box.y + 5;
        }
        src(cr, g.color("primary"));
        cairo_rectangle(cr, x - 1, y, 2, h);
        cairo_fill(cr);
    }

    // The list of installed fonts, each name in its own font, above the Font button.
    void drawFontPicker(cairo_t* cr) {
        const Hit* btn = nullptr;
        for (const Hit& h : hits) if (h.id == FontBtn) btn = &h;
        if (!btn) return;
        const double h = std::min(kSearchH + kPopRows * kRowH + 2 * kPopPad, std::max(kSearchH + 2 * kRowH, bar.y - 24));
        const double x = std::clamp(btn->r.x + btn->r.w / 2 - kPopW / 2, 8.0, std::max(8.0, W - kPopW - 8));
        fontPop = {x, bar.y - 10 - h, kPopW, h};
        const double listTop = fontPop.y + kSearchH, max = std::max(0.0, static_cast<double>(fontRows.size()) * kRowH - (h - kSearchH - 2 * kPopPad));
        fontScroll = std::clamp(fontScroll, 0.0, max);
        const Color fg = g.color("on_surface");
        rrect(cr, fontPop.x + 1, fontPop.y + 3, fontPop.w, fontPop.h, 20);
        src(cr, Color{0, 0, 0}, 0.25);
        cairo_fill(cr);
        rrect(cr, fontPop.x, fontPop.y, fontPop.w, fontPop.h, 20);
        src(cr, g.color("surface_container_highest"));
        cairo_fill_preserve(cr);
        src(cr, g.color("outline_variant"), 0.6);
        cairo_set_line_width(cr, 1);
        cairo_stroke_preserve(cr);
        cairo_clip(cr);
        // the search field
        const Color panel = g.color("surface_container_highest");
        rrect(cr, fontPop.x + 10, fontPop.y + 10, fontPop.w - 20, kSearchH - 18, (kSearchH - 18) / 2);
        src(cr, mix(fg, panel, 0.08));
        cairo_fill(cr);
        ::icon(cr, "magnifer", fontPop.x + 22, fontPop.y + 10 + (kSearchH - 18 - 20) / 2.0, 20, g.color("on_surface_variant"));
        if (fontFilter.empty()) {
            double lw = 0, lh = 0;
            measure("Search", 15, false, 0, lw, lh);
            text(cr, "Search", fontPop.x + 50, fontPop.y + 10 + (kSearchH - 18 - lh) / 2, 15, false, g.color("on_surface_variant"), 0, 0.8);
        } else {
            double lw = 0, lh = 0;
            ffdraw::textSize(fontFilter, 15, lw, lh);
            PangoLayout* l = pango_cairo_create_layout(cr);
            ffdraw::userLayout(l, fontFilter, 15, 0, false);
            src(cr, fg);
            cairo_move_to(cr, fontPop.x + 50, fontPop.y + 10 + (kSearchH - 18 - lh) / 2);
            pango_cairo_show_layout(cr, l);
            g_object_unref(l);
        }
        {
            double lw = 0, lh = 0;
            ffdraw::textSize(fontFilter, 15, lw, lh);
            src(cr, g.color("primary"));
            cairo_rectangle(cr, fontPop.x + 50 + lw + 1, fontPop.y + 10 + (kSearchH - 18 - 20) / 2.0, 2, 20);
            cairo_fill(cr);
        }
        cairo_save(cr);
        cairo_rectangle(cr, fontPop.x, listTop, fontPop.w, fontPop.h - kSearchH);
        cairo_clip(cr);
        if (fontRows.empty()) {
            double lw = 0, lh = 0;
            measure("No fonts match", 15, false, 0, lw, lh);
            text(cr, "No fonts match", fontPop.x + (fontPop.w - lw) / 2, listTop + 24, 15, false, g.color("on_surface_variant"));
        }
        for (size_t r = 0; r < fontRows.size(); r++) {
            const size_t i = static_cast<size_t>(fontRows[r]);
            const double y = listTop + kPopPad + r * kRowH - fontScroll;
            if (y + kRowH < listTop || y > fontPop.y + fontPop.h) continue;
            const bool on = families[i] == font, hot = static_cast<int>(r) == fontHover;
            if (on || hot) {
                rrect(cr, fontPop.x + 8, y + 1, fontPop.w - 16, kRowH - 2, 12);
                src(cr, on ? g.color("primary_container") : mix(fg, g.color("surface_container_highest"), 0.1));
                cairo_fill(cr);
            }
            PangoLayout* l = pango_cairo_create_layout(cr);
            const std::string label = families[i].empty() ? i18n::tr("Default") : families[i];
            ffdraw::userLayout(l, label, 16, 0, false, families[i]);
            pango_layout_set_width(l, static_cast<int>((fontPop.w - 40) * PANGO_SCALE));
            pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(l, &tw, &th);
            src(cr, on ? g.color("on_primary_container") : fg);
            cairo_move_to(cr, fontPop.x + 20, y + (kRowH - th) / 2.0);
            pango_cairo_show_layout(cr, l);
            g_object_unref(l);
        }
        cairo_restore(cr);
        cairo_reset_clip(cr);
    }

    // The Material shapes in a grid above their button.
    void drawShapePicker(cairo_t* cr) {
        const Hit* btn = nullptr;
        for (const Hit& h : hits) if (h.id == ShapeBtn) btn = &h;
        if (!btn) return;
        const double w = kShapeCols * kCell + 2 * kShapePad, rows = std::ceil(shapeNames.size() / double(kShapeCols));
        const double h = std::min(rows * kCell + 2 * kShapePad, std::max(2 * kCell, bar.y - 24));
        const double x = std::clamp(btn->r.x + btn->r.w / 2 - w / 2, 8.0, std::max(8.0, W - w - 8));
        shapePop = {x, bar.y - 10 - h, w, h};
        shapeScroll = std::clamp(shapeScroll, 0.0, std::max(0.0, rows * kCell + 2 * kShapePad - h));
        const Color fg = g.color("on_surface"), panel = g.color("surface_container_highest");
        rrect(cr, shapePop.x + 1, shapePop.y + 3, shapePop.w, shapePop.h, 20);
        src(cr, Color{0, 0, 0}, 0.25);
        cairo_fill(cr);
        rrect(cr, shapePop.x, shapePop.y, shapePop.w, shapePop.h, 20);
        src(cr, panel);
        cairo_fill_preserve(cr);
        src(cr, g.color("outline_variant"), 0.6);
        cairo_set_line_width(cr, 1);
        cairo_stroke_preserve(cr);
        cairo_clip(cr);
        for (size_t i = 0; i < shapeNames.size(); i++) {
            const ff::Box c = cellBox(static_cast<int>(i));
            const double cx = shapePop.x + c.x, cy = shapePop.y + c.y - shapeScroll;
            if (cy + c.h < shapePop.y || cy > shapePop.y + shapePop.h) continue;
            const bool on = shapeNames[i] == mshape, hot = static_cast<int>(i) == shapeHover;
            if (on || hot) {
                rrect(cr, cx + 2, cy + 2, c.w - 4, c.h - 4, 16);
                src(cr, on ? g.color("primary_container") : mix(fg, panel, 0.1));
                cairo_fill(cr);
            }
            shapePath(cr, shapeNames[i], cx + 10, cy + 10, c.w - 20);
            src(cr, on ? g.color("on_primary_container") : fg, 0.9);
            cairo_fill(cr);
        }
        cairo_reset_clip(cr);
    }

    void drawEmptyHint(cairo_t* cr) {
        const Color muted = g.color("on_surface_variant");
        const char* lines[3] = {"Draw, type or double-click to add a node", "Concept maps: pick Node, type, press Tab for a connected one", "Drag from a node to an empty place with the arrow tool"};
        const double px[3] = {19, 14, 14}, alpha[3] = {0.9, 0.65, 0.65};
        double y = canvas.h / 2 - 58;
        ::icon(cr, "pen-new-round", canvas.w / 2 - 28, y, 56, muted);
        y += 76;
        for (int i = 0; i < 3; i++) {
            double w = 0, h = 0;
            measure(lines[i], px[i], i == 0, 0, w, h);
            text(cr, lines[i], (canvas.w - w) / 2, y, px[i], i == 0, muted, 0, alpha[i]);
            y += h + 8;
        }
    }

    void drawBoardView(cairo_t* cr, const Color& boardBg) {
        const Color primary = g.color("primary");
        // a faint grid of dots, so there is something to place things against
        src(cr, g.color("outline"), 0.22);
        for (double gy = 24; gy < canvas.h; gy += 32)
            for (double gx = 24; gx < canvas.w; gx += 32) { cairo_new_sub_path(cr); cairo_arc(cr, gx, gy, 1.1, 0, 2 * M_PI); }
        cairo_fill(cr);

        if (items.empty() && !editId && drag == Drag::None) drawEmptyHint(cr);
        ffdraw::drawBoard(cr, items, boardBg, editId);

        if (const ff::Item* sel = find(selId)) {
            const ff::Box b = ff::boundsIn(items, *sel);
            if (b.w > 0 || b.h > 0) ffdraw::drawSelection(cr, b, primary);
            if (tool == Select && !editId && sel->kind != ff::Item::Link && (b.w > 0 || b.h > 0)) {  // the handles of the resize
                const ff::Box sb = selectionBox(*sel);
                for (int h = 0; h < 8; h++) {
                    if (sel->kind == ff::Item::Text && h % 2 == 1) continue;  // a text only scales: corners
                    const ff::Pt q = ff::handlePoint(sb, h);
                    rrect(cr, q.x - 6, q.y - 6, 12, 12, 4);
                    src(cr, boardBg);
                    cairo_fill(cr);
                    rrect(cr, q.x - 4.5, q.y - 4.5, 9, 9, 3);
                    src(cr, primary);
                    cairo_fill(cr);
                }
            }
        }
        if (drag == Drag::Pen) ffdraw::drawItem(cr, items, draft, boardBg);
        if (drag == Drag::Shape) ffdraw::drawItem(cr, items, draft, boardBg);
        if (drag == Drag::LinkFrom) {
            if (const ff::Item* from = find(dragId)) {
                const ff::Box fb = ff::boundsIn(items, *from);
                const int to = targetFor(pointer, dragId);
                const ff::Pt a = ff::edgePoint(fb, pointer, from->boxed || from->kind == ff::Item::Shape ? from->shape : 0);
                ff::Pt b = pointer;
                if (to >= 0) {
                    const ff::Box tb = ff::boundsIn(items, items[to]);
                    b = ff::edgePoint(tb, {fb.x + fb.w / 2, fb.y + fb.h / 2}, items[to].boxed || items[to].kind == ff::Item::Shape ? items[to].shape : 0);
                    ffdraw::drawSelection(cr, tb, primary);
                }
                src(cr, primary);
                cairo_set_line_width(cr, 2.5);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                const double dash[2] = {7, 6};
                cairo_set_dash(cr, dash, 2, 0);
                cairo_move_to(cr, a.x, a.y);
                cairo_line_to(cr, b.x, b.y);
                cairo_stroke(cr);
                cairo_set_dash(cr, nullptr, 0, 0);
            }
        }
        if (editId) drawCaret(cr);
    }

    void draw(cairo_t* cr, Rect r) override {
        W = r.w;
        H = r.h;
        canvas = {0, 0, r.w, r.h - kToolbarH - kGap};
        const Color boardBg = boardColor();
        cairo_save(cr);
        rrect(cr, canvas.x, canvas.y, canvas.w, canvas.h, 28);
        src(cr, boardBg);
        cairo_fill_preserve(cr);
        cairo_clip(cr);
        drawBoardView(cr, boardBg);
        cairo_restore(cr);

        drawBars(cr, r);

        if (flashUntil != Clock::time_point{}) pillLabel(cr, flashMsg, canvas.w / 2, canvas.h - 20, canvas.w);
        if (fontOpen) drawFontPicker(cr);
        if (shapesOpen) drawShapePicker(cr);
        const Color tb = g.color("inverse_surface"), tf = g.color("inverse_on_surface");
        tipMoving = bubbleDraw(cr, tip, {tb.r, tb.g, tb.b}, {tf.r, tf.g, tf.b}, nullptr, kTipSeconds);
    }
