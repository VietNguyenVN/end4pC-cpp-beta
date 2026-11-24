// Task manager: the window `end4pC-tools tasks` (a window of its own, not a tab of Tools). The programs of the user grouped by app, with CPU,
// memory and disk of each, live graphs of the machine, and End task / Force kill. Everything comes from /proc: no helper and no root.
// Included by src/tools.cpp (inside its anonymous namespace).

std::string tkT(const char* s) { return std::string(i18n::tr(s)); }

constexpr int kTkHistory = 60;
constexpr double kTkSampleSec = 1.0, kTkRowH = 50, kTkChildH = 38, kTkHeadH = 60, kTkTileH = 124, kTkEdge = 14, kTkPillH = 36, kTkGap = 12;

struct TkProc {
    int pid = 0, ppid = 0;
    std::string comm;
    unsigned long long ticks = 0, io = 0;
    double cpu = 0, disk = 0;  // percent of the whole machine, bytes per second
    long rssKb = 0;
};
struct TkApp {
    std::string key, name, iconName;
    double cpu = 0, disk = 0;
    long rssKb = 0;
    std::vector<TkProc> procs;
};
struct TkEntry { std::string name, icon; };

// ---- reading /proc ----
std::string tkRead(const std::string& path, size_t max = 8192) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string out(max, '\0');
    const ssize_t n = read(fd, out.data(), max);
    close(fd);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}
std::string tkBase(const std::string& path) { const size_t s = path.rfind('/'); return s == std::string::npos ? path : path.substr(s + 1); }

// The desktop entries: "Name", "Icon" and the program of "Exec", so a process shows as the app it belongs to.
std::map<std::string, TkEntry> tkLoadEntries() {
    std::map<std::string, TkEntry> out;
    std::vector<std::string> dirs = {"/usr/share/applications", "/usr/local/share/applications", "/var/lib/flatpak/exports/share/applications",
                                     envDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/applications"};
    for (const std::string& d : dirs) {
        DIR* dir = opendir(d.c_str());
        if (!dir) continue;
        while (const dirent* e = readdir(dir)) {
            const std::string f = e->d_name;
            if (f.size() < 9 || f.compare(f.size() - 8, 8, ".desktop") != 0) continue;
            std::istringstream in(tkRead(d + "/" + f, 32768));
            TkEntry en;
            std::string exec, line;
            bool main = false, hidden = false;
            while (std::getline(in, line)) {
                if (!line.empty() && line[0] == '[') { main = line == "[Desktop Entry]"; continue; }
                if (!main) continue;
                if (line.rfind("Name=", 0) == 0 && en.name.empty()) en.name = line.substr(5);
                else if (line.rfind("Icon=", 0) == 0) en.icon = line.substr(5);
                else if (line.rfind("Exec=", 0) == 0 && exec.empty()) exec = line.substr(5);
                else if (line == "NoDisplay=true" || line == "Hidden=true") hidden = true;
            }
            (void)hidden;
            std::istringstream ex(exec);
            std::string tok, prog;
            while (ex >> tok) {
                if (tok == "env" || tok.find('=') != std::string::npos || tok[0] == '-') continue;
                prog = tkBase(tok);
                break;
            }
            if (!prog.empty() && !en.name.empty() && !out.count(prog)) out[prog] = en;
            const std::string id = f.substr(0, f.size() - 8);  // (the file name often is the program or its class)
            if (!en.name.empty() && !out.count(id)) out[id] = en;
        }
        closedir(dir);
    }
    return out;
}

cairo_surface_t* tkLoadIcon(const std::string& name, int size) {
    if (name.empty()) return nullptr;
    std::vector<std::string> paths;
    if (name[0] == '/') paths.push_back(name);
    else {
        const std::string home = envDir("XDG_DATA_HOME", homeDir() + "/.local/share");
        for (const char* s : {"48x48", "64x64", "128x128", "256x256", "scalable", "32x32", "512x512"})
            for (const char* ext : {".png", ".svg"}) {
                paths.push_back(std::string("/usr/share/icons/hicolor/") + s + "/apps/" + name + ext);
                paths.push_back(home + "/icons/hicolor/" + s + "/apps/" + name + ext);
            }
        for (const char* ext : {".png", ".svg"}) paths.push_back("/usr/share/pixmaps/" + name + ext);
    }
    for (const std::string& p : paths) {
        if (access(p.c_str(), R_OK) != 0) continue;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_size(p.c_str(), size * 2, size * 2, nullptr);
        if (!pb) continue;
        GdkPixbuf* rgba = gdk_pixbuf_get_has_alpha(pb) ? pb : gdk_pixbuf_add_alpha(pb, FALSE, 0, 0, 0);
        const int w = gdk_pixbuf_get_width(rgba), h = gdk_pixbuf_get_height(rgba), rs = gdk_pixbuf_get_rowstride(rgba);
        const guchar* px = gdk_pixbuf_get_pixels(rgba);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        unsigned char* d = cairo_image_surface_get_data(s);
        const int ds = cairo_image_surface_get_stride(s);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const guchar* q = px + y * rs + x * 4;
                unsigned char* o = d + y * ds + x * 4;
                const unsigned a = q[3];
                o[0] = static_cast<unsigned char>(q[2] * a / 255);
                o[1] = static_cast<unsigned char>(q[1] * a / 255);
                o[2] = static_cast<unsigned char>(q[0] * a / 255);
                o[3] = static_cast<unsigned char>(a);
            }
        cairo_surface_mark_dirty(s);
        if (rgba != pb) g_object_unref(rgba);
        g_object_unref(pb);
        return s;
    }
    return nullptr;
}

