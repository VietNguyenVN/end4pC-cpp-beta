#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace hotcorners {
inline constexpr std::array<std::string_view, 4> keys = {"cornerTL", "cornerTR", "cornerBL", "cornerBR"};

inline std::string prefix(std::string_view screen, std::string_view key) {
    return std::string(screen) + "/" + std::string(key) + "=";
}

inline constexpr std::string_view connectorChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:+";

// One "connector/cornerKey=action" entry: a plain connector name, a known corner key and an action from `actions`.
template<class Actions>
bool validEntry(std::string_view entry, const Actions& actions) {
    const auto slash = entry.find('/'), equal = entry.find('=');
    if (slash == 0 || slash == entry.npos || equal == entry.npos || equal <= slash) return false;
    const auto screen = entry.substr(0, slash), key = entry.substr(slash + 1, equal - slash - 1), action = entry.substr(equal + 1);
    return screen.find_first_not_of(connectorChars) == screen.npos && std::find(keys.begin(), keys.end(), key) != keys.end() &&
           std::find(actions.begin(), actions.end(), action) != actions.end();
}

// Every entry of a comma-separated list must be valid (what `set` and the settings window send).
template<class Actions>
bool validRules(std::string_view rules, const Actions& actions) {
    while (!rules.empty()) {
        const auto end = rules.find(',');
        if (!validEntry(rules.substr(0, end), actions)) return false;
        if (end == rules.npos) break;
        rules.remove_prefix(end + 1);
    }
    return true;
}

// What is read from a config file: the valid entries stay and the bad ones go, so one typo does not cost the user the others.
template<class Actions>
std::string keepValid(std::string_view rules, const Actions& actions) {
    std::string result;
    while (!rules.empty()) {
        const auto end = rules.find(',');
        const auto entry = rules.substr(0, end);
        if (validEntry(entry, actions)) {
            if (!result.empty()) result += ',';
            result += entry;
        }
        if (end == rules.npos) break;
        rules.remove_prefix(end + 1);
    }
    return result;
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
