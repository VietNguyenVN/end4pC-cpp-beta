// Translations for the shell, the Settings window and the lock screen (one header, no library).
//
// A text is its English sentence. Right before a text is drawn it is looked up in the catalog of the chosen language
// (assets/i18n/<lang>.json, a flat {"English": "translated"} object); without a translation the English one is drawn.
// So code never wraps its strings: this header turns every pango_layout_set_text() into a lookup (see the #define at the end).
// A catalog in ~/.local/share/end4pC/i18n/<lang>.json wins over the one in the project, so a user can fix or add a language.
#pragma once
#include <pango/pango.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace i18n {
inline std::unordered_map<std::string, std::string>& catalog() {
    static std::unordered_map<std::string, std::string> c;
    return c;
}
inline std::vector<const std::pair<const std::string, std::string>*>& prefixes() {
    static std::vector<const std::pair<const std::string, std::string>*> v;
    return v;
}
inline std::string& current() {
    static std::string s = "en";
    return s;
}

inline void appendUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
}

inline void readFile(const std::string& path, std::unordered_map<std::string, std::string>& out) {
    std::ifstream f(path);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string t = ss.str();
    size_t i = 0;
    auto str = [&](std::string& s) -> bool {
        while (i < t.size() && t[i] != '"') { if (t[i] == '}') return false; i++; }
        if (i >= t.size()) return false;
        i++;
        s.clear();
        while (i < t.size() && t[i] != '"') {
            if (t[i] != '\\') { s += t[i++]; continue; }
            i++;
            if (i >= t.size()) break;
            const char c = t[i++];
            if (c == 'n') s += '\n';
            else if (c == 't') s += '\t';
            else if (c == 'u' && i + 4 <= t.size()) {
                unsigned cp = static_cast<unsigned>(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    const unsigned lo = static_cast<unsigned>(std::strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16));
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(s, cp);
            } else s += c;
        }
        i++;
        return true;
    };
    std::string k, v;
    while (str(k)) {
        while (i < t.size() && t[i] != ':') i++;
        if (!str(v)) break;
        if (!v.empty() && v != k) out[k] = v;
    }
}

inline bool exists(const std::string& p) { return std::ifstream(p).good(); }

// "es_EC.UTF-8" -> "es-EC"
inline std::string normalize(std::string s) {
    s = s.substr(0, s.find_first_of(".@"));
    std::replace(s.begin(), s.end(), '_', '-');
    return s;
}

inline std::string systemLanguage() {
    for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* e = std::getenv(v);
        if (e && *e && std::strcmp(e, "C") != 0 && std::strcmp(e, "POSIX") != 0) return normalize(e);
    }
    return "en";
}

inline std::string findFile(const std::string& dir, const std::string& tag) {
    if (dir.empty() || tag.empty()) return "";
    if (exists(dir + "/" + tag + ".json")) return dir + "/" + tag + ".json";
    const std::string base = tag.substr(0, tag.find('-'));
    if (exists(dir + "/" + base + ".json")) return dir + "/" + base + ".json";
    static const char* const regions[] = {"-BR", "-ES", "-UA", "-Hans", "-GB", "-Latn", "-Hant"};
    for (const char* r : regions)
        if (exists(dir + "/" + base + r + ".json")) return dir + "/" + base + r + ".json";
    return "";
}

inline void load(const std::string& language, const std::string& assetsDir, const std::string& userDir) {
    catalog().clear();
    prefixes().clear();
    const std::string tag = language.empty() ? systemLanguage() : normalize(language);
    current() = tag;
    if (tag.empty() || tag == "en" || tag.rfind("en-US", 0) == 0) return;
    const std::string a = findFile(assetsDir + "/i18n", tag), u = findFile(userDir, tag);
    if (!a.empty()) readFile(a, catalog());
    if (!u.empty()) readFile(u, catalog());
    for (const auto& kv : catalog())
        if (kv.first.back() == ' ') prefixes().push_back(&kv);
}

inline const char* tr(const char* s) {
    auto& c = catalog();
    if (c.empty() || !s || !*s) return s;
    const auto it = c.find(s);
    if (it != c.end()) return it->second.c_str();
    static thread_local std::string joined;
    const size_t n = std::strlen(s);
    for (const auto* kv : prefixes()) {
        const std::string& k = kv->first;
        if (k.size() < n && s[0] == k[0] && std::strncmp(s, k.c_str(), k.size()) == 0) {
            joined = kv->second + (s + k.size());
            return joined.c_str();
        }
    }
    return s;
}

inline size_t strftime(char* out, size_t max, const char* fmt, const std::tm* tm) {
    std::string f;
    for (const char* c = fmt; *c; c++) {
        if (*c == '%' && (c[1] == 'a' || c[1] == 'A' || c[1] == 'b' || c[1] == 'B')) {
            char name[40];
            const char spec[3] = {'%', c[1], 0};
            std::strftime(name, sizeof name, spec, tm);
            f += tr(name);
            c++;
        } else if (*c == '%' && c[1]) { f += *c; f += *++c; }
        else f += *c;
    }
    return std::strftime(out, max, f.c_str(), tm);
}
}

inline void i18nSetText(PangoLayout* l, const char* s, int len) {
    if (len < 0) pango_layout_set_text(l, i18n::tr(s), -1);
    else pango_layout_set_text(l, s, len);
}
#define pango_layout_set_text i18nSetText
