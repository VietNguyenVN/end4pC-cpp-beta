// Tools: the file of an icon by name, found as the desktop finds it (shell: src/parts/06_desktop_entries.inc): the icon theme in use (gsettings), the themes it inherits
// from, hicolor, then /usr/share/pixmaps. Used for the apps of the Autostart page, whose entries often name a theme icon that hicolor does not have.
// Included by src/tools.cpp (inside its anonymous namespace), before autostart_store.inc.

std::vector<std::string> thSplit(const std::string& v, char sep = ',') {
    std::vector<std::string> out;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, sep)) {
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && (item.back() == ' ' || item.back() == '\r')) item.pop_back();
        if (!item.empty()) out.push_back(item);
    }
    return out;
}
std::string asLower(std::string s);

struct ThThemeDir {
    std::string path;
    int size = 0, minSize = 0, maxSize = 0, threshold = 2;
    bool scalable = false;
};
struct ThIconTheme {
    std::vector<std::string> roots;
    std::vector<ThThemeDir> dirs;
    std::vector<std::string> inherits;
};

std::vector<std::string> thBaseDirs() {
    return {homeDir() + "/.local/share/icons", homeDir() + "/.icons", "/usr/share/icons", "/usr/local/share/icons",
            "/var/lib/flatpak/exports/share/icons", homeDir() + "/.local/share/flatpak/exports/share/icons"};
}

ThIconTheme thLoad(const std::string& name) {
    ThIconTheme t;
    std::string indexPath;
    for (const auto& base : thBaseDirs()) {
        if (access((base + "/" + name + "/index.theme").c_str(), R_OK) == 0) {
            t.roots.push_back(base + "/" + name);
            if (indexPath.empty()) indexPath = base + "/" + name + "/index.theme";
        }
    }
    if (indexPath.empty()) return t;
    std::ifstream f(indexPath);
    std::string line, section;
    std::vector<std::string> listed;
    std::map<std::string, ThThemeDir> dirs;
    std::map<std::string, std::string> contexts;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (section == "Icon Theme") {
            if (k == "Directories") listed = thSplit(v);
            else if (k == "Inherits") t.inherits = thSplit(v);
        } else {
            ThThemeDir& d = dirs[section];
            d.path = section;
            if (k == "Size") { d.size = std::atoi(v.c_str()); if (!d.minSize) d.minSize = d.size; if (!d.maxSize) d.maxSize = d.size; }
            else if (k == "MinSize") d.minSize = std::atoi(v.c_str());
            else if (k == "MaxSize") d.maxSize = std::atoi(v.c_str());
            else if (k == "Threshold") d.threshold = std::atoi(v.c_str());
            else if (k == "Type") d.scalable = v == "Scalable";
            else if (k == "Context") contexts[section] = v;
        }
    }
    for (const auto& name2 : listed) {
        auto it = dirs.find(name2);
        if (it == dirs.end()) continue;
        const std::string lower = asLower(name2);
        if (lower.find("symbolic") != std::string::npos) continue;
        const bool apps = contexts[name2] == "Applications" || lower.find("apps") != std::string::npos;
        if (apps) t.dirs.push_back(it->second);
    }
    return t;
}

int thDistance(const ThThemeDir& d, int want) {
    if (d.scalable || d.minSize != d.maxSize) {
        if (want < d.minSize) return d.minSize - want;
        if (want > d.maxSize) return want - d.maxSize;
        return 0;
    }
    return std::abs(d.size - want);
}

std::string thFind(const ThIconTheme& t, const std::string& name, int want) {
    std::vector<const ThThemeDir*> order;
    for (const auto& d : t.dirs) order.push_back(&d);
    std::stable_sort(order.begin(), order.end(),
                     [&](const ThThemeDir* a, const ThThemeDir* b) { return thDistance(*a, want) < thDistance(*b, want); });
    for (const ThThemeDir* d : order)
        for (const auto& root : t.roots)
            for (const char* ext : {".svg", ".png"}) {
                const std::string path = root + "/" + d->path + "/" + name + ext;
                if (access(path.c_str(), R_OK) == 0) return path;
            }
    return "";
}

std::string thCurrent() {
    static std::string kept;
    static struct timespec keptStamp = {};
    static std::chrono::steady_clock::time_point keptAt{};
    static bool have = false;
    struct stat st = {};
    const bool statOk = stat((homeDir() + "/.config/dconf/user").c_str(), &st) == 0;
    const auto now = std::chrono::steady_clock::now();
    if (have && (statOk ? (st.st_mtim.tv_sec == keptStamp.tv_sec && st.st_mtim.tv_nsec == keptStamp.tv_nsec) : now - keptAt < std::chrono::seconds(5))) return kept;
    FILE* f = popen("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null", "r");
    char buf[128] = {};
    const char* got = f ? std::fgets(buf, sizeof buf, f) : nullptr;
    if (f) pclose(f);
    std::string v = got ? buf : "";
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\'' || c == '\n' || c == '"'; }), v.end());
    kept = v.empty() ? "hicolor" : v;
    keptStamp = st.st_mtim;
    keptAt = now;
    have = true;
    return kept;
}

std::string thLookupIcon(const std::string& name, int size) {
    if (name.empty()) return "";
    if (name[0] == '/') return access(name.c_str(), R_OK) == 0 ? name : "";
    static std::map<std::string, ThIconTheme> cache;
    std::vector<std::string> todo = {thCurrent()}, seen;
    for (size_t i = 0; i < todo.size(); i++) {
        const std::string th = todo[i];
        if (std::find(seen.begin(), seen.end(), th) != seen.end()) continue;
        seen.push_back(th);
        auto it = cache.find(th);
        if (it == cache.end()) it = cache.emplace(th, thLoad(th)).first;
        const std::string hit = thFind(it->second, name, size);
        if (!hit.empty()) return hit;
        for (const auto& parent : it->second.inherits) todo.push_back(parent);
        if (i + 1 == todo.size() && th != "hicolor") todo.push_back("hicolor");
    }
    for (const char* ext : {".svg", ".png"}) {
        const std::string path = std::string("/usr/share/pixmaps/") + name + ext;
        if (access(path.c_str(), R_OK) == 0) return path;
    }
    return "";
}
