#include "video_proc.inc"
#include "video_ops.inc"
#include "video_media.inc"
#include "video_ui.inc"

struct VideoPage final : ToolPage {
    enum class Stage { Empty, Probing, Ready };
    enum class Run { Idle, Running, Done, Failed };
    enum Hit { kOpen = 1, kPaste, kPlay, kFolder, kRun, kCancel, kDismiss, kStart, kEnd, kTimeline, kSetStart, kSetEnd, kOpBase = 20, kOptBase = 40 };
    enum Drag { kNoDrag, kDragStart, kDragEnd, kDragHead };

    static constexpr double kGap = 10, kMinRange = 0.1, kHandleGrab = 14;
    static constexpr int kMessageMs = 6000;

    Stage stage = Stage::Empty;
    Run run = Run::Idle;
    std::string path, message;
    bool messageIsError = false;
    Clock::time_point messageAt;

    vid::Info info;
    double start = 0, end = 0, head = 0;
    Drag drag = kNoDrag;
    int editing = -1;
    std::string editBuf;
    bool editFresh = false;

    vid::Op op = vid::kTrim;
    int sel[vid::kOpCount][2] = {{0, 0}, {0, 0}, {0, 0}, {1, 0}, {1, 0}, {0, 0}};

    vid::Media media;
    vid::Proc picker, paster, probe, exporter, player;
    std::vector<std::unique_ptr<vid::Proc>> helpers;  // fire-and-forget children (open folder), reaped in tick()

    std::string outPath, errLine;
    double progress = 0, runLen = 0;
    long long outSize = 0;

    std::vector<vid::HitRect> hits;
    int hover = -1;
    Rect strip;

    const char* name() const override { return "Video Editor"; }
    const char* icon() const override { return "video-frame-cut"; }
    ~VideoPage() override {
        if (exporter.running()) discardOutput();
    }

    void setMessage(const std::string& m, bool isError) {
        message = m;
        messageIsError = isError;
        messageAt = Clock::now();
    }

    void pickFile() {
        if (picker.running() || run == Run::Running) return;
        const std::string filter = "Video files (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.flv *.ts *.mpg *.mpeg *.wmv *.ogv *.gif)";
        std::vector<std::string> argv;
        if (!vid::findExe("kdialog").empty()) argv = {"kdialog", "--title", i18n::tr("Open video"), "--getopenfilename", homeDir(), filter};
        else if (!vid::findExe("zenity").empty()) argv = {"zenity", "--file-selection", "--title=" + std::string(i18n::tr("Open video")), "--filename=" + homeDir() + "/"};
        else { setMessage(i18n::tr("No file dialog found, paste a path with Ctrl+V"), true); return; }
        if (!picker.start(argv, true)) setMessage(i18n::tr("Could not open the file dialog"), true);
    }

    void pastePath() {
        if (paster.running() || run == Run::Running) return;
        if (!paster.start({"wl-paste", "--no-newline"}, true)) setMessage(i18n::tr("Could not read the clipboard"), true);
    }

    void openFile(const std::string& file) {
        if (file.empty() || !vid::isRegularFile(file)) { setMessage(i18n::tr("That is not a file"), true); return; }
        media.clear();
        path = file;
        run = Run::Idle;
        stage = Stage::Probing;
        message.clear();
        if (!probe.start(vid::probeArgs(file), true)) { stage = Stage::Empty; setMessage(i18n::tr("ffprobe not found"), true); }
    }

    void probeDone() {
        if (probe.code != 0 || !vid::parseProbe(probe.out, info)) {
            stage = Stage::Empty;
            setMessage(i18n::tr("No video found in that file"), true);
            return;
        }
        stage = Stage::Ready;
        start = 0;
        end = info.dur;
        head = 0;
        media.open(path, info.dur);
        media.request(0);
    }

    void play() {
        if (stage != Stage::Ready) return;
        if (vid::findExe("mpv").empty()) { setMessage(i18n::tr("mpv not found"), true); return; }
        const std::vector<std::string> argv = {"mpv", "--no-terminal", "--start=" + vid::fmtSec(start), "--end=" + vid::fmtSec(end), "--", path};
        if (!player.start(argv, false)) setMessage(i18n::tr("Could not start mpv"), true);
    }

