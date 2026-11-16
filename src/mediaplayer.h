#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace mediaplayer {
struct Snapshot {
    std::string instance, status, browserPid, artwork, url, title;
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
    std::string fields[9];
    size_t start = 0;
    for (int i = 0; i < 8; ++i) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) return false;
        fields[i] = line.substr(start, tab - start);
        start = tab + 1;
    }
    fields[8] = line.substr(start);
    if (!fields[8].empty() && fields[8].back() == '\n') fields[8].pop_back();
    if (fields[0].empty() || fields[0].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos) return false;
    out = {};
    out.instance = fields[0]; out.status = fields[4];
    out.browserPid = fields[5]; out.artwork = fields[6]; out.url = fields[7]; out.title = fields[8];
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

inline std::string coverUrl(const Snapshot& metadata) {
    if (!metadata.artwork.empty()) return metadata.artwork;
    // The bridge may omit artwork until playback is toggled. YouTube's public
    // thumbnail is a fallback for this exact video, not an artist/title search.
    constexpr const char* prefix = "https://music.youtube.com/watch?";
    if (!metadata.url.starts_with(prefix)) return {};
    std::string query = metadata.url.substr(std::char_traits<char>::length(prefix));
    while (!query.empty()) {
        const size_t end = query.find('&');
        const std::string field = query.substr(0, end);
        if (field.starts_with("v=")) {
            const std::string id = field.substr(2);
            if (id.size() == 11 && id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos)
                return "https://i.ytimg.com/vi/" + id + "/hqdefault.jpg";
            return {};
        }
        if (end == std::string::npos) break;
        query.erase(0, end + 1);
    }
    return {};
}
}
