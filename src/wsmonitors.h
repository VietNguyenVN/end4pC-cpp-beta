// The workspaces a bar shows when there is more than one screen. Pure text and numbers, so tests/wsmonitors can run it on its own.
//
// Each bar shows the workspaces of its own screen: a window of `shown` ids that contains the workspace in use there. The first id of the window is
// taken from that workspace, rounded down to a group of `shown` (end-4's bar does the same): with 8 workspaces shown, workspaces 1..8 are one group,
// 9..16 the next. While the workspace in use is inside the first group the bar is what it always was (1, 2, ...).
#pragma once

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace wsmon {

// The first workspace id of the group that has `active` (1 when there is none). `shown` is how many workspaces a bar shows.
inline int groupBase(int active, int shown) {
    if (active <= 0 || shown <= 0) return 1;
    return ((active - 1) / shown) * shown + 1;
}

namespace detail {
// The end of the string that starts at s[i] == '"' (the index of its closing quote), or npos.
inline size_t stringEnd(std::string_view s, size_t i) {
    for (size_t k = i + 1; k < s.size(); k++) {
        if (s[k] == '\\') k++;
        else if (s[k] == '"') return k;
    }
    return std::string::npos;
}
inline std::string unescape(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; out += s[i]; }
        else out += s[i];
    }
    return out;
}
// Read direct fields only, so a nested workspace name cannot be mistaken for the connector.
inline std::string_view field(std::string_view obj, std::string_view key) {
    int depth = 0;
    for (size_t i = 0; i < obj.size(); ++i) {
        if (obj[i] == '{' || obj[i] == '[') { ++depth; continue; }
        if (obj[i] == '}' || obj[i] == ']') { --depth; continue; }
        if (obj[i] != '"') continue;
        const size_t end = stringEnd(obj, i);
        if (end == obj.npos) break;
        if (depth == 1 && obj.substr(i + 1, end - i - 1) == key) {
            const size_t colon = obj.find_first_not_of(" \t\r\n", end + 1);
            if (colon != obj.npos && obj[colon] == ':') {
                const size_t value = obj.find_first_not_of(" \t\r\n", colon + 1);
                if (value != obj.npos) return obj.substr(value);
            }
        }
        i = end;
    }
    return {};
}

inline std::string stringField(std::string_view obj, std::string_view key) {
    const auto value = field(obj, key);
    if (value.empty() || value.front() != '"') return {};
    const size_t end = stringEnd(value, 0);
    return end == value.npos ? std::string{} : unescape(value.substr(1, end - 1));
}

inline std::string_view objectField(std::string_view obj, std::string_view key) {
    const auto value = field(obj, key);
    if (value.empty() || value.front() != '{') return {};
    int depth = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '"') {
            i = stringEnd(value, i);
            if (i == value.npos) break;
        } else if (value[i] == '{') ++depth;
        else if (value[i] == '}' && --depth == 0) return value.substr(0, i + 1);
    }
    return {};
}
template<class Visitor>
void forObjects(std::string_view json, Visitor visit) {
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') {
            i = detail::stringEnd(json, i);
            if (i == json.npos) break;
        } else if (c == '{') {
            if (depth++ == 0) start = i;
        } else if (c == '}' && depth > 0 && --depth == 0) {
            visit(json.substr(start, i - start + 1));
        }
    }
}

inline int intField(std::string_view obj, std::string_view key) {
    const auto value = field(obj, key);
    int n = 0;
    if (value.empty() || std::from_chars(value.data(), value.data() + value.size(), n).ec != std::errc{}) return 0;
    return n;
}
inline bool isSpecial(std::string_view name) { return name == "special" || name.starts_with("special:"); }
inline std::string specialLabel(std::string name) {
    if (name.starts_with("special:")) name.erase(0, 8);
    return name.empty() ? "special" : name;
}
}  // namespace detail

struct MonitorWorkspaces {
    std::map<std::string, int> active;
    std::map<std::string, std::string> special;
    std::set<std::string> openSpecialKeys;
    std::string focusedConnector;
    const std::string* focusedSpecial(std::string_view connector) const {
        if (focusedConnector.empty() || connector != focusedConnector) return nullptr;
        const auto it = special.find(focusedConnector);
        return it == special.end() ? nullptr : &it->second;
    }
    bool operator==(const MonitorWorkspaces&) const = default;
};

// Read both workspace kinds from the same Hyprland monitor snapshot.
inline MonitorWorkspaces monitorWorkspaces(std::string_view json) {
    MonitorWorkspaces out;
    detail::forObjects(json, [&](std::string_view obj) {
        const std::string connector = detail::stringField(obj, "name");
        if (connector.empty()) return;
        if (detail::field(obj, "focused").starts_with("true")) out.focusedConnector = connector;
        const int id = detail::intField(detail::objectField(obj, "activeWorkspace"), "id");
        if (id > 0) out.active.emplace(connector, id);
        auto name = detail::stringField(detail::objectField(obj, "specialWorkspace"), "name");
        if (!name.empty()) {
            out.openSpecialKeys.insert(detail::isSpecial(name) ? name : "special:" + name);
            out.special.emplace(connector, detail::specialLabel(std::move(name)));
        }
    });
    return out;
}

struct OccupiedWorkspaces {
    std::set<int> ids;
    std::set<std::string> special;
};

inline OccupiedWorkspaces occupiedWorkspaces(std::string_view json) {
    OccupiedWorkspaces out;
    detail::forObjects(json, [&](std::string_view obj) {
        if (detail::intField(obj, "windows") <= 0) return;
        const int id = detail::intField(obj, "id");
        if (id != 0) out.ids.insert(id);
        auto name = detail::stringField(obj, "name");
        if (detail::isSpecial(name)) out.special.insert(std::move(name));
    });
    return out;
}

inline size_t closedSpecialCount(const MonitorWorkspaces& monitors, const std::set<std::string>& occupied) {
    return std::count_if(occupied.begin(), occupied.end(), [&](const auto& name) {
        return !monitors.openSpecialKeys.contains(name);
    });
}

inline std::map<std::string, int> activeWorkspaces(const std::string& json) {
    return monitorWorkspaces(json).active;
}
}  // namespace wsmon
