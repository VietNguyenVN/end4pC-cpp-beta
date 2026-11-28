// The audio engine of Play3r without a sound card: a real file is decoded, the position runs, a seek lands where it should and the end is
// reported. Needs one audio file (the first argument, or any .mp3 of ~/Music); skips with exit 0 when there is none.
#include "../../src/player/engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

using namespace player;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string findFile(int argc, char** argv) {
    if (argc > 1) return argv[1];
    const char* home = std::getenv("HOME");
    if (!home) return "";
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(std::string(home) + "/Music", ec))
        if (e.path().extension() == ".mp3" || e.path().extension() == ".flac" || e.path().extension() == ".ogg") return e.path().string();
    return "";
}

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    const std::string file = findFile(argc, argv);
    if (file.empty()) { std::printf("no audio file to test with: skipped\n"); return 0; }
    std::printf("file: %s\n", file.c_str());
    const TrackInfo tags = readTags(file);
    std::printf("tags: '%s' / '%s' / '%s', %.1f s, cover %zu bytes\n", tags.title.c_str(), tags.artist.c_str(), tags.album.c_str(), tags.duration, tags.cover.size());
    CHECK(!tags.title.empty(), "no title");
    CHECK(tags.duration > 5, "duration %.1f", tags.duration);

    Engine e(Output::Null, 8.0);  // the clock runs 8 times faster than real time
    CHECK(!e.open("/nonexistent/file.mp3"), "a missing file opened");
    CHECK(e.state() == State::Stopped, "state after a failed open");
    CHECK(e.open(file, true), "open failed: %s", e.error().c_str());
    CHECK(e.state() == State::Playing, "not playing");
    CHECK(std::abs(e.duration() - tags.duration) < 2, "engine duration %.1f vs tags %.1f", e.duration(), tags.duration);

    sleepMs(1000);
    const double p1 = e.position();
    std::printf("after 1 s at 8x: position %.2f\n", p1);
    CHECK(p1 > 3 && p1 < 12, "position %.2f is not about 8 s", p1);

    e.pause();
    const double paused = e.position();
    sleepMs(300);
    CHECK(std::abs(e.position() - paused) < 0.2, "the position moved while paused");
    e.play();

    const double target = std::min(tags.duration * 0.5, tags.duration - 30);
    e.seek(target);
    CHECK(std::abs(e.position() - target) < 0.5, "right after the seek: %.2f vs %.2f", e.position(), target);
    sleepMs(500);
    const double after = e.position();
    std::printf("seek to %.1f, 0.5 s later %.2f\n", target, after);
    CHECK(after >= target && after < target + 10, "after the seek the position is %.2f (target %.1f)", after, target);

    e.seek(tags.duration - 2);  // near the end: it must end by itself
    bool ended = false;
    for (int i = 0; i < 100 && !ended; i++) { sleepMs(100); ended = e.takeEnded(); }
    CHECK(ended, "the end was not reported");
    CHECK(e.state() == State::Stopped, "state after the end");

    CHECK(e.open(file, false), "reopen failed");
    CHECK(e.state() == State::Paused, "autoplay off should leave it paused");
    e.setVolume(2.0f);
    CHECK(e.volume() <= 1.0f, "volume not clamped to 100%%");

    if (argc > 2 && std::string(argv[2]) == "pipewire") {  // by hand: really plays 3 seconds, very quietly, to see the stream in PipeWire
        Engine real(Output::PipeWire);
        real.setVolume(0.2f);
        CHECK(real.open(file, true), "PipeWire open failed: %s", real.error().c_str());
        sleepMs(1500);
        const double a = real.position();
        std::system("pactl list sink-inputs short 2>/dev/null | head -3");
        sleepMs(1500);
        const double b = real.position();
        std::printf("PipeWire: %.2f -> %.2f s in 1.5 s of real time\n", a, b);
        CHECK(b - a > 1.2 && b - a < 1.8, "the real clock moved %.2f s in 1.5 s", b - a);
        real.pause();
        const double held = real.position();
        sleepMs(400);
        CHECK(std::abs(real.position() - held) < 0.1, "moved while paused on PipeWire");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
