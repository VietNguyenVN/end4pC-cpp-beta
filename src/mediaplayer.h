#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace mediaplayer {
struct Snapshot {
    std::string instance, status, browserPid, artwork, title;
    double position = 0, duration = 0;
    std::optional<double> volume;
};

inline bool number(const std::string& text, double& result) {
    if (text.empty()) return false;
    char* end = nullptr;
    result = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(result) && result >= 0;
}

inline bool parse(const std::string& line, Snapshot& out) {
    std::string fields[8];
    size_t start = 0;
    for (int i = 0; i < 7; ++i) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) return false;
        fields[i] = line.substr(start, tab - start);
        start = tab + 1;
    }
    fields[7] = line.substr(start);
    if (!fields[7].empty() && fields[7].back() == '\n') fields[7].pop_back();
    if (fields[0].empty() || fields[0].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos) return false;
    out = {};
    out.instance = fields[0]; out.status = fields[4];
    out.browserPid = fields[5]; out.artwork = fields[6]; out.title = fields[7];
    double position, duration, volume;
    if (!number(fields[1], position)) return false;
    out.position = position / 1e6;
    if (number(fields[2], duration)) out.duration = duration / 1e6;
    if (number(fields[3], volume)) out.volume = volume;
    return out.status == "Playing" || out.status == "Paused" || out.status == "Stopped";
}

inline std::string trackTitle(std::string title) {
    constexpr const char* suffix = " | YouTube Music";
    if (title.ends_with(suffix)) title.resize(title.size() - std::char_traits<char>::length(suffix));
    const auto first = title.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return title.substr(first, title.find_last_not_of(" \t\r\n") - first + 1);
}

inline const Snapshot* find(const std::vector<Snapshot>& players, const std::string& instance, const std::string& title) {
    for (const auto& p : players)
        if (p.instance == instance && trackTitle(p.title) == trackTitle(title)) return &p;
    return nullptr;
}

inline const Snapshot* playback(const std::vector<Snapshot>& players, const Snapshot& metadata) {
    // Plasma Browser Integration reads the HTML media element. With YouTube's
    // gapless streams its currentTime/duration can span multiple songs. The
    // browser's native MediaSession MPRIS service has the per-song timeline.
    // Pair them only by browser PID AND matching title, never by availability.
    if (metadata.instance == "plasma-browser-integration" && !metadata.browserPid.empty() &&
        metadata.browserPid.find_first_not_of("0123456789") == std::string::npos && !trackTitle(metadata.title).empty()) {
        const std::string suffix = ".instance" + metadata.browserPid;
        const Snapshot* match = nullptr;
        for (const auto& p : players) {
            if (!p.instance.ends_with(suffix) || trackTitle(p.title) != trackTitle(metadata.title) ||
                (p.status != "Playing" && p.status != "Paused")) continue;
            if (match) return &metadata;  // ambiguous: retain the original source
            match = &p;
        }
        if (match) return match;
    }
    return &metadata;
}
}
