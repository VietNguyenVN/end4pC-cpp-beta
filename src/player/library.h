// The queue, the lyrics and the settings of Play3r. Included once by player.cpp, inside its anonymous namespace (after draw.h).
#pragma once

struct LyricLine { double time = 0; std::string text; };

struct Track {
    std::string path;
};

const char* const kAudioExt[] = {".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".aac", ".wav", ".wma", ".ape", ".wv", ".mka"};
bool isAudio(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* x : kAudioExt) if (e == x) return true;
    return false;
}

// Files and folders (folders are searched below, in alphabetical order) -> tracks.
std::vector<Track> collectTracks(const std::vector<std::string>& inputs) {
    namespace fs = std::filesystem;
    std::vector<Track> out;
    for (const std::string& in : inputs) {
        std::error_code ec;
        if (fs::is_directory(in, ec)) {
            std::vector<std::string> found;
            for (fs::recursive_directory_iterator it(in, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
                if (isAudio(it->path()) && it->is_regular_file(ec)) found.push_back(it->path().string());  // (the cheap test first)
            std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
            for (auto& f : found) out.push_back({f});
        } else if (fs::is_regular_file(in, ec) && isAudio(in)) out.push_back({in});
    }
    return out;
}

// ---- the lyrics: the same script and the same cache as the lyrics of the bar's player card ----------------------------------------------
struct Lyrics {
    std::mutex mutex;
    std::vector<LyricLine> lines;
    std::string status = "idle";  // idle, loading, ok, none
    std::atomic<uint64_t> version{0}, serial{0};
} g_lyrics;

std::string lyricsCachePath(const std::string& title, const std::string& artist) {
    char key[32];
    std::snprintf(key, sizeof key, "%016zx", std::hash<std::string>{}(std::string("auto\n") + artist + "\n" + title));
    return cacheDir() + "/lyrics/" + key + ".txt";
}
std::string shellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}
bool parseLyrics(const std::string& text, std::vector<LyricLine>& out) {
    std::stringstream ss(text);
    std::string line;
    bool ok = false;
    while (std::getline(ss, line)) {
        if (!line.empty() && line[0] == '#') { ok = line == "#ok"; continue; }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        out.push_back({std::atof(line.substr(0, tab).c_str()), line.substr(tab + 1)});
    }
    return ok && !out.empty();
}
// Asks for the lyrics of a track in the background; the window sees the answer through g_lyrics.version.
void requestLyrics(const std::string& title, const std::string& artist, double duration) {
    const uint64_t mine = ++g_lyrics.serial;
    {
        std::lock_guard<std::mutex> lock(g_lyrics.mutex);
        g_lyrics.lines.clear();
        g_lyrics.status = title.empty() || artist.empty() ? "none" : "loading";
    }
    g_lyrics.version++;
    if (title.empty() || artist.empty()) return;
    std::thread([=] {
        std::vector<LyricLine> lines;
        const std::string cache = lyricsCachePath(title, artist);
        std::string raw = readFile(cache);
        bool ok = !raw.empty() && parseLyrics(raw, lines);
        if (!ok) {
            lines.clear();
            raw.clear();
            const std::string cmd = "timeout 30 python3 -I " + shellQuote(projectDir() + "/scripts/lyrics.py") + " " + shellQuote(title) + " " + shellQuote(artist) + " " +
                                    std::to_string(static_cast<int>(duration)) + " auto 2>/dev/null";
            if (FILE* f = popen(cmd.c_str(), "r")) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
                pclose(f);
            }
            ok = parseLyrics(raw, lines);
            if (ok) {
                std::error_code ec;
                std::filesystem::create_directories(cacheDir() + "/lyrics", ec);
                std::ofstream(cache) << raw;
            }
        }
        if (mine != g_lyrics.serial.load()) return;  // another track was chosen meanwhile
        std::lock_guard<std::mutex> lock(g_lyrics.mutex);
        g_lyrics.lines = std::move(lines);
        g_lyrics.status = ok ? "ok" : "none";
        g_lyrics.version++;
    }).detach();
}

