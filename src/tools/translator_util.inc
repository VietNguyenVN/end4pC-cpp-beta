// Translator helpers: a child process without a shell, tiny JSON reading and writing for the history, UTF-8 steps.
// Included by page_translator.inc (inside the anonymous namespace of tools.cpp).
}
#include <csignal>
#include <sys/wait.h>
namespace {
constexpr size_t kTrMaxText = 5000;
constexpr int kTrHistoryMax = 30;

// Runs argv with optional stdin data, collects stdout, never blocks in poll(). The child gets its own process group so a kill
// also takes down what `trans` started (curl). The destructor kills and reaps, so nothing outlives the page.
struct TrJob {
    pid_t pid = -1;
    int outFd = -1, inFd = -1;
    std::string out, in;
    size_t inOff = 0;
    Clock::time_point deadline;
    bool finished = false, ok = false, timedOut = false, eof = false;

    TrJob() = default;
    TrJob(const TrJob&) = delete;
    TrJob& operator=(const TrJob&) = delete;
    ~TrJob() { kill(); }

    bool running() const { return pid > 0; }

    bool start(const std::vector<std::string>& argv, const std::string& stdinData, int timeoutMs, bool capture = true) {
        kill();
        out.clear();
        in = stdinData;
        inOff = 0;
        finished = ok = timedOut = eof = false;
        int po[2], pi[2];
        if (pipe2(po, O_CLOEXEC) < 0) return false;
        if (pipe2(pi, O_CLOEXEC) < 0) { close(po[0]); close(po[1]); return false; }
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        const pid_t p = fork();
        if (p < 0) { close(po[0]); close(po[1]); close(pi[0]); close(pi[1]); return false; }
        if (p == 0) {
            setpgid(0, 0);
            dup2(pi[0], 0);
            const int nul = open("/dev/null", O_WRONLY);
            dup2(capture ? po[1] : nul, 1);
            if (nul >= 0) dup2(nul, 2);
            execvp(args[0], args.data());
            _exit(127);
        }
        setpgid(p, p);
        close(po[1]);
        close(pi[0]);
        if (!capture) { close(po[0]); po[0] = -1; eof = true; }
        pid = p;
        outFd = po[0];
        inFd = pi[1];
        if (outFd >= 0) fcntl(outFd, F_SETFL, O_NONBLOCK);
        fcntl(inFd, F_SETFL, O_NONBLOCK);
        if (in.empty()) { close(inFd); inFd = -1; }
        deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        return true;
    }

    void kill() {
        if (pid > 0) {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        pid = -1;
        closeFds();
    }

    bool poll() {
        if (finished) return true;
        if (pid <= 0) return false;
        pumpInput();
        pumpOutput();
        int status = 0;
        if (eof && waitpid(pid, &status, WNOHANG) == pid) {
            pid = -1;
            closeFds();
            finished = true;
            ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        } else if (Clock::now() > deadline) {
            kill();
            finished = timedOut = true;
        }
        return finished;
    }

private:
    void closeFds() {
        if (outFd >= 0) close(outFd);
        if (inFd >= 0) close(inFd);
        outFd = inFd = -1;
    }
    void pumpInput() {
        while (inFd >= 0 && inOff < in.size()) {
            const ssize_t n = write(inFd, in.data() + inOff, in.size() - inOff);
            if (n > 0) { inOff += static_cast<size_t>(n); continue; }
            if (n < 0 && errno == EAGAIN) return;
            inOff = in.size();
        }
        if (inFd >= 0) { close(inFd); inFd = -1; }
    }
    void pumpOutput() {
        char buf[4096];
        while (outFd >= 0 && !eof) {
            const ssize_t n = read(outFd, buf, sizeof buf);
            if (n > 0) { if (out.size() < (1u << 20)) out.append(buf, static_cast<size_t>(n)); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
            eof = true;
        }
    }
};

bool trRunSync(const std::vector<std::string>& argv, const std::string& stdinData, std::string* out, int timeoutMs) {
    TrJob job;
    if (!job.start(argv, stdinData, timeoutMs, out != nullptr)) return false;
    while (!job.poll()) usleep(2000);
    if (out) *out = job.out;
    return job.ok;
}

size_t trPrevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    do i--; while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
size_t trNextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do i++; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}
// Keeps only valid UTF-8, drops CR and control characters except newline and tab (tab becomes four spaces).
std::string trClean(const std::string& in) {
    std::string o;
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
        if (len == 0 || i + len > in.size()) { i++; continue; }
        bool good = true;
        for (size_t k = 1; k < len; k++) good = good && (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (!good) { i++; continue; }
        if (c == '\t') o += "    ";
        else if (c >= 32 && c != 127) o.append(in, i, len);
        else if (c == '\n') o += '\n';
        i += len;
    }
    return o;
}
std::string trStripAnsi(const std::string& in) {
    std::string o;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '\x1b' && i + 1 < in.size() && in[i + 1] == '[') {
            i += 2;
            while (i < in.size() && !std::isalpha(static_cast<unsigned char>(in[i]))) i++;
        } else o += in[i];
    }
    return o;
}
std::string trTrim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    return s.substr(a);
}
std::string trHead(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
    return s.substr(0, n);
}

