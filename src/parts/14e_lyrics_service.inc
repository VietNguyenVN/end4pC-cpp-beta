
std::mutex g_lyMx;
std::vector<LyricLine> g_lyLines;
std::string g_lyStatus = "idle";
std::atomic<uint64_t> g_lyVersion{0}, g_lySerial{0};
std::string g_lyReqKey;

std::string lyCachePath(const std::string& title, const std::string& artist, const std::string& provider) {
    const char* home = std::getenv("HOME");
    char key[32];
    std::snprintf(key, sizeof key, "%016zx", std::hash<std::string>{}(provider + "\n" + artist + "\n" + title));
    return std::string(home ? home : "") + "/.cache/end4pC/lyrics/" + key + ".txt";
}

std::string lyShellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

bool lyParse(const std::string& text, std::vector<LyricLine>& out) {
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

void lyricsSvcRequest(const std::string& title, const std::string& artist, double duration, const std::string& provider) {
    g_lyReqKey = title + "\x1f" + artist + "\x1f" + provider;
    const uint64_t serial = ++g_lySerial;
    {
        std::lock_guard<std::mutex> lk(g_lyMx);
        g_lyLines.clear();
        g_lyStatus = title.empty() || artist.empty() ? "none" : "loading";
    }
    g_lyVersion++;
    if (title.empty() || artist.empty()) return;
    const std::string script = assetsDir() + "/../scripts/lyrics.py";
    std::thread([=]() {
        std::vector<LyricLine> lines;
        const std::string cache = lyCachePath(title, artist, provider);
        std::string raw;
        if (std::ifstream in(cache); in) { std::stringstream b; b << in.rdbuf(); raw = b.str(); }
        bool ok = !raw.empty() && lyParse(raw, lines);
        if (!ok) {
            lines.clear();
            const std::string cmd = "timeout 30 python3 -I " + lyShellQuote(script) + " " + lyShellQuote(title) + " " + lyShellQuote(artist) + " " + std::to_string(static_cast<int>(duration)) + " " + lyShellQuote(provider) + " 2>/dev/null";
            raw.clear();
            if (FILE* f = popen(cmd.c_str(), "r")) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
                pclose(f);
            }
            ok = lyParse(raw, lines);
            if (ok) {
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::path(cache).parent_path(), ec);
                std::ofstream(cache, std::ios::trunc) << raw;
            }
        }
        if (g_lySerial.load() != serial) return;
        {
            std::lock_guard<std::mutex> lk(g_lyMx);
            g_lyLines = ok ? lines : std::vector<LyricLine>{};
            g_lyStatus = ok ? "ok" : "none";
        }
        g_lyVersion++;
        if (g_weatherWrite >= 0) { const char b = 'l'; (void)!write(g_weatherWrite, &b, 1); }
    }).detach();
}

void lyricsSvcGet(std::vector<LyricLine>& lines, std::string& status, uint64_t* version) {
    std::lock_guard<std::mutex> lk(g_lyMx);
    lines = g_lyLines;
    status = g_lyStatus;
    if (version) *version = g_lyVersion.load();
}

void lyricsSvcEnsure(const std::string& title, const std::string& artist, double duration, const std::string& provider) {
    if (g_lyReqKey == title + "\x1f" + artist + "\x1f" + provider) return;
    lyricsSvcRequest(title, artist, duration, provider);
}

uint64_t lyricsSvcVersion() { return g_lyVersion.load(); }
