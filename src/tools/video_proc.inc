extern "C" pid_t waitpid(pid_t, int*, int);

namespace vid {
constexpr size_t kCaptureMax = 1 << 20;

// One child process. The parent reads its stdout (stderr merged) without blocking; the child is killed and reaped on stop()/destruction.
struct Proc {
    pid_t pid = -1;
    int fd = -1;
    std::string out;
    bool exited = false;
    int code = -1;

    Proc() = default;
    Proc(const Proc&) = delete;
    Proc& operator=(const Proc&) = delete;
    ~Proc() { stop(); }

    bool running() const { return pid > 0; }

    void stop() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
        if (fd >= 0) { close(fd); fd = -1; }
    }

    bool start(const std::vector<std::string>& argv, bool capture) {
        stop();
        out.clear();
        exited = false;
        code = -1;
        if (argv.empty()) return false;
        std::vector<char*> av;
        for (const std::string& s : argv) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        int p[2] = {-1, -1};
        if (capture && pipe2(p, O_CLOEXEC) != 0) return false;
        const pid_t child = fork();
        if (child < 0) {
            if (capture) { close(p[0]); close(p[1]); }
            return false;
        }
        if (child == 0) {
            const int nul = open("/dev/null", O_RDWR | O_CLOEXEC);
            dup2(nul, 0);
            dup2(capture ? p[1] : nul, 1);
            dup2(capture ? p[1] : nul, 2);
            execvp(av[0], av.data());
            _exit(127);
        }
        pid = child;
        if (capture) {
            close(p[1]);
            fd = p[0];
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        }
        return true;
    }

    // Reads what is available and reaps the child when it has ended. Returns true when something changed: new data, or the child
    // just ended (exited and code are then set, and only this call reports the end).
    bool pump() {
        if (pid <= 0) return false;
        bool changed = false;
        while (fd >= 0) {
            char b[4096];
            const ssize_t n = read(fd, b, sizeof b);
            if (n > 0) { if (out.size() < kCaptureMax) out.append(b, static_cast<size_t>(n)); changed = true; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno == EAGAIN) break;
            close(fd);
            fd = -1;
            changed = true;
        }
        if (fd >= 0) return changed;
        int st = 0;
        if (waitpid(pid, &st, WNOHANG) != pid) return changed;
        pid = -1;
        exited = true;
        code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        return true;
    }
};

std::string findExe(const char* name) {
    const char* path = std::getenv("PATH");
    std::stringstream ss(path ? path : "/usr/bin");
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        const std::string full = dir + "/" + name;
        if (!dir.empty() && access(full.c_str(), X_OK) == 0) return full;
    }
    return "";
}

std::string baseName(const std::string& p) { const size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }
std::string dirName(const std::string& p) { const size_t s = p.rfind('/'); return s == std::string::npos ? "." : (s == 0 ? "/" : p.substr(0, s)); }
std::string stemOf(const std::string& p) {
    const std::string b = baseName(p);
    const size_t d = b.rfind('.');
    return d == std::string::npos || d == 0 ? b : b.substr(0, d);
}
std::string extOf(const std::string& p) {
    const std::string b = baseName(p);
    const size_t d = b.rfind('.');
    return d == std::string::npos || d == 0 ? "" : b.substr(d + 1);
}
bool fileExists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }
long long fileSize(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : -1;
}
bool isRegularFile(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool mkdirs(const std::string& path) {
    std::string cur;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += "/" + part;
        if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

std::string uniquePath(const std::string& dir, const std::string& stem, const std::string& ext) {
    for (int i = 1; i < 10000; i++) {
        const std::string suffix = i == 1 ? "_edit" : "_edit_" + std::to_string(i);
        const std::string cand = dir + "/" + stem + suffix + "." + ext;
        if (!fileExists(cand)) return cand;
    }
    return dir + "/" + stem + "_edit_x." + ext;
}

std::string percentDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return o;
}

std::string pathFromText(const std::string& text) {
    std::stringstream ss(text);
    std::string line, found;
    while (std::getline(ss, line)) {
        const size_t a = line.find_first_not_of(" \t\r\"'");
        const size_t b = line.find_last_not_of(" \t\r\"'");
        if (a == std::string::npos) continue;
        found = line.substr(a, b - a + 1);
        break;
    }
    if (found.rfind("file://", 0) == 0) found = percentDecode(found.substr(7));
    if (found.rfind("~/", 0) == 0) found = homeDir() + found.substr(1);
    return !found.empty() && found[0] == '/' ? found : "";
}

std::string fmtClock(double s) {
    s = std::max(0.0, s);
    const long ms = std::lround(s * 1000);
    char b[32];
    std::snprintf(b, sizeof b, "%02ld:%02ld:%02ld.%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
    return b;
}
std::string fmtShort(double s) {
    const long t = std::lround(std::max(0.0, s));
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%ld:%02ld", t / 60, t % 60);
    return b;
}
bool parseClock(const std::string& s, double& out) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string p;
    while (std::getline(ss, p, ':')) parts.push_back(p);
    if (parts.empty() || parts.size() > 3 || s.empty()) return false;
    double total = 0;
    for (const std::string& q : parts) {
        char* end = nullptr;
        const double v = q.empty() ? 0 : std::strtod(q.c_str(), &end);
        if (!q.empty() && (end == q.c_str() || *end)) return false;
        total = total * 60 + v;
    }
    out = total;
    return true;
}
std::string humanSize(long long b) {
    const char* unit[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(std::max(0LL, b));
    int u = 0;
    while (v >= 1024 && u < 4) { v /= 1024; u++; }
    char buf[32];
    std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : "%.1f %s", v, unit[u]);
    return buf;
}

// Cuts text with an ellipsis so that it fits maxW px (UTF-8 safe).
std::string ellipsize(const std::string& s, double px, bool bold, double maxW) {
    double w = 0, h = 0;
    measure(s, px, bold, 0, w, h);
    if (w <= maxW) return s;
    size_t n = s.size();
    while (n > 0) {
        n--;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
        measure(s.substr(0, n) + "\xE2\x80\xA6", px, bold, 0, w, h);
        if (w <= maxW) break;
    }
    return s.substr(0, n) + "\xE2\x80\xA6";
}
}
