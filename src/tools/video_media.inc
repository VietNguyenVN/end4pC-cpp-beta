namespace vid {
constexpr int kThumbs = 12, kThumbJobs = 3;
constexpr int kDebounceMs = 150;

cairo_surface_t* loadPng(const std::string& path) {
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return s;
    cairo_surface_destroy(s);
    return nullptr;
}
void freeSurface(cairo_surface_t*& s) {
    if (s) cairo_surface_destroy(s);
    s = nullptr;
}

std::vector<std::string> frameArgs(const std::string& in, double t, int width, const std::string& out) {
    return {"ffmpeg", "-loglevel", "error", "-nostdin", "-y", "-ss", fmtSec(t), "-i", in, "-frames:v", "1", "-vf", "scale=" + std::to_string(width) + ":-1", out};
}

struct Media {
    struct Job {
        Proc proc;
        int index = -1;
        std::string tmp, fin;
    };

    std::string file, cache;
    double dur = 0;
    cairo_surface_t* thumbs[kThumbs] = {};
    cairo_surface_t* frame = nullptr;
    Job jobs[kThumbJobs];
    int nextThumb = kThumbs;
    Proc framer;
    std::string frameTmp;
    bool pending = false;
    double pendingTime = 0;
    Clock::time_point due;

    ~Media() { clear(); }

    void clear() {
        for (Job& j : jobs) { j.proc.stop(); j.index = -1; }
        framer.stop();
        for (cairo_surface_t*& t : thumbs) freeSurface(t);
        freeSurface(frame);
        pending = false;
        nextThumb = kThumbs;
        file.clear();
    }

    void open(const std::string& path, double duration) {
        clear();
        file = path;
        dur = duration;
        char key[32];
        std::snprintf(key, sizeof key, "%016zx", std::hash<std::string>{}(path + "|" + std::to_string(fileSize(path)) + "|" + std::to_string(mtimeOf(path))));
        cache = homeDir() + "/.cache/end4pC/tools/video/" + key;
        if (!mkdirs(cache)) { std::fprintf(stderr, "end4pC-tools: cannot create %s\n", cache.c_str()); cache.clear(); return; }
        nextThumb = 0;
        startThumbs();
    }

    bool busy() const {
        if (pending || framer.running()) return true;
        for (const Job& j : jobs) if (j.proc.running()) return true;
        return nextThumb < kThumbs;
    }

    void request(double t) {
        pending = true;
        pendingTime = std::clamp(t, 0.0, std::max(0.0, dur - 0.1));
        due = Clock::now() + std::chrono::milliseconds(kDebounceMs);
    }

    cairo_surface_t* best(double t) const {
        if (frame) return frame;
        const int i = std::clamp(static_cast<int>(t / std::max(dur, 0.001) * kThumbs), 0, kThumbs - 1);
        for (int d = 0; d < kThumbs; d++)
            for (int k : {i - d, i + d})
                if (k >= 0 && k < kThumbs && thumbs[k]) return thumbs[k];
        return nullptr;
    }

    bool tick() {
        bool changed = false;
        for (Job& j : jobs) if (j.proc.pump() && j.proc.exited) changed |= finishThumb(j);
        if (framer.pump() && framer.exited) changed |= finishFrame();
        if (pending && Clock::now() >= due && !cache.empty()) { startFrame(); changed = true; }
        startThumbs();
        return changed;
    }

private:
    void startThumbs() {
        for (Job& j : jobs) {
            while (!j.proc.running() && nextThumb < kThumbs) {
                const int i = nextThumb++;
                char name[32];
                std::snprintf(name, sizeof name, "t%02d.png", i);
                j.index = i;
                j.fin = cache + "/" + name;
                j.tmp = cache + "/tmp" + name;
                if (fileSize(j.fin) > 0) { freeSurface(thumbs[i]); thumbs[i] = loadPng(j.fin); if (thumbs[i]) continue; }
                const double t = dur * (i + 0.5) / kThumbs;
                if (!j.proc.start(frameArgs(file, t, 160, j.tmp), false)) std::fprintf(stderr, "end4pC-tools: cannot start ffmpeg\n");
            }
        }
    }

    bool finishThumb(Job& j) {
        if (j.proc.code != 0 || fileSize(j.tmp) <= 0) { unlink(j.tmp.c_str()); return false; }
        if (rename(j.tmp.c_str(), j.fin.c_str()) != 0) return false;
        freeSurface(thumbs[j.index]);
        thumbs[j.index] = loadPng(j.fin);
        return thumbs[j.index] != nullptr;
    }

    void startFrame() {
        pending = false;
        frameTmp = cache + "/tmpprev.png";
        if (!framer.start(frameArgs(file, pendingTime, 640, frameTmp), false)) std::fprintf(stderr, "end4pC-tools: cannot start ffmpeg\n");
    }

    bool finishFrame() {
        if (framer.code != 0 || fileSize(frameTmp) <= 0) { unlink(frameTmp.c_str()); return false; }
        cairo_surface_t* s = loadPng(frameTmp);
        unlink(frameTmp.c_str());
        if (!s) return false;
        freeSurface(frame);
        frame = s;
        return true;
    }
};
}
