

constexpr int kLaunPadTop = 16;
constexpr int kLaunW = 500 + 2 * 20, kLaunH = 700;
constexpr double kLaunCollapsedW = 370, kLaunExpandedW = 500;
constexpr double kLaunBarH = 56, kLaunRadius = 28;
constexpr double kLaunItemH = 48, kLaunItemGap = 2, kLaunListPad = 10, kLaunListMax = 600;
constexpr int kLaunMaxResults = 15;
constexpr const char* kLaunFieldFont = "Sans 11";
constexpr const char* kLaunNameFont = "Sans 11";
constexpr const char* kLaunSmallFont = "Sans 9";

void closeLauncher(App& app);
void drawLauncher(App& app);
void retractLauncher(App& app);

std::string stripFieldCodes(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); i++) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%') out += '%';
            i++;
            continue;
        }
        out += exec[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> applicationDirs() {
    return {homeDir() + "/.local/share/applications", "/usr/share/applications", "/usr/local/share/applications",
            "/var/lib/flatpak/exports/share/applications", homeDir() + "/.local/share/flatpak/exports/share/applications"};
}

time_t newestAppDirTime(const std::string& dir, int depth = 0) {
    time_t t = 0;
    struct stat st{};
    if (stat(dir.c_str(), &st) == 0) t = st.st_mtime;
    if (depth >= 4) return t;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            const std::string sub = dir + "/" + e->d_name;
            if (stat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) t = std::max(t, newestAppDirTime(sub, depth + 1));
        }
        closedir(d);
    }
    return t;
}
time_t newestAppDirTime() {
    time_t t = 0;
    for (const auto& d : applicationDirs()) t = std::max(t, newestAppDirTime(d));
    return t;
}

bool parseDesktopEntry(const std::string& file, const std::string& id, AppEntry& out) {
    std::ifstream f(file);
    if (!f) return false;
    std::string line, group;
    bool noDisplay = false, hidden = false, isApp = true;
    std::vector<std::string> actionNames;
    std::map<std::string, AppAction> actions;
    out = AppEntry{};
    out.id = id;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { group = line.substr(1, line.rfind(']') - 1); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (group == "Desktop Entry") {
            if (k == "Name" && out.name.empty()) out.name = v;
            else if (k == "GenericName" && out.generic.empty()) out.generic = v;
            else if (k == "Comment" && out.comment.empty()) out.comment = v;
            else if (k == "Keywords" && out.keywords.empty()) out.keywords = v;
            else if (k == "Icon") out.icon = v;
            else if (k == "Exec") out.exec = v;
            else if (k == "Terminal") out.terminal = v == "true";
            else if (k == "NoDisplay") noDisplay = v == "true";
            else if (k == "Hidden") hidden = v == "true";
            else if (k == "Type") isApp = v == "Application";
            else if (k == "Actions") actionNames = splitList(v, ';');
        } else if (group.rfind("Desktop Action ", 0) == 0) {
            AppAction& a = actions[group.substr(15)];
            if (k == "Name" && a.name.empty()) a.name = v;
            else if (k == "Icon") a.icon = v;
            else if (k == "Exec") a.exec = v;
        }
    }
    if (noDisplay || hidden || !isApp || out.name.empty() || out.exec.empty()) return false;
    for (const auto& n : actionNames)
        if (auto it = actions.find(n); it != actions.end() && !it->second.name.empty() && !it->second.exec.empty())
            out.actions.push_back(it->second);
    return true;
}

void ensureApps(App& app) {
    const time_t stamp = newestAppDirTime();
    if (!app.apps.empty() && stamp == app.appsStamp) return;
    app.appsStamp = stamp;
    app.apps.clear();
    std::set<std::string> seen;
    std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& prefix, int depth) {
        DIR* d = opendir(dir.c_str());
        if (!d) return;
        std::vector<std::string> subdirs;
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name[0] == '.') continue;
            if (name.size() >= 9 && name.compare(name.size() - 8, 8, ".desktop") == 0) {
                const std::string id = prefix + name.substr(0, name.size() - 8);
                if (!seen.insert(id).second) continue;
                AppEntry entry;
                if (parseDesktopEntry(dir + "/" + name, id, entry)) app.apps.push_back(std::move(entry));
            } else if (depth < 4) {
                struct stat st{};
                if (stat((dir + "/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) subdirs.push_back(name);
            }
        }
        closedir(d);
        for (const auto& sub : subdirs) walk(dir + "/" + sub, prefix + sub + "-", depth + 1);
    };
    for (const auto& dir : applicationDirs()) walk(dir, "", 0);
}

double fuzzyScore(const std::string& needle, const std::string& hayRaw) {
    if (needle.empty() || hayRaw.empty()) return -1;
    const std::string h = lowerCopy(hayRaw);
    if (h == needle) return 1000;
    if (const size_t pos = h.find(needle); pos != std::string::npos) {
        if (pos == 0) return 900 - static_cast<double>(h.size() - needle.size());
        const char before = h[pos - 1];
        if (before == ' ' || before == '-' || before == '_' || before == '.') return 700 - static_cast<double>(pos);
        return 500 - static_cast<double>(pos);
    }
    size_t j = 0, gaps = 0, last = std::string::npos;
    for (size_t i = 0; i < h.size() && j < needle.size(); i++)
        if (h[i] == needle[j]) {
            if (last != std::string::npos && i != last + 1) gaps++;
            last = i;
            j++;
        }
    return j < needle.size() ? -1 : 300 - static_cast<double>(gaps) * 10 - static_cast<double>(h.size() - needle.size());
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string cleanPrefix(const std::string& q, char prefix) {
    return !q.empty() && q[0] == prefix ? trimmed(q.substr(1)) : q;
}

std::vector<const AppEntry*> searchApps(const App& app, const std::string& queryRaw) {
    const std::string needle = lowerCopy(trimmed(queryRaw));
    std::vector<std::pair<double, const AppEntry*>> scored;
    for (const auto& e : app.apps) {
        double best = fuzzyScore(needle, e.name);
        best = std::max(best, fuzzyScore(needle, e.generic) * 0.8);
        best = std::max(best, fuzzyScore(needle, e.keywords) * 0.7);
        best = std::max(best, fuzzyScore(needle, e.id) * 0.6);
        if (best <= 0 && lowerCopy(e.comment).find(needle) != std::string::npos) best = 120;
        if (best > 0) scored.emplace_back(best, &e);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : strcasecmp(a.second->name.c_str(), b.second->name.c_str()) < 0;
    });
    std::vector<const AppEntry*> out;
    for (size_t i = 0; i < scored.size() && static_cast<int>(i) < kLaunMaxResults; i++) out.push_back(scored[i].second);
    return out;
}

std::vector<std::string> launcherActions() {
    std::vector<std::string> out = {"dark", "light", "wallpaper", "wipeclipboard"};
    const std::string dir = configDir() + "/actions";
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name[0] == '.') continue;
            if (access((dir + "/" + name).c_str(), X_OK) != 0) continue;
            out.push_back(name.substr(0, name.rfind('.')));
        }
        closedir(d);
    }
    return out;
}

