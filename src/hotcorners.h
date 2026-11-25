#pragma once

#include <array>
#include <string>
#include <string_view>

namespace hotcorners {
inline constexpr std::array<std::string_view, 4> keys = {"cornerTL", "cornerTR", "cornerBL", "cornerBR"};

inline std::string prefix(std::string_view screen, std::string_view key) {
    return std::string(screen) + "/" + std::string(key) + "=";
}

inline std::string_view overrideAction(std::string_view rules, std::string_view screen, std::string_view key) {
    if (screen.empty() || rules.empty()) return {};
    const auto match = prefix(screen, key);
    while (!rules.empty()) {
        const auto end = rules.find(',');
        const auto entry = rules.substr(0, end);
        if (entry.starts_with(match)) return entry.substr(match.size());
        if (end == rules.npos) break;
        rules.remove_prefix(end + 1);
    }
    return {};
}

inline std::string withAction(std::string_view rules, std::string_view screen,
                              std::string_view key, std::string_view action) {
    const auto match = prefix(screen, key);
    std::string result;
    while (!rules.empty()) {
        const auto end = rules.find(',');
        const auto entry = rules.substr(0, end);
        if (!entry.empty() && !entry.starts_with(match)) {
            if (!result.empty()) result += ',';
            result += entry;
        }
        if (end == rules.npos) break;
        rules.remove_prefix(end + 1);
    }
    if (!action.empty()) {
        if (!result.empty()) result += ',';
        result += match;
        result += action;
    }
    return result;
}
}
