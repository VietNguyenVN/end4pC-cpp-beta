// Notes: the files. One markdown file per note in <data>/end4pC/notes/<YYYYMMDD-HHMMSS>.md, pins in notes/.pinned (one id per
// line), deleted notes in notes/.trash. Every write goes to a temporary file first and is renamed over the real one, so a crash
// never leaves a half-written note. Included by page_notes.inc (inside the anonymous namespace of tools.cpp).
}
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>

#include <set>
namespace {
namespace notes_ui {
constexpr size_t kMaxPaste = 512 * 1024;

struct Note {
    std::string id, content;
    time_t mtime = 0;
    bool pinned = false;
};

std::string notesDir() { return configDir() + "/notes"; }
std::string trashDir() { return notesDir() + "/.trash"; }

bool makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i != path.size() && path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

std::string validUtf8(const std::string& s) {
    if (g_utf8_validate(s.data(), static_cast<gssize>(s.size()), nullptr)) return s;
    char* fixed = g_utf8_make_valid(s.data(), static_cast<gssize>(s.size()));
    std::string out = fixed;
    g_free(fixed);
    return out;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t done = 0;
    bool ok = true;
    while (ok && done < data.size()) {
        const ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) ok = false;
        else done += static_cast<size_t>(n);
    }
    ok = ok && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    if (ok && rename(tmp.c_str(), path.c_str()) != 0) ok = false;
    if (!ok) unlink(tmp.c_str());
    return ok;
}

std::string utcStamp() {
    const time_t t = time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// Runs argv (no shell). `in` goes to its stdin, its stdout is collected into `out` (or dropped). The child is always reaped,
// and killed when it takes longer than timeoutMs.
bool runChild(const std::vector<std::string>& args, const std::string* in, std::string* out, int timeoutMs) {
    static const bool ignoreSigpipe = (signal(SIGPIPE, SIG_IGN), true);
    (void)ignoreSigpipe;
    int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1};
    if ((in && pipe2(inPipe, O_CLOEXEC) != 0) || (out && pipe2(outPipe, O_CLOEXEC) != 0)) return false;
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const int nul = open("/dev/null", O_RDWR);
        dup2(in ? inPipe[0] : nul, 0);
        dup2(out ? outPipe[1] : nul, 1);
        dup2(nul, 2);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    if (in) close(inPipe[0]);
    if (out) close(outPipe[1]);
    bool ok = true;
    if (in) {
        size_t done = 0;
        while (done < in->size()) {
            const ssize_t n = write(inPipe[1], in->data() + done, in->size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = false; break; }
            done += static_cast<size_t>(n);
        }
        close(inPipe[1]);
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    if (out) {
        char buf[8192];
        while (ok) {
            pollfd p = {outPipe[0], POLLIN, 0};
            const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
            if (left <= 0) { ok = false; break; }
            if (poll(&p, 1, left) <= 0) continue;
            const ssize_t n = read(outPipe[0], buf, sizeof buf);
            if (n <= 0) break;
            if (out->size() < kMaxPaste) out->append(buf, static_cast<size_t>(n));
        }
        close(outPipe[0]);
    }
    int status = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (Clock::now() > deadline) { kill(pid, SIGKILL); waitpid(pid, &status, 0); ok = false; break; }
        usleep(3000);
    }
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool clipboardCopy(const std::string& s) { return runChild({"wl-copy"}, &s, nullptr, 2000); }
bool clipboardPaste(std::string& s) {
    s.clear();
    if (!runChild({"wl-paste", "--no-newline"}, nullptr, &s, 2000)) return false;
    s = validUtf8(s.substr(0, kMaxPaste));
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return true;
}

struct NoteStore {
    std::vector<Note> notes;

    static std::string pathOf(const std::string& id) { return notesDir() + "/" + id + ".md"; }

    void load() {
        notes.clear();
        std::set<std::string> pins = readPins();
        if (DIR* d = opendir(notesDir().c_str())) {
            while (const dirent* e = readdir(d)) {
                const std::string n = e->d_name;
                if (n.size() < 4 || n[0] == '.' || n.compare(n.size() - 3, 3, ".md") != 0) continue;
                Note note;
                note.id = n.substr(0, n.size() - 3);
                note.content = validUtf8(readFile(pathOf(note.id)));
                note.mtime = mtimeOf(pathOf(note.id));
                note.pinned = pins.count(note.id) > 0;
                notes.push_back(std::move(note));
            }
            closedir(d);
        }
    }

    std::set<std::string> readPins() const {
        std::set<std::string> pins;
        std::stringstream ss(readFile(notesDir() + "/.pinned"));
        for (std::string line; std::getline(ss, line);) if (!line.empty()) pins.insert(line);
        return pins;
    }
    bool savePins() const {
        std::string data;
        for (const Note& n : notes) if (n.pinned) data += n.id + "\n";
        return makeDirs(notesDir()) && writeFileAtomic(notesDir() + "/.pinned", data);
    }

    int create() {
        const std::string base = utcStamp();
        std::string id = base;
        for (int n = 2; exists(id); n++) id = base + "-" + std::to_string(n);
        Note note;
        note.id = id;
        note.mtime = time(nullptr);
        notes.push_back(std::move(note));
        return static_cast<int>(notes.size()) - 1;
    }
    bool exists(const std::string& id) const {
        for (const Note& n : notes) if (n.id == id) return true;
        return access(pathOf(id).c_str(), F_OK) == 0 || access((trashDir() + "/" + id + ".md").c_str(), F_OK) == 0;
    }

    bool save(Note& n) {
        if (!makeDirs(notesDir()) || !writeFileAtomic(pathOf(n.id), n.content)) return false;
        n.mtime = mtimeOf(pathOf(n.id));
        return true;
    }

    bool trash(int index) {
        const Note& n = notes[index];
        if (access(pathOf(n.id).c_str(), F_OK) == 0) {
            if (!makeDirs(trashDir()) || rename(pathOf(n.id).c_str(), (trashDir() + "/" + n.id + ".md").c_str()) != 0) return false;
        }
        notes.erase(notes.begin() + index);
        return true;
    }
    int restore(const std::string& id, bool pinned) {
        const std::string from = trashDir() + "/" + id + ".md";
        if (rename(from.c_str(), pathOf(id).c_str()) != 0) return -1;
        Note n;
        n.id = id;
        n.content = validUtf8(readFile(pathOf(id)));
        n.mtime = mtimeOf(pathOf(id));
        n.pinned = pinned;
        notes.push_back(std::move(n));
        return static_cast<int>(notes.size()) - 1;
    }
    void discardEmpty(int index) {
        unlink(pathOf(notes[index].id).c_str());
        notes.erase(notes.begin() + index);
    }

    void importLegacy() {
        const std::string marker = notesDir() + "/.imported";
        if (access(marker.c_str(), F_OK) == 0 || !makeDirs(notesDir())) return;
        load();
        if (notes.empty()) {
            const std::string old = readFile(envDir("XDG_STATE_HOME", homeDir() + "/.local/state") + "/quickshell/user/notes.txt");
            if (old.find_first_not_of(" \t\r\n") != std::string::npos) {
                Note n;
                n.id = utcStamp();
                n.content = validUtf8(old);
                if (!save(n)) return;
                notes.push_back(std::move(n));
            }
        }
        if (!writeFileAtomic(marker, "")) std::fprintf(stderr, "end4pC-tools: cannot write %s\n", marker.c_str());
    }
};
}