    void openFolder(const std::string& file) {
        auto p = std::make_unique<vid::Proc>();
        if (p->start({"xdg-open", vid::dirName(file)}, false)) helpers.push_back(std::move(p));
        else setMessage(i18n::tr("Could not open the folder"), true);
    }

    bool partial() const { return start > 0.001 || end < info.dur - 0.001; }

    void startExport() {
        if (stage != Stage::Ready || run == Run::Running) return;
        if (op == vid::kAudio && !info.audio) { setMessage(i18n::tr("This video has no audio"), true); return; }
        const std::string ext = vid::outExt(op, sel[op], vid::extOf(path));
        outPath = vid::uniquePath(vid::dirName(path), vid::stemOf(path), ext);
        runLen = partial() ? end - start : info.dur;
        errLine.clear();
        progress = 0;
        message.clear();
        if (!exporter.start(vid::ffmpegArgs(op, sel[op], info, path, outPath, partial(), start, runLen), true)) {
            run = Run::Failed;
            errLine = i18n::tr("Could not start ffmpeg");
            return;
        }
        run = Run::Running;
    }

    void discardOutput() {
        exporter.stop();
        if (!outPath.empty() && unlink(outPath.c_str()) != 0 && errno != ENOENT) std::fprintf(stderr, "end4pC-tools: cannot remove %s\n", outPath.c_str());
    }

    void cancelExport() {
        if (run != Run::Running) return;
        discardOutput();
        run = Run::Idle;
        setMessage(i18n::tr("Canceled"), false);
    }

    void parseExportLines() {
        size_t nl;
        while ((nl = exporter.out.find('\n')) != std::string::npos) {
            const std::string line = exporter.out.substr(0, nl);
            exporter.out.erase(0, nl + 1);
            const size_t eq = line.find('=');
            const bool kv = eq != std::string::npos && line.find(' ') > eq;
            if (!kv) { if (!line.empty()) errLine = line; continue; }
            if (line.compare(0, eq, "out_time_us") == 0 || line.compare(0, eq, "out_time_ms") == 0) {
                char* e = nullptr;
                const double us = std::strtod(line.c_str() + eq + 1, &e);
                if (e != line.c_str() + eq + 1 && runLen > 0) progress = std::clamp(us / 1e6 / runLen, 0.0, 1.0);
            }
        }
    }

    void exportDone() {
        if (exporter.code == 0 && vid::fileSize(outPath) > 0) {
            run = Run::Done;
            progress = 1;
            outSize = vid::fileSize(outPath);
            return;
        }
        if (unlink(outPath.c_str()) != 0 && errno != ENOENT) std::fprintf(stderr, "end4pC-tools: cannot remove %s\n", outPath.c_str());
        run = Run::Failed;
        if (exporter.code == 127) errLine = i18n::tr("ffmpeg not found");
        else if (errLine.empty()) errLine = i18n::tr("Export failed");
    }

    bool tick() override {
        bool changed = false;
        if (picker.pump() && picker.exited) {
            if (picker.code == 0) openFile(vid::pathFromText(picker.out));
            changed = true;
        }
        if (paster.pump() && paster.exited) {
            const std::string p = paster.code == 0 ? vid::pathFromText(paster.out) : "";
            if (p.empty()) setMessage(i18n::tr("The clipboard has no file path"), true);
            else openFile(p);
            changed = true;
        }
        if (probe.pump() && probe.exited) { probeDone(); changed = true; }
        if (exporter.pump()) {
            parseExportLines();
            if (exporter.exited) exportDone();
            changed = true;
        }
        if (player.pump() && player.exited && player.code != 0 && player.code != 4) { setMessage(i18n::tr("mpv could not play this file"), true); changed = true; }
        for (size_t i = 0; i < helpers.size();) {
            if (helpers[i]->pump() && helpers[i]->exited) helpers.erase(helpers.begin() + static_cast<long>(i));
            else i++;
        }
        if (stage == Stage::Ready) changed |= media.tick();
        if (!message.empty() && Clock::now() - messageAt > std::chrono::milliseconds(kMessageMs)) { message.clear(); changed = true; }
        return changed;
    }
    bool animating() const override {
        return probe.running() || paster.running() || drag != kNoDrag || (stage == Stage::Ready && media.busy());
    }

