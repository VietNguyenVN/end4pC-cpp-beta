// MPRIS for Play3r. See mpris.h.
#include "mpris.h"

#include <poll.h>
#include <systemd/sd-bus.h>

#include <cmath>
#include <cstring>

namespace mpris {

namespace {
constexpr const char* kPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRoot = "org.mpris.MediaPlayer2";
constexpr const char* kPlayer = "org.mpris.MediaPlayer2.Player";
}  // namespace

struct Service::Impl {
    sd_bus* bus = nullptr;
    sd_bus_slot* rootSlot = nullptr;
    sd_bus_slot* playerSlot = nullptr;
    Control control;
    State sent;          // what the others were told last
    bool sentOnce = false;
};

namespace {
Service::Impl* g_impl = nullptr;

State current() { return g_impl && g_impl->control.state ? g_impl->control.state() : State{}; }

// ---- org.mpris.MediaPlayer2 ---------------------------------------------------------------------------------------------------------------
int rootRaise(sd_bus_message* m, void*, sd_bus_error*) { if (g_impl->control.raise) g_impl->control.raise(); return sd_bus_reply_method_return(m, nullptr); }
int rootQuit(sd_bus_message* m, void*, sd_bus_error*) { if (g_impl->control.quit) g_impl->control.quit(); return sd_bus_reply_method_return(m, nullptr); }
int getTrue(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", 1); }
int getFalse(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", 0); }
int getIdentity(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "s", "Play3r"); }
int getDesktopEntry(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "s", "end4pC-play3r"); }
int getSchemes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "as", 1, "file"); }
int getMimes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "as", 6, "audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4", "audio/x-wav", "audio/opus");
}

