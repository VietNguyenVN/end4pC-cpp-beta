#include "converter_proc.inc"
#include "converter_job.inc"

namespace conv {
enum class St { Pending, Running, Ok, Fail, Cancelled };
constexpr size_t kMaxItems = 300;
constexpr long long kMerged = -2;

struct Item {
    std::string path, name, out, msg;
    long long before = -1, after = -1;
    St st = St::Pending;
    cairo_surface_t* thumb = nullptr;
    bool thumbTried = false;
    Item() = default;
    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    ~Item() { if (thumb) cairo_surface_destroy(thumb); }
};

class Engine {
public:
    Options opt;
    std::vector<std::unique_ptr<Item>> items;

    bool running() const { return running_; }
    bool toolsMissing() const { return !hasMagick_ && !hasFfmpeg_; }
    const std::string& lastDir() const { return lastDir_; }

    int add(const std::vector<std::string>& paths) {
        int added = 0;
        for (const std::string& p : paths) {
            if (items.size() >= kMaxItems) break;
            const bool dup = std::any_of(items.begin(), items.end(), [&](const auto& i) { return i->path == p; });
            if (dup) continue;
            auto it = std::make_unique<Item>();
            it->path = p;
            it->name = baseName(p);
            it->before = fileSize(p);
            items.push_back(std::move(it));
            added++;
        }
        return added;
    }
    void remove(size_t i) { if (!running_ && i < items.size()) items.erase(items.begin() + static_cast<long>(i)); }
    void clear() { if (!running_) items.clear(); }

    void probeTools() { hasMagick_ = onPath("magick"); hasFfmpeg_ = onPath("ffmpeg"); }

    int doneCount() const { return count([](St s) { return s != St::Pending && s != St::Running; }); }
    int okCount() const { return count([](St s) { return s == St::Ok; }); }
    int failCount() const { return count([](St s) { return s == St::Fail; }); }
    bool finished() const { return !running_ && doneCount() > 0; }
    long long savedBytes(long long& before) const {
        long long after = 0;
        before = 0;
        for (const auto& i : items)
            if (i->st == St::Ok) { before += std::max(0LL, i->before); after += std::max(0LL, i->after); }
        return after;
    }

    void start() {
        if (running_ || items.empty()) return;
        jobs_.clear();
        for (auto& i : items) { i->st = St::Pending; i->msg.clear(); i->out.clear(); i->after = -1; }
        if (opt.format == kPdf && items.size() > 1) {
            std::vector<int> all;
            for (size_t i = 0; i < items.size(); i++) all.push_back(static_cast<int>(i));
            jobs_.push_back(all);
        } else for (size_t i = 0; i < items.size(); i++) jobs_.push_back({static_cast<int>(i)});
        jobOpt_ = opt;
        job_ = 0;
        running_ = true;
        launch(false);
    }

    void cancel() {
        if (!running_) return;
        proc_.stop();
        if (!tmp_.empty()) unlink(tmp_.c_str());
        for (auto& i : items) if (i->st == St::Running || i->st == St::Pending) i->st = St::Cancelled;
        running_ = false;
    }

    bool tick() {
        if (!running_ || !proc_.finished()) return false;
        onExit(proc_.exitCode());
        return true;
    }

private:
    template <class F> int count(F f) const {
        return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const auto& i) { return f(i->st); }));
    }
    Item& first() { return *items[static_cast<size_t>(jobs_[job_][0])]; }

    void markJob(St st, const std::string& msg) {
        for (int i : jobs_[job_]) { items[static_cast<size_t>(i)]->st = st; items[static_cast<size_t>(i)]->msg = msg; }
    }
    void nextJob() {
        tmp_.clear();
        if (++job_ < jobs_.size()) { launch(false); return; }
        running_ = false;
    }
    void failJob(const std::string& msg) { markJob(St::Fail, msg); nextJob(); }

    void launch(bool ffmpeg) {
        Item& f = first();
        const char* ext = kFormats[jobOpt_.format].ext;
        const std::string dir = outputDir(f.path, jobOpt_);
        if (!makeDirs(dir)) { failJob("Cannot create the folder"); return; }
        useFfmpeg_ = ffmpeg || !hasMagick_;
        if (useFfmpeg_ && !(hasFfmpeg_ && ffmpegCan(jobOpt_) && jobs_[job_].size() == 1)) { failJob("magick is not installed"); return; }
        tmp_ = tempPath(dir, stemOf(f.path), ext);
        std::vector<std::string> inputs;
        for (int i : jobs_[job_]) inputs.push_back(items[static_cast<size_t>(i)]->path);
        const auto args = useFfmpeg_ ? ffmpegArgs(inputs[0], tmp_, jobOpt_) : magickArgs(inputs, tmp_, jobOpt_);
        markJob(St::Running, "");
        if (!proc_.start(args, true)) failJob("Cannot start the converter");
    }

    void onExit(int code) {
        const bool ok = code == 0 && fileSize(tmp_) > 0;
        if (!ok) unlink(tmp_.c_str());
        if (!ok && !useFfmpeg_ && hasFfmpeg_ && ffmpegCan(jobOpt_) && jobs_[job_].size() == 1) { launch(true); return; }
        if (!ok) {
            const std::string why = firstLine(proc_.output());
            failJob(why.empty() ? (code == 127 ? "Converter not found" : "Conversion failed") : why);
            return;
        }
        commit();
    }

    void commit() {
        const char* ext = kFormats[jobOpt_.format].ext;
        const long long size = fileSize(tmp_);
        const std::string dest = reserveName(outputDir(first().path, jobOpt_), stemOf(first().path), ext);
        if (dest.empty() || rename(tmp_.c_str(), dest.c_str()) != 0) {
            unlink(tmp_.c_str());
            failJob("Cannot save the file");
            return;
        }
        lastDir_ = dirOf(dest);
        markJob(St::Ok, "");
        bool head = true;
        for (int i : jobs_[job_]) {
            Item& it = *items[static_cast<size_t>(i)];
            it.out = dest;
            it.after = head ? size : kMerged;
            head = false;
        }
        nextJob();
    }

    std::vector<std::vector<int>> jobs_;
    size_t job_ = 0;
    Options jobOpt_;
    Proc proc_;
    std::string tmp_, lastDir_;
    bool running_ = false, useFfmpeg_ = false, hasMagick_ = true, hasFfmpeg_ = true;
};
}
