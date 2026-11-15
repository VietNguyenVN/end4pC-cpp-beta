#pragma once

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <string>

#include "textsel.h"

namespace textsel {
inline size_t indexAtX(PangoLayout* layout, const std::string& text, double x) {
    if (text.empty()) return 0;
    int index = 0, trailing = 0;
    pango_layout_xy_to_index(layout, static_cast<int>(x * PANGO_SCALE), 0, &index, &trailing);
    size_t at = static_cast<size_t>(std::max(0, index));
    for (; trailing > 0; trailing--) at = nextChar(text, at);
    return std::min(at, text.size());
}

inline size_t indexAtXInFont(const std::string& text, const char* font, double x) {
    static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    static cairo_t* cr = cairo_create(scratch);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text.c_str(), -1);
    const size_t at = indexAtX(layout, text, x);
    g_object_unref(layout);
    return at;
}

inline double xAtIndex(PangoLayout* layout, size_t index) {
    PangoRectangle pos{};
    pango_layout_index_to_pos(layout, static_cast<int>(index), &pos);
    return static_cast<double>(pos.x) / PANGO_SCALE;
}
}