    double timeAt(double x) const { return std::clamp((x - strip.x) / std::max(1.0, strip.w), 0.0, 1.0) * info.dur; }
    double xAt(double t) const { return strip.x + t / std::max(info.dur, 0.001) * strip.w; }

    void setStart(double t) { start = std::clamp(t, 0.0, end - kMinRange); head = start; media.request(head); run = run == Run::Running ? run : Run::Idle; }
    void setEnd(double t) { end = std::clamp(t, start + kMinRange, info.dur); head = end; media.request(head); run = run == Run::Running ? run : Run::Idle; }
    void setHead(double t) { head = std::clamp(t, 0.0, info.dur); media.request(head); }

    Drag grab(double x) const {
        const double ds = std::abs(x - xAt(start)), de = std::abs(x - xAt(end));
        if (std::min(ds, de) <= kHandleGrab) return ds < de || (ds == de && x < xAt(start)) ? kDragStart : kDragEnd;
        return kDragHead;
    }
    void dragTo(double x) {
        const double t = timeAt(x);
        if (drag == kDragStart) setStart(t);
        else if (drag == kDragEnd) setEnd(t);
        else if (drag == kDragHead) setHead(t);
    }

    void startEdit(int which) {
        editing = which;
        editBuf = vid::fmtClock(which == 0 ? start : end);
        editFresh = true;
    }
    void commitEdit() {
        double t = 0;
        if (editing >= 0 && vid::parseClock(editBuf, t)) { if (editing == 0) setStart(t); else setEnd(t); }
        else if (editing >= 0) setMessage(i18n::tr("Use hh:mm:ss.ms"), true);
        editing = -1;
    }

    void selectOp(int o) { op = static_cast<vid::Op>(o); if (run != Run::Running) run = Run::Idle; }

    bool press(int id, double x) {
        if (id >= kOpBase && id < static_cast<int>(kOpBase) + static_cast<int>(vid::kOpCount)) { selectOp(id - kOpBase); return true; }
        if (id >= kOptBase) {
            sel[op][(id - kOptBase) / 10] = (id - kOptBase) % 10;
            if (run != Run::Running) run = Run::Idle;
            return true;
        }
        switch (id) {
            case kOpen: pickFile(); return true;
            case kPlay: play(); return true;
            case kPaste: pastePath(); return true;
            case kFolder: openFolder(run == Run::Done ? outPath : path); return true;
            case kRun: startExport(); return true;
            case kCancel: cancelExport(); return true;
            case kDismiss: run = Run::Idle; return true;
            case kStart: startEdit(0); return true;
            case kEnd: startEdit(1); return true;
            case kSetStart: setStart(head); return true;
            case kSetEnd: setEnd(head); return true;
            case kTimeline: drag = grab(x); dragTo(x); return true;
            default: return false;
        }
    }

