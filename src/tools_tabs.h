#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace tools_tabs {

// Order matches the original TOOLS_PAGE indices.
enum Tab { Translator, Notes, Freeform, Converter, Video, Units, Timers, Stopwatch, Reminders, Keybinds, Autostart, Phone, Count };
struct TabInfo { const char* id; const char* label; };
inline constexpr std::array<TabInfo, Count> tabs{{
    {"translator", "Translator"},
    {"notes", "Notes"},
    {"freeform", "Freeform"},
    {"converter", "Image Converter"},
    {"video", "Video Editor"},
    {"units", "Units"},
    {"timers", "Timers"},
    {"stopwatch", "Stopwatch"},
    {"reminders", "Reminders"},
    {"keybinds", "Keybinds"},
    {"autostart", "Autostart"},
    {"phone", "Phone"},
}};

using Order = std::array<int, Count>;

inline Order parseOrder(std::string_view list) {
    Order order{};
    std::array<bool, Count> seen{};
    size_t next = 0;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const auto id = list.substr(0, comma);
        for (int tab = 0; tab < Count; ++tab) {
            if (id == tabs[tab].id && !seen[tab]) {
                order[next++] = tab;
                seen[tab] = true;
                break;
            }
        }
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
    }
    for (int tab = 0; tab < Count; ++tab)
        if (!seen[tab]) order[next++] = tab;
    return order;
}

inline std::string formatOrder(const Order& order) {
    std::string list;
    for (int tab : order) {
        if (!list.empty()) list += ',';
        list += tabs[tab].id;
    }
    return list;
}

inline void moveTab(Order& order, int from, int to) {
    if (from < to) std::rotate(order.begin() + from, order.begin() + from + 1, order.begin() + to + 1);
    else if (to < from) std::rotate(order.begin() + to, order.begin() + from, order.begin() + from + 1);
}

inline bool isHidden(std::string_view list, std::string_view id) {
    while (!list.empty()) {
        const auto comma = list.find(',');
        if (list.substr(0, comma) == id) return true;
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
    }
    return false;
}

}  // namespace tools_tabs
