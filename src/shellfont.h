#pragma once
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include "matsym.h"
#include <fstream>
#include <sstream>
#include <string>

inline bool validFontFamily(const std::string& f) {
    if (f.size() > 60) return false;
    for (const char c : f)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '.' && c != '_' && c != '+') return false;
    return true;
}

inline void applyShellFont(const std::string& family) {
    static std::string current = "\x01";
    if (family == current) return;
    current = family;
    FcInitReinitialize();
    if (!family.empty() && validFontFamily(family)) {
        std::string xml = "<?xml version=\"1.0\"?><fontconfig>";
        for (const char* alias : {"Sans", "sans-serif", "sans"})
            xml += std::string("<match target=\"pattern\"><test name=\"family\" qual=\"any\"><string>") + alias +
                   "</string></test><edit name=\"family\" mode=\"prepend\" binding=\"strong\"><string>" + family + "</string></edit></match>";
        xml += "</fontconfig>";
        FcConfigParseAndLoadFromMemory(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(xml.c_str()), FcTrue);
    }
    if (matsym::fontsAdded()) matsym::addFontFiles();
    if (PangoFontMap* map = pango_cairo_font_map_get_default())
        if (PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
}

inline std::string fontFamilyFromConfig(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    const size_t k = text.find("\"fontFamily\"");
    if (k == std::string::npos) return "";
    const size_t q1 = text.find('"', text.find(':', k) + 1), q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    return q2 == std::string::npos ? "" : text.substr(q1 + 1, q2 - q1 - 1);
}