std::string tkBytes(double b) {
    char buf[32];
    if (b >= 1024.0 * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024.0 * 1024) std::snprintf(buf, sizeof buf, "%.0f MB", b / (1024.0 * 1024));
    else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024);
    return buf;
}
std::string tkRate(double b) {
    if (b < 1024) return "0 KB/s";
    return tkBytes(b) + "/s";
}
std::string tkPercent(double v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, v < 10 ? "%.1f%%" : "%.0f%%", v);
    return buf;
}

#include "page_tasks_perf.inc"

struct TkProcessSample {
    std::vector<TkApp> apps;
    std::map<int, std::pair<unsigned long long, unsigned long long>> before;  // pid -> ticks, io bytes
    std::vector<double> cpuH, memH, diskH, netH;
    double cpuNow = 0, memNow = 0, memTotalKb = 0, memUsedKb = 0, diskNow = 0, netNow = 0, netUp = 0, netDown = 0;
    unsigned long long lastTotal = 0, lastIdle = 0, lastDisk = 0, lastNetRx = 0, lastNetTx = 0;
    Clock::time_point lastSample{};
    int cores = 1;
    long clkTck = 100, pageKb = 4;
    bool primed = false;
    int procTotal = 0;
    bool netWifi = false;  // the connection that carries the traffic is Wi-Fi (the icon of the network tile)

    static void push(std::vector<double>& h, double v) {
        h.push_back(v);
        if (static_cast<int>(h.size()) > kTkHistory) h.erase(h.begin());
    }

    void sampleMachine(double dt) {
        {  // CPU of the machine
            std::istringstream in(tkRead("/proc/stat", 512));
            std::string tag;
            unsigned long long v[8] = {};
            in >> tag;
            for (auto& x : v) in >> x;
            unsigned long long total = 0;
            for (auto x : v) total += x;
            const unsigned long long idle = v[3] + v[4];
            if (lastTotal && total > lastTotal) cpuNow = 100.0 * (1.0 - double(idle - lastIdle) / double(total - lastTotal));
            lastTotal = total;
            lastIdle = idle;
        }
        {  // memory
            std::istringstream in(tkRead("/proc/meminfo", 2048));
            std::string k, unit;
            double v = 0, avail = 0;
            while (in >> k >> v >> unit) {
                if (k == "MemTotal:") memTotalKb = v;
                else if (k == "MemAvailable:") avail = v;
            }
            memUsedKb = memTotalKb - avail;
            memNow = memTotalKb > 0 ? 100.0 * memUsedKb / memTotalKb : 0;
        }
        {  // disks (whole devices only) and network (everything but lo)
            std::istringstream in(tkRead("/proc/diskstats", 65536));
            unsigned long long sectors = 0;
            for (std::string line; std::getline(in, line);) {
                std::istringstream ls(line);
                std::string maj, min, nm;
                unsigned long long f[8] = {};
                ls >> maj >> min >> nm;
                for (auto& x : f) ls >> x;
                if (nm.rfind("loop", 0) == 0 || nm.rfind("ram", 0) == 0 || nm.rfind("zram", 0) == 0) continue;
                if (access(("/sys/block/" + nm).c_str(), F_OK) != 0) continue;
                sectors += f[2] + f[6];
            }
            const unsigned long long bytes = sectors * 512;
            if (lastDisk && bytes >= lastDisk) diskNow = double(bytes - lastDisk) / dt;
            lastDisk = bytes;
            std::istringstream nd(tkRead("/proc/net/dev", 16384));
            unsigned long long rx = 0, tx = 0;
            for (std::string line; std::getline(nd, line);) {
                const size_t c = line.find(':');
                if (c == std::string::npos) continue;
                std::string nm = line.substr(0, c);
                nm.erase(0, nm.find_first_not_of(' '));
                if (nm == "lo") continue;
                std::istringstream ls(line.substr(c + 1));
                unsigned long long f[16] = {};
                for (auto& x : f) ls >> x;
                rx += f[0];
                tx += f[8];
            }
            if (lastNetRx && rx >= lastNetRx) netDown = double(rx - lastNetRx) / dt;
            if (lastNetTx && tx >= lastNetTx) netUp = double(tx - lastNetTx) / dt;
            lastNetRx = rx;
            lastNetTx = tx;
            netNow = netDown + netUp;
            {  // the interface of the default route: Wi-Fi or not
                std::istringstream rt(tkRead("/proc/net/route", 4096));
                std::string line;
                std::getline(rt, line);
                netWifi = false;
                while (std::getline(rt, line)) {
                    std::istringstream ls(line);
                    std::string ifc, dest;
                    ls >> ifc >> dest;
                    if (dest == "00000000") { netWifi = access(("/sys/class/net/" + ifc + "/wireless").c_str(), F_OK) == 0; break; }
                }
            }
        }
        push(cpuH, cpuNow);
        push(memH, memNow);
        push(diskH, diskNow);
        push(netH, netNow);
    }