const sd_bus_vtable kRootVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Raise", "", "", rootRaise, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", rootQuit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("CanQuit", "b", getTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", getTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", getFalse, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", getIdentity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("DesktopEntry", "s", getDesktopEntry, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", getSchemes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", getMimes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

// ---- org.mpris.MediaPlayer2.Player --------------------------------------------------------------------------------------------------------
#define METHOD0(NAME, FIELD) \
    int NAME(sd_bus_message* m, void*, sd_bus_error*) { if (g_impl->control.FIELD) g_impl->control.FIELD(); return sd_bus_reply_method_return(m, nullptr); }
METHOD0(mPlay, play)
METHOD0(mPause, pause)
METHOD0(mPlayPause, playPause)
METHOD0(mStop, stop)
METHOD0(mNext, next)
METHOD0(mPrevious, previous)
#undef METHOD0

int mSeek(sd_bus_message* m, void*, sd_bus_error*) {
    int64_t offset = 0;
    if (sd_bus_message_read(m, "x", &offset) < 0) return -EINVAL;
    if (g_impl->control.seekBy) g_impl->control.seekBy(offset / 1e6);
    return sd_bus_reply_method_return(m, nullptr);
}
int mSetPosition(sd_bus_message* m, void*, sd_bus_error*) {
    const char* track = nullptr;
    int64_t position = 0;
    if (sd_bus_message_read(m, "ox", &track, &position) < 0) return -EINVAL;
    if (g_impl->control.seekTo) g_impl->control.seekTo(position / 1e6);
    return sd_bus_reply_method_return(m, nullptr);
}
int mOpenUri(sd_bus_message* m, void*, sd_bus_error*) {
    const char* uri = nullptr;
    if (sd_bus_message_read(m, "s", &uri) < 0) return -EINVAL;
    if (g_impl->control.openUri && uri) g_impl->control.openUri(uri);
    return sd_bus_reply_method_return(m, nullptr);
}

int pStatus(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "s", current().status.c_str()); }
int pLoop(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    const int l = current().loop;
    return sd_bus_message_append(reply, "s", l == 2 ? "Track" : l == 1 ? "Playlist" : "None");
}
int pSetLoop(sd_bus*, const char*, const char*, const char*, sd_bus_message* value, void*, sd_bus_error*) {
    const char* v = nullptr;
    if (sd_bus_message_read(value, "s", &v) < 0 || !v) return -EINVAL;
    if (g_impl->control.setLoop) g_impl->control.setLoop(!std::strcmp(v, "Track") ? 2 : !std::strcmp(v, "Playlist") ? 1 : 0);
    return 0;
}
int pRate(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "d", 1.0); }
int pShuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", current().shuffle ? 1 : 0); }
int pSetShuffle(sd_bus*, const char*, const char*, const char*, sd_bus_message* value, void*, sd_bus_error*) {
    int v = 0;
    if (sd_bus_message_read(value, "b", &v) < 0) return -EINVAL;
    if (g_impl->control.setShuffle) g_impl->control.setShuffle(v != 0);
    return 0;
}
int pVolume(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "d", static_cast<double>(current().volume)); }
int pSetVolume(sd_bus*, const char*, const char*, const char*, sd_bus_message* value, void*, sd_bus_error*) {
    double v = 0;
    if (sd_bus_message_read(value, "d", &v) < 0) return -EINVAL;
    if (g_impl->control.setVolume) g_impl->control.setVolume(std::max(0.0, std::min(1.0, v)));
    return 0;
}
int pPosition(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "x", static_cast<int64_t>(current().position * 1e6)); }
int pMin(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "d", 1.0); }
int pCanNext(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", current().canNext ? 1 : 0); }
int pCanPrev(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", current().canPrevious ? 1 : 0); }
int pCanSeek(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) { return sd_bus_message_append(reply, "b", current().hasTrack ? 1 : 0); }

int pMetadata(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    const State s = current();
    int r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) return r;
    char trackId[64];
    std::snprintf(trackId, sizeof trackId, "/org/play3r/track/%llu", static_cast<unsigned long long>(s.trackId));
    auto entry = [&](const char* key, const char* type, auto&&... value) {
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", key);
        sd_bus_message_append(reply, "v", type, value...);
        sd_bus_message_close_container(reply);
    };
    entry("mpris:trackid", "o", s.hasTrack ? trackId : "/org/mpris/MediaPlayer2/TrackList/NoTrack");
    if (s.hasTrack) {
        entry("mpris:length", "x", static_cast<int64_t>(s.length * 1e6));
        if (!s.artUrl.empty()) entry("mpris:artUrl", "s", s.artUrl.c_str());
        entry("xesam:title", "s", s.title.c_str());
        entry("xesam:album", "s", s.album.c_str());
        entry("xesam:url", "s", s.url.c_str());
        sd_bus_message_open_container(reply, 'e', "sv");
        sd_bus_message_append(reply, "s", "xesam:artist");
        sd_bus_message_open_container(reply, 'v', "as");
        sd_bus_message_append(reply, "as", s.artist.empty() ? 0 : 1, s.artist.c_str());
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
    }
    return sd_bus_message_close_container(reply);
}

const sd_bus_vtable kPlayerVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Next", "", "", mNext, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", mPrevious, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", mPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PlayPause", "", "", mPlayPause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", mStop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", "", "", mPlay, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", mSeek, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", mSetPosition, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", "", mOpenUri, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("Seeked", "x", 0),
    SD_BUS_PROPERTY("PlaybackStatus", "s", pStatus, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("LoopStatus", "s", pLoop, pSetLoop, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Rate", "d", pRate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_WRITABLE_PROPERTY("Shuffle", "b", pShuffle, pSetShuffle, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", pMetadata, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_WRITABLE_PROPERTY("Volume", "d", pVolume, pSetVolume, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", pPosition, 0, 0),
    SD_BUS_PROPERTY("MinimumRate", "d", pMin, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("MaximumRate", "d", pMin, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoNext", "b", pCanNext, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanGoPrevious", "b", pCanPrev, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanPlay", "b", getTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPause", "b", getTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSeek", "b", pCanSeek, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("CanControl", "b", getTrue, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};
}  // namespace

Service::Service() : impl_(new Impl) {}

Service::~Service() {
    if (impl_->playerSlot) sd_bus_slot_unref(impl_->playerSlot);
    if (impl_->rootSlot) sd_bus_slot_unref(impl_->rootSlot);
    if (impl_->bus) { sd_bus_flush_close_unref(impl_->bus); }
    if (g_impl == impl_.get()) g_impl = nullptr;
}

bool Service::start(const Control& control) {
    Impl& s = *impl_;
    s.control = control;
    g_impl = &s;
    if (sd_bus_open_user(&s.bus) < 0) { s.bus = nullptr; return false; }
    if (sd_bus_add_object_vtable(s.bus, &s.rootSlot, kPath, kRoot, kRootVtable, nullptr) < 0 ||
        sd_bus_add_object_vtable(s.bus, &s.playerSlot, kPath, kPlayer, kPlayerVtable, nullptr) < 0) return false;
    // A second copy of the program would not own this name: the first one keeps it (the lock of the program stops a second copy anyway).
    if (sd_bus_request_name(s.bus, "org.mpris.MediaPlayer2.play3r", 0) < 0) return false;
    return true;
}

int Service::fd() const { return impl_->bus ? sd_bus_get_fd(impl_->bus) : -1; }
short Service::events() const {
    if (!impl_->bus) return 0;
    const int e = sd_bus_get_events(impl_->bus);
    return e < 0 ? POLLIN : static_cast<short>(e);
}
int Service::timeoutMs() const {
    if (!impl_->bus) return -1;
    uint64_t usec = 0;
    if (sd_bus_get_timeout(impl_->bus, &usec) < 0 || usec == UINT64_MAX) return -1;
    return static_cast<int>(std::min<uint64_t>(usec / 1000 + 1, 60000));
}
void Service::process() {
    if (!impl_->bus) return;
    for (int i = 0; i < 64; i++) {
        const int r = sd_bus_process(impl_->bus, nullptr);
        if (r <= 0) break;
    }
}

void Service::publish() {
    Impl& s = *impl_;
    if (!s.bus) return;
    const State now = current();
    const State& old = s.sent;
    const bool first = !s.sentOnce;
    if (first || now.status != old.status) sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "PlaybackStatus", nullptr);
    if (first || now.trackId != old.trackId || now.title != old.title || now.artist != old.artist || now.album != old.album || now.artUrl != old.artUrl || std::abs(now.length - old.length) > 0.5 || now.hasTrack != old.hasTrack)
        sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "Metadata", "CanSeek", nullptr);
    if (first || std::abs(now.volume - old.volume) > 0.004) sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "Volume", nullptr);
    if (first || now.shuffle != old.shuffle) sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "Shuffle", nullptr);
    if (first || now.loop != old.loop) sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "LoopStatus", nullptr);
    if (first || now.canNext != old.canNext || now.canPrevious != old.canPrevious) sd_bus_emit_properties_changed(s.bus, kPath, kPlayer, "CanGoNext", "CanGoPrevious", nullptr);
    s.sent = now;
    s.sentOnce = true;
}

void Service::seeked(double position) {
    if (impl_->bus) sd_bus_emit_signal(impl_->bus, kPath, kPlayer, "Seeked", "x", static_cast<int64_t>(position * 1e6));
}

}  // namespace mpris
