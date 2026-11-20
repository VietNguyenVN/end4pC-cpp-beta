// Task Manager > Performance: CPU (with every core), memory, each disk, each network interface and each GPU, with live graphs.
// All from /proc and /sys, so it works on any machine; the GPUs are read the way each maker exposes them (see TkPerf::sampleGpu):
//   AMD (amdgpu): /sys/class/drm/cardN/device/gpu_busy_percent, mem_info_vram_*, hwmon (temperature, power, clock)
//   NVIDIA: `nvidia-smi --query-gpu=...` (the proprietary driver has no sysfs numbers; the GPU is not woken if it is asleep)
//   Intel (i915): the clock (gt_act_freq_mhz) and the time the GPU is not in RC6 (the standby state) as its use
// A GPU that is powered off (runtime suspend, usual in laptops with two GPUs) is not read, so the page never wakes it.
// Included by src/tools/page_tasks.inc (inside the anonymous namespace of src/tools.cpp).

struct TkSeries {
    std::vector<double> v;
    void push(double x) {
        v.push_back(x);
        if (static_cast<int>(v.size()) > kTkHistory) v.erase(v.begin());
    }
    double last() const { return v.empty() ? 0 : v.back(); }
    double peak() const { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
};

struct TkCore { TkSeries use; double pct = 0, mhz = -1; unsigned long long lastTotal = 0, lastIdle = 0; };
struct TkDisk {
    std::string name, model;
    double capacity = 0, readBps = 0, writeBps = 0, busyPct = 0;
    bool rotational = false;
    TkSeries read, write, busy;
    unsigned long long lastR = 0, lastW = 0, lastIo = 0;
    bool primed = false;
};
struct TkNet {
    std::string name, ip;
    bool wifi = false;
    double speedMbps = -1, rx = 0, tx = 0;
    TkSeries down, up;
    unsigned long long lastRx = 0, lastTx = 0;
    bool primed = false;
};
struct TkGpu {
    std::string card, slot, name, driver, note;
    int kind = 3;  // 0 AMD, 1 NVIDIA, 2 Intel, 3 other
    int nvIndex = -1;
    double busy = -1, vramUsed = -1, vramTotal = -1, temp = -1, power = -1, mhz = -1, mhzMax = -1;
    bool asleep = false, shared = false;
    TkSeries use, mem;
    unsigned long long lastRc6 = 0;
    bool rc6Primed = false;
};

std::string tkTrim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
    return s.substr(a);
}
double tkNumber(const std::string& path) {  // a number from a file, or -1
    const std::string t = tkTrim(tkRead(path, 64));
    if (t.empty() || !(std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-')) return -1;
    return std::atof(t.c_str());
}
std::string tkFreq(double mhz) {
    char b[24];
    if (mhz >= 1000) std::snprintf(b, sizeof b, "%.2f GHz", mhz / 1000);
    else std::snprintf(b, sizeof b, "%.0f MHz", mhz);
    return b;
}
std::string tkDuration(double sec) {
    const long s = static_cast<long>(sec), d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
    char b[48];
    if (d > 0) std::snprintf(b, sizeof b, "%ldd %ldh %ldm", d, h, m);
    else std::snprintf(b, sizeof b, "%ldh %ldm %lds", h, m, s % 60);
    return b;
}
std::string tkSpeedBits(double bytesPerSec) {
    char b[32];
    const double bits = bytesPerSec * 8;
    if (bits >= 1e9) std::snprintf(b, sizeof b, "%.1f Gbps", bits / 1e9);
    else if (bits >= 1e6) std::snprintf(b, sizeof b, "%.1f Mbps", bits / 1e6);
    else std::snprintf(b, sizeof b, "%.0f Kbps", bits / 1e3);
    return b;
}

// A chart: the series one over the other, grid and a label of the top value. `maxV` is the value of the top line.
void tkChart(cairo_t* cr, const Rect& r, const std::vector<const TkSeries*>& series, const std::vector<Color>& colors, double maxV, const std::string& topLabel, const std::string& timeLabel) {
    src(cr, g.color("surface_container"));
    rrect(cr, r.x, r.y, r.w, r.h, 22);
    cairo_fill(cr);
    cairo_save(cr);
    rrect(cr, r.x, r.y, r.w, r.h, 22);
    cairo_clip(cr);
    const Rect in = {r.x + 14, r.y + 30, r.w - 28, r.h - 54};
    src(cr, g.color("outline_variant"), 0.5);
    cairo_set_line_width(cr, 1);
    for (int i = 0; i <= 4; i++) {
        const double y = in.y + in.h * i / 4.0;
        cairo_move_to(cr, in.x, y + 0.5);
        cairo_line_to(cr, in.x + in.w, y + 0.5);
    }
    for (int i = 0; i <= 10; i++) {
        const double x = in.x + in.w * i / 10.0;
        cairo_move_to(cr, x + 0.5, in.y);
        cairo_line_to(cr, x + 0.5, in.y + in.h);
    }
    cairo_stroke(cr);
    const double step = in.w / (kTkHistory - 1);
    for (size_t s = 0; s < series.size(); s++) {
        const std::vector<double>& h = series[s]->v;
        if (h.size() < 2) continue;
        const double x0 = in.x + in.w - step * (h.size() - 1);
        auto yOf = [&](double v) { return in.y + in.h - std::clamp(v / std::max(1e-9, maxV), 0.0, 1.0) * in.h; };
        cairo_move_to(cr, x0, in.y + in.h);
        for (size_t i = 0; i < h.size(); i++) cairo_line_to(cr, x0 + step * i, yOf(h[i]));
        cairo_line_to(cr, x0 + step * (h.size() - 1), in.y + in.h);
        cairo_close_path(cr);
        src(cr, colors[s], 0.18);
        cairo_fill(cr);
        cairo_move_to(cr, x0, yOf(h[0]));
        for (size_t i = 1; i < h.size(); i++) cairo_line_to(cr, x0 + step * i, yOf(h[i]));
        src(cr, colors[s]);
        cairo_set_line_width(cr, 2);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    text(cr, timeLabel, r.x + 16, r.y + r.h - 22, 12, false, g.color("on_surface_variant"));
    double tw = 0, th = 0;
    measure(topLabel, 12, false, 0, tw, th);
    text(cr, topLabel, r.x + r.w - 16 - tw, r.y + 10, 12, false, g.color("on_surface_variant"));
}

// A text that fits in maxW on one line: the end is cut and marked with "…" when it is too long.
std::string tkFit(const std::string& s, double px, bool bold, double maxW) {
    double w = 0, h = 0;
    measure(s, px, bold, 0, w, h);
    if (w <= maxW) return s;
    std::string cut = s;
    while (!cut.empty()) {  // (by characters, not bytes)
        do cut.pop_back(); while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80);
        measure(cut + "…", px, bold, 0, w, h);
        if (w <= maxW) return cut + "…";
    }
    return "…";
}

// The numbers as boxes that fill the width and the height they are given: no empty room on the right.
void tkStatGrid(cairo_t* cr, const Rect& a, const std::vector<std::pair<std::string, std::string>>& st, int cols, std::vector<Rect>* boxes = nullptr) {
    if (st.empty()) return;
    constexpr double gap = 8;
    if (boxes) boxes->clear();
    cols = std::max(1, std::min<int>(cols, static_cast<int>(st.size())));
    const int rows = (static_cast<int>(st.size()) + cols - 1) / cols;
    const double ch = (a.h - (rows - 1) * gap) / rows;
    for (size_t i = 0; i < st.size(); i++) {
        const int c = static_cast<int>(i) % cols, rr = static_cast<int>(i) / cols;
        const int inRow = rr == rows - 1 ? static_cast<int>(st.size()) - rr * cols : cols;  // (the last row shares its width among the boxes it has)
        const double w = (a.w - (inRow - 1) * gap) / inRow;
        const Rect b = {a.x + c * (w + gap), a.y + rr * (ch + gap), w, ch};
        if (boxes) boxes->push_back(b);
        src(cr, g.color("surface_container"));
        rrect(cr, b.x, b.y, b.w, b.h, 20);
        cairo_fill(cr);
        text(cr, st[i].first, b.x + 16, b.y + 12, 13, false, g.color("on_surface_variant"));
        double px = std::clamp(b.h * 0.34, 18.0, 28.0), tw = 0, th = 0;
        for (; px > 15; px -= 1) {  // a long value gets a smaller type before it gets cut
            measure(st[i].second, px, true, 0, tw, th);
            if (tw <= b.w - 28) break;
        }
        const std::string val = tkFit(st[i].second, px, true, b.w - 28);
        measure(val, px, true, 0, tw, th);
        text(cr, val, b.x + 16, b.y + b.h - 14 - th, px, true, g.color("on_surface"));
    }
}

struct TkPerf {
    // ---- the machine ----
    std::string cpuModel;
    int cores = 1;
    std::vector<TkCore> core;
    TkSeries cpuUse, memUse, swapUse;
    double cpuPct = 0, cpuMhz = -1, cpuMhzMax = -1, cpuTemp = -1, uptime = 0;
    int procCount = 0, threadCount = 0;
    unsigned long long lastTotal = 0, lastIdle = 0;
    double memTotal = 0, memAvail = 0, memFree = 0, memCached = 0, memBuffers = 0, swapTotal = 0, swapFree = 0;
    std::string cpuTempPath;
    std::vector<TkDisk> disks;
    std::vector<TkNet> nets;
    std::vector<TkGpu> gpus;
    bool nvidiaMissing = false;
    int tickCount = 0;
    Clock::time_point last{};

    // ---- the list on the left ----
    enum Kind { Cpu, Mem, Disk, Net, Gpu };
    struct Res { Kind kind; int index; };
    std::vector<Res> list;
    int sel = 0, hover = -1;
    struct ListHit { Rect r; int index; };
    std::vector<ListHit> hits;
    Elastic listScroll;
    Rect listArea;

    void init() {
        cores = std::max<long>(1, sysconf(_SC_NPROCESSORS_ONLN));
        core.resize(cores);
        std::istringstream ci(tkRead("/proc/cpuinfo", 16384));
        for (std::string line; std::getline(ci, line);)
            if (line.rfind("model name", 0) == 0) { cpuModel = tkTrim(line.substr(line.find(':') + 1)); break; }
        cpuMhzMax = tkNumber("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq") / 1000;
        for (int i = 0; i < 40; i++) {  // the sensor of the processor's temperature
            const std::string base = "/sys/class/hwmon/hwmon" + std::to_string(i);
            const std::string n = tkTrim(tkRead(base + "/name", 64));
            if (n == "coretemp" || n == "k10temp" || n == "zenpower" || n == "cpu_thermal" || n == "zenpower3") { cpuTempPath = base; break; }
        }
        findGpus();
        refreshDevices();
    }

    // ---- sampling ----
    void sample() {
        const auto t = Clock::now();
        const double dt = last == Clock::time_point{} ? 1.0 : std::max(0.2, std::chrono::duration<double>(t - last).count());
        last = t;
        tickCount++;
        sampleCpu();
        sampleMemory();
        if (tickCount % 5 == 1) refreshDevices();
        sampleDisks(dt);
        sampleNets(dt);
        sampleGpus(dt);
        buildList();
    }

    void sampleCpu() {
        std::istringstream in(tkRead("/proc/stat", 16384));
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("cpu", 0) != 0) break;
            std::istringstream ls(line);
            std::string tag;
            unsigned long long v[8] = {};
            ls >> tag;
            for (auto& x : v) ls >> x;
            unsigned long long total = 0;
            for (auto x : v) total += x;
            const unsigned long long idle = v[3] + v[4];
            if (tag == "cpu") {
                if (lastTotal && total > lastTotal) cpuPct = 100.0 * (1.0 - double(idle - lastIdle) / double(total - lastTotal));
                lastTotal = total;
                lastIdle = idle;
                continue;
            }
            const int i = std::atoi(tag.c_str() + 3);
            if (i < 0 || i >= cores) continue;
            TkCore& c = core[i];
            if (c.lastTotal && total > c.lastTotal) c.pct = 100.0 * (1.0 - double(idle - c.lastIdle) / double(total - c.lastTotal));
            c.lastTotal = total;
            c.lastIdle = idle;
            c.use.push(c.pct);
        }
        cpuUse.push(cpuPct);
        double sum = 0;
        int n = 0;
        for (int i = 0; i < cores; i++) {
            const double khz = tkNumber("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq");
            core[i].mhz = khz > 0 ? khz / 1000 : -1;
            if (khz > 0) { sum += khz / 1000; n++; }
        }
        cpuMhz = n ? sum / n : -1;
        cpuTemp = -1;
        if (!cpuTempPath.empty()) {
            const double m = tkNumber(cpuTempPath + "/temp1_input");
            if (m > 0) cpuTemp = m / 1000;
        }
        uptime = std::atof(tkRead("/proc/uptime", 64).c_str());
        const std::string la = tkRead("/proc/loadavg", 128);
        const size_t slash = la.find('/');
        threadCount = slash == std::string::npos ? 0 : std::atoi(la.c_str() + slash + 1);
        int pc = 0;
        if (DIR* d = opendir("/proc")) {
            while (const dirent* e = readdir(d)) if (e->d_name[0] >= '1' && e->d_name[0] <= '9') pc++;
            closedir(d);
        }
        procCount = pc;
    }

    void sampleMemory() {
        std::istringstream in(tkRead("/proc/meminfo", 4096));
        std::string k, unit;
        double v = 0;
        while (in >> k >> v >> unit) {
            v *= 1024;
            if (k == "MemTotal:") memTotal = v;
            else if (k == "MemAvailable:") memAvail = v;
            else if (k == "MemFree:") memFree = v;
            else if (k == "Cached:") memCached = v;
            else if (k == "Buffers:") memBuffers = v;
            else if (k == "SwapTotal:") swapTotal = v;
            else if (k == "SwapFree:") swapFree = v;
        }
        memUse.push(memTotal > 0 ? 100.0 * (memTotal - memAvail) / memTotal : 0);
        swapUse.push(swapTotal > 0 ? 100.0 * (swapTotal - swapFree) / swapTotal : 0);
    }

    // Disks and interfaces come and go (a USB stick, a VPN): looked at again every few seconds.
    void refreshDevices() {
        std::vector<std::string> names;
        if (DIR* d = opendir("/sys/block")) {
            while (const dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n[0] == '.' || n.rfind("loop", 0) == 0 || n.rfind("ram", 0) == 0 || n.rfind("zram", 0) == 0 || n.rfind("dm-", 0) == 0 || n.rfind("md", 0) == 0 || n.rfind("sr", 0) == 0) continue;
                if (tkNumber("/sys/block/" + n + "/size") <= 0) continue;
                names.push_back(n);
            }
            closedir(d);
        }
        std::sort(names.begin(), names.end());
        std::vector<TkDisk> nd;
        for (const std::string& n : names) {
            auto old = std::find_if(disks.begin(), disks.end(), [&](const TkDisk& x) { return x.name == n; });
            TkDisk dk = old != disks.end() ? *old : TkDisk{};
            dk.name = n;
            dk.model = tkTrim(tkRead("/sys/block/" + n + "/device/model", 128));
            dk.capacity = tkNumber("/sys/block/" + n + "/size") * 512;
            dk.rotational = tkNumber("/sys/block/" + n + "/queue/rotational") > 0;
            nd.push_back(std::move(dk));
        }
        disks = std::move(nd);

        std::vector<std::string> ifs;
        if (DIR* d = opendir("/sys/class/net")) {
            while (const dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n[0] == '.' || n == "lo" || n.rfind("veth", 0) == 0 || n.rfind("docker", 0) == 0 || n.rfind("br-", 0) == 0 || n.rfind("virbr", 0) == 0) continue;
                const std::string st = tkTrim(tkRead("/sys/class/net/" + n + "/operstate", 32));
                if (st == "down" || st == "notpresent") continue;
                ifs.push_back(n);
            }
            closedir(d);
        }
        std::sort(ifs.begin(), ifs.end());
        std::map<std::string, std::string> ips;
        if (ifaddrs* ifa = nullptr; getifaddrs(&ifa) == 0) {
            for (ifaddrs* p = ifa; p; p = p->ifa_next) {
                if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr, buf, sizeof buf);
                ips[p->ifa_name] = buf;
            }
            freeifaddrs(ifa);
        }
        std::vector<TkNet> nn;
        for (const std::string& n : ifs) {
            auto old = std::find_if(nets.begin(), nets.end(), [&](const TkNet& x) { return x.name == n; });
            TkNet nt = old != nets.end() ? *old : TkNet{};
            nt.name = n;
            nt.wifi = access(("/sys/class/net/" + n + "/wireless").c_str(), F_OK) == 0;
            nt.speedMbps = nt.wifi ? -1 : tkNumber("/sys/class/net/" + n + "/speed");
            nt.ip = ips.count(n) ? ips[n] : "";
            nn.push_back(std::move(nt));
        }
        nets = std::move(nn);
    }

    void sampleDisks(double dt) {
        std::istringstream in(tkRead("/proc/diskstats", 65536));
        for (std::string line; std::getline(in, line);) {
            std::istringstream ls(line);
            std::string maj, min, nm;
            unsigned long long f[11] = {};
            ls >> maj >> min >> nm;
            for (auto& x : f) ls >> x;
            auto it = std::find_if(disks.begin(), disks.end(), [&](const TkDisk& x) { return x.name == nm; });
            if (it == disks.end()) continue;
            const unsigned long long rb = f[2] * 512, wb = f[6] * 512, io = f[9];
            if (it->primed) {
                it->readBps = rb >= it->lastR ? double(rb - it->lastR) / dt : 0;
                it->writeBps = wb >= it->lastW ? double(wb - it->lastW) / dt : 0;
                it->busyPct = io >= it->lastIo ? std::min(100.0, double(io - it->lastIo) / (dt * 10.0)) : 0;
            }
            it->primed = true;
            it->lastR = rb; it->lastW = wb; it->lastIo = io;
            it->read.push(it->readBps);
            it->write.push(it->writeBps);
            it->busy.push(it->busyPct);
        }
    }

    void sampleNets(double dt) {
        std::istringstream in(tkRead("/proc/net/dev", 16384));
        for (std::string line; std::getline(in, line);) {
            const size_t c = line.find(':');
            if (c == std::string::npos) continue;
            std::string nm = line.substr(0, c);
            nm.erase(0, nm.find_first_not_of(' '));
            auto it = std::find_if(nets.begin(), nets.end(), [&](const TkNet& x) { return x.name == nm; });
            if (it == nets.end()) continue;
            std::istringstream ls(line.substr(c + 1));
            unsigned long long f[16] = {};
            for (auto& x : f) ls >> x;
            if (it->primed) {
                it->rx = f[0] >= it->lastRx ? double(f[0] - it->lastRx) / dt : 0;
                it->tx = f[8] >= it->lastTx ? double(f[8] - it->lastTx) / dt : 0;
            }
            it->primed = true;
            it->lastRx = f[0];
            it->lastTx = f[8];
            it->down.push(it->rx);
            it->up.push(it->tx);
        }
    }

    // ---- GPUs ----
    static std::string pciName(const std::string& slot, int kind) {
        FILE* p = popen(("lspci -mm -s " + slot + " 2>/dev/null").c_str(), "r");
        if (!p) return {};
        char buf[1024] = {};
        const char* got = fgets(buf, sizeof buf, p);
        pclose(p);
        if (!got) return {};
        std::vector<std::string> q;  // the quoted fields: class, vendor, device
        const std::string line = buf;
        for (size_t i = 0; i < line.size();) {
            if (line[i] != '"') { i++; continue; }
            const size_t e = line.find('"', i + 1);
            if (e == std::string::npos) break;
            q.push_back(line.substr(i + 1, e - i - 1));
            i = e + 1;
        }
        if (q.size() < 3) return {};
        std::string dev = q[2];
        const size_t a = dev.rfind('['), b = dev.rfind(']');
        if (a != std::string::npos && b != std::string::npos && b > a) dev = dev.substr(a + 1, b - a - 1);
        const char* vendor = kind == 0 ? "AMD " : kind == 1 ? "NVIDIA " : kind == 2 ? "Intel " : "";
        if (std::string(vendor) != "" && dev.rfind(std::string(vendor).substr(0, 3), 0) == 0) return dev;
        return vendor + dev;
    }

    void findGpus() {
        std::vector<std::string> cards;
        if (DIR* d = opendir("/sys/class/drm")) {
            while (const dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos) continue;
                if (access(("/sys/class/drm/" + n + "/device/vendor").c_str(), R_OK) != 0) continue;
                cards.push_back(n);
            }
            closedir(d);
        }
        std::sort(cards.begin(), cards.end());
        int nv = 0;
        for (const std::string& c : cards) {
            TkGpu gpu;
            gpu.card = c;
            const std::string dev = "/sys/class/drm/" + c + "/device";
            char real[4096];
            if (realpath(dev.c_str(), real)) gpu.slot = tkBase(real);
            const std::string vendor = tkTrim(tkRead(dev + "/vendor", 16));
            gpu.kind = vendor == "0x1002" ? 0 : vendor == "0x10de" ? 1 : vendor == "0x8086" ? 2 : 3;
            char drv[512];
            const ssize_t n = readlink((dev + "/driver").c_str(), drv, sizeof drv - 1);
            if (n > 0) gpu.driver = tkBase(std::string(drv, static_cast<size_t>(n)));
            gpu.name = pciName(gpu.slot, gpu.kind);
            if (gpu.name.empty()) gpu.name = gpu.kind == 0 ? "AMD GPU" : gpu.kind == 1 ? "NVIDIA GPU" : gpu.kind == 2 ? "Intel GPU" : "GPU";
            if (gpu.kind == 1) gpu.nvIndex = nv++;
            gpus.push_back(std::move(gpu));
        }
    }

    static std::string hwmonOf(const std::string& dev) {
        if (DIR* d = opendir((dev + "/hwmon").c_str())) {
            std::string out;
            while (const dirent* e = readdir(d)) if (std::string(e->d_name).rfind("hwmon", 0) == 0) { out = dev + "/hwmon/" + e->d_name; break; }
            closedir(d);
            return out;
        }
        return {};
    }

    void sampleGpus(double dt) {
        bool needSmi = false;
        for (TkGpu& g2 : gpus) {
            const std::string dev = "/sys/class/drm/" + g2.card + "/device";
            g2.asleep = tkTrim(tkRead(dev + "/power/runtime_status", 32)) == "suspended";
            if (g2.asleep) { g2.busy = 0; g2.use.push(0); g2.mem.push(g2.mem.last()); continue; }
            if (g2.kind == 0) sampleAmd(g2, dev);
            else if (g2.kind == 1) needSmi = true;
            else if (g2.kind == 2) sampleIntel(g2, dt);
            if (g2.busy >= 0) g2.use.push(g2.busy);
            else g2.use.push(0);
            if (g2.vramUsed >= 0 && g2.vramTotal > 0) g2.mem.push(100.0 * g2.vramUsed / g2.vramTotal);
        }
        if (needSmi && tickCount % 2 == 0) sampleNvidia();
    }

    static void sampleAmd(TkGpu& g2, const std::string& dev) {
        g2.busy = tkNumber(dev + "/gpu_busy_percent");
        double used = tkNumber(dev + "/mem_info_vram_used"), total = tkNumber(dev + "/mem_info_vram_total");
        g2.shared = false;
        if (total > 0 && total <= 512.0 * 1024 * 1024) {  // an APU: the video memory is a slice of the RAM, the games live in GTT
            const double gu = tkNumber(dev + "/mem_info_gtt_used"), gt = tkNumber(dev + "/mem_info_gtt_total");
            if (gt > 0) { used = gu; total = gt; g2.shared = true; }
        }
        g2.vramUsed = used;
        g2.vramTotal = total;
        const std::string hw = hwmonOf(dev);
        if (!hw.empty()) {
            const double t = tkNumber(hw + "/temp1_input");
            g2.temp = t > 0 ? t / 1000 : -1;
            double pw = tkNumber(hw + "/power1_average");
            if (pw < 0) pw = tkNumber(hw + "/power1_input");
            g2.power = pw >= 0 ? pw / 1e6 : -1;
            const double f = tkNumber(hw + "/freq1_input");
            if (f > 0) g2.mhz = f / 1e6;
        }
        if (g2.mhz < 0) {  // the active line of pp_dpm_sclk ("1: 1500Mhz *")
            std::istringstream in(tkRead(dev + "/pp_dpm_sclk", 1024));
            for (std::string line; std::getline(in, line);)
                if (line.find('*') != std::string::npos) g2.mhz = std::atof(line.c_str() + line.find(':') + 1);
        }
    }

    static void sampleIntel(TkGpu& g2, double dt) {
        const std::string card = "/sys/class/drm/" + g2.card;
        double f = tkNumber(card + "/gt_act_freq_mhz");
        if (f < 0) f = tkNumber(card + "/gt/gt0/rps_act_freq_mhz");
        if (f < 0) f = tkNumber(card + "/gt_cur_freq_mhz");
        g2.mhz = f;
        double mx = tkNumber(card + "/gt_max_freq_mhz");
        if (mx < 0) mx = tkNumber(card + "/gt/gt0/rps_max_freq_mhz");
        g2.mhzMax = mx;
        // i915 counts the milliseconds the GPU spends in RC6 (standby): what is not RC6 is use
        double rc6 = tkNumber(card + "/power/rc6_residency_ms");
        if (rc6 < 0) rc6 = tkNumber(card + "/gt/gt0/rc6_residency_ms");
        if (rc6 >= 0) {
            const unsigned long long v = static_cast<unsigned long long>(rc6);
            if (g2.rc6Primed && v >= g2.lastRc6) g2.busy = std::clamp(100.0 - double(v - g2.lastRc6) / (dt * 10.0), 0.0, 100.0);
            else if (!g2.rc6Primed) g2.busy = 0;
            g2.lastRc6 = v;
            g2.rc6Primed = true;
            g2.note = tkT("Use is the time the GPU is not in standby (RC6).");
        } else {
            g2.busy = -1;
            g2.note = tkT("This driver does not expose the GPU use.");
        }
        const std::string hw = hwmonOf(card + "/device");
        if (!hw.empty()) {
            const double t = tkNumber(hw + "/temp1_input");
            g2.temp = t > 0 ? t / 1000 : -1;
        }
    }

    void sampleNvidia() {
        if (nvidiaMissing) return;
        FILE* p = popen("nvidia-smi --query-gpu=index,name,utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw,clocks.gr,clocks.max.gr --format=csv,noheader,nounits 2>/dev/null", "r");
        if (!p) { nvidiaMissing = true; return; }
        char buf[1024];
        bool any = false;
        while (fgets(buf, sizeof buf, p)) {
            any = true;
            std::vector<std::string> f;
            std::stringstream ss(buf);
            for (std::string part; std::getline(ss, part, ',');) f.push_back(tkTrim(part));
            if (f.size() < 9) continue;
            const int idx = std::atoi(f[0].c_str());
            for (TkGpu& g2 : gpus) {
                if (g2.kind != 1 || g2.nvIndex != idx) continue;
                g2.name = "NVIDIA " + (f[1].rfind("NVIDIA ", 0) == 0 ? f[1].substr(7) : f[1]);
                auto num = [](const std::string& s) { return s.empty() || !std::isdigit(static_cast<unsigned char>(s[0])) ? -1.0 : std::atof(s.c_str()); };
                g2.busy = num(f[2]);
                g2.vramUsed = num(f[3]) >= 0 ? num(f[3]) * 1024 * 1024 : -1;
                g2.vramTotal = num(f[4]) >= 0 ? num(f[4]) * 1024 * 1024 : -1;
                g2.temp = num(f[5]);
                g2.power = num(f[6]);
                g2.mhz = num(f[7]);
                g2.mhzMax = num(f[8]);
                g2.note.clear();
            }
        }
        const int rc = pclose(p);
        if (!any || rc != 0) {
            for (TkGpu& g2 : gpus) if (g2.kind == 1) g2.note = tkT("Reading this GPU needs nvidia-smi (package nvidia-utils).");
            if (rc != 0) nvidiaMissing = true;
        }
    }

    // ---- the list ----
    void buildList() {
        list.clear();
        list.push_back({Cpu, 0});
        list.push_back({Mem, 0});
        for (size_t i = 0; i < disks.size(); i++) list.push_back({Disk, static_cast<int>(i)});
        for (size_t i = 0; i < nets.size(); i++) list.push_back({Net, static_cast<int>(i)});
        for (size_t i = 0; i < gpus.size(); i++) list.push_back({Gpu, static_cast<int>(i)});
        sel = std::clamp(sel, 0, static_cast<int>(list.size()) - 1);
    }

    std::string titleOf(const Res& r) const {
        switch (r.kind) {
            case Cpu: return "CPU";
            case Mem: return tkT("Memory");
            case Disk: return tkT("Disk") + " " + std::to_string(r.index) + " (" + disks[r.index].name + ")";
            case Net: return (nets[r.index].wifi ? "Wi-Fi " : "Ethernet ") + nets[r.index].name;
            case Gpu: return "GPU " + std::to_string(r.index);
        }
        return {};
    }
    std::string valueOf(const Res& r) const {
        switch (r.kind) {
            case Cpu: return tkPercent(cpuPct) + (cpuMhz > 0 ? "  " + tkFreq(cpuMhz) : "");
            case Mem: return memTotal > 0 ? tkBytes(memTotal - memAvail) + " / " + tkBytes(memTotal) : "";
            case Disk: return tkPercent(disks[r.index].busyPct);
            case Net: return "↓ " + tkRate(nets[r.index].rx) + "  ↑ " + tkRate(nets[r.index].tx);
            case Gpu: {
                const TkGpu& g2 = gpus[r.index];
                if (g2.asleep) return tkT("Powered off");
                return g2.busy >= 0 ? tkPercent(g2.busy) : (g2.mhz > 0 ? tkFreq(g2.mhz) : "—");
            }
        }
        return {};
    }
    const TkSeries* seriesOf(const Res& r) const {
        switch (r.kind) {
            case Cpu: return &cpuUse;
            case Mem: return &memUse;
            case Disk: return &disks[r.index].busy;
            case Net: return &nets[r.index].down;
            case Gpu: return &gpus[r.index].use;
        }
        return nullptr;
    }

    // ---- drawing ----
    static const char* iconOf(const Res& r, const std::vector<TkNet>& nets) {
        switch (r.kind) {
            case Cpu: return "cpu";
            case Mem: return "server-square";
            case Disk: return "ssd-square";
            case Net: return nets[r.index].wifi ? "wi-fi-router-minimalistic" : "server-path";
            case Gpu: return "monitor";
        }
        return "cpu";
    }

    // The resources as tall cards, like the hero of the resources popup: a big icon over a soft shape, the numbers, and the live graph under them.
    // With few resources the cards grow to fill the column; with many it scrolls.
    void drawList(cairo_t* cr, const Rect& area) {
        listArea = area;
        hits.clear();
        constexpr double gap = 8, minH = 92, maxH = 128;
        const double n = std::max<size_t>(1, list.size());
        const double itemH = std::clamp((area.h - (n - 1) * gap) / n, minH, maxH);
        listScroll.max = std::max(0.0, n * (itemH + gap) - gap - area.h);
        listScroll.fit();
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        double y = area.y - listScroll.off();
        for (size_t i = 0; i < list.size(); i++) {
            const Rect r = {area.x, y, area.w, itemH};
            y += itemH + gap;
            hits.push_back({r, static_cast<int>(i)});
            if (r.y + r.h < area.y || r.y > area.y + area.h) continue;
            const bool on = static_cast<int>(i) == sel, hot = static_cast<int>(i) == hover;
            const Color bg = g.color(on ? "primary_container" : "surface_container");
            const Color fg = g.color(on ? "on_primary_container" : "on_surface");
            src(cr, hot && !on ? mix(g.color("on_surface"), bg, 0.06) : bg);
            rrect(cr, r.x, r.y, r.w, r.h, 24);
            cairo_fill(cr);
            cairo_save(cr);
            rrect(cr, r.x, r.y, r.w, r.h, 24);
            cairo_clip(cr);
            shapePath(cr, "Cookie9Sided", r.x + r.w - 118, r.y - 52, r.h * 1.9);
            src(cr, g.color(on ? "primary" : "on_surface"), on ? 0.2 : 0.06);
            cairo_fill(cr);
            if (const TkSeries* s = seriesOf(list[i])) {  // the graph, soft, along the bottom of the card
                const double mx = list[i].kind == Net ? std::max(64.0 * 1024, std::max(s->peak(), nets[list[i].index].up.peak())) : 100.0;
                const Rect sp = {r.x, r.y + r.h * 0.56, r.w, r.h * 0.44};
                if (s->v.size() > 1) {
                    const double step = sp.w / (kTkHistory - 1);
                    const double x0 = sp.x + sp.w - step * (s->v.size() - 1);
                    auto yOf = [&](double v) { return sp.y + sp.h - std::clamp(v / mx, 0.0, 1.0) * sp.h; };
                    cairo_move_to(cr, x0, sp.y + sp.h);
                    for (size_t k = 0; k < s->v.size(); k++) cairo_line_to(cr, x0 + step * k, yOf(s->v[k]));
                    cairo_line_to(cr, x0 + step * (s->v.size() - 1), sp.y + sp.h);
                    src(cr, fg, 0.14);
                    cairo_fill(cr);
                    cairo_move_to(cr, x0, yOf(s->v[0]));
                    for (size_t k = 1; k < s->v.size(); k++) cairo_line_to(cr, x0 + step * k, yOf(s->v[k]));
                    src(cr, fg, 0.65);
                    cairo_set_line_width(cr, 1.6);
                    cairo_stroke(cr);
                }
            }
            cairo_restore(cr);
            const double isz = std::clamp(r.h * 0.42, 34.0, 52.0);
            ::icon(cr, iconOf(list[i], nets), r.x + r.w - 20 - isz, r.y + 16, static_cast<int>(isz), fg);
            text(cr, tkFit(titleOf(list[i]), 16, true, r.w - 36 - isz), r.x + 18, r.y + 16, 16, true, fg);
            text(cr, tkFit(valueOf(list[i]), 13, false, r.w - 36 - isz), r.x + 18, r.y + 42, 13, false, fg, 0, 0.85);
        }
        cairo_restore(cr);
    }

    using Stats = std::vector<std::pair<std::string, std::string>>;
    std::vector<Rect> boxes;  // where statGrid put each box
    Rect copyBtn;             // the button that copies the IP (empty when the page has none)
    bool copyHot = false;
    Clock::time_point copiedAt{};
    std::string copyText;

    // The numbers as boxes (see tkStatGrid); the boxes are kept so a page can put a button in one.
    void statGrid(cairo_t* cr, const Rect& a, const Stats& st, int cols) { tkStatGrid(cr, a, st, cols, &boxes); }

    void draw(cairo_t* cr, const Rect& area) {
        if (list.empty()) buildList();
        const double listW = 270;
        drawList(cr, {area.x, area.y, listW, area.h});
        const Rect d = {area.x + listW + 14, area.y, area.w - listW - 14, area.h};
        if (list.empty()) return;
        const Res& r = list[sel];
        text(cr, titleOf(r), d.x + 4, d.y + 2, 26, true, g.color("on_surface"));
        std::string sub;
        if (r.kind == Cpu) sub = cpuModel;
        else if (r.kind == Mem) sub = tkBytes(memTotal);
        else if (r.kind == Disk) sub = disks[r.index].model;
        else if (r.kind == Net) sub = nets[r.index].ip;
        else sub = gpus[r.index].name;
        double sw = 0, sh = 0;
        measure(sub, 14, false, 0, sw, sh);
        sub = tkFit(sub, 14, false, d.w * 0.6);
        measure(sub, 14, false, 0, sw, sh);
        text(cr, sub, d.x + d.w - 4 - sw, d.y + 12, 14, false, g.color("on_surface_variant"));
        const double top = d.y + 44, bottom = d.y + d.h, gap = 10;
        const std::string time = tkT("60 seconds");
        const Color c1 = g.color("primary"), c2 = g.color("tertiary");
        Stats st;
        int cols = 4;
        std::string note;
        char b[32];
        if (r.kind == Cpu) {
            st = {{tkT("Utilization"), tkPercent(cpuPct)}, {tkT("Speed"), cpuMhz > 0 ? tkFreq(cpuMhz) : "—"}, {tkT("Processes"), std::to_string(procCount)}, {tkT("Threads"), std::to_string(threadCount)},
                  {tkT("Up time"), tkDuration(uptime)}, {tkT("Cores"), std::to_string(cores)}};
            if (cpuTemp > 0) { std::snprintf(b, sizeof b, "%.0f °C", cpuTemp); st.push_back({tkT("Temperature"), b}); }
            if (cpuMhzMax > 0) st.push_back({tkT("Max speed"), tkFreq(cpuMhzMax)});
        } else if (r.kind == Mem) {
            st = {{tkT("In use"), tkBytes(memTotal - memAvail)}, {tkT("Available"), tkBytes(memAvail)}, {tkT("Cached"), tkBytes(memCached + memBuffers)}, {tkT("Free"), tkBytes(memFree)},
                  {"Swap", swapTotal > 0 ? tkBytes(swapTotal - swapFree) + " / " + tkBytes(swapTotal) : tkT("None")}, {tkT("Swap use"), swapTotal > 0 ? tkPercent(swapUse.last()) : "—"},
                  {tkT("Total"), tkBytes(memTotal)}, {tkT("Used"), tkPercent(memUse.last())}};
        } else if (r.kind == Disk) {
            const TkDisk& dk = disks[r.index];
            st = {{tkT("Active time"), tkPercent(dk.busyPct)}, {tkT("Read"), tkRate(dk.readBps)}, {tkT("Write"), tkRate(dk.writeBps)}, {tkT("Capacity"), tkBytes(dk.capacity)},
                  {tkT("Type"), dk.name.rfind("nvme", 0) == 0 ? "NVMe SSD" : dk.rotational ? "HDD" : "SSD"}, {tkT("Device"), "/dev/" + dk.name}};
            cols = 3;
        } else if (r.kind == Net) {
            const TkNet& n = nets[r.index];
            st = {{tkT("Receive"), tkRate(n.rx) + "  (" + tkSpeedBits(n.rx) + ")"}, {tkT("Send"), tkRate(n.tx) + "  (" + tkSpeedBits(n.tx) + ")"},
                  {tkT("IPv4 address"), n.ip.empty() ? "—" : n.ip}, {tkT("Connection"), n.wifi ? "Wi-Fi" : n.speedMbps > 0 ? "Ethernet  " + std::to_string(static_cast<int>(n.speedMbps)) + " Mbps" : "Ethernet"}};
            cols = 2;
        } else {
            const TkGpu& gp = gpus[r.index];
            const bool haveUse = gp.busy >= 0 && !gp.asleep;
            st = {{tkT("Utilization"), gp.asleep ? tkT("Powered off") : haveUse ? tkPercent(gp.busy) : "—"},
                  {tkT(gp.shared ? "Shared memory" : "Video memory"), gp.vramUsed >= 0 && gp.vramTotal > 0 ? tkBytes(gp.vramUsed) + " / " + tkBytes(gp.vramTotal) : "—"},
                  {tkT("Clock"), gp.mhz > 0 ? tkFreq(gp.mhz) + (gp.mhzMax > 0 ? " / " + tkFreq(gp.mhzMax) : "") : "—"}};
            std::snprintf(b, sizeof b, "%.0f °C", gp.temp);
            st.push_back({tkT("Temperature"), gp.temp > 0 ? b : "—"});
            std::snprintf(b, sizeof b, "%.0f W", gp.power);
            st.push_back({tkT("Power"), gp.power >= 0 ? b : "—"});
            st.push_back({tkT("Driver"), gp.driver.empty() ? "—" : gp.driver});
            cols = 3;
            note = gp.asleep ? tkT("The GPU is powered off to save energy; it is not read so it stays off.") : gp.note;
        }
        // from the bottom up: the boxes of numbers, the note, the cores; the chart takes what is left
        const int rows = (static_cast<int>(st.size()) + cols - 1) / cols;
        const double statH = rows * 76 + (rows - 1) * 8;
        double y = bottom - statH;
        copyBtn = {};
        statGrid(cr, {d.x, y, d.w, statH}, st, cols);
        if (r.kind == Net && boxes.size() > 2 && !nets[r.index].ip.empty()) {  // copies the IP address
            const Rect& b = boxes[2];
            const double bd = 38;
            copyBtn = {b.x + b.w - 16 - bd, b.y + (b.h - bd) / 2, bd, bd};
            copyText = nets[r.index].ip;
            const bool done = copiedAt != Clock::time_point{} && Clock::now() - copiedAt < std::chrono::milliseconds(1500);
            cairo_new_path(cr);
            cairo_arc(cr, copyBtn.x + bd / 2, copyBtn.y + bd / 2, bd / 2, 0, 2 * M_PI);
            src(cr, g.color(done ? "primary" : "surface_container_highest"), copyHot && !done ? 0.8 : 1);
            cairo_fill(cr);
            ::icon(cr, done ? "check-circle" : "copy", copyBtn.x + (bd - 20) / 2, copyBtn.y + (bd - 20) / 2, 20, g.color(done ? "on_primary" : "on_surface"));
        }
        double noteH = 0;
        if (!note.empty()) {
            double nw = 0, nh = 0;
            measure(note, 13, false, d.w - 32, nw, nh);
            noteH = nh + 22;
            y -= gap + noteH;
            src(cr, g.color("surface_container"));
            rrect(cr, d.x, y, d.w, noteH, 16);
            cairo_fill(cr);
            text(cr, note, d.x + 16, y + 11, 13, false, g.color("on_surface_variant"), d.w - 32);
        }
        if (r.kind == Cpu) {
            // Any number of cores (4 to 64+): more columns and shorter cells until the block takes at most about a third of the room
            const double room = std::max(90.0, (y - gap - top) * 0.34);
            int perRow = std::max(1, std::min(cores, static_cast<int>(d.w / 110)));
            double cellH = 44;
            for (;;) {
                const int rowsN = (cores + perRow - 1) / perRow;
                cellH = std::clamp((room - (rowsN - 1) * 8) / rowsN, 20.0, 44.0);
                const double need = rowsN * cellH + (rowsN - 1) * 8;
                if (need <= room + 0.5 || perRow >= cores || d.w / (perRow + 1) < 58) break;
                perRow++;
            }
            const int crows = (cores + perRow - 1) / perRow;
            const double cellW = (d.w - (perRow - 1) * 8) / perRow;
            const double coresH = crows * cellH + (crows - 1) * 8;
            const double cy = y - gap - coresH;
            for (int i = 0; i < cores; i++) {
                const Rect c = {d.x + (i % perRow) * (cellW + 8), cy + (i / perRow) * (cellH + 8), cellW, cellH};
                src(cr, g.color("surface_container"));
                rrect(cr, c.x, c.y, c.w, c.h, 14);
                cairo_fill(cr);
                cairo_save(cr);
                rrect(cr, c.x, c.y, c.w, c.h, 14);
                cairo_clip(cr);
                const std::vector<double>& h = core[i].use.v;
                if (h.size() > 1) {
                    const double step = c.w / (kTkHistory - 1), x0 = c.x + c.w - step * (h.size() - 1);
                    cairo_move_to(cr, x0, c.y + c.h);
                    for (size_t k = 0; k < h.size(); k++) cairo_line_to(cr, x0 + step * k, c.y + c.h - std::clamp(h[k] / 100, 0.0, 1.0) * c.h);
                    cairo_line_to(cr, x0 + step * (h.size() - 1), c.y + c.h);
                    src(cr, c1, 0.25);
                    cairo_fill(cr);
                }
                cairo_restore(cr);
                const bool tight = cellH < 34 || cellW < 84;
                text(cr, std::to_string(i) + (tight ? " " : "  ") + tkPercent(core[i].pct), c.x + (tight ? 6 : 10), c.y + (tight ? 3 : 6), tight ? 11 : 13, true, g.color("on_surface"));
            }
            y = cy;
        }
        const Rect chart = {d.x, top, d.w, std::max(120.0, y - gap - top)};
        if (r.kind == Cpu) tkChart(cr, chart, {&cpuUse}, {c1}, 100, "100%", time);
        else if (r.kind == Mem) tkChart(cr, chart, {&memUse}, {c1}, 100, tkBytes(memTotal), time);
        else if (r.kind == Disk) {
            const TkDisk& dk = disks[r.index];
            const double mx = std::max(1024.0 * 1024, std::max(dk.read.peak(), dk.write.peak()));
            tkChart(cr, chart, {&dk.read, &dk.write}, {c1, c2}, mx, tkRate(mx), time);
        } else if (r.kind == Net) {
            const TkNet& n = nets[r.index];
            const double mx = std::max(64.0 * 1024, std::max(n.down.peak(), n.up.peak()));
            tkChart(cr, chart, {&n.down, &n.up}, {c1, c2}, mx, tkRate(mx), time);
        } else {
            const TkGpu& gp = gpus[r.index];
            tkChart(cr, chart, {&gp.use}, {c1}, 100, gp.busy >= 0 && !gp.asleep ? "100%" : "", time);
        }
    }

    // ---- input ----
    bool motion(double x, double y) {
        const bool hot = copyBtn.contains(x, y);
        const bool hotChanged = hot != copyHot;
        copyHot = hot;
        int h = -1;
        if (listArea.contains(x, y)) for (const ListHit& l : hits) if (l.r.contains(x, y)) h = l.index;
        const bool changed = h != hover || hotChanged;
        hover = h;
        return changed;
    }
    bool click(double x, double y) {
        if (copyBtn.contains(x, y) && !copyText.empty()) {
            trRunSync({"wl-copy"}, copyText, nullptr, 1500);
            copiedAt = Clock::now();
            return true;
        }
        if (!listArea.contains(x, y)) return false;
        for (const ListHit& l : hits) if (l.r.contains(x, y)) { sel = l.index; return true; }
        return false;
    }
    bool scroll(double x, double y, double dy) {
        if (!listArea.contains(x, y)) return false;
        listScroll.by(dy * 3);
        return true;
    }
    void move(int d) { if (!list.empty()) sel = std::clamp(sel + d, 0, static_cast<int>(list.size()) - 1); }
};
