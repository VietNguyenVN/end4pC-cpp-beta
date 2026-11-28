// Play3r: the music player of the shell. A window of its own (a separate program, like the Tools window) that plays your music with the
// engine of src/player/engine.cpp, shows the cover, the synced lyrics and the queue, and looks like the rest of the shell.
//   end4pC-play3r [files or folders...]     without arguments it opens ~/Music paused
#include <cairo.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../elastic.h"
#include "../hidpi.h"
#include "../i18n.h"
#include "../shellfont.h"
#include "engine.h"
#include "mpris.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;

#include "../gui/draw.h"
#include "library.h"

struct Field {
    std::string s;
    size_t cur = 0;
};

constexpr int kDefaultW = 1000, kDefaultH = 640, kMinW = 820, kMinH = 520;
// kPad: the padding inside the cards. kEdge, kGap and kTop: the margin of the window, the space between the cards and where they start: the same as in Fil3s.
constexpr double kPad = 24, kRadius = 36, kLeftW = 400, kEdge = 16, kGap = 12, kTop = 72;

enum Id { None, BtnOpen, BtnClose, TabLyrics, TabQueue, TabPlaylists, BtnShuffle, BtnPrev, BtnPlay, BtnNext, BtnRepeat, Progress, VolumeBar, QueueRow, LyricRow, SearchField, SearchClear, NameField, SaveList, PlaylistRow, PlaylistDelete, UpNext };
struct Hit { Rect r; int id, arg; };
enum class Drag { None, Seek, Volume };

struct Buffer {
    hidpi::Frame frame;
    wl_buffer* buffer = nullptr;
    bool busy = false;
    void clear() {
        if (buffer) wl_buffer_destroy(buffer);
        hidpi::abandon(frame);
        buffer = nullptr;
        busy = false;
    }
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wl_callback* frameCallback = nullptr;
    hidpi::SurfaceScale scale;
    Buffer buffers[3];
    int width = kDefaultW, height = kDefaultH;
    bool configured = false, running = true, dirty = true;

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None, hoverArg = 0;
    std::vector<Hit> hits;

    // The music.
    std::unique_ptr<player::Engine> engine;
    std::vector<Track> queue;
    int current = -1;
    std::vector<int> history;  // the tracks played before, for Previous when shuffling
    std::mt19937 rng{std::random_device{}()};
    Saved saved;
    cairo_surface_t* cover = nullptr;
    cairo_surface_t* coverShaped = nullptr;  // the cover cut to its shape at the size it is drawn
    cairo_surface_t* coverShapedFor = nullptr;
    int coverShapedPx = 0;
    int tab = 1;  // 0 lyrics, 1 queue, 2 playlists (it opens on the queue)
    int focus = 0;  // the text field that has the keyboard: 0 none, 1 search, 2 name of a playlist
    Field search, plName;
    std::vector<int> view;  // the rows of the queue that pass the search, in order
    int nextIndex = -1;     // the song that comes next (chosen once per song, so "Up next" and Next agree even when shuffling)
    int selPos = 0;         // the row of the view that Enter plays (moved with the arrows while searching)
    bool viewDirty = true;
    std::vector<PlaylistInfo> playlists;
    bool playlistsDirty = true;
    double plPos = 0, plMax = 0;
    Elastic plBand;
    std::string plNote;
    bool caretOn = true;
    Clock::time_point caretReset = Clock::now();
    Drag drag = Drag::None;
    double dragFrac = 0;
    uint64_t lyricsSeen = 0;
    std::vector<LyricLine> lyrics;
    std::string lyricsStatus = "idle";
    double lyScroll = 0, lyHeightsWidth = 0;
    std::vector<double> lyHeights;
    Elastic queueBand;
    double queuePos = 0, queueMax = 0;
    bool queueToCurrent = false;
    Clock::time_point start = Clock::now(), lastTick = Clock::now();
    double shownPosition = 0;
    Rect bar;               // where the progress is (set when the window is drawn)
    bool barHot = false;
    int timeDrawn = -1;     // the second shown in the time label
    bool lyBusy = false;    // the lyrics are still gliding to their place
    int lyCurrentDrawn = -2;
    bool waveDirty = false; // only the progress changed: it can be drawn over the saved picture of the rest
    std::vector<unsigned char> base;
    int baseW = 0, baseH = 0;
    double baseScale = 0;
    Clock::time_point lastRender = Clock::now(), lastWave = Clock::now();
} g;

mpris::Service g_mpris;
std::string g_artUrl;
uint64_t g_trackSerial = 0;


std::string fmtTime(double s) {
    const int t = std::max(0, static_cast<int>(s));
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}
// The text of a queue row: the tags once the scanner got to the file, the name of the file until then.
player::TrackInfo trackInfo(int i) {
    player::TrackInfo info;
    if (i < 0 || i >= static_cast<int>(g.queue.size())) return info;
    const TagSlot slot = g_scanner.get(i);
    info.title = slot.done && !slot.title.empty() ? slot.title : std::filesystem::path(g.queue[static_cast<size_t>(i)].path).stem().string();
    info.artist = slot.artist;
    info.album = slot.album;
    info.duration = slot.duration;
    return info;
}

