#pragma once
#include "mediaplayer.h"
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <systemd/sd-bus.h>

namespace mediaplayer {
// Called in the detached command child. playerctl selectors also match NAME.*;
// resolving the exact bus name prevents commands from reaching a prefix peer.
inline bool command(const std::string& instance, std::initializer_list<const char*> args) {
    if (instance.empty() || args.size() == 0) return false;
    sd_bus* rawBus = nullptr;
    if (sd_bus_open_user(&rawBus) < 0) return false;
    const std::unique_ptr<sd_bus, decltype(&sd_bus_unref)> bus(rawBus, sd_bus_unref);
    sd_bus_set_method_call_timeout(bus.get(), 1500000);
    const std::string name = "org.mpris.MediaPlayer2." + instance;
    sd_bus_message* rawReply = nullptr;
    if (sd_bus_call_method(bus.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
                           "org.freedesktop.DBus", "GetNameOwner", nullptr, &rawReply,
                           "s", name.c_str()) < 0) return false;
    const std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> ownerReply(rawReply, sd_bus_message_unref);
    const char* owner = nullptr;
    if (sd_bus_message_read(ownerReply.get(), "s", &owner) <= 0) return false;
    // Address this connection for the entire operation. An exiting/replaced
    // player must not redirect the command or trigger service activation.
    constexpr const char* path = "/org/mpris/MediaPlayer2";
    constexpr const char* iface = "org.mpris.MediaPlayer2.Player";
    const std::string verb = *args.begin();
    if (args.size() == 1) {
        const char* method = verb == "play-pause" ? "PlayPause" : verb == "next" ? "Next" :
                             verb == "previous" ? "Previous" : nullptr;
        return method && sd_bus_call_method(bus.get(), owner, path, iface, method, nullptr, nullptr, "") >= 0;
    }
    if (args.size() != 2) return false;
    double value = 0;
    if (!number(*(args.begin() + 1), value)) return false;
    if (verb == "volume")
        return sd_bus_set_property(bus.get(), owner, path, iface, "Volume", nullptr,
                                   "d", std::clamp(value, 0.0, 1.0)) >= 0;
    if (verb != "position" || value >= double(std::numeric_limits<int64_t>::max()) / 1e6) return false;
    rawReply = nullptr;
    if (sd_bus_get_property(bus.get(), owner, path, iface, "Metadata", nullptr, &rawReply, "a{sv}") < 0) return false;
    const std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)> metadata(rawReply, sd_bus_message_unref);
    if (sd_bus_message_enter_container(metadata.get(), 'a', "{sv}") <= 0) return false;
    while (sd_bus_message_enter_container(metadata.get(), 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(metadata.get(), "s", &key) <= 0) return false;
        if (std::string(key) == "mpris:trackid") {
            const char* track = nullptr;
            if (sd_bus_message_enter_container(metadata.get(), 'v', "o") <= 0 ||
                sd_bus_message_read(metadata.get(), "o", &track) <= 0 ||
                std::string(track) == "/org/mpris/MediaPlayer2/TrackList/NoTrack") return false;
            return sd_bus_call_method(bus.get(), owner, path, iface, "SetPosition", nullptr, nullptr,
                                      "ox", track, static_cast<int64_t>(value * 1e6)) >= 0;
        }
        if (sd_bus_message_skip(metadata.get(), "v") < 0 || sd_bus_message_exit_container(metadata.get()) < 0) return false;
    }
    return false;
}
}
