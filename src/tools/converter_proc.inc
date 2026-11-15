namespace conv {
constexpr size_t kMaxCapture = 1 << 20;

// A child process whose stdout (or stderr) is read through a non-blocking pipe. It is killed and reaped with its object.
class Proc {
public:
    Proc() = default;
    Proc(const Proc&) = delete;
    Proc& operator=(const Proc&) = delete;
    ~Proc() { stop(); }

    bool active() const { return pid_ > 0; }
    const std::string& output() const { return out_; }
    int exitCode() const { return code_; }

    bool start(const std::vector<std::string>& args, bool captureStderr, double timeoutSec = 0) {
        stop();
        out_.clear();
        code_ = -1;
        std::vector<char*> argv;
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        int fds[2];
        if (args.empty() || pipe2(fds, O_CLOEXEC) != 0) return false;
        const pid_t pid = fork();
        if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
        if (pid == 0) runChild(argv, fds[1], captureStderr);
        close(fds[1]);
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        fd_ = fds[0];
        pid_ = pid;
        deadline_ = timeoutSec > 0 ? Clock::now() + std::chrono::milliseconds(static_cast<long>(timeoutSec * 1000)) : Clock::time_point{};
        return true;
    }

    bool finished() {
        if (pid_ <= 0) return false;
        drain();
        int st = 0;
        const pid_t r = waitpid(pid_, &st, WNOHANG);
        if (r == 0) {
            if (deadline_ == Clock::time_point{} || Clock::now() < deadline_) return false;
            stop();
            code_ = -2;
            return true;
        }
        drain();
        code_ = r < 0 ? -1 : (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        release();
        return true;
    }

    void stop() {
        if (pid_ <= 0) return;
        if (kill(-pid_, SIGKILL) != 0) kill(pid_, SIGKILL);
        int st = 0;
        while (waitpid(pid_, &st, 0) < 0 && errno == EINTR) {}
        release();
    }

private:
    [[noreturn]] static void runChild(std::vector<char*>& argv, int wfd, bool captureStderr) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        const int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) {
            dup2(nul, 0);
            dup2(captureStderr ? nul : wfd, 1);
            dup2(captureStderr ? wfd : nul, 2);
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    void drain() {
        char buf[4096];
        for (;;) {
            const ssize_t n = read(fd_, buf, sizeof buf);
            if (n > 0) { if (out_.size() < kMaxCapture) out_.append(buf, static_cast<size_t>(n)); continue; }
            if (n < 0 && errno == EINTR) continue;
            return;  // EAGAIN (nothing more yet) or end of file
        }
    }
    void release() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        pid_ = 0;
    }

    pid_t pid_ = 0;
    int fd_ = -1;
    int code_ = -1;
    std::string out_;
    Clock::time_point deadline_{};
};

// Fire and forget (a file manager): double fork so nothing stays our child and it survives this window.
inline void spawnDetached(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t first = fork();
    if (first < 0) return;
    if (first == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        const int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int st = 0;
    while (waitpid(first, &st, 0) < 0 && errno == EINTR) {}
}

inline bool onPath(const std::string& bin) {
    const char* p = std::getenv("PATH");
    std::stringstream ss(p ? p : "/usr/bin");
    std::string dir;
    while (std::getline(ss, dir, ':'))
        if (!dir.empty() && access((dir + "/" + bin).c_str(), X_OK) == 0) return true;
    return false;
}

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline std::string baseName(const std::string& p) { return p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1); }
inline std::string dirOf(const std::string& p) {
    const size_t s = p.rfind('/');
    return s == std::string::npos ? "." : (s == 0 ? "/" : p.substr(0, s));
}
inline std::string extOf(const std::string& p) {
    const std::string b = baseName(p);
    const size_t d = b.rfind('.');
    return d == std::string::npos || d == 0 ? "" : lower(b.substr(d + 1));
}
inline std::string stemOf(const std::string& p) {
    const std::string b = baseName(p);
    const size_t d = b.rfind('.');
    return d == std::string::npos || d == 0 ? b : b.substr(0, d);
}

inline bool isImageExt(const std::string& e) {
    static const char* kExts[] = {"png", "jpg", "jpeg", "webp", "avif", "bmp", "gif", "tif", "tiff", "heic", "heif", "jxl"};
    for (const char* x : kExts) if (e == x) return true;
    return false;
}
inline bool isDir(const std::string& p) { struct stat st{}; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
inline long long fileSize(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? static_cast<long long>(st.st_size) : -1;
}

inline std::string humanSize(long long b) {
    char buf[32];
    if (b < 0) return "?";
    if (b < 1024) std::snprintf(buf, sizeof buf, "%lld B", b);
    else if (b < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    else std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    return buf;
}

inline std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return o;
}

inline std::vector<std::string> parsePaths(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) b++;
        line = line.substr(b);
        if (line.size() >= 2 && (line[0] == '\'' || line[0] == '"') && line.back() == line[0]) line = line.substr(1, line.size() - 2);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("file://", 0) == 0) line = urlDecode(line.substr(7));
        if (line.rfind("~/", 0) == 0) line = homeDir() + line.substr(1);
        if (!line.empty() && line[0] == '/') out.push_back(line);
    }
    return out;
}

inline std::vector<std::string> expandInputs(const std::vector<std::string>& paths) {
    std::vector<std::string> out;
    for (const std::string& p : paths) {
        if (!isDir(p)) { if (isImageExt(extOf(p)) && fileSize(p) >= 0 && access(p.c_str(), R_OK) == 0) out.push_back(p); continue; }
        DIR* d = opendir(p.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (const dirent* e = readdir(d)) names.push_back(e->d_name);
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) {
            const std::string f = p + (p.back() == '/' ? "" : "/") + n;
            if (n[0] != '.' && isImageExt(extOf(f)) && fileSize(f) >= 0) out.push_back(f);
        }
    }
    return out;
}

inline bool makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i != path.size() && path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return isDir(path);
}
}
