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

inline void applyShellFont(const std::string& family, int textWeight = 450, int titleWeight = 550) {
    static std::string current = "\x01";
    const std::string key = family + "|" + std::to_string(textWeight) + "|" + std::to_string(titleWeight);
    if (key == current) return;
    current = key;
    FcInitReinitialize();
    if (!family.empty() && validFontFamily(family)) {
        std::string xml = "<?xml version=\"1.0\"?><fontconfig>";
        for (const char* alias : {"Sans", "sans-serif", "sans"})
            xml += std::string("<match target=\"pattern\"><test name=\"family\" qual=\"any\"><string>") + alias +
                   "</string></test><edit name=\"family\" mode=\"prepend\" binding=\"strong\"><string>" + family + "</string></edit></match>";
        // The weights of the variable font (end-4's defaults: 450 for the normal text, 550 for bold and titles; Settings changes them).
        // (Pattern stage: it sees the weight that was asked for; a font with no wght axis ignores the variation.)
        const std::string fam = "<test name=\"family\" qual=\"any\"><string>" + family + "</string></test>";
        xml += "<match target=\"pattern\">" + fam + "<test name=\"weight\" compare=\"less_eq\"><const>regular</const></test><edit name=\"fontvariations\" mode=\"assign\"><string>wght=" + std::to_string(textWeight) + "</string></edit></match>";
        xml += "<match target=\"pattern\">" + fam + "<test name=\"weight\" compare=\"more_eq\"><const>bold</const></test><edit name=\"fontvariations\" mode=\"assign\"><string>wght=" + std::to_string(titleWeight) + "</string></edit></match>";
        xml += "</fontconfig>";
        FcConfigParseAndLoadFromMemory(FcConfigGetCurrent(), reinterpret_cast<const FcChar8*>(xml.c_str()), FcTrue);
    }
    if (matsym::fontsAdded()) matsym::addFontFiles();
    if (PangoFontMap* map = pango_cairo_font_map_get_default())
        if (PANGO_IS_FC_FONT_MAP(map)) pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
}

inline int fontWeightFromConfig(const std::string& path, const char* key, int fallback) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    const size_t k = text.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return fallback;
    const size_t colon = text.find(':', k);
    return colon == std::string::npos ? fallback : std::atoi(text.c_str() + colon + 1) > 0 ? std::atoi(text.c_str() + colon + 1) : fallback;
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