    void sampleProcesses(double dt) {
        procTotal = 0;
        std::map<std::string, TkApp> groups;
        std::map<int, std::pair<unsigned long long, unsigned long long>> now;
        const uid_t me = getuid();
        DIR* dir = opendir("/proc");
        if (!dir) return;
        while (const dirent* e = readdir(dir)) {
            const int pid = std::atoi(e->d_name);
            if (pid <= 0) continue;
            struct stat st{};
            const std::string base = std::string("/proc/") + e->d_name;
            if (stat(base.c_str(), &st) != 0 || st.st_uid != me) continue;
            const std::string stat1 = tkRead(base + "/stat", 1024);
            const size_t lp = stat1.find('('), rp = stat1.rfind(')');
            if (lp == std::string::npos || rp <= lp || rp + 2 >= stat1.size()) continue;
            char state = 0;
            int ppid = 0;
            unsigned long long ut = 0, stm = 0;
            long threads = 0, rss = 0;
            if (std::sscanf(stat1.c_str() + rp + 2, "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %ld %*d %*u %*u %ld", &state, &ppid, &ut, &stm, &threads, &rss) < 6) continue;
            char exeBuf[4096];
            const ssize_t n = readlink((base + "/exe").c_str(), exeBuf, sizeof exeBuf - 1);
            if (n <= 0) continue;  // (kernel threads and the ones that cannot be read)
            std::string exe(exeBuf, static_cast<size_t>(n));
            if (const size_t d = exe.find(" (deleted)"); d != std::string::npos) exe.erase(d);
            TkProc p;
            p.pid = pid;
            p.ppid = ppid;
            p.comm = stat1.substr(lp + 1, rp - lp - 1);
            while (!p.comm.empty() && (p.comm.back() == '\n' || p.comm.back() == ' ')) p.comm.pop_back();
            p.ticks = ut + stm;
            p.rssKb = rss * pageKb;
            {
                std::istringstream io(tkRead(base + "/io", 512));
                std::string k;
                unsigned long long v = 0, total = 0;
                while (io >> k >> v) if (k == "read_bytes:" || k == "write_bytes:") total += v;
                p.io = total;
            }
            now[pid] = {p.ticks, p.io};
            if (const auto b = before.find(pid); b != before.end() && primed) {
                const double dticks = p.ticks >= b->second.first ? double(p.ticks - b->second.first) : 0;
                p.cpu = 100.0 * dticks / (double(clkTck) * dt * cores);
                p.disk = p.io >= b->second.second ? double(p.io - b->second.second) / dt : 0;
            }
            const std::string prog = tkBase(exe);
            TkApp& a = groups[exe];
            if (a.key.empty()) {
                a.key = exe;
                a.name = p.comm.empty() ? prog : p.comm;
                a.iconName = prog;
            }
            a.cpu += p.cpu;
            a.disk += p.disk;
            a.rssKb += p.rssKb;
            a.procs.push_back(std::move(p));
            procTotal++;
        }
        closedir(dir);
        before = std::move(now);
        apps.clear();
        for (auto& [k, a] : groups) {
            std::sort(a.procs.begin(), a.procs.end(), [](const TkProc& x, const TkProc& y) { return x.cpu != y.cpu ? x.cpu > y.cpu : x.pid < y.pid; });
            apps.push_back(std::move(a));
        }
        primed = true;
    }

    void sample() {
        const auto t = Clock::now();
        const double dt = lastSample == Clock::time_point{} ? 1.0 : std::max(0.2, std::chrono::duration<double>(t - lastSample).count());
        lastSample = t;
        sampleMachine(dt);
        sampleProcesses(dt);
    }

};

struct TasksPage : ToolPage, TkProcessSample {
    const char* name() const override { return "Processes"; }
    const char* icon() const override { return "list-check"; }

    std::map<std::string, cairo_surface_t*> icons;  // by icon name; nullptr: none found (a letter is drawn)
    std::future<TkProcessSample> pending;
    std::future<std::map<std::string, TkEntry>> pendingEntries;
    std::map<std::string, TkEntry> entries;
    bool loaded = false, resample = false;

    // ---- view ----
    int sortCol = 1;  // 0 name, 1 cpu, 2 memory, 3 disk
    bool sortDesc = true;
    std::string filter, selKey;
    int selPid = 0;  // 0: the whole app
    std::set<std::string> open;
    Elastic scrollY;
    struct Hit { Rect r; int app = -1, pid = 0; bool chevron = false; };
    std::vector<Hit> hits;
    Rect tiles[4], endBtn, killBtn, search, table, head;
    Rect colRect[4];
    int hover = -1, hoverBtn = -1, hoverHead = -1;
    Clock::time_point pressAt{};
    bool dirtyView = false;
    // the confirmation before a program is ended: 0 none, else the signal that goes when it is accepted
    int askSig = 0;
    std::string askName;
    int askCount = 0;
    bool askCritical = false;
    Rect askCard, askYes, askNo;
    int askHover = -1;

    void init() override { startSample(); }
    void startSample() {
        if (pending.valid()) return;
        pending = sampleInBackground([snapshot = static_cast<const TkProcessSample&>(*this)]() mutable {
            if (snapshot.lastSample == Clock::time_point{}) {
                snapshot.cores = std::max<long>(1, sysconf(_SC_NPROCESSORS_ONLN));
                snapshot.clkTck = std::max<long>(1, sysconf(_SC_CLK_TCK));
                snapshot.pageKb = std::max<long>(1, sysconf(_SC_PAGESIZE) / 1024);
            }
            snapshot.sample();
            return std::move(snapshot);
        });
    }
    ~TasksPage() override {
        for (auto& [k, s] : icons) if (s) cairo_surface_destroy(s);
    }
    bool animating() const override { return scrollY.busy(); }

