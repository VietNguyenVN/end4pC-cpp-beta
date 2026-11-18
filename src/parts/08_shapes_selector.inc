

const std::map<std::string, std::vector<double>>& materialShapes() {
    static const auto shapes = [] {
        std::map<std::string, std::vector<double>> out;
        std::ifstream f(assetsDir() + "/shapes.txt");
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            std::string name;
            ss >> name;
            std::vector<double> nums;
            for (double v; ss >> v;) nums.push_back(v);
            if (!name.empty() && nums.size() >= 8 && (nums.size() - 2) % 6 == 0) out[name] = std::move(nums);
        }
        return out;
    }();
    return shapes;
}

void shapePath(cairo_t* cr, const std::string& name, double x, double y, double size) {
    const auto& all = materialShapes();
    auto it = all.find(name);
    if (it == all.end()) it = all.find("Cookie9Sided");
    if (it == all.end()) return;
    const auto& n = it->second;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x + n[0] * size, y + n[1] * size);
    for (size_t i = 2; i + 5 < n.size(); i += 6)
        cairo_curve_to(cr, x + n[i] * size, y + n[i + 1] * size, x + n[i + 2] * size, y + n[i + 3] * size, x + n[i + 4] * size, y + n[i + 5] * size);
    cairo_close_path(cr);
}

constexpr int kSelW = 1400, kSelH = 812, kSelRadius = 32;
constexpr int kMainX = 252, kMainW = kSelW - kMainX - 16;
constexpr int kTileW = 216, kTileH = 122, kTileGap = 12, kCols = 5, kVisRows = 3;
constexpr int kSearchY = 16, kSearchH = 44, kHeroY = 72, kHeroH = 282, kHeadY = 370, kGridTop = 406;
constexpr int kMaxFolders = 6;
constexpr double kModeSeconds = 0.18;
constexpr const char* kSelTitleFont = "Sans Bold 15";
constexpr const char* kSelChipFont = "Sans 9";
constexpr const char* kSelSmallFont = "Sans 9";

int g_thumbFd = -1;
int g_thumbWrite = -1;

std::string expandHome(const std::string& path) {
    return !path.empty() && path[0] == '~' ? homeDir() + path.substr(1) : path;
}

std::string schemeLabel(const std::string& id) {
    std::string out = id.rfind("scheme-", 0) == 0 ? id.substr(7) : id;
    bool up = true;
    for (char& c : out) {
        if (c == '-') { c = ' '; up = true; }
        else if (up) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); up = false; }
    }
    return out;
}

const std::vector<std::pair<std::string, std::string>> kOnlineProviders = {{"wallhaven", "Wallhaven"}, {"unsplash", "Unsplash"}, {"pexels", "Pexels"}};
bool providerNeedsKey(const std::string& p) { return p == "unsplash" || p == "pexels"; }
std::string providerLabel(const std::string& p) {
    for (const auto& [id, name] : kOnlineProviders) if (id == p) return name;
    return p;
}
const std::vector<std::pair<std::string, std::string>> kWallhavenSorts = {{"Top", "toplist"}, {"Hot", "hot"}, {"Latest", "date_added"}, {"Random", "random"}, {"Views", "views"}, {"Favorites", "favorites"}};
const char* kOnlineResLabels[3] = {"1080p", "2K", "4K"};
const int kOnlineResWidths[3] = {1920, 2560, 3840};
std::string onlinePrefsFile() { return stateDir() + "/wallpaper-online.txt"; }
void loadOnlinePrefs(Selector& sel) {
    std::ifstream f(onlinePrefsFile());
    std::string line;
    bool resSet = false;
    while (std::getline(f, line)) {
        if (line.rfind("sort=", 0) == 0) {
            for (const auto& [lab, id] : kWallhavenSorts) if (line.substr(5) == id) sel.onlineSort = id;
        } else if (line.rfind("res=", 0) == 0) {
            for (int i = 0; i < 3; i++) if (line.substr(4) == kOnlineResLabels[i]) { sel.onlineRes = i; resSet = true; }
        }
    }
    (void)resSet;
}
void saveOnlinePrefs(const Selector& sel) {
    std::filesystem::create_directories(stateDir());
    std::ofstream f(onlinePrefsFile(), std::ios::trunc);
    f << "sort=" << sel.onlineSort << "\nres=" << kOnlineResLabels[std::clamp(sel.onlineRes, 0, 2)] << "\n";
}
std::string onlineTileKey(const std::string& provider, const std::string& id) { return "online:" + provider + ":" + id; }
bool isOnlineTile(const std::string& key) { return key.rfind("online:", 0) == 0; }
int totalRowsOf(size_t count) { return (static_cast<int>(count) + kCols - 1) / kCols; }
const OnlineItem* onlineItemFor(const Selector& sel, const std::string& key) {
    for (const auto& it : sel.onlineItems) if (onlineTileKey(sel.onlineProvider, it.id) == key) return &it;
    return nullptr;
}

std::vector<std::string> collectWallpapers(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            const size_t dot = name.rfind('.');
            if (dot == std::string::npos || name[0] == '.') continue;
            const std::string ext = lowerCopy(name.substr(dot + 1));
            if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp") out.push_back(dir + "/" + name);
        }
        closedir(d);
    }
    return out;
}
void sortNewestFirst(std::vector<std::string>& out) {
    std::map<std::string, long long> added;
    for (const auto& path : out) {
        struct statx st{};
        long long t = 0;
        if (statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME | STATX_MTIME, &st) == 0)
            t = (st.stx_mask & STATX_BTIME) ? static_cast<long long>(st.stx_btime.tv_sec) * 1000000000LL + st.stx_btime.tv_nsec
                                            : static_cast<long long>(st.stx_mtime.tv_sec) * 1000000000LL + st.stx_mtime.tv_nsec;
        added[path] = t;
    }
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        if (added[a] != added[b]) return added[a] > added[b];
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
}
std::vector<std::string> listWallpapers(const std::string& dir) {
    std::vector<std::string> out = collectWallpapers(dir);
    sortNewestFirst(out);
    return out;
}
std::vector<std::string> wallpaperFolders(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name[0] == '.') continue;
            struct stat st{};
            const std::string full = dir + "/" + name;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !collectWallpapers(full).empty()) out.push_back(full);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

std::string thumbCachePath(const std::string& src) {
    struct stat st{};
    stat(src.c_str(), &st);
    char name[48];
    std::snprintf(name, sizeof name, "%016zx.png", std::hash<std::string>{}(src + "|" + std::to_string(st.st_mtim.tv_sec) + "." + std::to_string(st.st_mtim.tv_nsec) + "|" + std::to_string(st.st_size)));
    const char* c = std::getenv("XDG_CACHE_HOME");
    return (c ? std::string(c) : homeDir() + "/.cache") + "/end4pC/thumbs/" + name;
}
std::string heroCachePath(const std::string& src) {
    std::string p = thumbCachePath(src);
    return p.substr(0, p.size() - 4) + ".hero.png";
}

cairo_surface_t* loadThumbSurface(const std::string& file) {
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(file.c_str(), nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* big = surfaceFromPixbuf(pb);
    const int bw = gdk_pixbuf_get_width(pb), bh = gdk_pixbuf_get_height(pb);
    g_object_unref(pb);
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kTileW, kTileH);
    cairo_t* cr = mkcr(small);
    cairo_scale(cr, double(kTileW) / bw, double(kTileH) / bh);
    cairo_set_source_surface(cr, big, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(big);
    return small;
}

void startThumbJobs(App& app) {
    Selector& sel = app.sel;
    while (sel.inflight < 3 && !sel.jobs.empty() && g_thumbWrite >= 0) {
        const auto job = sel.jobs.front();
        sel.jobs.erase(sel.jobs.begin());
        const bool hero = job.second.size() > 9 && job.second.compare(job.second.size() - 9, 9, ".hero.png") == 0;
        const char* size = hero ? "1280x720" : "384x216";
        if (fork() == 0) {
            childSignalReset();
            dup2(g_thumbWrite, STDOUT_FILENO);
            int nul = open("/dev/null", O_RDWR);
            dup2(nul, STDERR_FILENO);
            execlp("sh", "sh", "-c",
                   "mkdir -p \"$(dirname \"$2\")\" && magick \"$1[0]\" -thumbnail \"$3^\" -gravity center -extent \"$3\" \"$2.tmp.png\""
                   " && mv \"$2.tmp.png\" \"$2\"; echo done",
                   "sh", job.first.c_str(), job.second.c_str(), size, nullptr);
            _exit(127);
        }
        sel.inflight++;
    }
}

cairo_surface_t* thumbFor(App& app, const std::string& path) {
    Selector& sel = app.sel;
    if (auto it = sel.thumbs.find(path); it != sel.thumbs.end()) return it->second;
    if (isOnlineTile(path)) {
        const OnlineItem* oi = onlineItemFor(sel, path);
        cairo_surface_t* surf = oi ? loadThumbSurface(oi->thumb) : nullptr;
        if (oi) sel.thumbs[path] = surf;
        return surf;
    }
    const std::string file = thumbCachePath(path);
    if (access(file.c_str(), R_OK) == 0) {
        cairo_surface_t* surf = loadThumbSurface(file);
        sel.thumbs[path] = surf;
        return surf;
    }
    if (sel.requested.insert(path).second) {
        sel.jobs.emplace_back(path, file);
        startThumbJobs(app);
    }
    return nullptr;
}

cairo_surface_t* heroFor(App& app) {
    Selector& sel = app.sel;
    if (sel.heroPath != app.wallpaper) {
        if (sel.hero) cairo_surface_destroy(sel.hero);
        sel.hero = nullptr;
        sel.heroPath = app.wallpaper;
        sel.heroW = sel.heroH = 0;
        if (!sel.heroPath.empty()) gdk_pixbuf_get_file_info(sel.heroPath.c_str(), &sel.heroW, &sel.heroH);
    }
    if (!sel.hero && !sel.heroPath.empty()) {
        const std::string file = heroCachePath(sel.heroPath);
        if (access(file.c_str(), R_OK) == 0) {
            if (GdkPixbuf* pb = gdk_pixbuf_new_from_file(file.c_str(), nullptr)) {
                sel.hero = surfaceFromPixbuf(pb);
                g_object_unref(pb);
            }
        } else if (sel.requested.insert(file).second) {
            sel.jobs.insert(sel.jobs.begin(), {sel.heroPath, file});
            startThumbJobs(app);
        }
    }
    return sel.hero;
}

std::string wpListFile(const char* name) { return stateDir() + "/" + name; }
std::vector<std::string> readPathList(const std::string& file) {
    std::vector<std::string> out;
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) out.push_back(line);
    return out;
}
void writePathList(const std::string& file, const std::vector<std::string>& v) {
    std::filesystem::create_directories(stateDir());
    std::ofstream f(file, std::ios::trunc);
    for (const auto& p : v) f << p << "\n";
}
bool isFavourite(const Selector& sel, const std::string& path) { return std::find(sel.favs.begin(), sel.favs.end(), path) != sel.favs.end(); }