// ---- playing -------------------------------------------------------------------------------------------------------------------------
int pickNext();
void playIndex(int index, bool pushHistory = true, bool autoplay = true) {
    if (index < 0 || index >= static_cast<int>(g.queue.size())) return;
    if (pushHistory && g.current >= 0) g.history.push_back(g.current);
    if (g.history.size() > 200) g.history.erase(g.history.begin());
    // The cover and the tags of this file (the queue rows keep only the text).
    player::TrackInfo full = player::readTags(g.queue[static_cast<size_t>(index)].path);
    if (g.cover) { cairo_surface_destroy(g.cover); g.cover = nullptr; }
    if (g.coverShaped) { cairo_surface_destroy(g.coverShaped); g.coverShaped = nullptr; }
    g.cover = loadCover(g.queue[static_cast<size_t>(index)].path, full);
    g_artUrl = artFile(g.queue[static_cast<size_t>(index)].path, full, ++g_trackSerial);
    full.cover.clear();
    g_scanner.put(index, full);
    g.current = index;
    g.nextIndex = pickNext();
    if (!g.engine->open(g.queue[static_cast<size_t>(index)].path, autoplay)) {
        std::fprintf(stderr, "play3r: %s: %s\n", g.queue[static_cast<size_t>(index)].path.c_str(), g.engine->error().c_str());
    }
    requestLyrics(full.title, full.artist, g.engine->duration() > 0 ? g.engine->duration() : full.duration);
    g.lyScroll = 0;
    g.queueToCurrent = true;
    g.dirty = true;
}
int pickNext() {
    const int n = static_cast<int>(g.queue.size());
    if (n == 0) return -1;
    if (g.saved.shuffle && n > 1) {
        int i;
        do i = static_cast<int>(g.rng() % n); while (i == g.current);
        return i;
    }
    if (g.current + 1 < n) return g.current + 1;
    return g.saved.repeat == 1 ? 0 : -1;
}
void nextTrack() {
    const int i = g.nextIndex >= 0 && g.nextIndex < static_cast<int>(g.queue.size()) ? g.nextIndex : pickNext();
    if (i >= 0) playIndex(i);
    else { g.engine->stop(); g.dirty = true; }
}
void previousTrack() {
    if (g.engine->position() > 3 || g.queue.empty()) { g.engine->seek(0); return; }
    if (g.saved.shuffle && !g.history.empty()) { const int i = g.history.back(); g.history.pop_back(); playIndex(i, false); return; }
    if (g.current > 0) playIndex(g.current - 1, false);
    else g.engine->seek(0);
}
void togglePlay() {
    if (g.current < 0 && !g.queue.empty()) { playIndex(0); return; }
    if (g.engine->state() == player::State::Stopped) { if (g.current >= 0) playIndex(g.current, false); return; }
    g.engine->toggle();
    g.dirty = true;
}
void seekAbs(double seconds) {
    g.engine->seek(seconds);
    g_mpris.seeked(std::max(0.0, seconds));
    g.dirty = true;
}
std::string pathFromUri(const std::string& uri) {
    if (uri.rfind("file://", 0) != 0) return uri;
    std::string out;
    for (size_t i = 7; i < uri.size(); i++) {
        if (uri[i] == '%' && i + 2 < uri.size()) { out += static_cast<char>(std::strtol(uri.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else out += uri[i];
    }
    return out;
}
void openMusic(const std::vector<std::string>& inputs, bool autoplay);
mpris::State mprisState() {
    mpris::State s;
    const auto st = g.engine->state();
    s.status = st == player::State::Playing ? "Playing" : st == player::State::Paused ? "Paused" : "Stopped";
    s.hasTrack = g.current >= 0;
    s.trackId = g_trackSerial;
    if (s.hasTrack) {
        const player::TrackInfo info = trackInfo(g.current);
        s.title = info.title;
        s.artist = info.artist;
        s.album = info.album;
        s.url = "file://" + g.queue[static_cast<size_t>(g.current)].path;
        s.length = g.engine->duration() > 0 ? g.engine->duration() : info.duration;
        s.artUrl = g_artUrl;
    }
    s.position = g.engine->position();
    s.volume = std::min(1.0, static_cast<double>(g.engine->volume()));
    s.shuffle = g.saved.shuffle;
    s.loop = g.saved.repeat;
    s.canNext = !g.queue.empty();
    s.canPrevious = !g.queue.empty();
    return s;
}
mpris::Control mprisControl() {
    mpris::Control c;
    c.state = mprisState;
    c.play = [] { if (g.engine->state() == player::State::Playing) return; togglePlay(); };
    c.pause = [] { if (g.engine->state() == player::State::Playing) g.engine->pause(); g.dirty = true; };
    c.playPause = [] { togglePlay(); };
    c.stop = [] { g.engine->stop(); g.dirty = true; };
    c.next = [] { nextTrack(); };
    c.previous = [] { previousTrack(); };
    c.quit = [] { g.running = false; };
    c.seekBy = [](double d) { seekAbs(g.engine->position() + d); };
    c.seekTo = [](double s) { seekAbs(s); };
    c.setVolume = [](double v) { g.engine->setVolume(static_cast<float>(v)); g.saved.volume = g.engine->volume(); saveSaved(g.saved); g.dirty = true; };
    c.setShuffle = [](bool on) { g.saved.shuffle = on; saveSaved(g.saved); g.dirty = true; };
    c.setLoop = [](int l) { g.saved.repeat = std::clamp(l, 0, 2); saveSaved(g.saved); g.dirty = true; };
    c.openUri = [](const std::string& uri) { openMusic({pathFromUri(uri)}, true); };
    return c;
}

// ---- the search and the playlists ---------------------------------------------------------------------------------------------------------
std::string lowerAscii(std::string t) {
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
}
// The rows of the queue that contain every word of the search (in the title, the artist, the album or the file name).
void rebuildView() {
    g.view.clear();
    std::vector<std::string> words;
    {
        std::istringstream in(lowerAscii(g.search.s));
        for (std::string w; in >> w;) words.push_back(w);
    }
    for (int i = 0; i < static_cast<int>(g.queue.size()); i++) {
        if (!words.empty()) {
            const player::TrackInfo info = trackInfo(i);
            const std::string hay = lowerAscii(info.title + "\n" + info.artist + "\n" + info.album + "\n" + g.queue[static_cast<size_t>(i)].path.substr(g.queue[static_cast<size_t>(i)].path.rfind('/') + 1));
            bool all = true;
            for (const std::string& w : words) if (hay.find(w) == std::string::npos) { all = false; break; }
            if (!all) continue;
        }
        g.view.push_back(i);
    }
    g.viewDirty = false;
    g.selPos = 0;
}
void refreshPlaylists() { g.playlists = listPlaylists(); g.playlistsDirty = false; }
void saveQueueAsPlaylist() {
    const std::string name = cleanPlaylistName(g.plName.s);
    if (name.empty() || g.queue.empty()) return;
    std::vector<std::string> paths;
    for (const Track& t : g.queue) paths.push_back(t.path);
    g.plNote = writeM3u(playlistsDir() + "/" + name + ".m3u", paths) ? std::string(i18n::tr("Saved")) + ": " + name : i18n::tr("The playlist could not be saved");
    g.plName = {};
    g.focus = 0;
    refreshPlaylists();
    g.dirty = true;
}
void loadPlaylistFile(const std::string& path, bool autoplay);
void deletePlaylist(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    refreshPlaylists();
    g.dirty = true;
}

void setQueue(std::vector<Track> tracks, bool autoplay) {
    if (tracks.empty()) return;
    g.queue = std::move(tracks);
    g_scanner.set(g.queue);
    g.current = -1;
    g.history.clear();
    g.queuePos = 0;
    g.viewDirty = true;
    if (autoplay) playIndex(0, false);
    g.dirty = true;
}
void openMusic(const std::vector<std::string>& inputs, bool autoplay) { setQueue(collectTracks(inputs), autoplay); }
void loadPlaylistFile(const std::string& path, bool autoplay) {
    std::vector<Track> tracks;
    for (const std::string& p : readM3u(path)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec) && isAudio(p)) tracks.push_back({p});
    }
    setQueue(std::move(tracks), autoplay);
}
void saveLastQueue() {
    std::vector<std::string> paths;
    for (const Track& t : g.queue) paths.push_back(t.path);
    if (!paths.empty()) writeM3u(lastQueuePath(), paths);
    g.saved.current = g.current;
    saveSaved(g.saved);
}
std::string g_chosen;      // the folder the dialog gave (set by its thread, taken by the window loop)
std::mutex g_chosenMutex;
int g_wakeFd = -1;         // written by that thread to wake the loop
void chooseFolder() {
    std::thread([] {
        std::string out;
        if (FILE* f = popen("zenity --file-selection --directory --title='Play3r' 2>/dev/null", "r")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            pclose(f);
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (out.empty()) return;
        {
            std::lock_guard<std::mutex> lock(g_chosenMutex);
            g_chosen = out;
        }
        const char c = 1;
        if (write(g_wakeFd, &c, 1) < 0) {}
    }).detach();
}

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) {
    {
        const int lock = open((runtimeDir() + "/end4pC-play3r.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0) {  // already open: a song or folder given now is played by that window
            if (argc > 1) {
                char real[4096];
                if (realpath(argv[1], real)) {
                    const std::string uri = std::string("file://") + real;
                    execlp("playerctl", "playerctl", "-p", "play3r", "open", uri.c_str(), static_cast<char*>(nullptr));
                }
            }
            return 0;
        }
    }
    return run(argc, argv);
}