void runLauncherAction(App& app, const std::string& name, const std::string& args) {
    if (name == "dark" || name == "light") {
        app.cfg.colorMode = name;
        app.sel.modeTarget = name == "dark" ? 1 : 0;
        saveConfig(app);
        applyTheme(app, app.wallpaper);
    } else if (name == "wallpaper") {
        closeLauncher(app);
        openSelector(app);
    } else if (name == "wipeclipboard") {
        closeLauncher(app);
        const std::string script = assetsDir() + "/../scripts/wipeclipboard.sh";
        runDetached({"bash", script.c_str()});
    } else {
        const std::string dir = configDir() + "/actions";
        if (DIR* d = opendir(dir.c_str())) {
            while (const dirent* e = readdir(d)) {
                const std::string file = e->d_name;
                if (file[0] != '.' && file.substr(0, file.rfind('.')) == name && access((dir + "/" + file).c_str(), X_OK) == 0) {
                    const std::string path = dir + "/" + file;
                    runDetached({path.c_str(), args.c_str()});
                    break;
                }
            }
            closedir(d);
        }
    }
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

// What a QR code is offered for: a link (http(s)://, ftp://, www.) typed or pasted, or anything after "qr ".
std::string qrSource(const std::string& q) {
    const std::string t = trimmed(q);
    if (t.size() > 3 && lowerCopy(t.substr(0, 3)) == "qr " && !trimmed(t.substr(3)).empty()) return trimmed(t.substr(3));
    const std::string low = lowerCopy(t);
    if (t.find(' ') != std::string::npos || t.find('\n') != std::string::npos) return "";
    for (const char* p : {"http://", "https://", "ftp://", "www."})
        if (low.rfind(p, 0) == 0 && low.size() > std::strlen(p)) return t;
    return "";
}

// "1usd to eur", "5 km in miles", "cuanto es 100 dolares a pesos mexicanos": the answer as one row. Currency rates are the ones the Tools page keeps
// (refreshed here, in the background, when they are missing or old).
struct LauncherConversion { bool ok = false, waiting = false; std::string text, copy; };
LauncherConversion launcherConversion(const std::string& q) {
    using namespace unitconv;
    static std::map<std::string, double> rates;
    static long long ratesAt = 0, fileStamp = 0, askedAt = 0;
    static std::vector<UnCategory> cats = unStaticCategories();
    LauncherConversion out;
    if (q.size() < 4 || q.size() > 120) return out;
    const std::string path = configDir() + "/tools/rates.json";
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && static_cast<long long>(st.st_mtime) != fileStamp) {
        fileStamp = st.st_mtime;
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        if (unParseRates(ss.str(), rates, ratesAt)) cats[0].units = unCurrencyUnits(rates);
    }
    const UnQuery r = unParseQuery(q, cats, !rates.empty());
    const long long now = static_cast<long long>(std::time(nullptr));
    if ((r.needsRates || (r.ok && r.fromSym.size() == 3 && now - ratesAt > 6 * 3600)) && now - askedAt > 600) {
        askedAt = now;
        std::filesystem::create_directories(configDir() + "/tools");
        runDetached({"bash", "-c", "curl -sf -m 15 https://open.er-api.com/v6/latest/USD -o \"$1.tmp\" && mv \"$1.tmp\" \"$1\"", "bash", path.c_str()});
    }
    if (r.needsRates) { out.waiting = true; return out; }
    if (!r.ok) return out;
    out.ok = true;
    out.text = unFmt(r.value) + " " + r.toSym;
    std::string plain;
    for (char c : unFmt(r.value)) if (c != ',') plain += c;
    out.copy = plain + " " + r.toSym;
    return out;
}

bool looksLikeMath(const std::string& q) {
    if (q.empty()) return false;
    if (q[0] == kPfxMath || std::isdigit(static_cast<unsigned char>(q[0]))) return true;
    if (q[0] == kPfxAction || q[0] == kPfxApp || q[0] == kPfxClipboard || q[0] == kPfxEmoji ||
        q[0] == kPfxShell || q[0] == kPfxWeb || q[0] == kPfxKeybind || q[0] == kPfxTranslate) return false;
    return q.find_first_of("+-*/^%()") != std::string::npos && q.find_first_of("0123456789") != std::string::npos;
}