    int hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.contains(x, y)) return it->id;
        return -1;
    }

    bool click(double x, double y, bool pressed) override {
        if (!pressed) { const bool was = drag != kNoDrag; drag = kNoDrag; return was; }
        if (editing >= 0) commitEdit();
        const int id = hitAt(x, y);
        if (id < 0) return true;
        return press(id, x);
    }
    bool motion(double x, double y) override {
        if (drag != kNoDrag) { dragTo(x); return true; }
        const int now = hitAt(x, y);
        const bool changed = now != hover;
        hover = now;
        return changed;
    }
    bool leave() override { const bool changed = hover >= 0; hover = -1; return changed; }
    bool scroll(double, double, double dy) override {
        if (stage != Stage::Ready || hover != kTimeline) return false;
        setHead(head + (dy > 0 ? 1.0 : -1.0));
        return true;
    }
    Cursor cursor(double, double) const override { return hover >= 0 || drag != kNoDrag ? Cursor::Pointer : Cursor::Default; }

    bool editKey(const KeyEvent& k) {
        if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) commitEdit();
        else if (k.sym == XKB_KEY_Escape) editing = -1;
        else if (k.sym == XKB_KEY_BackSpace) { if (editFresh) editBuf.clear(); else if (!editBuf.empty()) editBuf.pop_back(); editFresh = false; }
        else if (!k.text.empty() && k.text.find_first_not_of("0123456789:.") == std::string::npos) {
            if (editFresh) editBuf.clear();
            if (editBuf.size() < 12) editBuf += k.text;
            editFresh = false;
        }
        return true;
    }

    bool readyKey(const KeyEvent& k) {
        const double step = k.shift ? 0.1 : 1.0;
        switch (k.sym) {
            case XKB_KEY_Left: setHead(head - step); return true;
            case XKB_KEY_Right: setHead(head + step); return true;
            case XKB_KEY_space: play(); return true;
            case XKB_KEY_Return: case XKB_KEY_KP_Enter: startExport(); return true;
            case XKB_KEY_bracketleft: case XKB_KEY_i: setStart(head); return true;
            case XKB_KEY_bracketright: case XKB_KEY_o: setEnd(head); return true;
            case XKB_KEY_Escape: if (run == Run::Running) { cancelExport(); return true; } return false;
            default: return false;
        }
    }

    bool key(const KeyEvent& k) override {
        if (editing >= 0) return editKey(k);
        if (k.ctrl && (k.sym == XKB_KEY_v || k.sym == XKB_KEY_V)) { pastePath(); return true; }
        if (k.ctrl && (k.sym == XKB_KEY_o || k.sym == XKB_KEY_O)) { pickFile(); return true; }
        if (k.ctrl || k.alt) return false;
        if (stage == Stage::Ready) return readyKey(k);
        if (stage == Stage::Empty && (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter)) { pickFile(); return true; }
        return false;
    }

    void addHit(Rect r, int id) { hits.push_back({r, id}); }
    bool hot(int id) const { return hover == id; }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        if (stage == Stage::Ready) drawEditor(cr, r);
        else drawEmpty(cr, r);
    }

    void drawEmpty(cairo_t* cr, Rect r) {
        vid::card(cr, r, g.color("primary_container"));
        cairo_save(cr);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_clip(cr);
        shapePath(cr, "Cookie12Sided", r.x + r.w - 300, r.y + r.h - 260, 420);
        src(cr, g.color("on_primary_container"), 0.08);
        cairo_fill(cr);
        cairo_restore(cr);
        const Color fg = g.color("on_primary_container");
        const double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        ::icon(cr, "video-frame-cut", cx - 40, cy - 130, 80, fg);
        const bool busy = stage == Stage::Probing;
        const std::string title = vid::tr(busy ? "Reading video" : "Open a video");
        text(cr, title, cx - vid::textWidth(title, 30, true) / 2, cy - 36, 30, true, fg);
        if (!busy) drawEmptyButtons(cr, cx, cy + 30);
        if (!message.empty() && !busy) {
            const std::string m = vid::ellipsize(message, 15, false, r.w - 60);
            text(cr, m, cx - vid::textWidth(m, 15, false) / 2, cy + 104, 15, false, messageIsError ? g.color("error") : fg);
        }
    }

    void drawEmptyButtons(cairo_t* cr, double cx, double y) {
        const std::string a = vid::tr("Choose file"), b = vid::tr("Paste path");
        const double h = 48, wa = vid::pillWidth("folder-open", a, h, 22), wb = vid::pillWidth("clipboard", b, h, 22);
        const Rect ra = {cx - (wa + wb + kGap) / 2, y, wa, h}, rb = {ra.x + wa + kGap, y, wb, h};
        vid::pill(cr, ra, "folder-open", a, {g.color("primary"), g.color("on_primary"), hot(kOpen)});
        vid::pill(cr, rb, "clipboard", b, {g.color("surface_container_high"), g.color("on_surface"), hot(kPaste)});
        addHit(ra, kOpen);
        addHit(rb, kPaste);
    }

    void drawEditor(cairo_t* cr, Rect r) {
        const double hT = std::clamp(r.h * 0.23, 96.0, 130.0), hO = std::clamp(r.h * 0.32, 140.0, 180.0), hA = r.h - hT - hO - 2 * kGap;
        const double wP = std::round(r.w * 0.56);
        drawPreview(cr, {r.x, r.y, wP, hA});
        drawInfo(cr, {r.x + wP + kGap, r.y, r.w - wP - kGap, hA});
        drawTimeline(cr, {r.x, r.y + hA + kGap, r.w, hT});
        drawOps(cr, {r.x, r.y + hA + hT + 2 * kGap, r.w, hO});
    }

    void drawFrame(cairo_t* cr, Rect r) {
        cairo_surface_t* s = media.best(head);
        if (!s) { ::icon(cr, "video-frame", r.x + r.w / 2 - 24, r.y + r.h / 2 - 24, 48, g.color("on_surface_variant")); return; }
        const double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
        const double k = std::min(r.w / iw, r.h / ih);
        cairo_save(cr);
        cairo_translate(cr, r.x + (r.w - iw * k) / 2, r.y + (r.h - ih * k) / 2);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    void drawPreview(cairo_t* cr, Rect r) {
        vid::card(cr, r, g.color("surface_container_lowest"));
        cairo_save(cr);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_clip(cr);
        drawFrame(cr, r);
        cairo_restore(cr);
        const std::string t = vid::fmtClock(head);
        const double tw = vid::textWidth(t, 14, true) + 28;
        const Rect tr = {r.x + 14, r.y + r.h - 14 - 34, tw, 34};
        vid::pill(cr, tr, nullptr, t, {Color{0, 0, 0}, Color{1, 1, 1}, false});
        const std::string c = vid::tr("Play");
        const double pw = vid::pillWidth("play", c, 34, 16);
        const Rect pr = {r.x + r.w - 14 - pw, tr.y, pw, 34};
        vid::pill(cr, pr, "play", c, {g.color("primary"), g.color("on_primary"), hot(kPlay)});
        addHit(pr, kPlay);
    }

    void drawInfo(cairo_t* cr, Rect r) {
        vid::card(cr, r, g.color("surface_container_high"));
        const double pad = r.h < 170 ? 12 : 18, btn = r.h < 170 ? 30 : 36;
        const Rect fb = {r.x + r.w - pad - btn, r.y + pad, btn, btn};
        vid::pill(cr, fb, "folder-open", "", {g.color("secondary_container"), g.color("on_secondary_container"), hot(kOpen)});
        addHit(fb, kOpen);
        vid::label(cr, vid::baseName(path), r.x + pad, fb.y + btn / 2, 18, true, g.color("on_surface"), fb.x - r.x - pad - 10);
        const double ty = fb.y + btn + (r.h < 170 ? 8 : 12), th = (r.y + r.h - pad - ty - kGap) / 2, tw = (r.w - 2 * pad - kGap) / 2;
        std::string codec = info.vcodec + (info.audio ? " / " + info.acodec : "");
        const std::string vals[4] = {vid::fmtShort(info.dur), std::to_string(info.w) + " x " + std::to_string(info.h), codec, vid::humanSize(info.size)};
        const char* keys[4] = {"Duration", "Resolution", "Codec", "Size"};
        for (int i = 0; i < 4; i++) drawTile(cr, {r.x + pad + (i % 2) * (tw + kGap), ty + (i / 2) * (th + kGap), tw, th}, keys[i], vals[i]);
    }

    void drawTile(cairo_t* cr, Rect t, const char* key, const std::string& value) {
        vid::card(cr, t, g.color("surface_container_highest"), 18);
        const bool compact = t.h < 46;
        if (!compact) vid::label(cr, key, t.x + 14, t.y + t.h * 0.30, 12, false, g.color("on_surface_variant"));
        vid::label(cr, value, t.x + 14, t.y + (compact ? t.h / 2 : t.h * 0.68), compact ? 15 : 17, true, g.color("on_surface"), t.w - 28);
    }

    void drawStrip(cairo_t* cr, Rect s) {
        cairo_save(cr);
        rrect(cr, s.x, s.y, s.w, s.h, 12);
        cairo_clip(cr);
        src(cr, g.color("surface_container_highest"));
        cairo_paint(cr);
        const double cw = s.w / vid::kThumbs;
        for (int i = 0; i < vid::kThumbs; i++) {
            cairo_surface_t* t = media.thumbs[i];
            if (!t) continue;
            const double iw = cairo_image_surface_get_width(t), ih = cairo_image_surface_get_height(t);
            const double k = std::max(cw / iw, s.h / ih);
            cairo_save(cr);
            cairo_rectangle(cr, s.x + i * cw, s.y, cw + 0.5, s.h);
            cairo_clip(cr);
            cairo_translate(cr, s.x + i * cw + (cw - iw * k) / 2, s.y + (s.h - ih * k) / 2);
            cairo_scale(cr, k, k);
            cairo_set_source_surface(cr, t, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        src(cr, Color{0, 0, 0}, 0.62);
        cairo_rectangle(cr, s.x, s.y, xAt(start) - s.x, s.h);
        cairo_rectangle(cr, xAt(end), s.y, s.x + s.w - xAt(end), s.h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    void drawHandle(cairo_t* cr, double x, bool isHot) {
        const double w = 12;
        rrect(cr, x - w / 2, strip.y - 6, w, strip.h + 12, 6);
        src(cr, isHot ? mix(g.color("on_primary"), g.color("primary"), 0.2) : g.color("primary"));
        cairo_fill(cr);
        rrect(cr, x - 1, strip.y + strip.h / 2 - 9, 2, 18, 1);
        src(cr, g.color("on_primary"));
        cairo_fill(cr);
    }

    void drawTimeline(cairo_t* cr, Rect r) {
        vid::card(cr, r, g.color("surface_container"));
        const double pad = 16, fieldH = 34;
        strip = {r.x + pad + 8, r.y + 18, r.w - 2 * (pad + 8), r.h - 18 - 14 - fieldH - 8};
        drawStrip(cr, strip);
        addHit({strip.x - 12, strip.y - 8, strip.w + 24, strip.h + 16}, kTimeline);
        const double xs = xAt(start), xe = xAt(end);
        src(cr, g.color("primary"));
        cairo_rectangle(cr, xs, strip.y - 2, xe - xs, 3);
        cairo_rectangle(cr, xs, strip.y + strip.h - 1, xe - xs, 3);
        cairo_fill(cr);
        drawHandle(cr, xs, drag == kDragStart);
        drawHandle(cr, xe, drag == kDragEnd);
        const double hx = xAt(head);
        src(cr, g.color("tertiary"));
        cairo_rectangle(cr, hx - 1, strip.y - 8, 2, strip.h + 12);
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, hx, strip.y - 8, 6, 0, 2 * M_PI);
        cairo_fill(cr);
        drawFields(cr, {r.x + pad, r.y + r.h - 14 - fieldH, r.w - 2 * pad, fieldH});
    }

    void drawField(cairo_t* cr, Rect f, const char* key, int which, const std::string& value) {
        const bool edit = which >= 0 && editing == which, isHot = which >= 0 && hot(which == 0 ? kStart : kEnd);
        src(cr, isHot ? g.color("surface_bright") : g.color("surface_container_highest"));
        rrect(cr, f.x, f.y, f.w, f.h, f.h / 2);
        cairo_fill(cr);
        if (edit) { rrect(cr, f.x + 1, f.y + 1, f.w - 2, f.h - 2, f.h / 2); src(cr, g.color("primary")); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        const double lw = vid::label(cr, key, f.x + 16, f.y + f.h / 2, 12, false, g.color("on_surface_variant"));
        vid::label(cr, edit ? editBuf + "|" : value, f.x + 16 + lw + 8, f.y + f.h / 2, 15, true, g.color("on_surface"));
    }

    void drawFields(cairo_t* cr, Rect r) {
        const double vw = vid::textWidth("00:00:00.000|", 15, true);
        double x = r.x;
        const struct { const char* key; int which; double t; } fields[3] = {{"Start", 0, start}, {"End", 1, end}, {"Length", -1, end - start}};
        for (const auto& f : fields) {
            const double w = 16 + vid::textWidth(vid::tr(f.key), 12, false) + 8 + vw + 12;
            if (x + w > r.x + r.w) break;
            const Rect fr = {x, r.y, w, r.h};
            drawField(cr, fr, f.key, f.which, vid::fmtClock(f.t));
            if (f.which >= 0) addHit(fr, f.which == 0 ? kStart : kEnd);
            x += w + kGap;
        }
        drawSetButtons(cr, r, x);
    }

    void drawSetButtons(cairo_t* cr, Rect r, double minX) {
        const std::string a = vid::tr("Set start"), b = vid::tr("Set end");
        const double wb = vid::pillWidth(nullptr, b, r.h, 16), wa = vid::pillWidth(nullptr, a, r.h, 16);
        if (r.x + r.w - wb - kGap - wa < minX) return;
        const Rect rb = {r.x + r.w - wb, r.y, wb, r.h}, ra = {rb.x - kGap - wa, r.y, wa, r.h};
        vid::pill(cr, ra, nullptr, a, {g.color("secondary_container"), g.color("on_secondary_container"), hot(kSetStart)});
        vid::pill(cr, rb, nullptr, b, {g.color("secondary_container"), g.color("on_secondary_container"), hot(kSetEnd)});
        addHit(ra, kSetStart);
        addHit(rb, kSetEnd);
    }

    struct OptGroup { std::vector<const char*> labels; };
    std::vector<OptGroup> groups() const {
        switch (op) {
            case vid::kTrim: return {{{"Exact", "Fast copy"}}};
            case vid::kConvert: return {{{"MP4", "WebM", "MKV", "AV1"}}};
            case vid::kAudio: return {{{"Opus", "MP3", "M4A"}}};
            case vid::kGif: return {{{"320 px", "480 px", "640 px"}}};
            case vid::kCompress: return {{{"Small", "Medium", "High"}}, {{"Original", "1080p", "720p", "480p"}}};
            default: return {};
        }
    }

    void drawOps(cairo_t* cr, Rect r) {
        vid::card(cr, r, g.color("surface_container_low"));
        const double chipH = 38, optH = 32, actH = 42, pad = 16;
        const double rest = r.h - chipH - optH - actH;
        const double edge = std::min(14.0, rest / 4), gap = std::max(4.0, (rest - 2 * edge) / 2);
        drawOpChips(cr, {r.x + pad, r.y + edge, r.w - 2 * pad, chipH});
        drawOptions(cr, {r.x + pad, r.y + edge + chipH + gap, r.w - 2 * pad, optH});
        drawAction(cr, {r.x + pad, r.y + edge + chipH + gap + optH + gap, r.w - 2 * pad, actH});
    }

    void drawOpChips(cairo_t* cr, Rect r) {
        static const struct { const char* icon; const char* name; } ops[vid::kOpCount] = {
            {"scissors", "Trim"}, {"transfer-horizontal", "Convert"}, {"music-note", "Extract audio"},
            {"gallery", "GIF"}, {"zip-file", "Compress"}, {"muted", "Mute"}};
        double widths[vid::kOpCount], total = 0;
        for (int i = 0; i < vid::kOpCount; i++) { widths[i] = vid::pillWidth(ops[i].icon, vid::tr(ops[i].name), r.h, 16); total += widths[i]; }
        total += kGap * (vid::kOpCount - 1);
        const bool compact = total > r.w;
        double x = r.x;
        if (compact) { total = 0; for (int i = 0; i < vid::kOpCount; i++) { widths[i] = i == op ? vid::pillWidth(ops[i].icon, vid::tr(ops[i].name), r.h, 16) : r.h; total += widths[i]; } }
        const double spread = compact ? 6 : kGap;
        for (int i = 0; i < vid::kOpCount; i++) {
            const bool on = i == op;
            const std::string cap = compact && !on ? "" : vid::tr(ops[i].name);
            const Rect c = {x, r.y, widths[i], r.h};
            const Color bg = on ? g.color("primary") : g.color("surface_container_highest");
            vid::pill(cr, c, ops[i].icon, cap, {bg, on ? g.color("on_primary") : g.color("on_surface_variant"), hot(kOpBase + i)});
            addHit(c, kOpBase + i);
            x += widths[i] + spread;
        }
    }

    void drawOptions(cairo_t* cr, Rect r) {
        double x = r.x;
        const auto gs = groups();
        for (size_t gi = 0; gi < gs.size(); gi++) {
            for (size_t i = 0; i < gs[gi].labels.size(); i++) {
                const std::string cap = vid::tr(gs[gi].labels[i]);
                const double w = vid::pillWidth(nullptr, cap, r.h, 16);
                const bool on = sel[op][gi] == static_cast<int>(i);
                const int id = kOptBase + static_cast<int>(gi) * 10 + static_cast<int>(i);
                const Rect c = {x, r.y, w, r.h};
                vid::pill(cr, c, nullptr, cap, {on ? g.color("secondary_container") : g.color("surface_container"), on ? g.color("on_secondary_container") : g.color("on_surface_variant"), hot(id)});
                addHit(c, id);
                x += w + 8;
            }
            x += 14;
        }
        if (gs.empty()) vid::label(cr, "Removes the audio track", r.x, r.y + r.h / 2, 14, false, g.color("on_surface_variant"));
        if (partial()) drawRangeHint(cr, r);
    }

    void drawRangeHint(cairo_t* cr, Rect r) {
        const std::string t = vid::tr("Range") + " " + vid::fmtShort(start) + " - " + vid::fmtShort(end);
        const double w = vid::textWidth(t, 14, true);
        vid::label(cr, t, r.x + r.w - w, r.y + r.h / 2, 14, true, g.color("on_surface_variant"));
    }

    std::string previewName() const { return vid::stemOf(path) + "_edit." + vid::outExt(op, sel[op], vid::extOf(path)); }

    void drawAction(cairo_t* cr, Rect a) {
        if (run == Run::Running) drawRunning(cr, a);
        else if (run == Run::Done) drawDone(cr, a);
        else drawIdle(cr, a);
    }

    void drawIdle(cairo_t* cr, Rect a) {
        const std::string cap = vid::tr(run == Run::Failed ? "Try again" : "Export");
        const double w = vid::pillWidth("export", cap, a.h, 24);
        const Rect b = {a.x + a.w - w, a.y, w, a.h};
        vid::pill(cr, b, "export", cap, {g.color("primary"), g.color("on_primary"), hot(kRun)});
        addHit(b, kRun);
        const double maxW = b.x - a.x - 16;
        const bool bad = run == Run::Failed || (!message.empty() && messageIsError);
        std::string line = run == Run::Failed ? errLine : (!message.empty() ? message : previewName());
        ::icon(cr, bad ? "close-circle" : "file-right", a.x + 4, a.y + (a.h - 22) / 2, 22, bad ? g.color("error") : g.color("on_surface_variant"));
        vid::label(cr, line, a.x + 36, a.y + a.h / 2, 15, false, bad ? g.color("error") : g.color("on_surface_variant"), maxW - 32);
    }

    void drawRunning(cairo_t* cr, Rect a) {
        const std::string cap = vid::tr("Cancel");
        const double w = vid::pillWidth("close-circle", cap, a.h, 22);
        const Rect b = {a.x + a.w - w, a.y, w, a.h};
        vid::pill(cr, b, "close-circle", cap, {g.color("error_container"), g.color("on_error_container"), hot(kCancel)});
        addHit(b, kCancel);
        const Rect bar = {a.x, a.y, b.x - a.x - kGap, a.h};
        vid::card(cr, bar, g.color("surface_container_highest"), a.h / 2);
        cairo_save(cr);
        rrect(cr, bar.x, bar.y, bar.w, bar.h, bar.h / 2);
        cairo_clip(cr);
        src(cr, g.color("primary"));
        cairo_rectangle(cr, bar.x, bar.y, std::max(bar.h, bar.w * progress), bar.h);
        cairo_fill(cr);
        cairo_restore(cr);
        char pc[16];
        std::snprintf(pc, sizeof pc, "%d%%", static_cast<int>(progress * 100));
        const bool onFill = bar.w * progress > bar.w / 2 + 20;
        vid::label(cr, pc, bar.x + bar.w / 2 - vid::textWidth(pc, 15, true) / 2, bar.y + bar.h / 2, 15, true, onFill ? g.color("on_primary") : g.color("on_surface"));
    }

    void drawDone(cairo_t* cr, Rect a) {
        const double d = a.h;
        const Rect x = {a.x + a.w - d, a.y, d, d};
        vid::pill(cr, x, "close-circle", "", {g.color("surface_container_highest"), g.color("on_surface_variant"), hot(kDismiss)});
        addHit(x, kDismiss);
        const std::string cap = vid::tr("Open folder");
        const double w = vid::pillWidth("folder-open", cap, a.h, 22);
        const Rect b = {x.x - kGap - w, a.y, w, a.h};
        vid::pill(cr, b, "folder-open", cap, {g.color("primary"), g.color("on_primary"), hot(kFolder)});
        addHit(b, kFolder);
        ::icon(cr, "check-circle", a.x + 4, a.y + (a.h - 26) / 2, 26, g.color("primary"));
        const std::string meta = vid::humanSize(outSize);
        const double mw = vid::textWidth(meta, 15, true);
        vid::label(cr, meta, b.x - 16 - mw, a.y + a.h / 2, 15, true, g.color("on_surface"));
        vid::label(cr, vid::baseName(outPath), a.x + 40, a.y + a.h / 2, 15, false, g.color("on_surface_variant"), b.x - 16 - mw - 52);
    }
};