std::string baseName(const std::string& path, bool noExt) {
    std::string n = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    if (noExt) if (const size_t dot = n.rfind('.'); dot != std::string::npos && dot > 0) n.resize(dot);
    return n;
}
std::string dirName(const std::string& path) { return path.substr(0, path.rfind('/') == std::string::npos ? 0 : path.rfind('/')); }

void drawSelector(App& app);
void applyTheme(App& app, const std::string& wallpaper);

struct OnlineEvent { std::string kind, provider, text, id; int gen = 0; bool append = false, apply = false; };
std::mutex g_onMx;
std::vector<OnlineEvent> g_onEvents;
int g_onlineFd = -1, g_onlineWrite = -1;

std::string captureArgv(const std::vector<std::string>& argv, const std::string& input = "") {
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) != 0) return "";
    if (pipe2(out, O_CLOEXEC) != 0) { close(in[0]); close(in[1]); return ""; }
    const int nul = open("/dev/null", O_WRONLY | O_CLOEXEC);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    if (nul >= 0) posix_spawn_file_actions_adddup2(&fa, nul, 2);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t none, dflt;  // the shell blocks some signals and ignores SIGCHLD: the helper starts clean
    sigemptyset(&none);
    sigemptyset(&dflt);
    sigaddset(&dflt, SIGCHLD);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &dflt);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    std::vector<char*> av;
    for (const auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0].c_str(), &fa, &at, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(in[0]);
    close(out[1]);
    if (nul >= 0) close(nul);
    std::string result;
    if (rc == 0) {
        for (size_t off = 0; off < input.size();) {
            const ssize_t n = write(in[1], input.data() + off, input.size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        close(in[1]);
        char buf[4096];
        ssize_t n;
        while ((n = read(out[0], buf, sizeof buf)) > 0) result.append(buf, static_cast<size_t>(n));
        int st = 0;
        waitpid(pid, &st, 0);  // (fails with ECHILD while SIGCHLD is ignored: nothing to do)
    } else {
        close(in[1]);
    }
    close(out[0]);
    return result;
}

void onlinePost(OnlineEvent ev) {
    {
        std::lock_guard<std::mutex> lk(g_onMx);
        g_onEvents.push_back(std::move(ev));
    }
    if (g_onlineWrite >= 0) { const ssize_t r = write(g_onlineWrite, "x\n", 2); (void)r; }
}
std::string onlineScript() { return assetsDir() + "/../scripts/wallpaper_online.py"; }
std::string onlineThumbDir() {
    const char* c = std::getenv("XDG_CACHE_HOME");
    return (c ? std::string(c) : homeDir() + "/.cache") + "/end4pC/online-thumbs";
}
std::string firstLine(const std::string& t) { return t.substr(0, t.find('\n')); }

void onlineRebuildFiles(Selector& sel) {
    sel.files.clear();
    for (const auto& it : sel.onlineItems) sel.files.push_back(onlineTileKey(sel.onlineProvider, it.id));
}

void onlineFetch(App& app, int page, bool append) {
    Selector& sel = app.sel;
    if (sel.onlineProvider.empty()) return;
    sel.onlineState = 1;
    sel.onlineError.clear();
    const int gen = ++sel.onlineGen;
    const int width = kOnlineResWidths[std::clamp(sel.onlineRes, 0, 2)];
    const std::string provider = sel.onlineProvider, query = sel.query, dir = onlineThumbDir(), script = onlineScript();
    const std::string sort = provider == "wallhaven" ? sel.onlineSort : "", seed = page > 1 ? sel.onlineSeed : "";
    sel.onlineQuery = query;
    std::thread([=] {
        const std::string out = captureArgv({"python3", "-I", script, "--search", provider, query, std::to_string(page), std::to_string(width), dir, sort, seed});
        onlinePost({"search", provider, out, "", gen, append});
    }).detach();
}

void onlineStart(App& app, const std::string& provider) {
    Selector& sel = app.sel;
    sel.onlineProvider = provider;
    sel.onlineItems.clear();
    sel.files.clear();
    sel.query.clear();
    sel.onlinePage = sel.onlinePages = 0;
    sel.keyPanel = sel.keyReveal = sel.keySaving = false;
    std::fill(sel.keyInput.begin(), sel.keyInput.end(), '\0');
    sel.keyInput.clear();
    sel.keyMsg.clear();
    sel.scrollRow = 0;
    onlineFetch(app, 1, false);
}

void onlineSearch(App& app) {
    Selector& sel = app.sel;
    sel.onlineItems.clear();
    sel.files.clear();
    sel.scrollRow = 0;
    sel.onlinePage = sel.onlinePages = 0;
    onlineFetch(app, 1, false);
}

void onlineMaybeMore(App& app) {
    Selector& sel = app.sel;
    if (sel.onlineProvider.empty() || sel.keyPanel || sel.onlineState != 0 || sel.onlinePage <= 0 || sel.onlinePage >= sel.onlinePages) return;
    if (sel.scrollRow + kVisRows + 1 >= totalRowsOf(sel.files.size())) onlineFetch(app, sel.onlinePage + 1, true);
}

void onlineSaveKey(App& app) {
    Selector& sel = app.sel;
    if (sel.keyInput.empty() || sel.keySaving || sel.onlineProvider.empty()) return;
    sel.keySaving = true;
    sel.keyMsg.clear();
    std::string key = sel.keyInput;
    const std::string provider = sel.onlineProvider, script = onlineScript();
    std::thread([key, provider, script]() mutable {
        const std::string out = captureArgv({"python3", "-I", script, "--key-set", provider}, key);
        std::fill(key.begin(), key.end(), '\0');
        onlinePost({"key", provider, firstLine(out), "", 0, false});
    }).detach();
}

void onlineDownload(App& app, const OnlineItem& it, bool apply) {
    Selector& sel = app.sel;
    const std::string tag = sel.onlineProvider + ":" + it.id;
    if (!sel.downloading.insert(tag).second) return;
    sel.onlineError.clear();
    sel.onlineNote.clear();
    const std::string provider = sel.onlineProvider, script = onlineScript(), dest = expandHome(app.cfg.wallpaperDir) + "/" + provider + "_" + it.id;
    const OnlineItem item = it;
    std::thread([=] {
        const std::string out = captureArgv({"python3", "-I", script, "--download", provider, item.id, item.full, item.dl, dest});
        onlinePost({"download", provider, firstLine(out), item.id, 0, false, apply});
    }).detach();
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> v;
    size_t a = 0;
    while (true) {
        const size_t b = line.find('\t', a);
        v.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}

void onlineDrain(App& app) {
    char junk[64];
    while (g_onlineFd >= 0 && read(g_onlineFd, junk, sizeof junk) > 0) {}
    std::vector<OnlineEvent> evs;
    {
        std::lock_guard<std::mutex> lk(g_onMx);
        evs.swap(g_onEvents);
    }
    Selector& sel = app.sel;
    for (const OnlineEvent& ev : evs) {
        if (ev.kind == "download") {
            sel.downloading.erase(ev.provider + ":" + ev.id);
            if (ev.text.empty() || ev.text.rfind("error", 0) == 0) { sel.onlineError = ev.text.empty() ? "The download failed." : ev.text.substr(ev.text.find(' ') == std::string::npos ? 0 : ev.text.find(' ') + 1); }
            else {
                sel.allFiles.insert(sel.allFiles.begin(), ev.text);
                if (ev.apply) applyTheme(app, ev.text);
                else sel.onlineNote = "Saved " + baseName(ev.text, false);
            }
        } else if (!sel.open()) {
            continue;
        } else if (ev.kind == "key") {
            if (ev.provider != sel.onlineProvider) continue;
            sel.keySaving = false;
            if (ev.text == "ok") {
                std::fill(sel.keyInput.begin(), sel.keyInput.end(), '\0');
                sel.keyInput.clear();
                sel.keyPanel = false;
                sel.keyMsg.clear();
                onlineFetch(app, 1, false);
            } else {
                sel.keyMsg = ev.text.rfind("error: ", 0) == 0 ? ev.text.substr(7) : "The key could not be saved.";
            }
        } else if (ev.kind == "search") {
            if (ev.gen != sel.onlineGen || ev.provider != sel.onlineProvider) continue;
            const auto lines = [&] {
                std::vector<std::string> l;
                std::istringstream in(ev.text);
                std::string x;
                while (std::getline(in, x)) if (!x.empty()) l.push_back(x);
                return l;
            }();
            sel.onlineState = 0;
            if (lines.empty()) { sel.onlineState = 2; sel.onlineError = "The service did not answer. Is the helper installed (python3)?"; }
            else if (lines[0].rfind("error\t", 0) == 0) {
                const auto f = splitTabs(lines[0]);
                if (f.size() > 1 && f[1] == "1") {
                    sel.keyPanel = true;
                    sel.keyMsg = sel.onlineItems.empty() && !sel.keyInput.empty() ? "" : (f.size() > 2 && f[2].find("rejected") != std::string::npos ? f[2] : "");
                    sel.files.clear();
                } else {
                    sel.onlineState = 2;
                    sel.onlineError = f.size() > 2 ? f[2] : "Something went wrong.";
                }
            } else {
                if (!ev.append) { sel.onlineItems.clear(); sel.scrollRow = 0; }
                for (size_t i = 0; i < lines.size(); i++) {
                    const auto f = splitTabs(lines[i]);
                    if (f.size() >= 1 && f[0] == "pages" && f.size() > 1) {
                        sel.onlinePages = std::atoi(f[1].c_str());
                        if (!ev.append && f.size() > 2) sel.onlineSeed = f[2];
                        continue;
                    }
                    if (f.size() < 3) continue;
                    OnlineItem it;
                    it.id = f[0]; it.thumb = f[1]; it.full = f[2];
                    it.dl = f.size() > 3 ? f[3] : "";
                    it.w = f.size() > 4 ? std::atoi(f[4].c_str()) : 0;
                    it.h = f.size() > 5 ? std::atoi(f[5].c_str()) : 0;
                    it.author = f.size() > 6 ? f[6] : "";
                    it.title = f.size() > 7 ? f[7] : "";
                    sel.onlineItems.push_back(it);
                }
                sel.onlinePage = ev.append ? sel.onlinePage + 1 : 1;
                sel.keyPanel = false;
                onlineRebuildFiles(sel);
            }
        }
    }
    if (sel.open()) drawSelector(app);
}

void rebuildSelectorList(App& app) {
    Selector& sel = app.sel;
    const std::string key = sel.cat >= 0 && sel.cat < static_cast<int>(sel.cats.size()) ? sel.cats[sel.cat].second : "all";
    if (key.rfind("online:", 0) == 0) { onlineRebuildFiles(sel); sel.focusIdx = 0; sel.hoverTile = -1; return; }
    const std::set<std::string> have(sel.allFiles.begin(), sel.allFiles.end());
    std::vector<std::string> base;
    if (key == "all") base = sel.allFiles;
    else if (key == "fav") { for (const auto& p : sel.favs) if (have.count(p)) base.push_back(p); }
    else if (key == "recent") { for (const auto& p : sel.recents) if (have.count(p)) base.push_back(p); }
    else if (key.rfind("dir:", 0) == 0) { const std::string d = key.substr(4); for (const auto& p : sel.allFiles) if (dirName(p) == d) base.push_back(p); }
    sel.files.clear();
    const std::string q = lowerCopy(sel.query);
    for (const auto& p : base)
        if (q.empty() || lowerCopy(baseName(p, false)).find(q) != std::string::npos) sel.files.push_back(p);
    sel.scrollRow = 0;
    sel.focusIdx = 0;
    sel.hoverTile = -1;
}

struct SelGeom {
    Rect search, hero, light, dark, shuffle, fav, folder, pick, close;
    Rect keyCard, keyInput, keyEye, keySave, keyOpen, change, retry;
    Rect dlgCard, dlgPreview, dlgCancel, dlgOk;
    std::vector<Rect> nav;
    std::vector<std::pair<double, std::string>> labels;
    std::vector<Rect> sortChips, resChips;
    double sortLabelX = 0, resLabelX = 0, chipLabelY = 0;
};
SelGeom selGeom(const Selector& sel) {
    SelGeom g;
    g.search = {double(kMainX), double(kSearchY), double(kMainW - 40 - 10), double(kSearchH)};
    g.close = {double(kMainX + kMainW - 40), double(kSearchY + 2), 40, 40};
    g.hero = {double(kMainX), double(kHeroY), double(kMainW), double(kHeroH)};
    const double by0 = g.hero.y + g.hero.h - 28 - 46, right = g.hero.x + g.hero.w - 28;
    g.pick = {right - 46, by0, 46, 46};
    const double my = kSelH - 12 - 16 - 46;
    g.light = {24, my, 96, 46};
    g.dark = {120, my, 96, 46};
    const double by = g.hero.y + g.hero.h - 28 - 46;
    g.shuffle = {g.hero.x + 28, by, 156, 46};
    g.fav = {g.shuffle.x + g.shuffle.w + 10, by, 46, 46};
    g.folder = {g.fav.x + 56, by, 46, 46};
    double y = 92;
    bool seenDir = false, seenOnline = false;
    for (size_t i = 0; i < sel.cats.size(); i++) {
        const std::string& k = sel.cats[i].second;
        if (k.rfind("dir:", 0) == 0 && !seenDir) { seenDir = true; g.labels.push_back({y + 6, "FOLDERS"}); y += 32; }
        if (k.rfind("online:", 0) == 0 && !seenOnline) { seenOnline = true; g.labels.push_back({y + 6, "ONLINE"}); y += 32; }
        g.nav.push_back({24, y, 192, 40});
        y += 44;
    }
    {
        auto chipW = [](const std::string& label) { return 28.0 + 7.4 * label.size(); };
        const double cy = g.hero.y + g.hero.h - 28 - 34;
        g.chipLabelY = cy - 18;
        double x = g.hero.x + 28;
        if (sel.onlineProvider == "wallhaven") {
            g.sortLabelX = x;
            for (const auto& [label, id] : kWallhavenSorts) { g.sortChips.push_back({x, cy, chipW(label), 34}); x += chipW(label) + 6; }
            x += 20;
        }
        g.resLabelX = x;
        for (int i = 0; i < 3; i++) { g.resChips.push_back({x, cy, chipW(kOnlineResLabels[i]), 34}); x += chipW(kOnlineResLabels[i]) + 6; }
    }
    g.keyCard = {kMainX + (kMainW - 620) / 2.0, double(kHeadY), 620, 428};
    g.keyInput = {g.keyCard.x + 40, g.keyCard.y + 228, g.keyCard.w - 80, 52};
    g.keyEye = {g.keyInput.x + g.keyInput.w - 46, g.keyInput.y + 8, 36, 36};
    g.keySave = {g.keyCard.x + g.keyCard.w / 2 - 164, g.keyCard.y + 318, 160, 46};
    g.keyOpen = {g.keyCard.x + g.keyCard.w / 2 + 4, g.keyCard.y + 318, 160, 46};
    g.dlgCard = {(kSelW - 480) / 2.0, (kSelH - 432) / 2.0, 480, 432};
    g.dlgPreview = {g.dlgCard.x + 40, g.dlgCard.y + 36, 400, 225};
    g.dlgCancel = {g.dlgCard.x + 40, g.dlgCard.y + 358, 190, 46};
    g.dlgOk = {g.dlgCard.x + 250, g.dlgCard.y + 358, 190, 46};
    g.change = {double(kMainX + kMainW - 150), double(kHeadY - 8), 150, 32};
    g.retry = {kMainX + kMainW / 2.0 - 60, double(kGridTop + 56), 120, 40};
    return g;
}

void layoutSelector(App& app) {
    Selector& sel = app.sel;
    sel.chipRects.clear();
    sel.panelH = kSelH;
    sel.gridY = kGridTop;
}

Rect tileRect(const Selector& sel, int row, int col) {
    return {double(kMainX + col * (kTileW + kTileGap)), double(sel.gridY + row * (kTileH + kTileGap)), double(kTileW), double(kTileH)};
}
int totalRows(const Selector& sel) { return (static_cast<int>(sel.files.size()) + kCols - 1) / kCols; }
int maxScrollRow(const Selector& sel) { return std::max(0, totalRows(sel) - kVisRows); }

Rect tileDlBtn(const Rect& t, int which) { return {t.x + t.w - 8 - 34 - (which == 0 ? 42.0 : 0.0), t.y + t.h - 8 - 34, 34, 34}; }

struct SelHit { int tile = -1, chip = -1, mode = -1, nav = -1, btn = -1; };

SelHit selectorHit(const Selector& sel, double x, double y) {
    SelHit h;
    const SelGeom g = selGeom(sel);
    if (!sel.confirmPath.empty()) {
        if (g.dlgOk.contains(x, y)) h.btn = 15;
        else if (g.dlgCancel.contains(x, y)) h.btn = 14;
        return h;
    }
    if (g.light.contains(x, y)) h.mode = 0;
    else if (g.dark.contains(x, y)) h.mode = 1;
    const bool online = !sel.onlineProvider.empty();
    if (online && sel.keyPanel) {
        if (g.keyEye.contains(x, y)) h.btn = 9;
        else if (g.keySave.contains(x, y)) h.btn = 7;
        else if (g.keyOpen.contains(x, y)) h.btn = 8;
    } else if (online) {
        for (size_t i = 0; i < g.sortChips.size(); i++) if (g.sortChips[i].contains(x, y)) h.btn = 20 + static_cast<int>(i);
        for (size_t i = 0; i < g.resChips.size(); i++) if (g.resChips[i].contains(x, y)) h.btn = 30 + static_cast<int>(i);
        if (h.btn < 0 && providerNeedsKey(sel.onlineProvider) && g.change.contains(x, y)) h.btn = 6;
        else if (h.btn < 0 && sel.onlineState == 2 && sel.files.empty() && g.retry.contains(x, y)) h.btn = 10;
    }
    if (h.btn >= 0) {}
    else if (g.close.contains(x, y)) h.btn = 5;
    else if (g.pick.contains(x, y)) h.btn = 4;
    else if (!online && g.shuffle.contains(x, y)) h.btn = 0;
    else if (!online && g.fav.contains(x, y)) h.btn = 1;
    else if (!online && g.folder.contains(x, y)) h.btn = 2;
    for (size_t i = 0; i < g.nav.size(); i++)
        if (g.nav[i].contains(x, y)) h.nav = static_cast<int>(i);
    for (size_t i = 0; i < sel.chipRects.size(); i++)
        if (sel.chipRects[i].contains(x, y)) h.chip = static_cast<int>(i);
    for (int r = 0; r < kVisRows; r++)
        for (int c = 0; c < kCols; c++) {
            const int idx = (sel.scrollRow + r) * kCols + c;
            if (idx >= static_cast<int>(sel.files.size())) continue;
            const Rect t = tileRect(sel, r, c);
            if (!t.contains(x, y)) continue;
            h.tile = idx;
            if (isOnlineTile(sel.files[idx])) {
                if (tileDlBtn(t, 0).contains(x, y)) h.btn = 11;
                else if (tileDlBtn(t, 1).contains(x, y)) h.btn = 12;
            } else if (Rect{t.x + t.w - 38, t.y + 8, 30, 30}.contains(x, y)) h.btn = 3;
            else if (tileDlBtn(t, 1).contains(x, y)) h.btn = 13;
        }
    return h;
}

void drawSelector(App& app) {
    Selector& sel = app.sel;
    if (!sel.open() || !sel.configured) return;
    const int W = kSelW, H = kSelH;
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-selector", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const bool grown = sel.anim.phase != kPopOpen;
    const double rev = std::max(0.0, sel.anim.reveal);
    if (grown) popGroupBegin(cr);
    cairo_t* const windowCr = cr;
    const bool fromPicture = grown && sel.animPicture;
    if (grown && !sel.animPicture) {
        sel.animPicture = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cr = mkcr(sel.animPicture);
    }
    if (!grown && sel.animPicture) { cairo_surface_destroy(sel.animPicture); sel.animPicture = nullptr; }
    if (!fromPicture) {
    const SelGeom G = selGeom(sel);
    const Color white{1, 1, 1};

    roundedRect(cr, 0, 0, W, H, kSelRadius);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);
    roundedRect(cr, 0.5, 0.5, W - 1, H - 1, kSelRadius);
    setSource(cr, app.color("outline_variant"), 0.5);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto text = [&](const std::string& t, const char* fontName, double x, double y, const Color& c, double a = 1.0, double maxW = -1) {
        PangoFontDescription* fdesc = pango_font_description_from_string(fontName);
        pango_layout_set_font_description(layout, fdesc);
        pango_layout_set_width(layout, maxW > 0 ? static_cast<int>(maxW * PANGO_SCALE) : -1);
        pango_layout_set_ellipsize(layout, maxW > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
        pango_layout_set_text(layout, t.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        setSource(cr, c, a);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(fdesc);
        return w;
    };
    auto textW = [&](const std::string& t, const char* fontName) {
        PangoFontDescription* fdesc = pango_font_description_from_string(fontName);
        pango_layout_set_font_description(layout, fdesc);
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        pango_layout_set_text(layout, t.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout, &w, &h);
        pango_font_description_free(fdesc);
        return std::pair<int, int>(w, h);
    };
    auto cover = [&](cairo_surface_t* img, double x, double y, double w, double h) {
        const double iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img), sc = std::max(w / iw, h / ih);
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        cairo_translate(cr, x + (w - iw * sc) / 2, y + (h - ih * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, img, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    };

    roundedRect(cr, 12, 12, 216, H - 24, 24);
    setSource(cr, app.color("surface_container"), 0.7);
    cairo_fill(cr);
    constexpr double kShapeBox = 44;
    shapePath(cr, app.cfg.selectorShape, 28, 26, kShapeBox);
    setSource(cr, app.color("secondary_container"));
    cairo_fill(cr);
    drawIcon(cr, "gallery", 28 + (kShapeBox - 24) / 2.0, 26 + (kShapeBox - 24) / 2.0, 24, app.color("on_secondary_container"));
    text("Wallpapers", kSelTitleFont, 84, 27, app.color("on_surface"));
    text(std::to_string(sel.allFiles.size()) + " images", kSelSmallFont, 84, 50, app.color("on_surface_variant"));
    for (const auto& [ly, lt] : G.labels) text(lt, "Sans Bold 8", 38, ly, app.color("on_surface_variant"), 0.8);
    const bool navSliding = sel.navAnim.moving() && !G.nav.empty();
    if (navSliding) {
        const double pos = sel.navAnim.drawPos(static_cast<int>(G.nav.size()), 40, 0);
        const size_t i0 = static_cast<size_t>(std::clamp(static_cast<int>(std::floor(pos)), 0, static_cast<int>(G.nav.size()) - 1));
        const size_t i1 = std::min(i0 + 1, G.nav.size() - 1);
        const double f = std::clamp(pos - static_cast<double>(i0), 0.0, 1.0);
        roundedRect(cr, G.nav[i0].x, G.nav[i0].y + (G.nav[i1].y - G.nav[i0].y) * f, G.nav[i0].w, G.nav[i0].h, 20);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
    }
    for (size_t i = 0; i < G.nav.size(); i++) {
        const Rect& r = G.nav[i];
        const bool on = navSliding ? std::fabs(sel.navAnim.pos - static_cast<double>(i)) < 0.5 : static_cast<int>(i) == sel.cat;
        const std::string& kind = sel.cats[i].second;
        if (on && !navSliding) {
            roundedRect(cr, r.x, r.y, r.w, r.h, 20);
            setSource(cr, app.color("secondary_container"));
            cairo_fill(cr);
        } else if (!on && sel.hoverNav == static_cast<int>(i)) {
            roundedRect(cr, r.x, r.y, r.w, r.h, 20);
            setSource(cr, app.color("on_surface"), 0.08);
            cairo_fill(cr);
        }
        const Color ink = app.color(on ? "on_secondary_container" : "on_surface_variant");
        const char* ic = kind == "all" ? "gallery" : kind == "fav" ? "star" : kind == "recent" ? "history" : kind.rfind("online:", 0) == 0 ? "global" : "folder";
        drawIcon(cr, ic, r.x + 14, r.y + 10, 20, ink, on);
        text(sel.cats[i].first, "Sans 10", r.x + 46, r.y + 11, app.color(on ? "on_secondary_container" : "on_surface"), 1.0, r.w - 58);
    }

    roundedRect(cr, G.search.x, G.search.y, G.search.w, G.search.h, 22);
    setSource(cr, app.color("surface_container_high"));
    cairo_fill(cr);
    drawIcon(cr, "magnifer", G.search.x + 16, G.search.y + 13, 18, app.color("on_surface_variant"));
    if (sel.query.empty()) {
        text(sel.onlineProvider.empty() ? "Search wallpapers by name…" : "Search " + providerLabel(sel.onlineProvider) + "… and press Enter", "Sans 10", G.search.x + 46, G.search.y + 13, app.color("on_surface_variant"), 0.8);
    } else {
        textsel::Field qf(sel.query, sel.qCur, sel.qAnc);
        const double tx0 = G.search.x + 46, maxW = G.search.w - 160;
        const auto xAt = [&](size_t i) { return std::min(maxW, static_cast<double>(textW(sel.query.substr(0, i), "Sans 10").first)); };
        if (qf.hasSel()) {
            const double x0 = xAt(qf.lo()), x1 = xAt(qf.hi());
            roundedRect(cr, tx0 + x0 - 1, G.search.y + 8, x1 - x0 + 2, G.search.h - 16, 7);
            setSource(cr, app.color("primary"), 0.35);
            cairo_fill(cr);
        }
        text(sel.query, "Sans 10", tx0, G.search.y + 13, app.color("on_surface"), 1.0, maxW);
        setSource(cr, app.color("primary"));
        cairo_rectangle(cr, tx0 + 1 + xAt(qf.caret), G.search.y + 11, 1.5, 22);
        cairo_fill(cr);
    }
    {
        const std::string cnt = sel.onlineProvider.empty() || sel.onlineState != 1 ? std::to_string(sel.files.size()) + (sel.files.size() == 1 ? " result" : " results") : "Loading…";
        const auto [cw, ch] = textW(cnt, kSelSmallFont);
        text(cnt, kSelSmallFont, G.search.x + G.search.w - 18 - cw, G.search.y + (G.search.h - ch) / 2.0, app.color("on_surface_variant"));
    }

    {
        const Rect& h = G.hero;
        // The layers (base, picture, shade) go into a group and the rounded shape cuts it once: clipping every layer to the curve
        // blended each one separately at the edge pixels, and the unshaded picture showed through as a fringe of light dots.
        cairo_save(cr);
        cairo_rectangle(cr, h.x, h.y, h.w, h.h);
        cairo_clip(cr);
        cairo_push_group(cr);
        setSource(cr, app.color("surface_container_highest"));
        cairo_paint(cr);
        cairo_surface_t* big = heroFor(app);
        if (!big) {
            const auto it = sel.thumbs.find(app.wallpaper);
            big = it != sel.thumbs.end() ? it->second : nullptr;
        }
        if (big) cover(big, h.x, h.y, h.w, h.h);
        cairo_pattern_t* shade = cairo_pattern_create_linear(h.x, 0, h.x + h.w * 0.7, 0);
        cairo_pattern_add_color_stop_rgba(shade, 0, 0, 0, 0, 0.78);
        cairo_pattern_add_color_stop_rgba(shade, 0.55, 0, 0, 0, 0.38);
        cairo_pattern_add_color_stop_rgba(shade, 1, 0, 0, 0, 0.0);
        cairo_set_source(cr, shade);
        cairo_paint(cr);
        cairo_pattern_destroy(shade);
        cairo_pop_group_to_source(cr);
        roundedRect(cr, h.x, h.y, h.w, h.h, 28);
        cairo_fill(cr);
        cairo_restore(cr);

        const double tx = h.x + 28;
        roundedRect(cr, tx, h.y + 26, 112, 28, 14);
        setSource(cr, white, 0.16);
        cairo_fill(cr);
        cairo_arc(cr, tx + 16, h.y + 40, 4, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        text("Now using", "Sans Bold 9", tx + 28, h.y + 32, white, 0.95);
        const std::string title = app.wallpaper.empty() ? "No wallpaper" : baseName(app.wallpaper, true);
        text(title, "Sans Bold 26", tx, h.y + 72, white, 1.0, h.w - 56);
        std::string detail;
        if (sel.heroW > 0) detail = std::to_string(sel.heroW) + " × " + std::to_string(sel.heroH);
        if (!app.wallpaper.empty()) detail += (detail.empty() ? "" : "  ·  ") + baseName(dirName(app.wallpaper), false);
        text(detail, "Sans 10", tx, h.y + 112, white, 0.78, h.w - 56);

        if (!sel.onlineProvider.empty()) {
            auto chip = [&](const Rect& r, const std::string& label, bool on, bool hot) {
                roundedRect(cr, r.x, r.y, r.w, r.h, 17);
                if (on) setSource(cr, app.color("primary"));
                else setSource(cr, Color{0, 0, 0}, hot ? 0.62 : 0.42);
                cairo_fill(cr);
                if (!on) {
                    roundedRect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 16.5);
                    setSource(cr, white, hot ? 0.30 : 0.14);
                    cairo_set_line_width(cr, 1);
                    cairo_stroke(cr);
                }
                const auto [lw, lh] = textW(label, "Sans Bold 10");
                text(label, "Sans Bold 10", r.x + (r.w - lw) / 2.0, r.y + (r.h - lh) / 2.0, on ? app.color("on_primary") : white);
            };
            if (!sel.keyPanel) {
                if (!G.sortChips.empty()) text("ORDER", "Sans Bold 8", G.sortLabelX + 4, G.chipLabelY, white, 0.75);
                for (size_t i = 0; i < G.sortChips.size(); i++) chip(G.sortChips[i], kWallhavenSorts[i].first, kWallhavenSorts[i].second == sel.onlineSort, sel.hoverBtn == 20 + static_cast<int>(i));
                text("SIZE", "Sans Bold 8", G.resLabelX + 4, G.chipLabelY, white, 0.75);
                for (int i = 0; i < 3; i++) chip(G.resChips[i], kOnlineResLabels[i], i == sel.onlineRes, sel.hoverBtn == 30 + i);
            }
        } else {
        const bool shuffleHot = sel.hoverBtn == 0;
        roundedRect(cr, G.shuffle.x, G.shuffle.y, G.shuffle.w, G.shuffle.h, 23);
        setSource(cr, app.color(shuffleHot ? "primary_fixed_dim" : "primary"));
        cairo_fill(cr);
        drawIcon(cr, "shuffle", G.shuffle.x + 22, G.shuffle.y + 13, 20, app.color("on_primary"), true);
        text("Shuffle", "Sans Bold 11", G.shuffle.x + 54, G.shuffle.y + 13, app.color("on_primary"));
        const bool isFav = !app.wallpaper.empty() && isFavourite(sel, app.wallpaper);
        for (int b = 1; b <= 2; b++) {
            const Rect& r = b == 1 ? G.fav : G.folder;
            cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
            setSource(cr, white, sel.hoverBtn == b ? 0.30 : 0.18);
            cairo_fill(cr);
            drawIcon(cr, b == 1 ? "star" : "folder_open", r.x + 13, r.y + 13, 20, b == 1 && isFav ? app.color("primary") : white, b == 1 && isFav);
        }
        }

        cairo_arc(cr, G.pick.x + G.pick.w / 2, G.pick.y + G.pick.h / 2, G.pick.w / 2, 0, 2 * M_PI);
        setSource(cr, Color{0, 0, 0}, sel.hoverBtn == 4 ? 0.62 : 0.42);
        cairo_fill(cr);
        cairo_arc(cr, G.pick.x + G.pick.w / 2, G.pick.y + G.pick.h / 2, G.pick.w / 2 - 0.5, 0, 2 * M_PI);
        setSource(cr, white, sel.hoverBtn == 4 ? 0.30 : 0.14);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        drawIcon(cr, "pipette", G.pick.x + (G.pick.w - 22) / 2.0, G.pick.y + (G.pick.h - 22) / 2.0, 22, white);
    }

    {
        const bool hot = sel.hoverBtn == 5;
        const Color base = app.color("surface_container_highest"), err = app.color("error");
        cairo_new_sub_path(cr);
        cairo_arc(cr, G.close.x + G.close.w / 2, G.close.y + G.close.h / 2, G.close.w / 2, 0, 2 * M_PI);
        setSource(cr, hot ? Color{err.r * 0.3 + base.r * 0.7, err.g * 0.3 + base.g * 0.7, err.b * 0.3 + base.b * 0.7} : base);
        cairo_fill(cr);
        drawIcon(cr, "close-circle", G.close.x + 9, G.close.y + 9, 22, hot ? err : app.color("on_surface_variant"));
    }

    {
        roundedRect(cr, G.light.x, G.light.y, G.light.w * 2, G.light.h, 23);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        const double hx = G.light.x + (G.dark.x - G.light.x) * sel.modeT;
        roundedRect(cr, hx + 4, G.light.y + 4, G.light.w - 8, G.light.h - 8, 19);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        for (int m = 0; m < 2; m++) {
            const Rect& r = m == 0 ? G.light : G.dark;
            const double closeness = m == 0 ? 1 - sel.modeT : sel.modeT;
            if (sel.hoverMode == m && closeness < 0.5) {
                roundedRect(cr, r.x + 4, r.y + 4, r.w - 8, r.h - 8, 19);
                setSource(cr, app.color("on_surface"), 0.08);
                cairo_fill(cr);
            }
            const Color on = app.color("on_primary"), off = app.color("on_surface_variant");
            const Color ink{off.r + (on.r - off.r) * closeness, off.g + (on.g - off.g) * closeness, off.b + (on.b - off.b) * closeness};
            const std::string label = m == 0 ? "Light" : "Dark";
            const auto [lw, lh] = textW(label, "Sans Bold 10");
            const double cx = r.x + (r.w - (20 + 8 + lw)) / 2.0;
            drawIcon(cr, m == 0 ? "light_mode" : "dark_mode", cx, r.y + (r.h - 20) / 2.0, 20, ink, closeness > 0.5);
            text(label, "Sans Bold 10", cx + 28, r.y + (r.h - lh) / 2.0, ink);
        }
    }

    const bool onlineList = !sel.onlineProvider.empty();
    {
        const std::string& kind = sel.cats[std::clamp(sel.cat, 0, static_cast<int>(sel.cats.size()) - 1)].second;
        const std::string name = kind == "all" ? "All wallpapers" : kind == "fav" ? "Favorites" : kind == "recent" ? "Recently used" : sel.cats[sel.cat].first;
        text(name, "Sans Bold 13", kMainX + 4, kHeadY + 2, app.color("on_surface"));
        if (onlineList && providerNeedsKey(sel.onlineProvider) && !sel.keyPanel) {
            roundedRect(cr, G.change.x, G.change.y, G.change.w, G.change.h, 16);
            setSource(cr, app.color("surface_container_high"));
            cairo_fill(cr);
            if (sel.hoverBtn == 6) { roundedRect(cr, G.change.x, G.change.y, G.change.w, G.change.h, 16); setSource(cr, app.color("on_surface"), 0.08); cairo_fill(cr); }
            drawIcon(cr, "key", G.change.x + 14, G.change.y + 6, 20, app.color("on_surface_variant"));
            text("Change API key", "Sans Bold 9", G.change.x + 42, G.change.y + 8, app.color("on_surface_variant"));
        }
        {
            const bool isErr = !sel.onlineError.empty() && !sel.files.empty();
            const std::string msg = isErr ? sel.onlineError : sel.onlineNote;
            if (msg != sel.msgShown) { sel.msgShown = msg; sel.msgAt = std::chrono::steady_clock::now(); }
            const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - sel.msgAt).count();
            if (!msg.empty() && age >= 4.0) { sel.onlineNote.clear(); if (isErr) sel.onlineError.clear(); sel.msgShown.clear(); }
            else if (!msg.empty()) {
                const double alpha = std::clamp((4.0 - age) / 0.5, 0.0, 1.0);
                const double right = onlineList && providerNeedsKey(sel.onlineProvider) && !sel.keyPanel ? G.change.x - 12 : kMainX + kMainW - 4;
                const auto [mw, mh] = textW(msg, "Sans 10");
                const double bw = std::min<double>(mw + 28, right - kMainX - 200), bx = right - bw, by = kHeadY - 8;
                roundedRect(cr, bx, by, bw, 32, 16);
                setSource(cr, isErr ? app.color("error_container") : app.color("secondary_container"), alpha);
                cairo_fill(cr);
                text(msg, "Sans 10", bx + 14, by + (32 - mh) / 2.0, isErr ? app.color("on_error_container") : app.color("on_secondary_container"), alpha, bw - 28);
            }
        }
    }
    if (onlineList && sel.keyPanel) {
        const Rect& c = G.keyCard;
        roundedRect(cr, c.x, c.y, c.w, c.h, 28);
        setSource(cr, app.color("surface_container"));
        cairo_fill(cr);
        cairo_arc(cr, c.x + c.w / 2, c.y + 46, 28, 0, 2 * M_PI);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
        drawIcon(cr, "key", c.x + c.w / 2 - 14, c.y + 32, 28, app.color("on_secondary_container"), true);
        const std::string pname = providerLabel(sel.onlineProvider);
        {
            const std::string title = pname + " needs an API key";
            const auto [tw, th] = textW(title, "Sans Bold 16");
            text(title, "Sans Bold 16", c.x + (c.w - tw) / 2.0, c.y + 88, app.color("on_surface"));
            (void)th;
        }
        const bool unsplash = sel.onlineProvider == "unsplash";
        const std::string steps = unsplash
            ? "1.  Open unsplash.com/developers and sign in.\n2.  Create a new application and accept the terms.\n3.  Copy the Access Key and paste it below."
            : "1.  Open pexels.com/api and sign in.\n2.  Ask for a free API key.\n3.  Copy it and paste it below.";
        {
            std::istringstream lines(steps);
            std::string ln;
            for (int i = 0; std::getline(lines, ln); i++) text(ln, "Sans 10", c.x + 56, c.y + 128 + i * 25, app.color("on_surface_variant"), 1.0, c.w - 112);
        }
        const Rect& in = G.keyInput;
        roundedRect(cr, in.x, in.y, in.w, in.h, 26);
        setSource(cr, app.color("surface_container_highest"));
        cairo_fill(cr);
        roundedRect(cr, in.x + 1, in.y + 1, in.w - 2, in.h - 2, 25);
        setSource(cr, app.color(sel.keyMsg.empty() ? "primary" : "error"), 0.9);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        drawIcon(cr, "lock", in.x + 18, in.y + 15, 22, app.color("on_surface_variant"));
        if (sel.keyInput.empty()) {
            text("Paste your key here", "Sans 11", in.x + 52, in.y + 15, app.color("on_surface_variant"), 0.8);
        } else {
            std::string shown;
            if (sel.keyReveal) shown = sel.keyInput;
            else for (size_t i = 0; i < sel.keyInput.size(); i++) shown += "•";
            const int tw = text(shown, "Sans 11", in.x + 52, in.y + 15, app.color("on_surface"), 1.0, in.w - 52 - 60);
            setSource(cr, app.color("primary"));
            cairo_rectangle(cr, in.x + 53 + tw, in.y + 13, 1.5, 26);
            cairo_fill(cr);
        }
        if (sel.hoverBtn == 9) {
            cairo_arc(cr, G.keyEye.x + 18, G.keyEye.y + 18, 18, 0, 2 * M_PI);
            setSource(cr, app.color("on_surface"), 0.10);
            cairo_fill(cr);
        }
        drawIcon(cr, sel.keyReveal ? "eye-closed" : "eye", G.keyEye.x + 8, G.keyEye.y + 8, 20, app.color("on_surface_variant"));
        if (!sel.keyMsg.empty()) text(sel.keyMsg, "Sans 10", in.x + 18, in.y + in.h + 8, app.color("error"), 1.0, in.w - 36);
        const bool canSave = !sel.keyInput.empty() && !sel.keySaving;
        roundedRect(cr, G.keySave.x, G.keySave.y, G.keySave.w, G.keySave.h, 23);
        if (canSave) setSource(cr, app.color(sel.hoverBtn == 7 ? "primary_fixed_dim" : "primary"));
        else setSource(cr, app.color("on_surface"), 0.12);
        cairo_fill(cr);
        {
            const std::string lab = sel.keySaving ? "Saving…" : "Save key";
            const auto [lw, lh] = textW(lab, "Sans Bold 11");
            text(lab, "Sans Bold 11", G.keySave.x + (G.keySave.w - lw) / 2.0, G.keySave.y + (G.keySave.h - lh) / 2.0, canSave ? app.color("on_primary") : app.color("on_surface"), canSave ? 1.0 : 0.38);
        }
        roundedRect(cr, G.keyOpen.x, G.keyOpen.y, G.keyOpen.w, G.keyOpen.h, 23);
        setSource(cr, app.color("secondary_container"), sel.hoverBtn == 8 ? 1.0 : 0.7);
        cairo_fill(cr);
        {
            const auto [lw, lh] = textW("Open the site", "Sans Bold 11");
            text("Open the site", "Sans Bold 11", G.keyOpen.x + (G.keyOpen.w - lw) / 2.0, G.keyOpen.y + (G.keyOpen.h - lh) / 2.0, app.color("on_secondary_container"));
        }
        {
            const std::string note = "Saved in your system keyring (GNOME Keyring, KWallet…), never in a file.";
            const auto [nw, nh] = textW(note, "Sans 9");
            const double nx = c.x + (c.w - (nw + 24)) / 2.0, ny = c.y + 392;
            (void)nh;
            drawIcon(cr, "lock-keyhole", nx, ny, 16, app.color("on_surface_variant"), false);
            text(note, "Sans 9", nx + 22, ny + 1, app.color("on_surface_variant"));
        }
    } else if (sel.files.empty()) {
        const std::string& kind = sel.cats[std::clamp(sel.cat, 0, static_cast<int>(sel.cats.size()) - 1)].second;
        std::string msg = !sel.query.empty() && !onlineList ? "Nothing matches “" + sel.query + "”"
                        : kind == "fav" ? "No favorites yet: the star on a wallpaper adds it here."
                        : kind == "recent" ? "Wallpapers you apply show up here."
                        : onlineList ? (sel.onlineState == 1 ? "Looking for wallpapers…" : sel.onlineState == 2 ? sel.onlineError : "Nothing found. Try another search.")
                        : "No wallpapers in " + app.cfg.wallpaperDir;
        text(msg, "Sans 11", kMainX + 4, sel.gridY + 24, app.color(onlineList && sel.onlineState == 2 ? "error" : "on_surface_variant"));
        if (onlineList && sel.onlineState == 2) {
            roundedRect(cr, G.retry.x, G.retry.y, G.retry.w, G.retry.h, 20);
            setSource(cr, app.color("secondary_container"), sel.hoverBtn == 10 ? 1.0 : 0.7);
            cairo_fill(cr);
            const auto [rw, rh] = textW("Try again", "Sans Bold 10");
            text("Try again", "Sans Bold 10", G.retry.x + (G.retry.w - rw) / 2.0, G.retry.y + (G.retry.h - rh) / 2.0, app.color("on_secondary_container"));
        }
    }
    for (int r = 0; r < kVisRows; r++)
        for (int c = 0; c < kCols; c++) {
            const int idx = (sel.scrollRow + r) * kCols + c;
            if (idx >= static_cast<int>(sel.files.size())) continue;
            const Rect t = tileRect(sel, r, c);
            cairo_save(cr);
            cairo_rectangle(cr, t.x, t.y, t.w, t.h);
            cairo_clip(cr);
            cairo_push_group(cr);
            if (cairo_surface_t* th = thumbFor(app, sel.files[idx])) {
                cairo_set_source_surface(cr, th, t.x, t.y);
                cairo_paint(cr);
            } else {
                setSource(cr, app.color("surface_container_highest"));
                cairo_paint(cr);
                drawIcon(cr, "gallery", t.x + (t.w - 28) / 2.0, t.y + (t.h - 28) / 2.0, 28, app.color("on_surface_variant"));
            }
            const bool hot = sel.hoverTile == idx;
            if (hot) {
                setSource(cr, app.color("on_surface"), 0.10);
                cairo_paint(cr);
                cairo_pattern_t* fade = cairo_pattern_create_linear(0, t.y + t.h - 46, 0, t.y + t.h);
                cairo_pattern_add_color_stop_rgba(fade, 0, 0, 0, 0, 0);
                cairo_pattern_add_color_stop_rgba(fade, 1, 0, 0, 0, 0.7);
                cairo_set_source(cr, fade);
                cairo_paint(cr);
                cairo_pattern_destroy(fade);
                const OnlineItem* oi = isOnlineTile(sel.files[idx]) ? onlineItemFor(sel, sel.files[idx]) : nullptr;
                std::string label = oi ? (!oi->title.empty() ? oi->title : oi->author) : baseName(sel.files[idx], true);
                if (oi && !oi->author.empty() && !oi->title.empty()) label = oi->author + " · " + oi->title;
                text(label, "Sans 9", t.x + 12, t.y + t.h - 24, white, 0.95, oi ? t.w - 24 - 92 : t.w - 24 - 46);
                if (!oi) {
                    const Rect r = tileDlBtn(t, 1);
                    const bool bh = sel.hoverBtn == 13;
                    cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
                    setSource(cr, bh ? app.color("error") : Color{0, 0, 0}, bh ? 0.95 : 0.6);
                    cairo_fill(cr);
                    drawIcon(cr, "trash-bin-minimalistic", r.x + 7, r.y + 7, 20, bh ? app.color("on_error") : white);
                }
                if (oi) {
                    for (int b = 0; b < 2; b++) {
                        const Rect r = tileDlBtn(t, b);
                        const bool bh = sel.hoverBtn == 11 + b;
                        cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
                        if (b == 1) setSource(cr, app.color(bh ? "primary_fixed_dim" : "primary"));
                        else setSource(cr, Color{0, 0, 0}, bh ? 0.82 : 0.6);
                        cairo_fill(cr);
                        if (b == 0) { cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - 0.5, 0, 2 * M_PI); setSource(cr, white, 0.25); cairo_set_line_width(cr, 1); cairo_stroke(cr); }
                        drawIcon(cr, b == 0 ? "download-minimalistic" : "gallery-check", r.x + 7, r.y + 7, 20, b == 0 ? white : app.color("on_primary"), b == 1);
                    }
                }
            }
            const OnlineItem* oit = isOnlineTile(sel.files[idx]) ? onlineItemFor(sel, sel.files[idx]) : nullptr;
            if (oit && sel.downloading.count(sel.onlineProvider + ":" + oit->id)) {
                setSource(cr, Color{0, 0, 0}, 0.58);
                cairo_paint(cr);
                const auto [dw, dh] = textW("Downloading…", "Sans Bold 10");
                text("Downloading…", "Sans Bold 10", t.x + (t.w - dw) / 2.0, t.y + (t.h - dh) / 2.0, white);
            }
            cairo_pop_group_to_source(cr);
            roundedRect(cr, t.x, t.y, t.w, t.h, 18);
            cairo_fill(cr);
            cairo_restore(cr);
            const bool fav = !oit && isFavourite(sel, sel.files[idx]);
            if (!oit && (hot || fav)) {
                const bool starHot = hot && sel.hoverBtn == 3;
                cairo_arc(cr, t.x + t.w - 23, t.y + 23, 15, 0, 2 * M_PI);
                setSource(cr, Color{0, 0, 0}, starHot ? 0.62 : 0.42);
                cairo_fill(cr);
                drawIcon(cr, "star", t.x + t.w - 33, t.y + 13, 20, fav ? app.color("primary") : white, fav);
            }
            if (sel.kbFocus && idx == sel.focusIdx) {
                roundedRect(cr, t.x - 4, t.y - 4, t.w + 8, t.h + 8, 22);
                setSource(cr, app.color("tertiary"));
                cairo_set_line_width(cr, 2.5);
                cairo_stroke(cr);
            }
            if (sel.files[idx] == app.wallpaper) {
                roundedRect(cr, t.x + 1.5, t.y + 1.5, t.w - 3, t.h - 3, 17);
                setSource(cr, app.color("primary"));
                cairo_set_line_width(cr, 3);
                cairo_stroke(cr);
            }
        }

    if (totalRows(sel) > kVisRows && sel.overGrid) {
        const double trackH = kVisRows * kTileH + (kVisRows - 1) * kTileGap;
        const double thumbH = std::max(24.0, trackH * kVisRows / totalRows(sel));
        const double thumbY = sel.gridY + (trackH - thumbH) * sel.scrollRow / std::max(1, maxScrollRow(sel));
        roundedRect(cr, kSelW - 12, thumbY, 4, thumbH, 2);
        setSource(cr, app.color("outline"), 0.7);
        cairo_fill(cr);
    }

    if (!sel.confirmPath.empty()) {
        roundedRect(cr, 0, 0, W, H, kSelRadius);
        setSource(cr, Color{0, 0, 0}, 0.58);
        cairo_fill(cr);
        const Rect& c = G.dlgCard;
        roundedRect(cr, c.x, c.y, c.w, c.h, 28);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        cairo_save(cr);
        cairo_rectangle(cr, G.dlgPreview.x, G.dlgPreview.y, G.dlgPreview.w, G.dlgPreview.h);
        cairo_clip(cr);
        cairo_push_group(cr);
        setSource(cr, app.color("surface_container_highest"));
        cairo_paint(cr);
        if (cairo_surface_t* th = thumbFor(app, sel.confirmPath)) cover(th, G.dlgPreview.x, G.dlgPreview.y, G.dlgPreview.w, G.dlgPreview.h);
        cairo_pop_group_to_source(cr);
        roundedRect(cr, G.dlgPreview.x, G.dlgPreview.y, G.dlgPreview.w, G.dlgPreview.h, 18);
        cairo_fill(cr);
        cairo_restore(cr);
        const std::string name = baseName(sel.confirmPath, false);
        const auto [tw, th2] = textW("Move to trash?", "Sans Bold 15");
        text("Move to trash?", "Sans Bold 15", c.x + (c.w - tw) / 2.0, c.y + 278, app.color("on_surface"));
        (void)th2;
        const auto [nw, nh] = textW(name, "Sans 10");
        text(name, "Sans 10", c.x + std::max(40.0, (c.w - std::min<double>(nw, c.w - 80)) / 2.0), c.y + 308, app.color("on_surface_variant"), 1.0, c.w - 80);
        (void)nh;
        {
            const std::string hint = "You can restore it from the trash.";
            const auto [hw, hh] = textW(hint, "Sans 9");
            text(hint, "Sans 9", c.x + (c.w - hw) / 2.0, c.y + 330, app.color("on_surface_variant"), 0.8);
            (void)hh;
        }
        roundedRect(cr, G.dlgCancel.x, G.dlgCancel.y, G.dlgCancel.w, G.dlgCancel.h, 23);
        setSource(cr, app.color("secondary_container"), sel.hoverBtn == 14 ? 1.0 : 0.7);
        cairo_fill(cr);
        {
            const auto [lw, lh] = textW("Cancel", "Sans Bold 11");
            text("Cancel", "Sans Bold 11", G.dlgCancel.x + (G.dlgCancel.w - lw) / 2.0, G.dlgCancel.y + (G.dlgCancel.h - lh) / 2.0, app.color("on_secondary_container"));
        }
        roundedRect(cr, G.dlgOk.x, G.dlgOk.y, G.dlgOk.w, G.dlgOk.h, 23);
        setSource(cr, app.color("error"), sel.hoverBtn == 15 ? 1.0 : 0.88);
        cairo_fill(cr);
        {
            const auto [lw, lh] = textW("Move to trash", "Sans Bold 11");
            const double cx = G.dlgOk.x + (G.dlgOk.w - (22 + 8 + lw)) / 2.0;
            drawIcon(cr, "trash-bin-minimalistic", cx, G.dlgOk.y + (G.dlgOk.h - 22) / 2.0, 22, app.color("on_error"));
            text("Move to trash", "Sans Bold 11", cx + 30, G.dlgOk.y + (G.dlgOk.h - lh) / 2.0, app.color("on_error"));
        }
    }

    {
        const char* tip = sel.hoverBtn == 4 ? "Pick a color" : sel.hoverBtn == 2 ? "Open the folder" : sel.hoverBtn == 1 ? (!app.wallpaper.empty() && isFavourite(sel, app.wallpaper) ? "Remove from favorites" : "Add to favorites")
                        : sel.hoverBtn == 11 ? "Download" : sel.hoverBtn == 12 ? "Download and use as wallpaper" : sel.hoverBtn == 13 ? "Move to trash" : nullptr;
        Rect tileBtn{};
        if ((sel.hoverBtn == 11 || sel.hoverBtn == 12 || sel.hoverBtn == 13) && sel.hoverTile >= 0) {
            const int rel = sel.hoverTile - sel.scrollRow * kCols;
            tileBtn = tileDlBtn(tileRect(sel, rel / kCols, rel % kCols), sel.hoverBtn == 13 ? 1 : sel.hoverBtn - 11);
        }
        const Rect* anchor = sel.hoverBtn == 5 ? &G.close : sel.hoverBtn == 4 ? &G.pick : sel.hoverBtn == 2 ? &G.folder : (sel.hoverBtn >= 11 && sel.hoverBtn <= 13) ? &tileBtn : &G.fav;
        if (tip) bubbleShow(sel.bubble, std::string("btn") + std::to_string(sel.hoverBtn) + tip, tip, anchor->x, anchor->y, anchor->w, anchor->h, kSelW, kSelH);
        else bubbleHide(sel.bubble);
        const Color bg = app.color("inverse_surface"), fg = app.color("inverse_on_surface");
        sel.bubbleMoving = bubbleDraw(cr, sel.bubble, {bg.r, bg.g, bg.b}, {fg.r, fg.g, fg.b});
    }

    g_object_unref(layout);
    }
    if (grown) {
        if (cr != windowCr) { cairo_destroy(cr); cr = windowCr; }
        cairo_set_source_surface(cr, sel.animPicture, 0, 0);
        cairo_paint(cr);
        if (rev > 0.001) popGroupEnd(cr, rev, W / 2.0, H / 2.0, sel.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8));
        else cairo_pattern_destroy(cairo_pop_group(cr));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, sel.surface, sel.blur, W, H, [&](cairo_t* m) {
        if (grown && rev <= 0.001) return;
        const auto panel = [&] { roundedRect(m, 0, 0, W, H, kSelRadius); cairo_fill(m); };
        if (grown) popPaintGrown(m, rev, W / 2.0, H / 2.0, 1.0, panel);
        else panel();
    });
    wl_surface_attach(sel.surface, buffer, 0, 0);
    wl_surface_damage_buffer(sel.surface, 0, 0, W, H);
    wl_surface_commit(sel.surface);
}

void closeSelector(App& app) {
    Selector& sel = app.sel;
    if (!sel.open()) return;
    if (sel.layer) zwlr_layer_surface_v1_destroy(sel.layer);
    releaseBlurSurface(sel.blur);
    wl_surface_destroy(sel.surface);
    for (auto& [path, surf] : sel.thumbs)
        if (surf) cairo_surface_destroy(surf);
    if (sel.hero) cairo_surface_destroy(sel.hero);
    if (sel.animPicture) cairo_surface_destroy(sel.animPicture);
    sel.animPicture = nullptr;
    sel.hero = nullptr;
    sel.heroPath.clear();
    sel.surface = nullptr;
    sel.layer = nullptr;
    sel.configured = false;
    sel.anim = PopAnim{};
    sel.thumbs.clear();
    sel.requested.clear();
    sel.jobs.clear();
    sel.allFiles.clear();
    sel.files.clear();
    sel.onlineItems.clear();
    sel.onlineProvider.clear();
    ++sel.onlineGen;
    std::fill(sel.keyInput.begin(), sel.keyInput.end(), '\0');
    sel.keyInput.clear();
    sel.keyPanel = sel.keyReveal = sel.keySaving = false;
    sel.confirmPath.clear();
    sel.hoverTile = sel.hoverChip = sel.hoverMode = sel.hoverNav = sel.hoverBtn = -1;
    sel.kbFocus = false;
    app.repeatKey = 0;
    app.onSelector = false;
    malloc_trim(0);
    updateCursor(app);
}

void onSelectorConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->sel.configured) app->sel.anim.animStart = std::chrono::steady_clock::now();
    app->sel.configured = true;
    drawSelector(*app);
}
void onSelectorClosed(void* d, zwlr_layer_surface_v1*) { closeSelector(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kSelectorListener = {onSelectorConfigure, onSelectorClosed};

void openSelector(App& app) {
    Selector& sel = app.sel;
    if (sel.open()) return;
    closeLauncher(app);
    closeNotifPanel(app);
    const std::string root = expandHome(app.cfg.wallpaperDir);
    sel.allFiles = collectWallpapers(root);
    sel.cats.clear();
    sel.cats.push_back({"All", "all"});
    sel.cats.push_back({"Favorites", "fav"});
    sel.cats.push_back({"Recent", "recent"});
    int shown = 0;
    for (const std::string& dir : wallpaperFolders(root)) {
        for (const auto& f : collectWallpapers(dir)) sel.allFiles.push_back(f);
        if (shown++ < kMaxFolders) sel.cats.push_back({baseName(dir, false), "dir:" + dir});
    }
    for (const auto& [id, name] : kOnlineProviders) sel.cats.push_back({name, "online:" + id});
    sortNewestFirst(sel.allFiles);
    sel.favs = readPathList(wpListFile("wallpaper-favorites.txt"));
    sel.recents = readPathList(wpListFile("wallpaper-recent.txt"));
    sel.cat = 0;
    sel.navAnim.snap(0);
    sel.query.clear();
    loadOnlinePrefs(sel);
    sel.onlineProvider.clear();
    sel.onlineItems.clear();
    sel.keyPanel = sel.keyReveal = sel.keySaving = false;
    sel.onlineState = 0;
    sel.onlineError.clear();
    sel.modeT = sel.modeTarget = app.cfg.colorMode == "dark" ? 1 : 0;
    layoutSelector(app);
    rebuildSelectorList(app);
    sel.kbFocus = false;
    for (size_t i = 0; i < sel.files.size(); i++)
        if (sel.files[i] == app.wallpaper) {
            sel.scrollRow = std::clamp(static_cast<int>(i) / kCols - 1, 0, maxScrollRow(sel));
            sel.focusIdx = static_cast<int>(i);
        }
    sel.surface = wl_compositor_create_surface(app.compositor);
    sel.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, sel.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-selector");
    zwlr_layer_surface_v1_set_size(sel.layer, kSelW, kSelH);
    zwlr_layer_surface_v1_set_exclusive_zone(sel.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(sel.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(sel.layer, &kSelectorListener, &app);
    sel.anim = PopAnim{};
    sel.anim.overshoot = 1.1;
    popStart(sel.anim);
    wl_surface_commit(sel.surface);
}

void retractSelector(App& app) {
    Selector& sel = app.sel;
    if (!sel.open() || popIsOut(sel.anim)) return;
    if (!sel.configured) { closeSelector(app); return; }
    popRetract(sel.anim, std::chrono::steady_clock::now());
    sel.hoverTile = sel.hoverChip = sel.hoverMode = sel.hoverNav = sel.hoverBtn = -1;
    sel.overGrid = false;
    drawSelector(app);
}

void toggleSelector(App& app) {
    Selector& sel = app.sel;
    if (!sel.open()) { openSelector(app); return; }
    if (popIsOut(sel.anim)) {
        popReverse(sel.anim, std::chrono::steady_clock::now());
        drawSelector(app);
    } else retractSelector(app);
}

void selectorMotion(App& app);
void tickSelector(App& app, std::chrono::steady_clock::time_point now) {
    Selector& sel = app.sel;
    if (!sel.open() || !sel.configured) return;
    const int before = sel.anim.phase;
    bool moving = false;
    if (popTick(sel.anim, now, moving)) { closeSelector(app); return; }
    if (moving || before != sel.anim.phase) drawSelector(app);
    if (before == kPopIn && sel.anim.phase == kPopOpen) selectorMotion(app);
}
std::chrono::steady_clock::time_point selectorWake(const App& app) {
    if (!app.sel.open() || !app.sel.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.sel.anim.phase, app.sel.anim.settleAt, std::chrono::steady_clock::now());
}

void applyTheme(App& app, const std::string& wallpaper) {
    if (wallpaper.empty()) return;
    Selector& sel = app.sel;
    std::vector<std::string> rec = sel.open() ? sel.recents : readPathList(wpListFile("wallpaper-recent.txt"));
    rec.erase(std::remove(rec.begin(), rec.end(), wallpaper), rec.end());
    rec.insert(rec.begin(), wallpaper);
    if (rec.size() > 30) rec.resize(30);
    writePathList(wpListFile("wallpaper-recent.txt"), rec);
    if (sel.open()) sel.recents = rec;
    changeWallpaper(app, wallpaper);
    runPalette(app, wallpaper);
}

void toggleFavourite(App& app, const std::string& path) {
    Selector& sel = app.sel;
    if (path.empty()) return;
    auto it = std::find(sel.favs.begin(), sel.favs.end(), path);
    if (it != sel.favs.end()) sel.favs.erase(it);
    else sel.favs.insert(sel.favs.begin(), path);
    writePathList(wpListFile("wallpaper-favorites.txt"), sel.favs);
    if (sel.cat >= 0 && sel.cat < static_cast<int>(sel.cats.size()) && sel.cats[sel.cat].second == "fav") {
        const int keep = sel.scrollRow;
        rebuildSelectorList(app);
        sel.scrollRow = std::clamp(keep, 0, maxScrollRow(sel));
    }
}

void selectorMotion(App& app);

void askTrash(App& app, const std::string& path) {
    Selector& sel = app.sel;
    if (path.empty() || isOnlineTile(path)) return;
    sel.onlineNote.clear();
    sel.onlineError.clear();
    if (path == app.wallpaper) { sel.onlineError = "That is the wallpaper in use: choose another one first."; return; }
    sel.confirmPath = path;
    sel.hoverBtn = -1;
}

void doTrash(App& app) {
    Selector& sel = app.sel;
    const std::string path = sel.confirmPath;
    sel.confirmPath.clear();
    if (path.empty()) return;
    const std::string cache = thumbCachePath(path);
    captureArgv({"gio", "trash", path});
    if (access(path.c_str(), F_OK) == 0) { sel.onlineError = "It could not be moved to the trash."; return; }
    const auto drop = [&](std::vector<std::string>& v) { v.erase(std::remove(v.begin(), v.end(), path), v.end()); };
    drop(sel.allFiles);
    if (std::find(sel.favs.begin(), sel.favs.end(), path) != sel.favs.end()) { drop(sel.favs); writePathList(wpListFile("wallpaper-favorites.txt"), sel.favs); }
    if (std::find(sel.recents.begin(), sel.recents.end(), path) != sel.recents.end()) { drop(sel.recents); writePathList(wpListFile("wallpaper-recent.txt"), sel.recents); }
    if (auto it = sel.thumbs.find(path); it != sel.thumbs.end()) { if (it->second) cairo_surface_destroy(it->second); sel.thumbs.erase(it); }
    std::remove(cache.c_str());
    const int keep = sel.scrollRow;
    rebuildSelectorList(app);
    sel.scrollRow = std::clamp(keep, 0, maxScrollRow(sel));
    sel.onlineNote = "Moved to trash: " + baseName(path, false);
}

void selectCategory(App& app, int idx) {
    Selector& sel = app.sel;
    if (idx < 0 || idx >= static_cast<int>(sel.cats.size())) return;
    if (idx != sel.cat) sel.navAnim.select(idx);
    sel.cat = idx;
    sel.onlineError.clear();
    sel.onlineNote.clear();
    const std::string& kind = sel.cats[idx].second;
    if (kind.rfind("online:", 0) == 0) {
        onlineStart(app, kind.substr(7));
        return;
    }
    ++sel.onlineGen;
    sel.onlineProvider.clear();
    sel.onlineItems.clear();
    sel.onlineState = 0;
    sel.keyPanel = false;
    std::fill(sel.keyInput.begin(), sel.keyInput.end(), '\0');
    sel.keyInput.clear();
    sel.query.clear();
    rebuildSelectorList(app);
}

void selectorClick(App& app) {
    Selector& sel = app.sel;
    // While it holds the keyboard, Hyprland sends every pointer event to this surface, even outside the panel
    // (coordinates just fall outside it). A click out there is a click "away": close, like any popup.
    if (popIsOut(sel.anim)) return;
    if (app.pointerX < 0 || app.pointerY < 0 || app.pointerX >= kSelW || app.pointerY >= kSelH) {
        retractSelector(app);
        return;
    }
    if (sel.anim.phase != kPopOpen) return;
    if (!sel.confirmPath.empty()) {
        const SelGeom g = selGeom(sel);
        if (sel.hoverBtn == 15) doTrash(app);
        else if (sel.hoverBtn == 14 || !g.dlgCard.contains(app.pointerX, app.pointerY)) sel.confirmPath.clear();
        selectorMotion(app);
        drawSelector(app);
        return;
    }
    {
        const SelGeom sg = selGeom(sel);
        if (!sel.query.empty() && !sel.keyPanel && sg.search.contains(app.pointerX, app.pointerY) && app.pointerX < sg.search.x + sg.search.w - 120) {
            textsel::Field f(sel.query, sel.qCur, sel.qAnc);
            const bool shift = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
            const int n = sel.qClicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), app.pointerX, app.pointerY);
            textsel::applyClick(f, textsel::indexAtXInFont(sel.query, "Sans 10", app.pointerX - (sg.search.x + 46)), n, shift);
            sel.qDragInfo = textsel::beginDrag(f, n);
            sel.qDrag = true;
            drawSelector(app);
            return;
        }
    }
    if (sel.hoverBtn == 13 && sel.hoverTile >= 0) {
        askTrash(app, sel.files[sel.hoverTile]);
    } else if (sel.hoverBtn == 3 && sel.hoverTile >= 0) {
        toggleFavourite(app, sel.files[sel.hoverTile]);
    } else if ((sel.hoverBtn == 11 || sel.hoverBtn == 12) && sel.hoverTile >= 0) {
        if (const OnlineItem* oi = onlineItemFor(sel, sel.files[sel.hoverTile])) onlineDownload(app, *oi, sel.hoverBtn == 12);
    } else if (sel.hoverTile >= 0) {
        if (isOnlineTile(sel.files[sel.hoverTile])) { if (const OnlineItem* oi = onlineItemFor(sel, sel.files[sel.hoverTile])) onlineDownload(app, *oi, true); }
        else applyTheme(app, sel.files[sel.hoverTile]);
    } else if (sel.hoverMode >= 0) {
        const std::string want = sel.hoverMode == 0 ? "light" : "dark";
        if (app.cfg.colorMode != want) {
            app.cfg.colorMode = want;
            sel.modeTarget = sel.hoverMode;
            saveConfig(app);
            applyTheme(app, app.wallpaper);
        }
    } else if (sel.hoverNav >= 0) {
        selectCategory(app, sel.hoverNav);
    } else if (sel.hoverBtn >= 20 && sel.hoverBtn < 30) {
        const size_t i = static_cast<size_t>(sel.hoverBtn - 20);
        if (i < kWallhavenSorts.size() && kWallhavenSorts[i].second != sel.onlineSort) { sel.onlineSort = kWallhavenSorts[i].second; saveOnlinePrefs(sel); onlineSearch(app); }
    } else if (sel.hoverBtn >= 30 && sel.hoverBtn < 33) {
        if (sel.hoverBtn - 30 != sel.onlineRes) { sel.onlineRes = sel.hoverBtn - 30; saveOnlinePrefs(sel); onlineSearch(app); }
    } else if (sel.hoverBtn == 6) {
        sel.keyPanel = true;
        sel.keyMsg.clear();
        sel.files.clear();
    } else if (sel.hoverBtn == 7) {
        onlineSaveKey(app);
    } else if (sel.hoverBtn == 8) {
        runDetached({"xdg-open", sel.onlineProvider == "unsplash" ? "https://unsplash.com/developers" : "https://www.pexels.com/api/"});
    } else if (sel.hoverBtn == 9) {
        sel.keyReveal = !sel.keyReveal;
    } else if (sel.hoverBtn == 10) {
        onlineFetch(app, 1, false);
    } else if (sel.hoverBtn == 0) {
        std::vector<std::string> pool;
        for (const auto& p : sel.allFiles) if (p != app.wallpaper) pool.push_back(p);
        if (!pool.empty()) applyTheme(app, pool[std::rand() % pool.size()]);
    } else if (sel.hoverBtn == 5) {
        retractSelector(app);
        return;
    } else if (sel.hoverBtn == 4) {
        closeSelector(app);
        pickAccent();
        return;
    } else if (sel.hoverBtn == 1) {
        toggleFavourite(app, app.wallpaper);
    } else if (sel.hoverBtn == 2 && !app.wallpaper.empty()) {
        runDetached({"xdg-open", dirName(app.wallpaper).c_str()});
    }
    drawSelector(app);
}

void selectorMotion(App& app) {
    Selector& sel = app.sel;
    if (sel.anim.phase != kPopOpen) return;
    if (sel.qDrag) {
        const SelGeom sg = selGeom(sel);
        const double left = sg.search.x + 46, right = sg.search.x + sg.search.w - 120;
        textsel::Field f(sel.query, sel.qCur, sel.qAnc);
        size_t at = textsel::indexAtXInFont(sel.query, "Sans 10", std::clamp(app.pointerX, left, right) - left);
        if (app.pointerX < left - 4) at = std::min(at, textsel::prevChar(sel.query, f.caret));
        else if (app.pointerX > right + 4) at = std::max(at, textsel::nextChar(sel.query, f.caret));
        textsel::drag(f, at, sel.qDragInfo);
        drawSelector(app);
        return;
    }
    const SelHit h = selectorHit(sel, app.pointerX, app.pointerY);
    const double gridH = kVisRows * kTileH + (kVisRows - 1) * kTileGap;
    const bool overGrid = Rect{double(kSelW - 28), double(sel.gridY), 26.0, gridH}.contains(app.pointerX, app.pointerY);
    if (h.tile == sel.hoverTile && h.chip == sel.hoverChip && h.mode == sel.hoverMode && h.nav == sel.hoverNav && h.btn == sel.hoverBtn && overGrid == sel.overGrid) return;
    sel.overGrid = overGrid;
    sel.hoverTile = h.tile;
    sel.hoverChip = h.chip;
    sel.hoverMode = h.mode;
    sel.hoverNav = h.nav;
    sel.hoverBtn = h.btn;
    sel.kbFocus = false;
    drawSelector(app);
    updateCursor(app);
}

void selectorScroll(App& app, int rows) {
    Selector& sel = app.sel;
    if (sel.anim.phase != kPopOpen) return;
    const int next = std::clamp(sel.scrollRow + rows, 0, maxScrollRow(sel));
    if (next == sel.scrollRow) { if (rows > 0) onlineMaybeMore(app); return; }
    sel.scrollRow = next;
    selectorMotion(app);
    drawSelector(app);
    onlineMaybeMore(app);
}
