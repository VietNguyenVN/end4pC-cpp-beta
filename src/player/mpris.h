// MPRIS for Play3r: the standard D-Bus interface that media players speak (org.mpris.MediaPlayer2). It is how the player of the bar, the launcher,
// the media keys and playerctl see Play3r and control it. The window loop gives it a descriptor to poll and calls process() when it is readable.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mpris {

struct State {
    std::string status = "Stopped";  // Playing, Paused or Stopped
    bool hasTrack = false;
    uint64_t trackId = 0;            // changes with every track
    std::string title, artist, album, artUrl, url;
    double length = 0, position = 0;  // seconds
    double volume = 1;                // 0 to 1
    bool shuffle = false;
    int loop = 0;                     // 0 none, 1 playlist, 2 track
    bool canNext = false, canPrevious = false;
};

struct Control {
    std::function<State()> state;
    std::function<void()> play, pause, playPause, stop, next, previous, raise, quit;
    std::function<void(double)> seekBy, seekTo, setVolume;  // seconds, seconds from the start, 0 to 1
    std::function<void(bool)> setShuffle;
    std::function<void(int)> setLoop;
    std::function<void(const std::string&)> openUri;
};

class Service {
public:
    struct Impl;  // (public so the D-Bus callbacks of the .cpp can name it)
    Service();
    ~Service();
    bool start(const Control& control);  // false: no session bus (the player works without it)
    int fd() const;                      // poll this for POLLIN (and POLLOUT when events() says so)
    short events() const;
    int timeoutMs() const;               // -1: none
    void process();
    // Tell the others what changed since the last call (compares with the last State it sent).
    void publish();
    void seeked(double position);        // the position jumped (a seek)
private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mpris