// ---- the settings that are kept between runs ---------------------------------------------------------------------------------------------
struct Saved {
    float volume = 0.8f;
    bool shuffle = false;
    int repeat = 0;  // 0 off, 1 all, 2 one
    int current = -1;  // the row that was loaded when it was closed
};
std::string savedPath() { return dataDir() + "/play3r.txt"; }
Saved loadSaved() {
    Saved s;
    std::istringstream in(readFile(savedPath()));
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "volume") s.volume = std::clamp(static_cast<float>(std::atof(v.c_str())), 0.0f, 1.0f);
        else if (k == "shuffle") s.shuffle = v == "1";
        else if (k == "repeat") s.repeat = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "current") s.current = std::atoi(v.c_str());
    }
    return s;
}
void saveSaved(const Saved& s) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir(), ec);
    std::ofstream(savedPath()) << "volume=" << s.volume << "\nshuffle=" << (s.shuffle ? 1 : 0) << "\nrepeat=" << s.repeat << "\ncurrent=" << s.current << "\n";
}

// A file the picture of a track is in (for MPRIS, which wants an address): the embedded picture is saved in the cache, or the folder's own
// cover.jpg is used. Old copies are deleted. Empty: no picture.
std::string artFile(const std::string& path, const player::TrackInfo& info, uint64_t serial) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (serial >= 3) for (const char* ext : {".jpg", ".png"}) fs::remove(cacheDir() + "/play3r/cover-" + std::to_string(serial - 3) + ext, ec);
    if (!info.cover.empty()) {
        fs::create_directories(cacheDir() + "/play3r", ec);
        const bool png = info.cover.size() > 4 && info.cover[1] == 'P' && info.cover[2] == 'N';
        const std::string file = cacheDir() + "/play3r/cover-" + std::to_string(serial) + (png ? ".png" : ".jpg");
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(info.cover.data()), static_cast<std::streamsize>(info.cover.size()));
        return out ? "file://" + file : "";
    }
    const fs::path dir = fs::path(path).parent_path();
    for (const char* name : {"cover", "folder", "front", "album", "albumart"})
        for (const char* ext : {".jpg", ".jpeg", ".png", ".webp"}) {
            const std::string p = (dir / (std::string(name) + ext)).string();
            if (access(p.c_str(), R_OK) == 0) return "file://" + p;
        }
    return "";
}

// The picture of a track: the one inside the file, or cover.jpg, folder.jpg... next to it.
cairo_surface_t* loadCover(const std::string& path, const player::TrackInfo& info) {
    if (!info.cover.empty())
        if (cairo_surface_t* s = imageFromBytes(info.cover.data(), info.cover.size())) return s;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(path).parent_path();
    for (const char* name : {"cover", "folder", "front", "album", "albumart"})
        for (const char* ext : {".jpg", ".jpeg", ".png", ".webp"}) {
            const std::string p = (dir / (std::string(name) + ext)).string();
            if (access(p.c_str(), R_OK) != 0) continue;
            const std::string bytes = readFile(p);
            if (cairo_surface_t* s = imageFromBytes(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size())) return s;
        }
    return nullptr;
}

// ---- the tags of the queue, read in the background ---------------------------------------------------------------------------------------
// Like Audacious does: a folder is in the queue at once, with the names of the files, and a thread fills in the titles, the artists and the
// lengths from the rows that are on screen onwards. No cover is read for the rows (only for the song that plays).
struct TagSlot {
    std::string title, artist, album;
    double duration = 0;
    bool done = false;
};
struct Scanner {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::string> paths;
    std::vector<TagSlot> slots;
    uint64_t generation = 0;
    std::atomic<int> focus{0};  // the first row on screen: it is scanned first
    std::atomic<bool> quit{false};
    std::thread thread;
    int notifyFd = -1;  // written to when new tags arrived: the window redraws