std::string trJsonEscape(const std::string& s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        char tmp[8];
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n";
        else if (c < 32) { std::snprintf(tmp, sizeof tmp, "\\u%04x", c); o += tmp; }
        else o += static_cast<char>(c);
    }
    return o + "\"";
}

bool trJsonString(const std::string& t, size_t& i, std::string& out) {
    if (i >= t.size() || t[i] != '"') return false;
    out.clear();
    for (i++; i < t.size(); i++) {
        const char c = t[i];
        if (c == '"') { i++; return true; }
        if (c != '\\') { out += c; continue; }
        if (++i >= t.size()) return false;
        const char e = t[i];
        if (e == 'n') out += '\n';
        else if (e == 't') out += '\t';
        else if (e == 'r') out += '\r';
        else if (e == 'u' && i + 4 < t.size()) {
            unsigned cp = static_cast<unsigned>(std::strtoul(t.substr(i + 1, 4).c_str(), nullptr, 16));
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && t.compare(i + 1, 2, "\\u") == 0 && i + 6 < t.size()) {
                const unsigned lo = static_cast<unsigned>(std::strtoul(t.substr(i + 3, 4).c_str(), nullptr, 16));
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
            }
            i18n::appendUtf8(out, cp);
        } else out += e;
    }
    return false;
}

std::vector<std::map<std::string, std::string>> trJsonObjects(const std::string& t) {
    std::vector<std::map<std::string, std::string>> all;
    size_t i = t.find('[');
    if (i == std::string::npos) return all;
    while ((i = t.find('{', i)) != std::string::npos) {
        std::map<std::string, std::string> obj;
        i++;
        while (i < t.size()) {
            while (i < t.size() && t[i] != '"' && t[i] != '}') i++;
            std::string key, val;
            if (i >= t.size() || t[i] == '}' || !trJsonString(t, i, key)) break;
            while (i < t.size() && t[i] != ':') i++;
            i++;
            while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) i++;
            if (i < t.size() && t[i] == '"') { if (!trJsonString(t, i, val)) return all; }
            else while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '-')) val += t[i++];
            obj[key] = val;
        }
        all.push_back(std::move(obj));
        i++;
    }
    return all;
}

// Writes via a temporary file and rename, so a crash never leaves half a file. Returns false (and says why on stderr) on failure.
bool trWriteAtomic(const std::string& dir, const std::string& name, const std::string& content) {
    mkdir((configDir()).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    const std::string path = dir + "/" + name, tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << content;
        if (!f.good()) { std::fprintf(stderr, "translator: cannot write %s\n", tmp.c_str()); return false; }
    }
    if (rename(tmp.c_str(), path.c_str()) != 0) { std::fprintf(stderr, "translator: cannot rename %s\n", tmp.c_str()); return false; }
    return true;
}
