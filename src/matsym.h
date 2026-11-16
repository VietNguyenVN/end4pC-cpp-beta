#pragma once
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace matsym {
constexpr const char* kFamilyOutline = "End4pC Material Symbols";
constexpr const char* kFamilyFill = "End4pC Material Symbols Fill";
constexpr double kScale = 1.0;

inline bool& enabled() { static bool on = false; return on; }
inline std::string& assetsDir() { static std::string d; return d; }
inline bool& fontsAdded() { static bool done = false; return done; }
inline bool& fontsOk() { static bool ok = false; return ok; }

inline void addFontFiles() {
    if (assetsDir().empty()) return;
    bool ok = true;
    for (const char* f : {"MaterialSymbolsRounded-Outline.ttf", "MaterialSymbolsRounded-Fill.ttf"}) {
        const std::string path = assetsDir() + "/fonts/" + f;
        ok = ok && access(path.c_str(), R_OK) == 0 && FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(path.c_str()));
    }
    fontsOk() = ok;
    fontsAdded() = true;
    if (PangoFontMap* map = pango_cairo_font_map_get_default())
        if (PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
}

inline const std::map<std::string, unsigned>& table() {
    static const std::map<std::string, unsigned> t = [] {
        std::map<std::string, unsigned> out;
        std::ifstream f(assetsDir() + "/icons/material-map.tsv");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t a = line.find('\t'), b = line.find('\t', a == std::string::npos ? 0 : a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            out[line.substr(0, a)] = static_cast<unsigned>(std::strtoul(line.c_str() + b + 1, nullptr, 16));
        }
        return out;
    }();
    return t;
}

inline std::string baseName(const std::string& name) { return name.substr(0, name.find('/')); }

inline std::string utf8(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    return s;
}

inline cairo_surface_t* makeMask(const std::string& name, bool filled, int size) {
    if (!enabled() || assetsDir().empty() || size <= 0) return nullptr;
    if (!fontsAdded()) addFontFiles();
    if (!fontsOk()) return nullptr;
    const auto& t = table();
    auto it = t.find(name);
    if (it == t.end()) it = t.find(baseName(name));
    if (it == t.end()) {
        static const bool log = std::getenv("END4PC_ICON_LOG") != nullptr;
        if (log) std::fprintf(stderr, "matsym: no glyph for '%s'\n", name.c_str());
        return nullptr;
    }
    const std::string glyph = utf8(it->second);
    auto render = [&]() -> cairo_surface_t* {
        cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = cairo_create(mask);
        cairo_font_options_t* fo = cairo_font_options_create();
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
        cairo_font_options_destroy(fo);
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_family(fd, filled ? kFamilyFill : kFamilyOutline);
        pango_font_description_set_absolute_size(fd, size * kScale * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(layout, glyph.c_str(), -1);
        const double base = static_cast<double>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
        const double em = size * kScale;
        cairo_set_source_rgba(cr, 0, 0, 0, 1);
        cairo_move_to(cr, (size - em) / 2.0, (size - em) / 2.0 - (base - em));
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_destroy(cr);
        return mask;
    };
    auto empty = [&](cairo_surface_t* m) {
        cairo_surface_flush(m);
        const unsigned char* d = cairo_image_surface_get_data(m);
        const int stride = cairo_image_surface_get_stride(m);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                if (d[y * stride + x * 4 + 3]) return false;
        return true;
    };
    cairo_surface_t* mask = render();
    static int reregistered = 0;
    if (empty(mask) && reregistered < 3) {
        reregistered++;
        cairo_surface_destroy(mask);
        addFontFiles();
        mask = render();
    }
    if (empty(mask)) {
        static const bool log = std::getenv("END4PC_ICON_LOG") != nullptr;
        if (log) std::fprintf(stderr, "matsym: the glyph of '%s' came out empty\n", name.c_str());
        cairo_surface_destroy(mask);
        return nullptr;
    }
    return mask;
}
}