void requestMath(App& app, const std::string& expr) {
    Launcher& L = app.launcher;
    if (L.mathPid > 0) { kill(L.mathPid, SIGKILL); L.mathPid = 0; }
    if (L.mathFd >= 0) { close(L.mathFd); L.mathFd = -1; }
    L.mathBuf.clear();
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execlp("qalc", "qalc", "-t", expr.c_str(), nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    L.mathPid = pid;
    L.mathFd = p[0];
}

// ---- Files: the recently used ones (GTK's recently-used.xbel) and, from three letters on, those `fd` finds under the home folder.
void loadRecentFiles(Launcher& L) {
    L.recentLoaded = true;
    L.recent.clear();
    const char* data = std::getenv("XDG_DATA_HOME");
    std::ifstream in((data && *data ? std::string(data) : homeDir() + "/.local/share") + "/recently-used.xbel");
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string xml = ss.str();
    std::vector<std::pair<std::string, std::string>> found;  // modified, path
    for (size_t at = xml.find("<bookmark href=\""); at != std::string::npos; at = xml.find("<bookmark href=\"", at + 1)) {
        const size_t end = xml.find('>', at);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(at, end - at);
        const size_t h0 = tag.find("href=\"") + 6, h1 = tag.find('"', h0);
        if (h1 == std::string::npos || tag.compare(h0, 7, "file://") != 0) continue;
        std::string path = percentDecode(tag.substr(h0 + 7, h1 - h0 - 7));
        const size_t m0 = tag.find("modified=\"");
        found.emplace_back(m0 == std::string::npos ? "" : tag.substr(m0 + 10, tag.find('"', m0 + 10) - m0 - 10), std::move(path));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& f : found) {
        if (L.recent.size() >= 300) break;
        if (access(f.second.c_str(), F_OK) == 0) L.recent.push_back(f.second);
    }
}

void requestFiles(App& app, const std::string& needle) {
    Launcher& L = app.launcher;
    if (L.filePid > 0) { kill(L.filePid, SIGKILL); L.filePid = 0; }
    if (L.fileFd >= 0) { close(L.fileFd); L.fileFd = -1; }
    L.fileBuf.clear();
    L.fileHits.clear();
    L.fileFor = needle;
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        dup2(p[1], STDOUT_FILENO);
        const int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        const std::string home = homeDir();
        execlp("timeout", "timeout", "2", "fd", "--type", "f", "--type", "d", "--max-results", "40", "--max-depth", "8", "--ignore-case", "--absolute-path",
               "--fixed-strings", "--exclude", "node_modules", "--exclude", ".cache", "--", needle.c_str(), home.c_str(), nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    L.filePid = pid;
    L.fileFd = p[0];
}

LauncherResult fileResult(const std::string& path) {
    LauncherResult r;
    const bool dir = std::filesystem::is_directory(path);
    std::string parent = path.substr(0, path.rfind('/'));
    const std::string home = homeDir();
    if (parent.compare(0, home.size(), home) == 0) parent = "~" + parent.substr(home.size());
    r.kind = ResFile; r.name = path.substr(path.rfind('/') + 1); r.type = parent.empty() ? "/" : parent; r.verb = dir ? "Open folder" : "Open"; r.icon = dir ? "folder" : "description"; r.payload = path;
    return r;
}

bool translateMode(const Launcher& L) { return !L.text.empty() && L.text[0] == kPfxTranslate; }
std::string translatePhrase(const Launcher& L) { return trimmed(cleanPrefix(L.text, kPfxTranslate)); }

void requestTranslate(App& app, const std::string& phrase) {
    Launcher& L = app.launcher;
    if (L.mathPid > 0) { kill(L.mathPid, SIGKILL); L.mathPid = 0; }
    if (L.mathFd >= 0) { close(L.mathFd); L.mathFd = -1; }
    L.mathBuf.clear();
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const std::string script = assetsDir() + "/../scripts/translate.sh";
    std::string a = "en", b = "es";
    {
        std::ifstream in(configDir() + "/tools/translator-state.json");
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string pair = ss.str();
        std::string x, y;
        readStringKey(pair, "src", x);
        readStringKey(pair, "tgt", y);
        if (!x.empty()) a = x;
        if (!y.empty()) b = y;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execlp("bash", "bash", script.c_str(), phrase.c_str(), a.c_str(), b.c_str(), nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    L.mathPid = pid;
    L.mathFd = p[0];
    L.mathIsTr = true;
    L.trFor = phrase;
}

int g_clipFd = -1;
int g_clipWrite = -1;

void copyToClipboard(const std::string& text) {
    int p[2];
    if (pipe(p) < 0) return;
    if (fork() == 0) {
        childSignalReset();
        close(p[1]);
        dup2(p[0], STDIN_FILENO);
        execlp("wl-copy", "wl-copy", nullptr);
        _exit(127);
    }
    close(p[0]);
    (void)!write(p[1], text.data(), text.size());
    close(p[1]);
}

bool clipIsImage(const std::string& line) { return line.find("[[ binary data") != std::string::npos && line.rfind("]]") != std::string::npos; }
std::string clipId(const std::string& line) { return line.substr(0, line.find('\t')); }
std::string clipText(const std::string& line) {
    const size_t t = line.find('\t');
    return t == std::string::npos ? line : line.substr(t + 1);
}

std::string clipLabel(const std::string& line) {
    const std::string text = clipText(line);
    if (!clipIsImage(line)) return text;
    char amount[32], unit[16], fmt[16], dims[32];
    if (std::sscanf(text.c_str(), "[[ binary data %31s %15s %15s %31s ]]", amount, unit, fmt, dims) == 4)
        return std::string("Image  ·  ") + dims + "  ·  " + fmt + "  ·  " + amount + " " + unit;
    return "Image";
}

std::vector<std::pair<std::string, std::string>> loadClipPins() {
    std::vector<std::pair<std::string, std::string>> pins;
    std::ifstream f(stateDir() + "/clipboard-pins.txt");
    std::string line;
    while (std::getline(f, line)) {
        const size_t t = line.find('\t');
        if (t != std::string::npos) pins.emplace_back(line.substr(0, t), line.substr(t + 1));
    }
    return pins;
}
void saveClipPins(const std::vector<std::pair<std::string, std::string>>& pins) {
    mkdir(stateDir().c_str(), 0755);
    std::ofstream f(stateDir() + "/clipboard-pins.txt");
    for (const auto& [id, text] : pins) f << id << '\t' << text << '\n';
}
bool pinMatches(const std::pair<std::string, std::string>& pin, const std::string& line) {
    return pin.first == clipId(line) || (!clipIsImage(line) && !pin.second.empty() && pin.second == clipText(line));
}
bool clipIsPinned(const std::vector<std::pair<std::string, std::string>>& pins, const std::string& line) {
    return std::any_of(pins.begin(), pins.end(), [&](const auto& pin) { return pinMatches(pin, line); });
}
void clipTogglePin(const std::string& line) {
    auto pins = loadClipPins();
    const auto it = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return pinMatches(pin, line); });
    if (it != pins.end()) pins.erase(it);
    else pins.emplace_back(clipId(line), clipIsImage(line) ? "" : clipText(line).substr(0, 400));
    saveClipPins(pins);
}

void loadClipboardLines(Launcher& L) {
    L.clipLines.clear();
    L.clipLoaded = true;
    FILE* f = popen("cliphist list 2>/dev/null", "r");
    if (!f) return;
    char* buf = nullptr;
    size_t cap = 0;
    ssize_t n;
    while (L.clipLines.size() < 600 && (n = getline(&buf, &cap, f)) > 0) {
        std::string line(buf, n);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) L.clipLines.push_back(std::move(line));
    }
    free(buf);
    pclose(f);
}

std::string clipThumbPath(const std::string& id) {
    const char* c = std::getenv("XDG_CACHE_HOME");
    return (c ? std::string(c) : homeDir() + "/.cache") + "/end4pC/clip/" + id + ".png";
}

void requestClipThumb(Launcher& L, const std::string& id) {
    if (!L.clipThumbAsked.insert(id).second || g_clipWrite < 0) return;
    const std::string out = clipThumbPath(id);
    if (access(out.c_str(), R_OK) == 0) return;
    if (fork() == 0) {
        childSignalReset();
        dup2(g_clipWrite, STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execlp("sh", "sh", "-c",
               "mkdir -p \"$(dirname \"$2\")\" && cliphist decode \"$1\" | magick - -thumbnail 96x96^ -gravity center -extent 96x96 \"$2.tmp.png\""
               " && mv \"$2.tmp.png\" \"$2\"; echo done",
               "sh", id.c_str(), out.c_str(), nullptr);
        _exit(127);
    }
}

const std::vector<std::string>& emojiLines() {
    static const auto lines = [] {
        std::vector<std::string> out;
        std::ifstream f(assetsDir() + "/emojis.txt");
        std::string line;
        while (std::getline(f, line))
            if (!trimmed(line).empty()) out.push_back(trimmed(line));
        return out;
    }();
    return lines;
}

std::string bindMods(int mask) {
    std::string out;
    auto add = [&](int bit, const char* name) { if (mask & bit) out += std::string(out.empty() ? "" : " + ") + name; };
    add(64, "SUPER"); add(4, "CTRL"); add(8, "ALT"); add(1, "SHIFT");
    return out;
}

std::string jsonString(const std::string& chunk, const char* key) {
    const size_t k = chunk.find(std::string("\"") + key + "\":");
    if (k == std::string::npos) return "";
    const size_t q1 = chunk.find('"', k + std::strlen(key) + 3);
    if (q1 == std::string::npos) return "";
    std::string out;
    for (size_t i = q1 + 1; i < chunk.size() && chunk[i] != '"'; i++) {
        if (chunk[i] == '\\' && i + 1 < chunk.size()) i++;
        out += chunk[i];
    }
    return out;
}

// `hyprctl binds -j`: the binds that have a description, as (description, "SUPER + W").
std::vector<std::pair<std::string, std::string>> hyprKeybinds() {
    std::vector<std::pair<std::string, std::string>> out;
    if (wmKind() != Wm::Hyprland) return out;
    FILE* f = popen("hyprctl binds -j 2>/dev/null", "r");
    if (!f) return out;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && all.size() < (4u << 20)) all.append(buf, n);
    pclose(f);
    size_t start = all.find("\"locked\":");
    while (start != std::string::npos) {
        const size_t next = all.find("\"locked\":", start + 9);
        const std::string chunk = all.substr(start, next == std::string::npos ? std::string::npos : next - start);
        const std::string desc = jsonString(chunk, "description"), key = jsonString(chunk, "key");
        const size_t mm = chunk.find("\"modmask\":");
        const int mask = mm == std::string::npos ? 0 : std::atoi(chunk.c_str() + mm + 10);
        if (!desc.empty() && !key.empty()) {
            const std::string mods = bindMods(mask);
            out.emplace_back(desc, mods.empty() ? key : mods + " + " + key);
        }
        start = next;
    }
    return out;
}

void updateLauncherResults(App& app, bool keepSelection = false) {
    Launcher& L = app.launcher;
    const int prevSelected = L.selected;
    L.results.clear();
    L.hover = -1;
    const std::string q = L.text;
    if (q.empty()) { L.selected = 0; L.scroll = 0; return; }
    const char first = q[0];
    auto push = [&](LauncherResult r) { L.results.push_back(std::move(r)); };
    if (first == kPfxTranslate) {
        const std::string phrase = translatePhrase(L);
        L.selected = 0;
        L.scroll = 0;
        if (phrase != L.trFor) {
            L.trOut.clear();
            L.trFailed = false;
            if (L.mathPid > 0) { kill(L.mathPid, SIGKILL); L.mathPid = 0; }
            if (L.mathFd >= 0) { close(L.mathFd); L.mathFd = -1; }
            L.trPending = !phrase.empty();
            L.trDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
            L.trFor.clear();
        }
        return;
    }

    const bool queryIsMath = looksLikeMath(q);
    if (!queryIsMath) L.mathResult.clear();
    auto mathRow = [&] {
        if (!queryIsMath || L.mathResult.empty() || L.mathResult.find("error") != std::string::npos) return;
        LauncherResult r;
        r.kind = ResMath; r.name = L.mathResult; r.type = "Math result"; r.verb = "Copy"; r.icon = "calculate";
        push(r);
    };
    auto commandRow = [&](bool asPriority = false) {
        LauncherResult r;
        r.kind = ResCommand; r.name = cleanPrefix(q, kPfxShell); r.type = "Command"; r.verb = "Run"; r.icon = "terminal";
        r.payload = r.name;
        r.footer = !asPriority;
        if (!r.name.empty()) push(r);
    };
    auto webRow = [&](bool asPriority = false) {
        LauncherResult r;
        r.kind = ResWeb; r.name = cleanPrefix(q, kPfxWeb); r.type = "Web search"; r.verb = "Search"; r.icon = "travel_explore";
        r.payload = r.name;
        r.footer = !asPriority;
        if (!r.name.empty()) push(r);
    };
    auto convRow = [&](const LauncherConversion& c) {
        LauncherResult r;
        r.kind = ResConv; r.name = c.waiting ? "Getting the exchange rates…" : c.text; r.type = "Conversion"; r.verb = c.waiting ? "" : "Copy"; r.icon = "transfer-horizontal"; r.payload = c.copy;
        push(r);
    };
    auto qrRow = [&](const std::string& text) {
        LauncherResult r;
        r.kind = ResQr; r.name = text; r.type = "QR code"; r.verb = "Create"; r.icon = "qr-code"; r.payload = text;
        push(r);
    };
    auto actionRows = [&] {
        std::vector<std::string> names = launcherActions();
        if (app.cfg.launcherFiles) names.push_back("recent");  // (the list of recent files belongs to the files search)
        for (const auto& name : names) {
            const std::string s = std::string(1, kPfxAction) + name;
            if (s.rfind(q, 0) == 0 || q.rfind(s, 0) == 0) {
                LauncherResult r;
                r.kind = ResAction; r.name = q.rfind(s, 0) == 0 ? q : s; r.type = "Action"; r.verb = "Run"; r.icon = "settings_suggest";
                r.payload = name;
                push(r);
            }
        }
    };

    if (first == kPfxClipboard) {
        if (!L.clipLoaded) loadClipboardLines(L);
        const std::string needle = lowerCopy(trimmed(cleanPrefix(q, kPfxClipboard)));
        const auto pins = loadClipPins();
        std::vector<std::pair<double, const std::string*>> hits;
        for (const auto& line : L.clipLines) {
            const double sc = needle.empty() ? 1 : fuzzyScore(needle, clipLabel(line));
            if (sc > 0) hits.emplace_back(sc, &line);
        }
        std::stable_sort(hits.begin(), hits.end(), [&](const auto& a, const auto& b) {
            const bool pa = clipIsPinned(pins, *a.second), pb = clipIsPinned(pins, *b.second);
            if (pa != pb) return pa;
            return needle.empty() ? false : a.first > b.first;
        });
        for (size_t i = 0; i < hits.size() && i < 200; i++) {
            const std::string& line = *hits[i].second;
            LauncherResult r;
            r.kind = ResClip; r.name = clipLabel(line).substr(0, 300); r.type = "#" + clipId(line); r.verb = "Copy";
            r.icon = "content_paste"; r.payload = line; r.image = clipIsImage(line); r.pinned = clipIsPinned(pins, line);
            push(r);
        }
    } else if (first == kPfxEmoji) {
        const std::string needle = lowerCopy(trimmed(cleanPrefix(q, kPfxEmoji)));
        std::vector<std::pair<double, const std::string*>> hits;
        for (const auto& line : emojiLines()) {
            const size_t sp = line.find(' ');
            const double sc = needle.empty() ? 1 : fuzzyScore(needle, sp == std::string::npos ? line : line.substr(sp + 1));
            if (sc > 0) hits.emplace_back(sc, &line);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < hits.size() && i < 120; i++) {
            const std::string& line = *hits[i].second;
            const size_t sp = line.find(' ');
            LauncherResult r;
            r.kind = ResEmoji; r.payload = line.substr(0, sp); r.name = sp == std::string::npos ? line : line.substr(sp + 1);
            r.type = "Emoji"; r.verb = "Copy";
            push(r);
        }
    } else if (first == kPfxKeybind) {
        const std::string needle = lowerCopy(trimmed(cleanPrefix(q, kPfxKeybind)));
        for (const auto& [desc, keys] : hyprKeybinds()) {
            if (!needle.empty() && lowerCopy(desc).find(needle) == std::string::npos && lowerCopy(keys).find(needle) == std::string::npos) continue;
            LauncherResult r;
            r.kind = ResKeybind; r.name = desc; r.type = "Keybind"; r.verb = keys; r.icon = "keyboard"; r.payload = keys;
            push(r);
        }
    } else if (first == kPfxAction && app.cfg.launcherFiles && q.rfind("/recent", 0) == 0) {  // "/recent [words]": the files used lately
        if (!L.recentLoaded) loadRecentFiles(L);
        const std::string needle = lowerCopy(trimmed(q.substr(7)));
        for (const std::string& path : L.recent) {
            if (L.results.size() >= 60) break;
            if (!needle.empty() && fuzzyScore(needle, path.substr(path.rfind('/') + 1)) <= 0) continue;
            push(fileResult(path));
        }
    } else if (first == kPfxAction) {
        actionRows();
    } else {
        const std::string qrText = qrSource(q);
        const LauncherConversion conv = qrText.empty() && first != kPfxShell && first != kPfxWeb ? launcherConversion(q) : LauncherConversion{};
        const bool math = qrText.empty() && !conv.ok && !conv.waiting && looksLikeMath(q);
        const bool shell = first == kPfxShell, web = first == kPfxWeb;
        if (!qrText.empty()) qrRow(qrText);
        if (conv.ok || conv.waiting) convRow(conv);
        if (math) requestMath(app, first == kPfxMath ? cleanPrefix(q, kPfxMath) : q);
        if (math) mathRow();
        else if (shell) commandRow(true);
        else if (web) webRow(true);
        std::vector<LauncherResult> appRows;
        for (const AppEntry* e : searchApps(app, cleanPrefix(q, kPfxApp))) {
            LauncherResult r;
            r.kind = ResApp; r.name = e->name; r.type = "App"; r.verb = "Open"; r.icon = e->icon; r.payload = e->id; r.comment = e->comment;
            r.terminal = e->terminal;
            appRows.push_back(std::move(r));
        }
        constexpr size_t kAppsBeforeFiles = 4;  // the best apps first, then the files, then the other apps
        size_t appAt = 0;
        for (; appAt < appRows.size() && appAt < kAppsBeforeFiles; appAt++) push(appRows[appAt]);
        if (!math && !shell && !web && app.cfg.launcherFiles) {  // files: the ones used lately that match, then what `fd` has found
            const std::string needle = trimmed(cleanPrefix(q, kPfxApp));
            if (!L.recentLoaded) loadRecentFiles(L);
            std::set<std::string> seen;
            int recentShown = 0;
            for (const std::string& path : L.recent) {
                if (recentShown >= 3) break;
                if (needle.empty() || fuzzyScore(lowerCopy(needle), path.substr(path.rfind('/') + 1)) <= 0) continue;
                if (seen.insert(path).second) { push(fileResult(path)); recentShown++; }
            }
            if (needle.size() >= 3) {
                if (needle != L.fileFor) requestFiles(app, needle);
                int found = 0;
                for (const std::string& path : L.fileHits) {
                    if (found >= 6) break;
                    if (seen.insert(path).second) { push(fileResult(path)); found++; }
                }
            }
        }
        for (; appAt < appRows.size(); appAt++) push(appRows[appAt]);
        actionRows();
        if (!shell) commandRow();
        if (!math) mathRow();
        if (!web) webRow();
    }
    L.selected = keepSelection ? std::clamp(prevSelected, 0, std::max(0, static_cast<int>(L.results.size()) - 1)) : 0;
    if (!keepSelection) L.scroll = 0;
}

const AppEntry* findApp(const App& app, const std::string& id) {
    for (const auto& e : app.apps)
        if (e.id == id) return &e;
    return nullptr;
}

void launcherActivate(App& app, int index) {
    Launcher& L = app.launcher;
    if (translateMode(L)) {
        if (!L.trOut.empty() && L.trFor == translatePhrase(L)) { const std::string out = L.trOut; copyToClipboard(out); closeLauncher(app); }
        return;
    }
    if (index < 0 || index >= static_cast<int>(L.results.size())) return;
    const LauncherResult r = L.results[index];
    if (r.kind == ResAction && r.payload == "recent") {  // not an action: it opens the list of recent files in the same box
        L.text = "/recent ";
        L.cursor = L.anchor = L.text.size();
        updateLauncherResults(app);
        drawLauncher(app);
        return;
    }
    switch (r.kind) {
        case ResFile: runDetached({"xdg-open", r.payload.c_str()}); break;
        case ResApp:
            if (const AppEntry* e = findApp(app, r.payload)) {
                if (e->terminal) {
                    const std::string cmd = stripFieldCodes(e->exec);
                    runDetached({app.cfg.terminal.c_str(), "-e", "sh", "-c", cmd.c_str()});
                } else {
                    runDetached({"gtk-launch", e->id.c_str()});
                }
            }
            break;
        case ResMath: copyToClipboard(r.name); break;
        case ResEmoji: copyToClipboard(r.payload); break;
        case ResKeybind: copyToClipboard(r.payload); break;
        case ResClip:
            runDetached({"sh", "-c", "printf '%s' \"$1\" | cliphist decode | wl-copy", "sh", r.payload.c_str()});
            break;
        case ResCommand: {
            const bool sudo = r.payload.rfind("sudo", 0) == 0;
            if (sudo) {
                const std::string cmd = r.payload + "; exec bash";
                runDetached({app.cfg.terminal.c_str(), "-e", "bash", "-c", cmd.c_str()});
            } else {
                runDetached({"bash", "-c", r.payload.c_str()});
            }
            break;
        }
        case ResQr: openQrCard(app, r.payload); return;
        case ResConv: if (!r.payload.empty()) copyToClipboard(r.payload); break;
        case ResWeb: {
            const std::string url = app.cfg.searchEngine + urlEncode(r.payload);
            runDetached({"xdg-open", url.c_str()});
            break;
        }
        case ResAction: {
            const std::string args = L.text.find(' ') == std::string::npos ? "" : L.text.substr(L.text.find(' ') + 1);
            runLauncherAction(app, r.payload, args);
            break;
        }
    }
    if (L.open()) retractLauncher(app);
}

constexpr double kLaunFooterGap = 8;

double launcherPy(const App& app) { return app.pointerY - kLaunPadTop; }

struct LaunButton { const char* icon; const char* key; };
const LaunButton kLaunButtons[] = {{"clipboard-list", "prefixClipboard"}, {"qr-code", "scanQr"}};
constexpr int kLaunButtonCount = 2;
constexpr double kLaunBtn = 34, kLaunBtnGap = 4, kLaunBtnMargin = 8;
Rect launcherButtonRect(double px, double pw, int i) {
    const double x = px + pw - kLaunBtnMargin - (kLaunButtonCount - i) * kLaunBtn - (kLaunButtonCount - 1 - i) * kLaunBtnGap;
    return {x, (kLaunBarH - kLaunBtn) / 2.0, kLaunBtn, kLaunBtn};
}
int launcherButtonAt(const App& app, double x, double y);

struct LaunLayout {
    double px, pw, ph;
    double listTop, listH;
    int nMain, nFooter;
    int mainVisible;
    int totalRows;
};

constexpr double kTrPad = 12, kTrGap = 10;
double trTextHeight(const std::string& s, double width) {
    static cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    static PangoLayout* l = [] { cairo_t* c = mkcr(surf); PangoLayout* x = pango_cairo_create_layout(c); cairo_destroy(c); return x; }();
    PangoFontDescription* fd = pango_font_description_from_string("Sans 12");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(l, static_cast<int>(width * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(l, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    return h;
}
double trCardW(double pw) { return (pw - 2 * kTrPad - kTrGap) / 2; }
std::string trTargetText(const Launcher& L) {
    const std::string phrase = translatePhrase(L);
    if (!L.trOut.empty() && L.trFor == phrase) return L.trOut;
    return phrase.empty() ? "Translation" : (L.trFailed ? "Could not translate" : "Translating…");
}
double trCardH(const Launcher& L, double pw) {
    const double tw = trCardW(pw) - 28;
    const std::string phrase = translatePhrase(L);
    const double h = std::max(trTextHeight(phrase.empty() ? "Type to translate" : phrase, tw), trTextHeight(trTargetText(L), tw));
    return std::clamp(28 + 8 + h + 28, 140.0, 340.0);
}

// cubic-bezier(x1, y1, x2, y2) like Qt's BezierSpline: the y for a time x in 0..1 (the curve may overshoot).
double bezierEase(double x, double x1, double y1, double x2, double y2) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    double lo = 0, hi = 1, t = x;
    for (int i = 0; i < 24; i++) {
        t = (lo + hi) / 2;
        const double bx = 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t;
        (bx < x ? lo : hi) = t;
    }
    return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t;
}
constexpr double kLaunHMs = 500, kLaunWMs = 300;
double launAnimValue(double from, double to, std::chrono::steady_clock::time_point start, double ms) {
    const double x = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / ms;
    return x >= 1 ? to : from + (to - from) * bezierEase(x, 0.38, 1.21, 0.22, 1.00);
}

LaunLayout launcherLayoutTarget(const App& app) {
    const Launcher& L = app.launcher;
    LaunLayout g{};
    g.pw = L.text.empty() ? kLaunCollapsedW : kLaunExpandedW;
    g.px = (kLaunW - g.pw) / 2.0;
    g.listTop = kLaunBarH + 1;
    if (L.text.empty()) {
        g.ph = kLaunBarH;
        return g;
    }
    if (translateMode(L)) {
        g.listH = 2 * kTrPad + trCardH(L, g.pw);
        g.ph = kLaunBarH + 1 + g.listH;
        return g;
    }
    for (const auto& r : L.results) (r.footer ? g.nFooter : g.nMain)++;
    const double step = kLaunItemH + kLaunItemGap;
    const double fixedExtra = 2 * kLaunListPad - kLaunItemGap + (g.nFooter > 0 && g.nMain > 0 ? kLaunFooterGap : 0);
    const int maxRows = static_cast<int>((kLaunListMax - fixedExtra) / step);
    g.mainVisible = std::min(g.nMain, std::max(g.nMain > 0 ? 1 : 0, maxRows - g.nFooter));
    g.totalRows = g.mainVisible + g.nFooter;
    g.listH = g.totalRows == 0 ? 64 : g.totalRows * step + fixedExtra - (g.nFooter > 0 && g.nMain > 0 && g.mainVisible == 0 ? kLaunFooterGap : 0);
    g.ph = kLaunBarH + 1 + g.listH;
    return g;
}

Rect launcherItemRect(const LaunLayout& g, int row) {
    const double extra = g.mainVisible > 0 && row >= g.mainVisible ? kLaunFooterGap : 0;
    return {g.px + 10, g.listTop + kLaunListPad + row * (kLaunItemH + kLaunItemGap) + extra, g.pw - 20, kLaunItemH};
}

LaunLayout launcherLayout(const App& app) {
    LaunLayout g = launcherLayoutTarget(app);
    const Launcher& L = app.launcher;
    if (L.animInit) {
        g.ph = std::min<double>(launAnimValue(L.hFrom, L.hTo, L.hStart, kLaunHMs), kLaunH - kLaunPadTop - 8);
        g.pw = std::min<double>(launAnimValue(L.wFrom, L.wTo, L.wStart, kLaunWMs), kLaunW - 4);
        g.px = (kLaunW - g.pw) / 2.0;
    }
    return g;
}

int launcherRowIndex(const LaunLayout& g, const Launcher& L, int row) {
    return row < g.mainVisible ? L.scroll + row : g.nMain + (row - g.mainVisible);
}

void launcherPrefixStyle(const std::string& q, std::string& shape, std::string& icon) {
    const char c = q.empty() ? 0 : q[0];
    if (c == kPfxAction) { shape = "Pill"; icon = "settings_suggest"; }
    else if (c == kPfxApp) { shape = "Clover4Leaf"; icon = "apps"; }
    else if (c == kPfxClipboard) { shape = "Gem"; icon = "content_paste_search"; }
    else if (c == kPfxEmoji) { shape = "Sunny"; icon = "add_reaction"; }
    else if (c == kPfxMath) { shape = "PuffyDiamond"; icon = "calculate"; }
    else if (c == kPfxShell) { shape = "PixelCircle"; icon = "terminal"; }
    else if (c == kPfxWeb) { shape = "SoftBurst"; icon = "travel_explore"; }
    else if (c == kPfxKeybind) { shape = "Cookie4Sided"; icon = "keyboard_command_key"; }
    else if (c == kPfxTranslate) { shape = "SoftBurst"; icon = "chat-line"; }
    else { shape = "Cookie7Sided"; icon = "search"; }
}

cairo_surface_t* launcherAppIcon(App& app, const std::string& name) {
    Launcher& L = app.launcher;
    if (auto it = L.icons.find(name); it != L.icons.end()) return it->second;
    cairo_surface_t* surf = nullptr;
    const std::string path = lookupIcon(name, 32);
    if (!path.empty()) surf = loadIconSurface(path, 32);
    L.icons[name] = surf;
    return surf;
}

double caretX(PangoLayout* layout, const std::string& text, size_t byteIndex) {
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoRectangle r;
    pango_layout_index_to_pos(layout, static_cast<int>(byteIndex), &r);
    return r.x / static_cast<double>(PANGO_SCALE);
}

size_t launcherIndexAtX(const std::string& text, double x) { return textsel::indexAtXInFont(text, kLaunFieldFont, x); }

void drawLauncher(App& app) {
    Launcher& L = app.launcher;
    if (!L.open() || !L.configured) return;
    const int W = kLaunW, H = kLaunH;
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-launcher", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    popGroupBegin(cr);

    {
        const LaunLayout want = launcherLayoutTarget(app);
        const auto now = std::chrono::steady_clock::now();
        if (!L.animInit) { L.animInit = true; L.hFrom = L.hTo = want.ph; L.wFrom = L.wTo = want.pw; }
        else {
            const LaunLayout cur = launcherLayout(app);
            if (want.ph != L.hTo) { L.hFrom = cur.ph; L.hTo = want.ph; L.hStart = now; }
            if (want.pw != L.wTo) { L.wFrom = cur.pw; L.wTo = want.pw; L.wStart = now; }
        }
        L.sliding = std::chrono::duration<double, std::milli>(now - L.hStart).count() < kLaunHMs || std::chrono::duration<double, std::milli>(now - L.wStart).count() < kLaunWMs;
    }
    const LaunLayout g = launcherLayout(app);
    const double slide = 1;

    // Drop shadow around the pill instead of a border. It is kept out of
    // the pill itself so it does not darken a translucent background, and faint enough that Hyprland's blur
    // (ignore_alpha 0.25) leaves it alone.
    if (!L.shadow || L.shadowW != static_cast<int>(g.pw) || L.shadowH != static_cast<int>(g.ph)) {
        if (L.shadow) cairo_surface_destroy(L.shadow);
        L.shadow = makeShadowMask(g.pw, g.ph, kLaunRadius);
        L.shadowW = static_cast<int>(g.pw);
        L.shadowH = static_cast<int>(g.ph);
    }
    setSource(cr, app.color("shadow"), 0.3);
    cairo_mask_surface(cr, L.shadow, g.px - kLaunShadow, kLaunPadTop - kLaunShadow + 2);
    cairo_translate(cr, 0, kLaunPadTop);
    cairo_save(cr);
    roundedRect(cr, g.px, 0, g.pw, g.ph, kLaunRadius);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_restore(cr);
    roundedRect(cr, g.px, 0, g.pw, g.ph, kLaunRadius);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    auto font = [&](const char* name) {
        PangoFontDescription* fd2 = pango_font_description_from_string(name);
        pango_layout_set_font_description(layout, fd2);
        pango_font_description_free(fd2);
    };

    std::string shape, icon;
    launcherPrefixStyle(L.text, shape, icon);
    const double sx = g.px + 10, sy = (kLaunBarH - 40) / 2.0;
    shapePath(cr, shape, sx, sy, 40);
    setSource(cr, app.color("secondary_container"));
    cairo_fill(cr);
    drawIcon(cr, icon, sx + 9, sy + 9, 22, app.color("on_secondary_container"));

    const double fx = sx + 40 + 10, fw = g.pw - (fx - g.px) - 16;
    L.fieldX = fx;
    L.fieldW = fw;
    font(kLaunFieldFont);
    int th = 0, tw = 0;
    if (L.text.empty()) {
        pango_layout_set_text(layout, "Search, calculate or run", -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface_variant"));
        cairo_move_to(cr, fx, (kLaunBarH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    } else {
        const double cx = caretX(layout, L.text, L.cursor);
        const double scrollX = std::max(0.0, cx - fw + 6);
        pango_layout_set_text(layout, L.text.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_save(cr);
        L.scrollX = scrollX;
        cairo_rectangle(cr, fx, 0, fw + 8, kLaunBarH);
        cairo_clip(cr);
        if (L.cursor != L.anchor) {
            const double x0 = caretX(layout, L.text, std::min(L.cursor, L.anchor)), x1 = caretX(layout, L.text, std::max(L.cursor, L.anchor));
            roundedRect(cr, fx - scrollX + x0 - 1, (kLaunBarH - 28) / 2.0, x1 - x0 + 2, 28, 7);
            setSource(cr, app.color("primary"), 0.35);
            cairo_fill(cr);
        }
        setSource(cr, app.color("on_surface"));
        cairo_move_to(cr, fx - scrollX, (kLaunBarH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);
        cairo_rectangle(cr, fx + cx - scrollX, (kLaunBarH - 22) / 2.0, 2, 22);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }

    if (L.text.empty()) {
        for (int i = 0; i < kLaunButtonCount; i++) {
            const Rect b = launcherButtonRect(g.px, g.pw, i);
            if (L.btnHover == i) {
                cairo_arc(cr, b.x + b.w / 2, b.y + b.h / 2, b.w / 2, 0, 2 * M_PI);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, kLaunButtons[i].icon, b.x + (b.w - 20) / 2.0, b.y + (b.h - 20) / 2.0, 20, app.color(L.btnHover == i ? "on_surface" : "on_surface_variant"));
        }
    }

    if (!L.text.empty()) {
        cairo_rectangle(cr, g.px + 1, kLaunBarH, g.pw - 2, 1);
        setSource(cr, app.color("outline_variant"));
        cairo_fill(cr);
        cairo_save(cr);
        roundedRect(cr, g.px, g.listTop, g.pw, g.ph - g.listTop, kLaunRadius);
        cairo_clip(cr);
        cairo_push_group(cr);
        if (translateMode(L)) {
            const double cw = trCardW(g.pw), ch = trCardH(L, g.pw), cy = g.listTop + kTrPad;
            const std::string phrase = translatePhrase(L);
            const bool ready = !L.trOut.empty() && L.trFor == phrase;
            auto langName = [](const std::string& c) {
                static const std::pair<const char*, const char*> kNames[] = {
                    {"en", "English"}, {"es", "Spanish"}, {"ar", "Arabic"}, {"be", "Belarusian"}, {"ca", "Catalan"}, {"cs", "Czech"}, {"de", "German"},
                    {"fr", "French"}, {"gl", "Galician"}, {"he", "Hebrew"}, {"hu", "Hungarian"}, {"it", "Italian"}, {"ja", "Japanese"}, {"ko", "Korean"},
                    {"nl", "Dutch"}, {"no", "Norwegian"}, {"pl", "Polish"}, {"pt", "Portuguese"}, {"ru", "Russian"}, {"sv", "Swedish"}, {"tr", "Turkish"},
                    {"uk", "Ukrainian"}, {"vi", "Vietnamese"}, {"zh-CN", "Chinese (Simplified)"}, {"zh-TW", "Chinese (Traditional)"}};
                for (const auto& [code, name] : kNames) if (c == code) return std::string(name);
                if (c.empty()) return std::string("…");
                if (c == "auto") return std::string("Detected");
                std::string u = c;
                for (auto& x : u) x = static_cast<char>(std::toupper(static_cast<unsigned char>(x)));
                return u;
            };
            for (int side = 0; side < 2; side++) {
                const double cx = g.px + kTrPad + side * (cw + kTrGap);
                const bool target = side == 1;
                roundedRect(cr, cx, cy, cw, ch, 20);
                setSource(cr, app.color(target ? "secondary_container" : "surface_container_high"));
                cairo_fill(cr);
                const Color ink = app.color(target ? "on_secondary_container" : "on_surface");
                const std::string lang = langName(ready ? (target ? L.trTgt : L.trSrc) : "");
                font("Sans 10");
                pango_layout_set_width(layout, -1);
                pango_layout_set_text(layout, lang.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                const double chipW = tw + 24 + 22 + 6;
                roundedRect(cr, cx + 14, cy + 14, chipW, 28, 14);
                setSource(cr, ink, 0.12);
                cairo_fill(cr);
                drawIcon(cr, target ? "chat-line" : "keyboard", cx + 14 + 10, cy + 14 + 5, 18, ink);
                setSource(cr, ink);
                cairo_move_to(cr, cx + 14 + 10 + 18 + 6, cy + 14 + (28 - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                if (target) {
                    drawIcon(cr, "copy", cx + cw - 14 - 20, cy + 14 + 4, 20, ink, false);
                    if (!ready) { }
                }
                const bool real = target ? ready : !phrase.empty();
                const std::string body = target ? trTargetText(L) : (phrase.empty() ? "Type to translate" : phrase);
                font("Sans 12");
                pango_layout_set_width(layout, static_cast<int>((cw - 28) * PANGO_SCALE));
                pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
                pango_layout_set_text(layout, body.c_str(), -1);
                cairo_save(cr);
                cairo_rectangle(cr, cx + 14, cy + 50, cw - 28, ch - 50 - 12);
                cairo_clip(cr);
                setSource(cr, ink, real ? 1.0 : 0.5);
                cairo_move_to(cr, cx + 14, cy + 50);
                pango_cairo_show_layout(cr, layout);
                cairo_restore(cr);
                pango_layout_set_width(layout, -1);
            }
        } else if (L.results.empty()) {
            font(kLaunNameFont);
            pango_layout_set_text(layout, "No results", -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            setSource(cr, app.color("on_surface_variant"));
            cairo_move_to(cr, g.px + (g.pw - tw) / 2.0, g.listTop + (g.listH - th) / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
        for (int row = 0; row < g.totalRows; row++) {
            const int idx = launcherRowIndex(g, L, row);
            if (idx >= static_cast<int>(L.results.size())) break;
            const LauncherResult& r = L.results[idx];
            const Rect it = launcherItemRect(g, row);
            const bool sel = idx == L.selected;
            if (sel || idx == L.hover) {
                roundedRect(cr, it.x, it.y, it.w, it.h, 16);
                if (sel) setSource(cr, app.color("primary_container"));
                else setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            if (sel) {
                cairo_save(cr);
                roundedRect(cr, it.x, it.y, it.w, it.h, 16);
                cairo_clip(cr);
                shapePath(cr, "Cookie12Sided", it.x + it.w - 96, it.y + it.h / 2.0 - 70, 140);
                setSource(cr, app.color("on_primary_container"), 0.09);
                cairo_fill(cr);
                cairo_restore(cr);
            }
            const Color ink = app.color(sel ? "on_primary_container" : "on_surface");
            const Color sub = app.color(sel ? "on_primary_container" : "on_surface_variant");
            const double ix = it.x + 12, iy = it.y + (it.h - 32) / 2.0;
            cairo_surface_t* appIcon = nullptr;
            if (r.kind == ResApp) appIcon = launcherAppIcon(app, r.icon);
            else if (r.kind == ResClip && r.image) {
                const std::string id = clipId(r.payload), key = "clip:" + id;
                if (auto cached = L.icons.find(key); cached != L.icons.end()) appIcon = cached->second;
                else if (access(clipThumbPath(id).c_str(), R_OK) == 0) appIcon = L.icons[key] = loadIconSurface(clipThumbPath(id), 32);
                else requestClipThumb(L, id);
                if (appIcon) {
                    cairo_save(cr);
                    roundedRect(cr, ix, iy, 32, 32, 7);
                    cairo_clip(cr);
                    cairo_set_source_surface(cr, appIcon, ix, iy);
                    cairo_paint(cr);
                    cairo_restore(cr);
                    appIcon = nullptr;
                } else {
                    drawIcon(cr, "image", ix + 3, iy + 3, 26, ink, sel);
                }
            }
            if (r.kind == ResEmoji) {
                font("Sans 18");
                pango_layout_set_text(layout, r.payload.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                cairo_move_to(cr, ix + (32 - tw) / 2.0, iy + (32 - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
            } else if (appIcon) {
                cairo_set_source_surface(cr, appIcon, ix, iy);
                cairo_paint(cr);
            } else if (!(r.kind == ResClip && r.image)) {
                drawIcon(cr, r.kind == ResApp ? "apps" : r.icon, ix + 3, iy + 3, 26, ink, sel);
            }
            const bool clip = r.kind == ResClip;
            const bool showButtons = clip && (sel || idx == L.hover);
            const double right = it.x + it.w - 10;
            const double tx = ix + 32 + 12;
            double verbW = 0;
            if (showButtons) {
                const Rect del{right - 30, it.y + (it.h - 30) / 2.0, 30, 30}, pin{right - 64, it.y + (it.h - 30) / 2.0, 30, 30};
                const bool onPin = L.hover == idx && pin.contains(app.pointerX, launcherPy(app)), onDel = L.hover == idx && del.contains(app.pointerX, launcherPy(app));
                for (const auto& [rect, hot] : {std::make_pair(pin, onPin), std::make_pair(del, onDel)})
                    if (hot) {
                        cairo_arc(cr, rect.x + 15, rect.y + 15, 15, 0, 2 * M_PI);
                        setSource(cr, ink, 0.14);
                        cairo_fill(cr);
                    }
                drawIcon(cr, r.pinned ? "keep_off" : "keep", pin.x + 5, pin.y + 5, 20, ink, r.pinned || onPin);
                drawIcon(cr, "delete", del.x + 5, del.y + 5, 20, ink, onDel);
                verbW = 64 + 8;
            } else if (clip && r.pinned) {
                drawIcon(cr, "keep", right - 20, it.y + (it.h - 18) / 2.0, 18, sub, true);
                verbW = 28;
            }
            if (sel && r.kind != ResKeybind) {
                font(kLaunSmallFont);
                pango_layout_set_text(layout, r.verb.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, ink);
                cairo_move_to(cr, right - 4 - verbW - tw, it.y + (it.h - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                verbW += tw + 16;
            } else if (r.kind == ResKeybind) {
                font(kLaunSmallFont);
                pango_layout_set_text(layout, r.verb.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, sub);
                cairo_move_to(cr, right - 4 - tw, it.y + (it.h - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                verbW = tw + 20;
            }
            const double textW = it.w - (tx - it.x) - 14 - verbW;
            const bool showType = r.kind != ResApp && r.kind != ResEmoji;
            int typeH = 0, nameH = 0;
            if (showType) {
                font(kLaunSmallFont);
                pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
                pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
                pango_layout_set_text(layout, r.type.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &typeH);
            }
            font(kLaunNameFont);
            pango_layout_set_width(layout, static_cast<int>(textW * PANGO_SCALE));
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_text(layout, r.name.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &nameH);
            const double top = it.y + (it.h - (typeH + nameH)) / 2.0;
            if (showType) {
                font(kLaunSmallFont);
                pango_layout_set_text(layout, r.type.c_str(), -1);
                setSource(cr, sub);
                cairo_move_to(cr, tx, top);
                pango_cairo_show_layout(cr, layout);
            }
            font(kLaunNameFont);
            pango_layout_set_text(layout, r.name.c_str(), -1);
            setSource(cr, ink);
            cairo_move_to(cr, tx, top + typeH);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, slide);
        cairo_restore(cr);
    }

    g_object_unref(layout);
    const double rev = std::max(0.0, L.anim.reveal), ox = W / 2.0, oy = kLaunPadTop + 24.0;
    cairo_identity_matrix(cr);
    if (rev > 0.001) popGroupEnd(cr, rev, ox, oy, L.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8));
    else cairo_pattern_destroy(cairo_pop_group(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, L.surface, L.blur, W, H, [&](cairo_t* m) {
        if (rev <= 0.001) return;
        popPaintGrown(m, rev, ox, oy, 1.0, [&] {
            cairo_translate(m, 0, kLaunPadTop);
            roundedRect(m, g.px, 0, g.pw, g.ph, kLaunRadius);
            cairo_fill(m);
        });
    });
    wl_surface_attach(L.surface, buffer, 0, 0);
    wl_surface_damage_buffer(L.surface, 0, 0, W, H);
    wl_surface_commit(L.surface);
}

void closeLauncher(App& app) {
    Launcher& L = app.launcher;
    if (!L.open()) return;
    if (L.mathPid > 0) kill(L.mathPid, SIGKILL);
    if (L.mathFd >= 0) close(L.mathFd);
    if (L.filePid > 0) kill(L.filePid, SIGKILL);
    if (L.fileFd >= 0) close(L.fileFd);
    if (L.layer) zwlr_layer_surface_v1_destroy(L.layer);
    releaseBlurSurface(L.blur);
    wl_surface_destroy(L.surface);
    for (auto& [name, surf] : L.icons)
        if (surf) cairo_surface_destroy(surf);
    if (L.shadow) cairo_surface_destroy(L.shadow);
    L = Launcher{};
    app.onLauncher = false;
    app.repeatKey = 0;
    updateCursor(app);
    malloc_trim(0);
}

void onLauncherConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->launcher.configured) app->launcher.anim.animStart = std::chrono::steady_clock::now();
    app->launcher.configured = true;
    drawLauncher(*app);
}
void onLauncherClosed(void* d, zwlr_layer_surface_v1*) { closeLauncher(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kLauncherListener = {onLauncherConfigure, onLauncherClosed};

void openLauncher(App& app) {
    Launcher& L = app.launcher;
    if (L.open()) return;
    closeSelector(app);
    closeNotifPanel(app);
    closeMediaPanel(app); closeClockPanel(app);
    ensureApps(app);
    L = Launcher{};
    L.anim.overshoot = 1.05;
    L.anim.inSeconds = 0.26;
    L.anim.outSeconds = 0.13;
    popStart(L.anim);
    L.surface = wl_compositor_create_surface(app.compositor);
    L.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, L.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-launcher");
    zwlr_layer_surface_v1_set_anchor(L.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
    zwlr_layer_surface_v1_set_margin(L.layer, app.cfg.frame + (app.cfg.barPosition == "top" ? kBarHeight : 0) + 30 - kLaunPadTop, 0, 0, 0);
    zwlr_layer_surface_v1_set_size(L.layer, kLaunW, kLaunH);
    zwlr_layer_surface_v1_set_exclusive_zone(L.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(L.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(L.layer, &kLauncherListener, &app);
    wl_surface_commit(L.surface);
}

void retractLauncher(App& app) {
    Launcher& L = app.launcher;
    if (!L.open() || popIsOut(L.anim)) return;
    if (!L.configured) { closeLauncher(app); return; }
    popRetract(L.anim, std::chrono::steady_clock::now());
    L.hover = -1;
    L.btnHover = -1;
    drawLauncher(app);
}
void toggleLauncher(App& app) {
    Launcher& L = app.launcher;
    if (!L.open()) { openLauncher(app); return; }
    if (popIsOut(L.anim)) {
        popReverse(L.anim, std::chrono::steady_clock::now());
        drawLauncher(app);
    } else retractLauncher(app);
}
void tickLauncher(App& app, std::chrono::steady_clock::time_point now) {
    Launcher& L = app.launcher;
    if (!L.open() || !L.configured) return;
    bool moving = false;
    if (popTick(L.anim, now, moving)) { closeLauncher(app); return; }
    if (moving) drawLauncher(app);
}
std::chrono::steady_clock::time_point launcherWake(const App& app) {
    if (!app.launcher.open() || !app.launcher.configured) return std::chrono::steady_clock::time_point::max();
    return popWake(app.launcher.anim.phase, app.launcher.anim.settleAt, std::chrono::steady_clock::now());
}

void launcherTextChanged(App& app) {
    app.launcher.anchor = app.launcher.cursor;
    updateLauncherResults(app);
    drawLauncher(app);
}

// UTF-8 aware caret steps.
size_t prevChar(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) pos--;
    return pos;
}
size_t nextChar(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    pos++;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) pos++;
    return pos;
}

void launcherMoveSelection(App& app, int delta) {
    Launcher& L = app.launcher;
    if (L.results.empty()) return;
    const LaunLayout g = launcherLayout(app);
    L.selected = std::clamp(L.selected + delta, 0, static_cast<int>(L.results.size()) - 1);
    if (L.selected < g.nMain) {
        if (L.selected < L.scroll) L.scroll = L.selected;
        else if (L.selected >= L.scroll + g.mainVisible) L.scroll = L.selected - g.mainVisible + 1;
    }
    drawLauncher(app);
}

// One key press (or repeat). Returns true when it did something. Text comes from xkbcommon, so any layout works.
bool launcherKey(App& app, uint32_t key) {
    Launcher& L = app.launcher;
    if (!app.xkbState || popIsOut(L.anim)) return false;
    const xkb_keycode_t code = key + 8;  // evdev -> xkb
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code);
    const bool ctrl = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    {
        textsel::Key k = textsel::Key::None;
        const xkb_keysym_t low = xkb_keysym_to_lower(sym);
        if (sym == XKB_KEY_Left) k = textsel::Key::Left;
        else if (sym == XKB_KEY_Right) k = textsel::Key::Right;
        else if (sym == XKB_KEY_Home) k = textsel::Key::Home;
        else if (sym == XKB_KEY_End) k = textsel::Key::End;
        else if (sym == XKB_KEY_BackSpace) k = textsel::Key::Backspace;
        else if (sym == XKB_KEY_Delete) k = textsel::Key::Delete;
        else if (ctrl && low == XKB_KEY_a) k = textsel::Key::SelectAll;
        else if (ctrl && low == XKB_KEY_c) k = textsel::Key::Copy;
        else if (ctrl && low == XKB_KEY_x) k = textsel::Key::Cut;
        else if (ctrl && low == XKB_KEY_v) k = textsel::Key::Paste;
        if (k != textsel::Key::None) {
            const bool shift = xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
            textsel::Field f(L.text, L.cursor, L.anchor);
            textsel::Result r = textsel::handleKey(f, k, ctrl, shift);
            if (r.wantCopy) clipboard::copy(r.copyText);
            if (r.wantPaste) {
                std::string clip;
                if (clipboard::paste(clip)) {
                    if (clip.size() > 4096) {  // cut at 4096 bytes, but not in the middle of a UTF-8 character
                        clip.resize(4096);
                        while (!clip.empty() && textsel::isContinuation(clip.back())) clip.pop_back();
                        if (!clip.empty() && (static_cast<unsigned char>(clip.back()) & 0xC0) == 0xC0) clip.pop_back();
                    }
                    r.changed = f.insert(clip);
                }
            }
            if (r.changed) launcherTextChanged(app);
            else drawLauncher(app);
            return true;
        }
    }
    switch (sym) {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: launcherActivate(app, L.selected); return true;
        case XKB_KEY_Down: launcherMoveSelection(app, 1); return true;
        case XKB_KEY_Up: launcherMoveSelection(app, -1); return true;
        case XKB_KEY_Page_Down: launcherMoveSelection(app, 5); return true;
        case XKB_KEY_Page_Up: launcherMoveSelection(app, -5); return true;
        case XKB_KEY_Tab:
            if (!L.results.empty()) {
                L.text = L.results[0].name;
                L.cursor = L.text.size();
                launcherTextChanged(app);
            }
            return true;
        default: break;
    }
    if (ctrl && (sym == XKB_KEY_p || sym == XKB_KEY_P)) {
        if (L.selected >= 0 && L.selected < static_cast<int>(L.results.size()) && L.results[L.selected].kind == ResClip) {
            clipTogglePin(L.results[L.selected].payload);
            updateLauncherResults(app, true);
            drawLauncher(app);
        }
        return true;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(app.xkbState, code, buf, sizeof buf);
    if (n <= 0 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f) return false;
    textsel::Field typed(L.text, L.cursor, L.anchor);
    typed.insert(std::string(buf, static_cast<size_t>(n)));
    launcherTextChanged(app);
    return true;
}

bool launcherRepeatable(uint32_t key) {
    constexpr uint32_t kTab = 15;
    return key != 28 && key != 96 && key != kTab && key != 1;
}

int launcherButtonAt(const App& app, double x, double y) {
    if (!app.launcher.text.empty()) return -1;
    const LaunLayout g = launcherLayout(app);
    for (int i = 0; i < kLaunButtonCount; i++)
        if (launcherButtonRect(g.px, g.pw, i).contains(x, y)) return i;
    return -1;
}

void launcherMotion(App& app) {
    Launcher& L = app.launcher;
    if (popIsOut(L.anim)) return;
    if (L.selDrag) {
        textsel::Field f(L.text, L.cursor, L.anchor);
        const double left = L.fieldX, right = L.fieldX + L.fieldW;
        const double x = std::clamp(app.pointerX, left, right);
        size_t at = launcherIndexAtX(L.text, x - (L.fieldX - L.scrollX));
        if (app.pointerX < left - 4) at = std::min(at, textsel::prevChar(L.text, f.caret));
        else if (app.pointerX > right + 4) at = std::max(at, textsel::nextChar(L.text, f.caret));
        textsel::drag(f, at, L.dragInfo);
        drawLauncher(app);
        return;
    }
    const LaunLayout g = launcherLayout(app);
    const int btn = launcherButtonAt(app, app.pointerX, launcherPy(app));
    if (btn != L.btnHover) { L.btnHover = btn; drawLauncher(app); updateCursor(app); }
    int hover = -1;
    for (int row = 0; row < g.totalRows; row++) {
        const int idx = launcherRowIndex(g, L, row);
        if (idx >= static_cast<int>(L.results.size())) break;
        if (launcherItemRect(g, row).contains(app.pointerX, launcherPy(app))) hover = idx;
    }
    if (hover == L.hover) return;
    L.hover = hover;
    drawLauncher(app);
    updateCursor(app);
}

void launcherClick(App& app) {
    Launcher& L = app.launcher;
    const LaunLayout g = launcherLayout(app);
    if (popIsOut(L.anim)) return;
    if (!Rect{g.px, 0, g.pw, g.ph}.contains(app.pointerX, launcherPy(app))) { retractLauncher(app); return; }
    if (const int btn = launcherButtonAt(app, app.pointerX, launcherPy(app)); btn >= 0) {
        if (std::string(kLaunButtons[btn].key) == "scanQr") { retractLauncher(app); openCapture(app); if (app.cap.open()) app.cap.autoAct = 3; return; }
        L.text = std::string(1, kPfxClipboard);
        L.cursor = 1;
        L.btnHover = -1;
        launcherTextChanged(app);
        return;
    }
    if (!L.text.empty() && launcherPy(app) >= 0 && launcherPy(app) <= kLaunBarH && app.pointerX >= L.fieldX && app.pointerX <= L.fieldX + L.fieldW + 8) {
        textsel::Field f(L.text, L.cursor, L.anchor);
        const bool shift = app.xkbState && xkb_state_mod_name_is_active(app.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
        const int n = L.clicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), app.pointerX, app.pointerY);
        textsel::applyClick(f, launcherIndexAtX(L.text, app.pointerX - (L.fieldX - L.scrollX)), n, shift);
        L.dragInfo = textsel::beginDrag(f, n);
        L.selDrag = true;
        drawLauncher(app);
        return;
    }
    if (translateMode(L)) { if (launcherPy(app) > g.listTop) launcherActivate(app, 0); return; }
    if (L.hover < 0) return;
    const LauncherResult& r = L.results[L.hover];
    if (r.kind == ResClip) {
        const int row = L.hover < g.nMain ? L.hover - L.scroll : g.mainVisible + (L.hover - g.nMain);
        const Rect it = launcherItemRect(g, row), right{it.x + it.w - 10, 0, 0, 0};
        const Rect del{right.x - 30, it.y + (it.h - 30) / 2.0, 30, 30}, pin{right.x - 64, it.y + (it.h - 30) / 2.0, 30, 30};
        if (pin.contains(app.pointerX, launcherPy(app))) {
            clipTogglePin(r.payload);
            updateLauncherResults(app, true);
            drawLauncher(app);
            return;
        }
        if (del.contains(app.pointerX, launcherPy(app))) {
            const std::string line = r.payload;
            runDetached({"sh", "-c", "printf '%s\\n' \"$1\" | cliphist delete", "sh", line.c_str()});
            L.clipLines.erase(std::remove(L.clipLines.begin(), L.clipLines.end(), line), L.clipLines.end());
            auto pins = loadClipPins();
            pins.erase(std::remove_if(pins.begin(), pins.end(), [&](const auto& pin2) { return pin2.first == clipId(line); }), pins.end());
            saveClipPins(pins);
            updateLauncherResults(app, true);
            drawLauncher(app);
            return;
        }
    }
    launcherActivate(app, L.hover);
}

void launcherScroll(App& app, int rows) {
    Launcher& L = app.launcher;
    const LaunLayout g = launcherLayout(app);
    const int maxScroll = std::max(0, g.nMain - g.mainVisible);
    const int next = std::clamp(L.scroll + rows, 0, maxScroll);
    if (next == L.scroll) return;
    L.scroll = next;
    launcherMotion(app);
    drawLauncher(app);
}
