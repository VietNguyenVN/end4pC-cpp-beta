// The audio engine of Player: ffmpeg decodes, PipeWire plays, TagLib reads the tags and the cover. No toolkit, no event loop of its own:
// the window asks it for the position and the state a few times a second.
#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace player {

struct TrackInfo {
    std::string title, artist, album;
    double duration = 0;
    std::vector<unsigned char> cover;  // the embedded picture (JPEG or PNG bytes), empty when there is none
};

// Title, artist, album, length and the embedded cover of a file. Without tags the title is the file name.
TrackInfo readTags(const std::string& path, bool withCover = true);  // without the cover it is several times faster: the queue never needs the pictures

// What the file is made of: shown as small chips.
struct StreamInfo {
    std::string codec;  // mp3, flac, aac, vorbis, opus, pcm_s16le...
    int bitrateKbps = 0, sampleRate = 0, channels = 0;
};

enum class State { Stopped, Playing, Paused };
enum class Output { PipeWire, Null };  // Null: no sound, the samples are consumed by a thread (tests)

class Engine {
public:
    explicit Engine(Output output = Output::PipeWire, double nullSpeed = 1.0);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool open(const std::string& path, bool autoplay = true);  // false: the file could not be read (see error())
    void play();
    void pause();
    void toggle();
    void stop();
    void seek(double seconds);
    void setVolume(float volume);  // 0 to 1: 100% is the level of the file itself (louder would clip)
    float volume() const { return volume_.load(); }

    double position() const;  // seconds of the track that are being heard now
    double duration() const { return duration_.load(); }
    State state() const { return state_.load(); }
    bool takeEnded();  // true once, when the track played to its end
    const std::string& error() const { return error_; }
    const StreamInfo& streamInfo() const { return stream_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<float> volume_{1.0f};
    std::atomic<double> duration_{0};
    std::atomic<State> state_{State::Stopped};
    std::string error_;
    StreamInfo stream_;
};

}  // namespace player