    static std::string lowerStr(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    void sortApps() {
        const int col = sortCol;
        const bool desc = sortDesc;
        std::stable_sort(apps.begin(), apps.end(), [&](const TkApp& a, const TkApp& b) {
            double x = 0, y = 0;
            if (col == 0) { const int c = lowerStr(a.name).compare(lowerStr(b.name)); return desc ? c > 0 : c < 0; }
            if (col == 1) { x = a.cpu; y = b.cpu; }
            else if (col == 2) { x = double(a.rssKb); y = double(b.rssKb); }
            else { x = a.disk; y = b.disk; }
            return desc ? x > y : x < y;
        });
    }
    bool matches(const TkApp& a) const {
        if (filter.empty()) return true;
        const std::string f = lowerStr(filter);
        if (lowerStr(a.name).find(f) != std::string::npos) return true;
        for (const TkProc& p : a.procs) if (lowerStr(p.comm).find(f) != std::string::npos || std::to_string(p.pid) == filter) return true;
        return false;
    }

    void applyAppNames() {
        for (TkApp& app : apps) {
            if (const auto entry = entries.find(tkBase(app.key)); entry != entries.end()) {
                app.name = entry->second.name;
                app.iconName = entry->second.icon;
            }
        }
        sortApps();
    }

    bool tick() override {
        bool moved = scrollY.step();
        if (sampleReady(pending)) {
            static_cast<TkProcessSample&>(*this) = pending.get();
            if (!loaded) pendingEntries = sampleInBackground(tkLoadEntries);
            loaded = true;
            applyAppNames();
            moved = true;
        }
        if (sampleReady(pendingEntries)) {
            entries = pendingEntries.get();
            applyAppNames();
            moved = true;
        }
        if (!pending.valid() && (resample || !loaded || Clock::now() - lastSample >= std::chrono::milliseconds(static_cast<int>(kTkSampleSec * 1000)))) {
            resample = false;
            startSample();
        }
        return moved;
    }

    // ---- actions ----
    void endSelected(int sig) {
        for (const TkApp& a : apps) {
            if (a.key != selKey) continue;
            for (const TkProc& p : a.procs) if (selPid == 0 || p.pid == selPid) kill(p.pid, sig);
        }
        resample = true;  // (look again soon: the list drops what ended)
    }
    // The target of End task: the app (all of its processes) or the one process that is selected.
    void ask(int sig) {
        for (const TkApp& a : apps) {
            if (a.key != selKey) continue;
            askSig = sig;
            askCount = selPid == 0 ? static_cast<int>(a.procs.size()) : 1;
            askName = a.name;
            if (selPid != 0) for (const TkProc& p : a.procs) if (p.pid == selPid) askName = p.comm + "  " + std::to_string(p.pid);
            static const char* critical[] = {"Hyprland", "end4pC", "end4pC-tools", "systemd", "dbus-daemon", "dbus-broker", "pipewire", "wireplumber", "sway", "niri", "Xwayland"};
            askCritical = false;
            const std::string prog = tkBase(a.key);
            for (const char* c : critical) if (prog == c) askCritical = true;
            askHover = -1;
        }
    }
    bool haveSel() const {
        for (const TkApp& a : apps) if (a.key == selKey) return true;
        return false;
    }

    // ---- drawing ----
    cairo_surface_t* iconFor(const TkApp& a) {
        if (pendingEntries.valid()) return nullptr;
        auto it = icons.find(a.iconName);
        if (it != icons.end()) return it->second;
        return icons[a.iconName] = tkLoadIcon(a.iconName, 28);
    }

    void spark(cairo_t* cr, const Rect& r, const std::vector<double>& h, double maxV, const Color& c) {
        if (h.size() < 2) return;
        const double step = r.w / (kTkHistory - 1);
        const double x0 = r.x + r.w - step * (h.size() - 1);
        auto yOf = [&](double v) { return r.y + r.h - std::clamp(v / std::max(1e-9, maxV), 0.0, 1.0) * r.h; };
        cairo_move_to(cr, x0, r.y + r.h);
        for (size_t i = 0; i < h.size(); i++) cairo_line_to(cr, x0 + step * i, yOf(h[i]));
        cairo_line_to(cr, x0 + step * (h.size() - 1), r.y + r.h);
        cairo_close_path(cr);
        src(cr, c, 0.22);
        cairo_fill(cr);
        cairo_move_to(cr, x0, yOf(h[0]));
        for (size_t i = 1; i < h.size(); i++) cairo_line_to(cr, x0 + step * i, yOf(h[i]));
        src(cr, c);
        cairo_set_line_width(cr, 2);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke(cr);
    }

    // A tile of the machine: a big icon over a soft shape (like the hero of the resources popup), the number, and the live graph along the bottom.
    void drawTile(cairo_t* cr, const Rect& r, const char* title, const std::string& big, const std::string& sub, const std::vector<double>& h, double maxV, const char* bg, const char* fg, const char* ic) {
        src(cr, g.color(bg));
        rrect(cr, r.x, r.y, r.w, r.h, 26);
        cairo_fill(cr);
        cairo_save(cr);
        rrect(cr, r.x, r.y, r.w, r.h, 26);
        cairo_clip(cr);
        shapePath(cr, "Cookie9Sided", r.x + r.w - 150, r.y - 70, 190);
        src(cr, g.color(fg), 0.10);
        cairo_fill(cr);
        spark(cr, {r.x, r.y + r.h * 0.52, r.w, r.h * 0.48}, h, maxV, g.color(fg));
        cairo_restore(cr);
        constexpr double pad = 20;  // the same room above, left and right
        ::icon(cr, ic, r.x + r.w - pad - 40, r.y + pad - 2, 40, g.color(fg));
        text(cr, tkT(title), r.x + pad, r.y + 16, 14, true, g.color(fg), 0, 0.9);
        text(cr, big, r.x + pad - 2, r.y + 36, 34, true, g.color(fg));
        if (!sub.empty()) text(cr, sub, r.x + pad, r.y + r.h - 28, 13, false, g.color(fg), r.w - 2 * pad, 0.85);
    }

    void button(cairo_t* cr, const Rect& r, const char* label, const char* ic, bool enabled, bool hot, const char* bg, const char* fg) {
        const Color b = g.color(bg), f = g.color(fg);
        src(cr, enabled ? (hot ? mix(f, b, 0.12) : b) : g.color("surface_container_high"), 1);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        const Color c = enabled ? f : g.color("outline");
        ::icon(cr, ic, r.x + 16, r.y + (r.h - 20) / 2, 20, c);
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        text(cr, label, r.x + 44, r.y + (r.h - th) / 2, 15, true, c);
    }

    void heat(cairo_t* cr, const Rect& r, double level) {
        if (level <= 0.02) return;
        src(cr, g.color("primary"), std::clamp(level, 0.0, 1.0) * 0.38);
        rrect(cr, r.x, r.y + 5, r.w, r.h - 10, 10);
        cairo_fill(cr);
    }

    void cell(cairo_t* cr, const Rect& r, const std::string& s, bool strong, double level) {
        heat(cr, r, level);
        double tw = 0, th = 0;
        measure(s, 15, strong, 0, tw, th);
        text(cr, s, r.x + r.w - 14 - tw, r.y + (r.h - th) / 2, 15, strong, g.color("on_surface"));
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        const double w = r.w, tileW = (w - 3 * kTkGap) / 4;
        if (!loaded) {
            const double toolbar = kTkTileH + kTkGap;
            const double tableTop = toolbar + 44 + kTkGap;
            src(cr, g.color("surface_container"));
            for (int i = 0; i < 4; ++i) {
                rrect(cr, i * (tileW + kTkGap), 0, tileW, kTkTileH, 20);
                cairo_fill(cr);
            }
            rrect(cr, 0, toolbar, w, 44, 22);
            cairo_fill(cr);
            const double tableHeight = std::max(0.0, r.h - tableTop);
            rrect(cr, 0, tableTop, w, tableHeight, 26);
            cairo_fill(cr);
            cairo_save(cr);
            cairo_translate(cr, 0, tableTop);
            drawSampleLoading(cr, {0, 0, w, tableHeight});
            cairo_restore(cr);
            return;
        }
        char b1[64], b2[64];
        std::snprintf(b1, sizeof b1, "%s / %s", tkBytes(memUsedKb * 1024).c_str(), tkBytes(memTotalKb * 1024).c_str());
        std::snprintf(b2, sizeof b2, "%d %s", cores, tkT("cores").c_str());
        for (int i = 0; i < 4; i++) tiles[i] = {i * (tileW + kTkGap), 0, tileW, kTkTileH};
        const auto peak = [](const auto& history) {
            return history.empty() ? 0.0 : *std::max_element(history.begin(), history.end());
        };
        drawTile(cr, tiles[0], "CPU", tkPercent(cpuNow), b2, cpuH, std::max(25.0, peak(cpuH) * 1.15), "primary_container", "on_primary_container", "cpu");
        drawTile(cr, tiles[1], "Memory", tkPercent(memNow), b1, memH, 100, "secondary_container", "on_secondary_container", "server");
        const double dMax = std::max(1024.0 * 1024, peak(diskH));
        drawTile(cr, tiles[2], "Disk", tkRate(diskNow), tkT("Read and write"), diskH, dMax, "tertiary_container", "on_tertiary_container", "ssd-square");
        const double nMax = std::max(1024.0 * 1024, peak(netH));
        drawTile(cr, tiles[3], "Network", tkRate(netNow), "↓ " + tkRate(netDown) + "   ↑ " + tkRate(netUp), netH, nMax, "surface_container_high", "on_surface", netWifi ? "wi-fi-router-minimalistic" : "server-path");

        // toolbar: search and the two actions
        const double ty = kTkTileH + kTkGap, th = 44;
        killBtn = {w - 150, ty, 150, th};
        endBtn = {killBtn.x - kTkGap - 150, ty, 150, th};
        // the count of what is running, at the left; the search takes all the room between it and the buttons
        const std::string count = tkT("Apps") + " " + std::to_string(apps.size()) + "  ·  " + tkT("Processes") + " " + std::to_string(procTotal);
        double cwid = 0, chh = 0;
        measure(count, 14, true, 0, cwid, chh);
        const double chipW = cwid + 40;
        src(cr, g.color("surface_container_high"));
        rrect(cr, 0, ty, chipW, th, th / 2);
        cairo_fill(cr);
        text(cr, count, 20, ty + (th - chh) / 2, 14, true, g.color("on_surface_variant"));
        search = {chipW + kTkGap, ty, endBtn.x - kTkGap - (chipW + kTkGap), th};
        src(cr, g.color("surface_container_high"));
        rrect(cr, search.x, search.y, search.w, search.h, th / 2);
        cairo_fill(cr);
        ::icon(cr, "magnifer", search.x + 16, search.y + (th - 20) / 2, 20, g.color("on_surface_variant"));
        const std::string shown = filter.empty() ? tkT("Type to search apps") : filter;
        double sw = 0, sh = 0;
        measure(shown, 15, false, 0, sw, sh);
        text(cr, shown, search.x + 46, search.y + (th - sh) / 2, 15, false, g.color(filter.empty() ? "outline" : "on_surface"));
        if (!filter.empty()) {
            src(cr, g.color("primary"));
            cairo_rectangle(cr, search.x + 46 + sw + 1, search.y + 12, 2, th - 24);
            cairo_fill(cr);
        }
        const bool sel = haveSel();
        button(cr, endBtn, tkT("End task").c_str(), "stop-circle", sel, hoverBtn == 0, "primary", "on_primary");
        button(cr, killBtn, tkT("Force kill").c_str(), "bomb-minimalistic", sel, hoverBtn == 1, "error_container", "on_error_container");

        // table
        const double top = ty + th + kTkGap;
        table = {0, top, w, r.h - top};
        src(cr, g.color("surface_container"));
        rrect(cr, table.x, table.y, table.w, table.h, 26);
        cairo_fill(cr);
        drawTable(cr);
        if (askSig) drawAsk(cr, r);
    }

    void drawAsk(cairo_t* cr, const Rect& r) {
        src(cr, g.color("scrim"), 0.55);
        cairo_rectangle(cr, 0, 0, r.w, r.h);
        cairo_fill(cr);
        const bool force = askSig == SIGKILL;
        std::string body = tkT(force ? "It closes at once, with no chance to save." : "Unsaved work in it will be lost.");
        if (askCritical) body = tkT("It is part of your desktop: ending it can close your session.");
        const double cw = 440, pad = 26;
        double bw = 0, bh = 0;
        measure(body, 15, false, cw - 2 * pad, bw, bh);
        const double ch = pad + 30 + 10 + 22 + 10 + bh + 10 + (askCount > 1 ? 24 : 0) + 22 + 48 + pad;
        askCard = {(r.w - cw) / 2, (r.h - ch) / 2, cw, ch};
        src(cr, g.color("surface_container_high"));
        rrect(cr, askCard.x, askCard.y, askCard.w, askCard.h, 28);
        cairo_fill(cr);
        double y = askCard.y + pad;
        ::icon(cr, force ? "bomb-minimalistic" : "stop-circle", askCard.x + pad, y + 2, 26, g.color(askCritical || force ? "error" : "primary"));
        text(cr, tkT(force ? "Force kill?" : "End task?"), askCard.x + pad + 38, y, 22, true, g.color("on_surface"));
        y += 30 + 10;
        text(cr, askName, askCard.x + pad, y, 16, true, g.color("on_surface"));
        y += 22 + 10;
        if (askCount > 1) {
            text(cr, tkT("Processes") + ": " + std::to_string(askCount), askCard.x + pad, y - 2, 14, false, g.color("on_surface_variant"));
            y += 24;
        }
        text(cr, body, askCard.x + pad, y, 15, false, g.color(askCritical ? "error" : "on_surface_variant"), cw - 2 * pad);
        const double by = askCard.y + askCard.h - pad - 44;
        askYes = {askCard.x + cw - pad - 150, by, 150, 44};
        askNo = {askYes.x - 10 - 120, by, 120, 44};
        const Color nb = g.color("surface_container_highest"), nf = g.color("on_surface");
        src(cr, askHover == 0 ? mix(nf, nb, 0.12) : nb);
        rrect(cr, askNo.x, askNo.y, askNo.w, askNo.h, 22);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(tkT("Cancel"), 15, true, 0, tw, th);
        text(cr, tkT("Cancel"), askNo.x + (askNo.w - tw) / 2, askNo.y + (44 - th) / 2, 15, true, nf);
        const Color yb = g.color(force || askCritical ? "error" : "primary"), yf = g.color(force || askCritical ? "on_error" : "on_primary");
        src(cr, askHover == 1 ? mix(yf, yb, 0.12) : yb);
        rrect(cr, askYes.x, askYes.y, askYes.w, askYes.h, 22);
        cairo_fill(cr);
        const std::string yes = tkT(force ? "Force kill" : "End task");
        measure(yes, 15, true, 0, tw, th);
        text(cr, yes, askYes.x + (askYes.w - tw) / 2, askYes.y + (44 - th) / 2, 15, true, yf);
    }

    void drawTable(cairo_t* cr) {
        const double nameW = table.w - 3 * 130 - kTkEdge;
        // the headers are rounded buttons with the same room above, left and right (kTkEdge); the numbers' columns end where the last one does
        colRect[0] = {table.x + kTkEdge, table.y + kTkEdge, nameW - kTkEdge, kTkPillH};
        for (int c = 1; c < 4; c++) colRect[c] = {table.x + nameW + (c - 1) * 130 + 4, table.y + kTkEdge, 126, kTkPillH};
        static const char* labels[4] = {"Name", "CPU", "Memory", "Disk"};
        for (int c = 0; c < 4; c++) {
            const bool on = sortCol == c, hot = hoverHead == c;
            const Rect& b = colRect[c];
            const Color base = g.color(on ? "secondary_container" : "surface_container_highest");
            src(cr, hot ? mix(g.color("primary"), base, 0.2) : base);
            rrect(cr, b.x, b.y, b.w, b.h, b.h / 2);
            cairo_fill(cr);
            const std::string label = tkT(labels[c]) + (on ? (sortDesc ? "  ↓" : "  ↑") : "");
            double tw = 0, th = 0;
            measure(label, 13, true, 0, tw, th);
            const Color col = g.color(on ? "on_secondary_container" : hot ? "on_surface" : "on_surface_variant");
            text(cr, label, c == 0 ? b.x + 38 : b.x + b.w - 14 - tw, b.y + (b.h - th) / 2, 13, true, col);
        }
        const Rect body = {table.x, table.y + kTkHeadH, table.w, table.h - kTkHeadH};
        std::vector<int> order;
        for (size_t i = 0; i < apps.size(); i++) if (matches(apps[i])) order.push_back(static_cast<int>(i));
        double total = 0;
        for (int i : order) total += kTkRowH + (open.count(apps[i].key) ? kTkChildH * apps[i].procs.size() : 0);
        scrollY.max = std::max(0.0, total + 8 - body.h);
        scrollY.fit();
        cairo_save(cr);
        rrect(cr, body.x, body.y, body.w, body.h, 22);
        cairo_clip(cr);
        double y = body.y - scrollY.off();
        for (int i : order) {
            const TkApp& a = apps[i];
            const bool isOpen = open.count(a.key) > 0;
            const Rect row = {body.x + 8, y, body.w - 16, kTkRowH};
            if (y + kTkRowH > body.y && y < body.y + body.h) drawAppRow(cr, a, i, row, isOpen);
            hits.push_back({row, i, 0, false});
            y += kTkRowH;
            if (!isOpen) continue;
            for (const TkProc& p : a.procs) {
                const Rect cr2 = {body.x + 8, y, body.w - 16, kTkChildH};
                if (y + kTkChildH > body.y && y < body.y + body.h) drawProcRow(cr, a, p, i, cr2);
                hits.push_back({cr2, i, p.pid, false});
                y += kTkChildH;
            }
        }
        cairo_restore(cr);
        if (order.empty()) {
            const std::string s = tkT("No apps match");
            double tw = 0, th = 0;
            measure(s, 16, false, 0, tw, th);
            text(cr, s, table.x + (table.w - tw) / 2, body.y + 40, 16, false, g.color("outline"));
        }
        // the thumb of the scroll
        if (scrollY.max > 0 && scrollY.thumbAlpha() > 0) {
            const double frac = body.h / (body.h + scrollY.max), th = std::max(36.0, body.h * frac);
            const double ty = body.y + (body.h - th) * (scrollY.off() / scrollY.max);
            src(cr, g.color("on_surface_variant"), 0.5 * scrollY.thumbAlpha());
            rrect(cr, table.x + table.w - 9, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    void drawAppRow(cairo_t* cr, const TkApp& a, int index, const Rect& row, bool isOpen) {
        const bool selected = a.key == selKey && selPid == 0, hot = hover == index && !selected;
        if (selected || hot) {
            src(cr, selected ? g.color("secondary_container") : g.color("surface_container_high"));
            rrect(cr, row.x, row.y, row.w, row.h, 16);
            cairo_fill(cr);
        }
        const double nameW = table.w - 3 * 130 - kTkEdge;
        if (a.procs.size() > 1) ::icon(cr, isOpen ? "alt-arrow-down" : "alt-arrow-right", row.x + 8, row.y + (row.h - 18) / 2, 18, g.color("on_surface_variant"));
        const double ix = row.x + 36, iy = row.y + (row.h - 28) / 2;
        if (cairo_surface_t* ic = iconFor(a)) {
            cairo_save(cr);
            cairo_translate(cr, ix, iy);
            cairo_scale(cr, 28.0 / cairo_image_surface_get_width(ic), 28.0 / cairo_image_surface_get_height(ic));
            cairo_set_source_surface(cr, ic, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            src(cr, g.color("primary_container"));
            cairo_arc(cr, ix + 14, iy + 14, 14, 0, 2 * M_PI);
            cairo_fill(cr);
            const std::string letter = a.name.empty() ? "?" : a.name.substr(0, 1);
            double tw = 0, th = 0;
            measure(letter, 15, true, 0, tw, th);
            text(cr, letter, ix + 14 - tw / 2, iy + 14 - th / 2, 15, true, g.color("on_primary_container"));
        }
        std::string title = a.name;
        if (a.procs.size() > 1) title += "  (" + std::to_string(a.procs.size()) + ")";
        double tw = 0, th = 0;
        measure(title, 15, true, 0, tw, th);
        cairo_save(cr);
        cairo_rectangle(cr, ix + 40, row.y, nameW - 100, row.h);
        cairo_clip(cr);
        text(cr, title, ix + 40, row.y + (row.h - th) / 2, 15, true, g.color("on_surface"));
        cairo_restore(cr);
        const Rect c1 = {table.x + nameW, row.y, 130, row.h}, c2 = {c1.x + 130, row.y, 130, row.h}, c3 = {c2.x + 130, row.y, 130, row.h};
        cell(cr, c1, tkPercent(a.cpu), a.cpu >= 10, a.cpu / 40);
        cell(cr, c2, tkBytes(a.rssKb * 1024.0), a.rssKb > 1024 * 1024, memTotalKb > 0 ? a.rssKb / memTotalKb * 6 : 0);
        cell(cr, c3, a.disk >= 1024 ? tkRate(a.disk) : "—", false, a.disk / (20.0 * 1024 * 1024));
    }

    void drawProcRow(cairo_t* cr, const TkApp& a, const TkProc& p, int index, const Rect& row) {
        const bool selected = a.key == selKey && selPid == p.pid, hot = hover == index && !selected && false;
        (void)hot;
        if (selected) {
            src(cr, g.color("secondary_container"));
            rrect(cr, row.x, row.y, row.w, row.h, 14);
            cairo_fill(cr);
        }
        const double nameW = table.w - 3 * 130 - kTkEdge;
        const std::string title = p.comm + "   " + std::to_string(p.pid);
        double tw = 0, th = 0;
        measure(title, 14, false, 0, tw, th);
        text(cr, title, row.x + 84, row.y + (row.h - th) / 2, 14, false, g.color("on_surface_variant"));
        const Rect c1 = {table.x + nameW, row.y, 130, row.h}, c2 = {c1.x + 130, row.y, 130, row.h}, c3 = {c2.x + 130, row.y, 130, row.h};
        cell(cr, c1, tkPercent(p.cpu), false, p.cpu / 40);
        cell(cr, c2, tkBytes(p.rssKb * 1024.0), false, memTotalKb > 0 ? p.rssKb / memTotalKb * 6 : 0);
        cell(cr, c3, p.disk >= 1024 ? tkRate(p.disk) : "—", false, p.disk / (20.0 * 1024 * 1024));
    }

    // ---- input ----
    bool motion(double x, double y) override {
        if (askSig) {
            const int h = askYes.contains(x, y) ? 1 : askNo.contains(x, y) ? 0 : -1;
            const bool changed = h != askHover;
            askHover = h;
            return changed;
        }
        int h = -1, b = -1, hh = -1;
        if (endBtn.contains(x, y)) b = 0;
        else if (killBtn.contains(x, y)) b = 1;
        else if (y >= table.y && y < table.y + kTkHeadH && table.contains(x, y)) {
            for (int c = 0; c < 4; c++) if (colRect[c].contains(x, y)) hh = c;
        } else if (y >= table.y + kTkHeadH)
            for (const Hit& hit : hits) if (hit.pid == 0 && hit.r.contains(x, y)) h = hit.app;
        const bool changed = h != hover || b != hoverBtn || hh != hoverHead;
        hover = h; hoverBtn = b; hoverHead = hh;
        return changed;
    }
    bool leave() override {
        const bool changed = hover >= 0 || hoverBtn >= 0 || hoverHead >= 0;
        hover = hoverBtn = hoverHead = -1;
        return changed;
    }
    Cursor cursor(double x, double y) const override {
        if (askSig) return askYes.contains(x, y) || askNo.contains(x, y) ? Cursor::Pointer : Cursor::Default;
        if (endBtn.contains(x, y) || killBtn.contains(x, y) || (y >= table.y && y < table.y + kTkHeadH) || (y >= table.y + kTkHeadH && table.contains(x, y))) return Cursor::Pointer;
        if (search.contains(x, y)) return Cursor::Text;
        return Cursor::Default;
    }
    bool click(double x, double y, bool press) override {
        if (askSig) {
            if (!press) return false;
            if (askYes.contains(x, y)) endSelected(askSig);
            if (askYes.contains(x, y) || askNo.contains(x, y) || !askCard.contains(x, y)) askSig = 0;
            return true;
        }
        if (!press) return false;
        if (endBtn.contains(x, y)) { if (haveSel()) ask(SIGTERM); return true; }
        if (killBtn.contains(x, y)) { if (haveSel()) ask(SIGKILL); return true; }
        if (y >= table.y && y < table.y + kTkHeadH && table.contains(x, y)) {
            for (int c = 0; c < 4; c++) {
                if (!colRect[c].contains(x, y)) continue;
                if (sortCol == c) sortDesc = !sortDesc;
                else { sortCol = c; sortDesc = c != 0; }
                sortApps();
            }
            return true;
        }
        for (const Hit& hit : hits) {
            if (!hit.r.contains(x, y) || y < table.y + kTkHeadH) continue;
            const TkApp& a = apps[hit.app];
            const bool onChevron = hit.pid == 0 && x < hit.r.x + 34 && a.procs.size() > 1;
            if (onChevron) { if (!open.erase(a.key)) open.insert(a.key); return true; }
            selKey = a.key;
            selPid = hit.pid;
            const auto t = Clock::now();
            if (hit.pid == 0 && a.procs.size() > 1 && t - pressAt < std::chrono::milliseconds(350)) { if (!open.erase(a.key)) open.insert(a.key); }
            pressAt = t;
            return true;
        }
        return false;
    }
    bool scroll(double x, double y, double dy) override {
        if (askSig) return false;
        if (!table.contains(x, y)) return false;
        scrollY.by(dy * 3);
        return true;
    }
    bool key(const KeyEvent& k) override {
        if (askSig) {
            if (k.sym == XKB_KEY_Escape) askSig = 0;
            else if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) { endSelected(askSig); askSig = 0; }
            return true;  // (the dialog takes every key)
        }
        if (k.sym == XKB_KEY_Escape) { if (filter.empty()) return false; filter.clear(); return true; }
        if (k.sym == XKB_KEY_BackSpace) {
            if (filter.empty()) return true;
            size_t i = filter.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(filter[i]) & 0xC0) == 0x80) i--;
            filter.erase(i);
            return true;
        }
        if (k.sym == XKB_KEY_Delete) { if (haveSel()) ask(k.shift ? SIGKILL : SIGTERM); return true; }
        if (k.sym == XKB_KEY_Down || k.sym == XKB_KEY_Up) { moveSel(k.sym == XKB_KEY_Down ? 1 : -1); return true; }
        if (!k.text.empty() && !k.ctrl && !k.alt && !k.logo) { filter += k.text; scrollY.reset(); return true; }
        return false;
    }
    bool grabsKeys() const override { return askSig != 0 || !filter.empty(); }  // (digits and arrows type or move while a search is on)

    void moveSel(int d) {
        std::vector<std::pair<std::string, int>> rowsList;
        for (const Hit& h : hits) rowsList.push_back({apps[h.app].key, h.pid});
        if (rowsList.empty()) return;
        int at = -1;
        for (size_t i = 0; i < rowsList.size(); i++) if (rowsList[i].first == selKey && rowsList[i].second == selPid) at = static_cast<int>(i);
        const int n = std::clamp(at + d, 0, static_cast<int>(rowsList.size()) - 1);
        selKey = rowsList[n].first;
        selPid = rowsList[n].second;
    }
};

// The second tab of the Task Manager window: CPU, memory, disks, network and GPUs. It reads only while it is the tab in view.
struct PerfPage : ToolPage {
    TkPerf perf;
    std::future<TkPerfSample> pending;
    const char* name() const override { return "Performance"; }
    const char* icon() const override { return "graph-up"; }
    void init() override { startSample(); }
    void startSample() {
        if (pending.valid()) return;
        pending = sampleInBackground([snapshot = static_cast<const TkPerfSample&>(perf)]() mutable {
            if (snapshot.last == Clock::time_point{}) snapshot.init();
            snapshot.sample();
            return std::move(snapshot);
        });
    }
    void draw(cairo_t* cr, Rect r) override {
        if (perf.last == Clock::time_point{}) { drawSampleLoading(cr, r); return; }
        perf.draw(cr, {0, 0, r.w, r.h});
    }
    bool tick() override {
        bool moved = perf.listScroll.step();
        if (sampleReady(pending)) {
            static_cast<TkPerfSample&>(perf) = pending.get();
            perf.buildList();
            moved = true;
        }
        if (perf.last == Clock::time_point{} || Clock::now() - perf.last >= std::chrono::milliseconds(static_cast<int>(kTkSampleSec * 1000)))
            startSample();
        return moved;
    }
    bool animating() const override { return perf.listScroll.busy(); }
    bool motion(double x, double y) override { return perf.motion(x, y); }
    bool leave() override { perf.hover = -1; perf.copyHot = false; return true; }
    bool click(double x, double y, bool press) override { return press && perf.click(x, y); }
    bool scroll(double x, double y, double dy) override { return perf.scroll(x, y, dy); }
    Cursor cursor(double x, double y) const override { return perf.listArea.contains(x, y) || perf.copyBtn.contains(x, y) ? Cursor::Pointer : Cursor::Default; }
    bool key(const KeyEvent& k) override {
        if (perf.list.empty()) return false;
        if (k.sym == XKB_KEY_Down) { perf.move(1); return true; }
        if (k.sym == XKB_KEY_Up) { perf.move(-1); return true; }
        return false;
    }
};