    void start(int fd) {
        notifyFd = fd;
        thread = std::thread([this] { loop(); });
    }
    ~Scanner() {
        quit = true;
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }
    void set(const std::vector<Track>& tracks) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            paths.clear();
            for (const Track& t : tracks) paths.push_back(t.path);
            slots.assign(paths.size(), TagSlot{});
            generation++;
            focus = 0;
        }
        wake.notify_all();
    }
    TagSlot get(int i) {
        std::lock_guard<std::mutex> lock(mutex);
        if (i < 0 || i >= static_cast<int>(slots.size())) return {};
        return slots[static_cast<size_t>(i)];
    }
    void put(int i, const player::TrackInfo& info) {
        std::lock_guard<std::mutex> lock(mutex);
        if (i < 0 || i >= static_cast<int>(slots.size())) return;
        slots[static_cast<size_t>(i)] = {info.title, info.artist, info.album, info.duration, true};
    }
    void loop() {
        auto lastPing = Clock::now();
        bool pending = false;
        while (!quit.load()) {
            std::string path;
            int index = -1;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> lock(mutex);
                gen = generation;
                const int n = static_cast<int>(slots.size());
                for (int k = 0; k < n && index < 0; k++) {
                    const int i = (focus.load() + k) % n;
                    if (!slots[static_cast<size_t>(i)].done) index = i;
                }
                if (index < 0) {
                    if (pending) { pending = false; ping(); }
                    wake.wait(lock, [&] { return quit.load() || generation != gen; });
                    continue;
                }
                path = paths[static_cast<size_t>(index)];
            }
            const player::TrackInfo info = player::readTags(path, false);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (gen != generation) continue;  // another folder was opened meanwhile
                slots[static_cast<size_t>(index)] = {info.title, info.artist, info.album, info.duration, true};
            }
            pending = true;
            if (Clock::now() - lastPing > std::chrono::milliseconds(120)) { lastPing = Clock::now(); pending = false; ping(); }
        }
    }
    void ping() {
        const char c = 2;
        if (notifyFd >= 0 && write(notifyFd, &c, 1) < 0) {}
    }
} g_scanner;

// ---- the playlists: M3U files in ~/.local/share/end4pC/play3r/playlists ----------------------------------------------------------------------
struct PlaylistInfo {
    std::string name, path;
    int count = 0;
};
std::string playlistsDir() { return dataDir() + "/play3r/playlists"; }
std::string cleanPlaylistName(std::string name) {
    std::string out;
    for (const unsigned char c : name) if (c >= 0x20 && c != '/' && c != '\\' && c != 0x7f) out += static_cast<char>(c);
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.size() > 80) out.resize(80);
    return out;
}
std::vector<std::string> readM3u(const std::string& path) {
    std::vector<std::string> paths;
    std::istringstream in(readFile(path));
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::filesystem::path p(line);
        if (p.is_relative()) p = std::filesystem::path(path).parent_path() / p;
        paths.push_back(p.string());
    }
    return paths;
}
bool writeM3u(const std::string& path, const std::vector<std::string>& paths) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path + ".tmp");
    out << "#EXTM3U\n";
    for (const std::string& p : paths) out << p << "\n";
    out.close();
    if (!out) return false;
    std::filesystem::rename(path + ".tmp", path, ec);
    return !ec;
}
std::vector<PlaylistInfo> listPlaylists() {
    namespace fs = std::filesystem;
    std::vector<PlaylistInfo> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(playlistsDir(), ec)) {
        if (e.path().extension() != ".m3u") continue;
        PlaylistInfo info;
        info.name = e.path().stem().string();
        info.path = e.path().string();
        info.count = static_cast<int>(readM3u(info.path).size());
        out.push_back(info);
    }
    std::sort(out.begin(), out.end(), [](const PlaylistInfo& a, const PlaylistInfo& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}
std::string lastQueuePath() { return dataDir() + "/play3r/queue.m3u"; }
