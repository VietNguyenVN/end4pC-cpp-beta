// The workspaces a bar shows when there is more than one screen. Pure text and numbers, so tests/wsmonitors can run it on its own.
//
// Each bar shows the workspaces of its own screen: a window of `shown` ids that contains the workspace in use there. The first id of the window is
// taken from that workspace, rounded down to a group of `shown` (end-4's bar does the same): with 8 workspaces shown, workspaces 1..8 are one group,
// 9..16 the next. While the workspace in use is inside the first group the bar is what it always was (1, 2, ...).
#pragma once

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

namespace wsmon {

// The first workspace id of the group that has `active` (1 when there is none). `shown` is how many workspaces a bar shows.
inline int groupBase(int active, int shown) {
    if (active <= 0 || shown <= 0) return 1;
    return ((active - 1) / shown) * shown + 1;
}

namespace detail {
// The end of the string that starts at s[i] == '"' (the index of its closing quote), or npos.
inline size_t stringEnd(const std::string& s, size_t i) {
    for (size_t k = i + 1; k < s.size(); k++) {
        if (s[k] == '\\') k++;
        else if (s[k] == '"') return k;
    }
    return std::string::npos;
}
inline std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; out += s[i]; }
        else out += s[i];
    }
    return out;
}
}  // namespace detail

// From the output of `hyprctl monitors -j` (or the socket request j/monitors): the workspace in use on each monitor, by name. A monitor that
// does not say its name or its workspace is left out, and so is a workspace id that is not a number above 0.
inline std::map<std::string, int> activeWorkspaces(const std::string& json) {
    std::map<std::string, int> out;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < json.size(); i++) {
        const char c = json[i];
        if (c == '"') {
            const size_t end = detail::stringEnd(json, i);
            if (end == std::string::npos) break;
            i = end;
        } else if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            if (depth > 0) depth--;
            if (depth == 0 && start != std::string::npos) {
                const std::string obj = json.substr(start, i - start + 1);
                start = std::string::npos;
                const size_t n = obj.find("\"name\": \"");
                const size_t a = obj.find("\"activeWorkspace\": {");
                if (n == std::string::npos || a == std::string::npos) continue;
                const size_t nameFrom = n + 9, nameEnd = detail::stringEnd(obj, nameFrom - 1);
                const size_t idAt = obj.find("\"id\": ", a);
                if (nameEnd == std::string::npos || idAt == std::string::npos || n > a) continue;  // (the name of the monitor comes before its workspace)
                const int id = std::atoi(obj.c_str() + idAt + 6);
                if (id > 0) out[detail::unescape(obj.substr(nameFrom, nameEnd - nameFrom))] = id;
            }
        }
    }
    return out;
}
}  // namespace wsmon
