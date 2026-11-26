// The name a person is shown with, and the host name of the computer: the rules, with no drawing and no system calls, so tests/identity can run them alone.
//
// The display name is what the bar, the control center, the desktop card, Settings and the lock screen call the person. When it is empty they use the user
// name, always. It is kept in the config file between quotes, so it can never hold a quote, a backslash or a control character.
#pragma once

#include <cctype>
#include <string>

namespace identity {

constexpr size_t kMaxDisplayName = 64;  // bytes
constexpr size_t kMaxHostname = 64;     // what hostnamectl takes

// UTF-8 as it comes from a text field: invalid bytes go, a cut character goes, the rest stays.
inline std::string validUtf8(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= in.size();
        for (size_t k = 1; ok && k < n; k++) ok = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (ok && n == 2) ok = c >= 0xC2;
        if (!ok) { i++; continue; }
        out.append(in, i, n);
        i += n;
    }
    return out;
}

// What is kept of a typed display name: no control characters, quotes or backslashes, runs of spaces made one, no spaces at the ends, at most
// kMaxDisplayName bytes (never in the middle of a character).
inline std::string cleanDisplayName(const std::string& typed) {
    std::string out;
    bool space = false;
    for (char ch : validUtf8(typed)) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\' || c == 127 || (c < 32 && c != '\t')) continue;
        if (c == ' ' || c == '\t') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += ch;
    }
    if (out.size() > kMaxDisplayName) {
        size_t cut = kMaxDisplayName;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
        out.resize(cut);
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out;
}

// The name to show: the display name when there is one, else the user name.
inline std::string displayName(const std::string& configured, const std::string& user) {
    const std::string clean = cleanDisplayName(configured);
    return clean.empty() ? user : clean;
}

// A host name hostnamectl accepts and the network can use: labels of letters, digits and hyphens (not at either end of a label) joined by single dots,
// at most kMaxHostname bytes, nothing else.
inline bool validHostname(const std::string& name) {
    if (name.empty() || name.size() > kMaxHostname) return false;
    size_t label = 0;
    for (size_t i = 0; i < name.size(); i++) {
        const char c = name[i];
        if (c == '.') {
            if (label == 0 || name[i - 1] == '-') return false;
            label = 0;
        } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
            if (c == '-' && label == 0) return false;
            if (++label > 63) return false;
        } else {
            return false;
        }
    }
    return label > 0 && name.back() != '-';
}
}  // namespace identity
