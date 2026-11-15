#pragma once

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

struct BubbleColor { double r = 0, g = 0, b = 0; };

struct Bubble {
    std::string key, text;
    double ax = 0, ay = 0, aw = 0, ah = 0;
    double viewW = 0, viewH = 0;
    std::chrono::steady_clock::time_point since{};
};

inline void bubbleShow(Bubble& b, const std::string& key, const std::string& text, double ax, double ay, double aw, double ah, double viewW, double viewH) {
    if (b.key != key) b.since = std::chrono::steady_clock::now();
    b.key = key; b.text = text;
    b.ax = ax; b.ay = ay; b.aw = aw; b.ah = ah;
    b.viewW = viewW; b.viewH = viewH;
}
inline void bubbleHide(Bubble& b) { b.key.clear(); }

inline double bubbleSpring(double t) { return t <= 0 ? 0 : 1 - std::exp(-7.5 * t) * std::cos(15.0 * t); }

inline bool bubbleDraw(cairo_t* cr, const Bubble& b, BubbleColor bg, BubbleColor fg) {
    if (b.key.empty()) return false;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - b.since).count();
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(f, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_font_description_free(f);
    pango_layout_set_text(layout, b.text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double bw = tw + 26, bh = th + 16, tip = 7, gap = 4;
    const double tcx = b.ax + b.aw / 2;
    const bool above = b.ay - bh - tip - gap >= 6;
    const double bx = std::clamp(tcx - bw / 2, 8.0, std::max(8.0, b.viewW - bw - 8));
    const double by = above ? b.ay - bh - tip - gap : b.ay + b.ah + tip + gap;
    const double tipY = above ? by + bh + tip : by - tip;
    const double s = std::max(0.0, bubbleSpring(t)), alpha = std::min(1.0, t / 0.09);

    cairo_save(cr);
    cairo_translate(cr, tcx, tipY);
    cairo_scale(cr, s, s);
    cairo_translate(cr, -tcx, -tipY);
    auto box = [&](double x, double y, double w, double h, double r) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
    };
    cairo_push_group(cr);
    box(bx + 1, by + 3, bw, bh, 12);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
    cairo_fill(cr);
    box(bx, by, bw, bh, 12);
    cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
    cairo_fill(cr);
    cairo_move_to(cr, tcx - tip, above ? by + bh - 0.5 : by + 0.5);
    cairo_line_to(cr, tcx, tipY);
    cairo_line_to(cr, tcx + tip, above ? by + bh - 0.5 : by + 0.5);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, fg.r, fg.g, fg.b);
    cairo_move_to(cr, bx + 13, by + (bh - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
    g_object_unref(layout);
    return t < 0.7;
}
