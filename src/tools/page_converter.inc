}
#include <dirent.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include <cerrno>
namespace {
#include "converter_queue.inc"

struct ConverterPage final : ToolPage {
    enum Kind { kNone, kChoose, kAdd, kClear, kFormat, kQuality, kSize, kStrip, kOut, kConvert, kCancel, kOpenDir, kRow, kRemove };
    struct Hit { Rect r; Kind kind; int arg; };

    conv::Engine eng;
    conv::Proc pick, paste;
    int pickStage = 0;
    std::vector<Hit> hits;
    Kind hotKind = kNone;
    int hotArg = 0;
    bool dragging = false;
    Rect listRect, sliderRect;
    double scrollY = 0, contentH = 0;
    std::string note;
    Clock::time_point noteUntil, started = Clock::now();

    const char* name() const override { return "Image Converter"; }
    const char* icon() const override { return "gallery"; }
    void init() override { eng.probeTools(); }

    void setNote(const std::string& s) { note = s; noteUntil = Clock::now() + std::chrono::seconds(4); }

    void addPaths(const std::vector<std::string>& raw) {
        const auto files = conv::expandInputs(raw);
        if (eng.running()) { setNote("Wait for the conversion"); return; }
        if (files.empty() || eng.add(files) == 0) setNote("No supported images");
    }

    void startPick() {
        if (pick.active() || eng.running()) return;
        pickStage = 1;
        launchPicker();
    }
    void launchPicker() {
        const std::string exts = "*.png *.jpg *.jpeg *.webp *.avif *.bmp *.gif *.tif *.tiff *.heic *.heif *.jxl";
        const std::string pics = homeDir() + "/Pictures";
        const std::string dir = access(pics.c_str(), R_OK) == 0 ? pics : homeDir();
        const std::vector<std::string> args = pickStage == 1
            ? std::vector<std::string>{"kdialog", "--multiple", "--separate-output", "--title", "Choose images", "--getopenfilename", dir + "/", exts + "|Images"}
            : std::vector<std::string>{"zenity", "--file-selection", "--multiple", "--separator=\n", "--title=Choose images", "--filename=" + dir + "/", "--file-filter=Images | " + exts};
        if (!pick.start(args, false)) { pickStage = 0; setNote("No file dialog found"); }
    }
    void onPickDone() {
        if (pick.exitCode() == 127 && pickStage == 1) { pickStage = 2; launchPicker(); return; }
        if (pick.exitCode() == 127) { pickStage = 0; setNote("No file dialog found"); return; }
        pickStage = 0;
        if (pick.exitCode() == 0) addPaths(conv::parsePaths(pick.output()));
    }

    void startPaste() {
        if (paste.active()) return;
        if (!paste.start({"wl-paste", "--no-newline"}, false, 3.0)) setNote("wl-paste not found");
    }
    void onPasteDone() {
        const std::string& t = paste.output();
        if (paste.exitCode() != 0 || t.empty()) { setNote("Clipboard has no path"); return; }
        if (t.size() > 65536 || t.find('\0') != std::string::npos) { setNote("Clipboard has no path"); return; }
        addPaths(conv::parsePaths(t));
    }

    void openDir(const std::string& dir) {
        if (!dir.empty()) conv::spawnDetached({"xdg-open", dir});
    }

    bool loadOneThumb() {
        for (auto& it : eng.items) {
            if (it->thumbTried) continue;
            it->thumbTried = true;
            it->thumb = makeThumb(it->path);
            return true;
        }
        return false;
    }
    bool thumbsPending() const {
        return std::any_of(eng.items.begin(), eng.items.end(), [](const auto& i) { return !i->thumbTried; });
    }
    static cairo_surface_t* makeThumb(const std::string& path) {
        GError* err = nullptr;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 112, 112, TRUE, &err);
        if (err) g_error_free(err);
        if (!pb) return nullptr;
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), n = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
        const guchar* px = gdk_pixbuf_get_pixels(pb);
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); g_object_unref(pb); return nullptr; }
        auto* dst = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(s));
        const int ds = cairo_image_surface_get_stride(s) / 4;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const guchar* p = px + y * rs + x * n;
                const unsigned a = n == 4 ? p[3] : 255;
                dst[y * ds + x] = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
            }
        cairo_surface_mark_dirty(s);
        g_object_unref(pb);
        return s;
    }

    static void glyph(cairo_t* cr, const std::string& n, double x, double y, int size, const Color& c, bool bold = false) { ::icon(cr, n, x, y, size, c, bold); }
    bool hot(Kind k, int a = 0) const { return hotKind == k && hotArg == a; }
    bool addHit(const Rect& r, Kind k, int a = 0) { hits.push_back({r, k, a}); return hot(k, a); }

    static std::string fit(const std::string& s, double px, bool bold, double maxW) {
        double w = 0, h = 0;
        measure(s, px, bold, 0, w, h);
        if (w <= maxW) return s;
        std::string t = s;
        while (!t.empty()) {
            size_t i = t.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80) i--;
            t.erase(i);
            measure(t + "…", px, bold, 0, w, h);
            if (w <= maxW) return t + "…";
        }
        return "…";
    }
    static void centered(cairo_t* cr, const std::string& s, const Rect& r, double px, bool bold, const Color& c, double alpha = 1) {
        double w = 0, h = 0;
        measure(s, px, bold, 0, w, h);
        text(cr, s, r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, px, bold, c, 0, alpha);
    }
    void chip(cairo_t* cr, const Rect& r, const std::string& label, bool on, Kind k, int a) {
        const bool h = addHit(r, k, a);
        const Color base = g.color(on ? "secondary_container" : "surface_container_highest");
        const Color fg = g.color(on ? "on_secondary_container" : "on_surface_variant");
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        src(cr, h ? mix(fg, base, 0.12) : base);
        cairo_fill(cr);
        centered(cr, label, r, 14, on, fg);
    }
    void pill(cairo_t* cr, const Rect& r, const std::string& label, const char* ic, bool filled, Kind k, int a, bool enabled = true) {
        const bool h = enabled && addHit(r, k, a);
        const Color base = g.color(filled ? "primary" : "secondary_container");
        const Color fg = g.color(filled ? "on_primary" : "on_secondary_container");
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        src(cr, h ? mix(fg, base, 0.12) : base, enabled ? 1 : 0.4);
        cairo_fill(cr);
        double tw = 0, th = 0;
        const double px = r.h > 60 ? 20 : 15;
        measure(label, px, true, 0, tw, th);
        const int is = r.h > 60 ? 26 : 20;
        const double lead = ic ? is + (label.empty() ? 0 : 8) : 0, total = lead + tw, x0 = r.x + (r.w - total) / 2;
        if (ic) glyph(cr, ic, x0, r.y + (r.h - is) / 2, is, fg);
        text(cr, label, x0 + lead, r.y + (r.h - th) / 2, px, true, fg, 0, enabled ? 1 : 0.5);
    }
    void sectionLabel(cairo_t* cr, const std::string& s, double x, double y) {
        text(cr, s, x, y, 14, true, g.color("on_surface_variant"));
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        const double gap = 10, lw = std::floor(r.w * 0.52), ah = 84, rw = r.w - lw - gap;
        drawQueue(cr, {0, 0, lw, r.h});
        drawOptions(cr, {lw + gap, 0, rw, r.h - ah - gap});
        drawAction(cr, {lw + gap, r.h - ah, rw, ah});
        if (!note.empty()) drawNote(cr, {0, 0, lw, r.h});
    }

    void drawQueue(cairo_t* cr, Rect r) {
        if (eng.items.empty()) { drawDropZone(cr, r); return; }
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        src(cr, g.color("surface_container"));
        cairo_fill(cr);
        const std::string count = std::to_string(eng.items.size());
        text(cr, count, 24, 18, 26, true, g.color("on_surface"));
        double tw = 0, th = 0;
        measure(count, 26, true, 0, tw, th);
        text(cr, eng.items.size() == 1 ? "image" : "images", 24 + tw + 8, 18 + fontBaseline(26, true) - fontBaseline(15, false), 15, false, g.color("on_surface_variant"));
        pill(cr, {r.w - 24 - 96, 14, 96, 40}, "Add", "add-circle", false, kAdd, 0, !eng.running());
        pill(cr, {r.w - 24 - 96 - 8 - 100, 14, 100, 40}, "Clear", "trash-bin-minimalistic", false, kClear, 0, !eng.running());
        drawList(cr, {0, 64, r.w, r.h - 64});
    }

    void drawDropZone(cairo_t* cr, Rect r) {
        const bool h = addHit(r, kChoose);
        const Color fg = g.color("on_primary_container");
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        src(cr, h ? mix(fg, g.color("primary_container"), 0.08) : g.color("primary_container"));
        cairo_fill(cr);
        cairo_save(cr);
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        cairo_clip(cr);
        shapePath(cr, "Cookie12Sided", r.w - 200, r.h - 190, 320);
        src(cr, fg, 0.08);
        cairo_fill(cr);
        shapePath(cr, "Cookie9Sided", r.w / 2 - 70, r.h / 2 - 190, 140);
        src(cr, fg, 0.14);
        cairo_fill(cr);
        cairo_restore(cr);
        glyph(cr, "gallery-add", r.w / 2 - 34, r.h / 2 - 154, 68, fg, true);
        centered(cr, "Add images", {0, r.h / 2 - 30, r.w, 40}, 28, true, fg);
        centered(cr, "Paste a path with Ctrl+V", {0, r.h / 2 + 12, r.w, 24}, 15, false, fg, 0.75);
        pill(cr, {r.w / 2 - 110, r.h / 2 + 64, 220, 54}, "Choose images", "upload", true, kChoose, 0);
    }

    void drawList(cairo_t* cr, Rect area) {
        listRect = area;
        constexpr double rowH = 66;
        contentH = eng.items.size() * rowH + 8;
        scrollY = std::clamp(scrollY, 0.0, std::max(0.0, contentH - area.h));
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        for (size_t i = 0; i < eng.items.size(); i++) {
            const double y = area.y + 4 + i * rowH - scrollY;
            if (y + rowH < area.y || y > area.y + area.h) continue;
            drawRow(cr, *eng.items[i], static_cast<int>(i), {area.x + 12, y, area.w - 24, rowH - 6});
        }
        if (contentH > area.h) {
            const double th = std::max(30.0, area.h * area.h / contentH), ty = area.y + (area.h - th) * scrollY / (contentH - area.h);
            rrect(cr, area.x + area.w - 6, ty, 3, th, 1.5);
            src(cr, g.color("on_surface_variant"), 0.4);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    void drawThumb(cairo_t* cr, const conv::Item& it, double x, double y) {
        const double s = 48;
        cairo_save(cr);
        rrect(cr, x, y, s, s, 12);
        cairo_clip_preserve(cr);
        src(cr, g.color("surface_container_highest"));
        cairo_fill(cr);
        if (it.thumb) {
            const double iw = cairo_image_surface_get_width(it.thumb), ih = cairo_image_surface_get_height(it.thumb), k = std::max(s / iw, s / ih);
            cairo_translate(cr, x + (s - iw * k) / 2, y + (s - ih * k) / 2);
            cairo_scale(cr, k, k);
            cairo_set_source_surface(cr, it.thumb, 0, 0);
            cairo_paint(cr);
        } else glyph(cr, "gallery", x + 12, y + 12, 24, g.color("on_surface_variant"));
        cairo_restore(cr);
    }

    std::string sizeLine(const conv::Item& it) const {
        if (it.st == conv::St::Ok && it.after == conv::kMerged) return conv::humanSize(it.before) + "  →  PDF";
        if (it.st != conv::St::Ok) return conv::humanSize(it.before);
        const int pct = it.before > 0 ? static_cast<int>(std::lround(100.0 * (it.after - it.before) / it.before)) : 0;
        return conv::humanSize(it.before) + "  →  " + conv::humanSize(it.after) + "   " + (pct > 0 ? "+" : "") + std::to_string(pct) + "%";
    }

    void drawRow(cairo_t* cr, const conv::Item& it, int index, Rect r) {
        const bool inView = r.y + r.h / 2 >= listRect.y && r.y + r.h / 2 <= listRect.y + listRect.h;
        const bool h = inView && addHit(r, kRow, index);
        rrect(cr, r.x, r.y, r.w, r.h, 18);
        src(cr, g.color("surface_container_high"), h ? 1.0 : 0.7);
        cairo_fill(cr);
        drawThumb(cr, it, r.x + 8, r.y + 5);
        const double tx = r.x + 66, tw = r.w - 66 - 52;
        text(cr, fit(it.name, 15, true, tw), tx, r.y + 9, 15, true, g.color("on_surface"));
        const bool bad = it.st == conv::St::Fail;
        std::string sub = bad ? it.msg : sizeLine(it);
        if (it.st == conv::St::Running) sub = "Converting…";
        if (it.st == conv::St::Cancelled) sub = "Cancelled";
        text(cr, fit(sub, 13, false, tw), tx, r.y + 32, 13, false, g.color(bad ? "error" : "on_surface_variant"));
        drawStatus(cr, it, index, {r.x + r.w - 44, r.y + (r.h - 32) / 2, 32, 32}, inView);
    }

    void drawStatus(cairo_t* cr, const conv::Item& it, int index, Rect s, bool inView) {
        const double cx = s.x + s.w / 2, cy = s.y + s.h / 2;
        if (it.st == conv::St::Running) {
            const double t = std::chrono::duration<double>(Clock::now() - started).count();
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, 11, t * 6, t * 6 + 4.2);
            cairo_set_line_width(cr, 3.5);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            src(cr, g.color("primary"));
            cairo_stroke(cr);
        } else if (it.st == conv::St::Ok) glyph(cr, hot(kRow, index) ? "folder-open" : "check-circle", s.x + 4, s.y + 4, 24, g.color("primary"), true);
        else if (it.st == conv::St::Fail) glyph(cr, "danger-triangle", s.x + 4, s.y + 4, 24, g.color("error"), true);
        else if (!eng.running()) {
            const bool h = inView && addHit(s, kRemove, index);
            glyph(cr, "close-circle", s.x + 4, s.y + 4, 24, g.color(h ? "error" : "on_surface_variant"));
        }
    }

    void drawNote(cairo_t* cr, Rect r) {
        double tw = 0, th = 0;
        measure(note, 15, true, 0, tw, th);
        const double w = tw + 40, x = r.x + (r.w - w) / 2, y = r.h - 62;
        rrect(cr, x, y, w, 40, 20);
        src(cr, g.color("error_container"));
        cairo_fill(cr);
        text(cr, note, x + 20, y + (40 - th) / 2, 15, true, g.color("on_error_container"));
    }

    void drawOptions(cairo_t* cr, Rect r) {
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        src(cr, g.color("surface_container"));
        cairo_fill(cr);
        const double pad = 20, x = r.x + pad, w = r.w - 2 * pad;
        const double extra = std::max(0.0, (r.h - 446) / 4);
        double y = r.y + 18;
        sectionLabel(cr, "Format", x, y);
        y += 24;
        const double cw = (w - 16) / 3;
        for (int i = 0; i < conv::kFormatCount; i++)
            chip(cr, {x + (i % 3) * (cw + 8), y + (i / 3) * 42, cw, 34}, conv::kFormats[i].label, eng.opt.format == i, kFormat, i);
        y += 3 * 42 + 6 + extra;
        drawQuality(cr, x, y, w);
        y += 56 + extra;
        sectionLabel(cr, "Max size", x, y);
        y += 24;
        const double sw = (w - 24) / 4;
        for (int i = 0; i < 4; i++)
            chip(cr, {x + i * (sw + 8), y, sw, 34}, i == 0 ? "Original" : std::to_string(conv::kSizeSteps[i]), eng.opt.size == i, kSize, i);
        y += 34 + 14 + extra;
        sectionLabel(cr, "Output", x, y);
        y += 24;
        const double ow = (w - 8) / 2;
        chip(cr, {x, y, ow, 34}, "Next to source", eng.opt.nextToSource, kOut, 0);
        chip(cr, {x + ow + 8, y, ow, 34}, "Converted folder", !eng.opt.nextToSource, kOut, 1);
        y += 34 + 14 + extra;
        drawToggle(cr, "Strip metadata", {x, y, w, 34}, eng.opt.strip);
    }

    void drawQuality(cairo_t* cr, double x, double y, double w) {
        const bool lossy = conv::kFormats[eng.opt.format].lossy;
        sectionLabel(cr, "Quality", x, y);
        const std::string val = lossy ? std::to_string(eng.opt.quality) : "Lossless";
        double tw = 0, th = 0;
        measure(val, 14, true, 0, tw, th);
        text(cr, val, x + w - tw, y, 14, true, g.color(lossy ? "primary" : "on_surface_variant"));
        sliderRect = {x, y + 34, w, 8};
        const double frac = (eng.opt.quality - 1) / 99.0, a = lossy ? 1.0 : 0.35;
        const Rect t = sliderRect;
        if (lossy) addHit({t.x - 8, t.y - 12, t.w + 16, 32}, kQuality, 0);
        rrect(cr, t.x, t.y, t.w, t.h, 4);
        src(cr, g.color("surface_container_highest"));
        cairo_fill(cr);
        rrect(cr, t.x, t.y, std::max(8.0, t.w * frac), t.h, 4);
        src(cr, g.color("primary"), a);
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, t.x + t.w * frac, t.y + 4, hot(kQuality) || dragging ? 11 : 9, 0, 2 * M_PI);
        src(cr, g.color("primary"), a);
        cairo_fill(cr);
    }

    void drawToggle(cairo_t* cr, const std::string& label, Rect r, bool on) {
        const bool h = addHit(r, kStrip);
        text(cr, label, r.x, r.y + 7, 15, true, g.color("on_surface"), 0, h ? 1 : 0.9);
        const Rect sw{r.x + r.w - 56, r.y + 3, 56, 28};
        rrect(cr, sw.x, sw.y, sw.w, sw.h, 14);
        src(cr, g.color(on ? "primary" : "surface_container_highest"));
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, on ? sw.x + sw.w - 14 : sw.x + 14, sw.y + 14, on ? 10 : 8, 0, 2 * M_PI);
        src(cr, g.color(on ? "on_primary" : "outline"));
        cairo_fill(cr);
    }

    void drawAction(cairo_t* cr, Rect r) {
        if (eng.running()) { drawProgress(cr, r); return; }
        if (eng.finished()) { drawSummary(cr, r); return; }
        const bool can = !eng.items.empty() && !eng.toolsMissing();
        const std::string label = eng.toolsMissing() ? "magick is not installed" : "Convert";
        pill(cr, r, label, "transfer-horizontal", true, kConvert, 0, can);
    }

    void drawProgress(cairo_t* cr, Rect r) {
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        src(cr, g.color("surface_container"));
        cairo_fill(cr);
        const int done = eng.doneCount(), total = static_cast<int>(eng.items.size());
        const std::string cnt = std::to_string(done) + " / " + std::to_string(total);
        text(cr, cnt, r.x + 24, r.y + 14, 20, true, g.color("on_surface"));
        const Rect bar{r.x + 24, r.y + r.h - 28, r.w - 48 - 130, 10};
        rrect(cr, bar.x, bar.y, bar.w, bar.h, 5);
        src(cr, g.color("surface_container_highest"));
        cairo_fill(cr);
        const double f = total ? static_cast<double>(done) / total : 0;
        rrect(cr, bar.x, bar.y, std::max(10.0, bar.w * f), bar.h, 5);
        src(cr, g.color("primary"));
        cairo_fill(cr);
        pill(cr, {r.x + r.w - 24 - 118, r.y + (r.h - 48) / 2, 118, 48}, "Cancel", "stop-circle", false, kCancel, 0);
    }

    void drawSummary(cairo_t* cr, Rect r) {
        rrect(cr, r.x, r.y, r.w, r.h, 28);
        src(cr, g.color("surface_container"));
        cairo_fill(cr);
        long long before = 0;
        const long long after = eng.savedBytes(before);
        const double x = r.x + 22;
        const int ok = eng.okCount(), bad = eng.failCount();
        glyph(cr, "check-circle", x, r.y + 16, 24, g.color("primary"), true);
        text(cr, std::to_string(ok), x + 32, r.y + 16, 20, true, g.color("on_surface"));
        if (bad > 0) {
            glyph(cr, "danger-triangle", x + 76, r.y + 16, 24, g.color("error"), true);
            text(cr, std::to_string(bad), x + 108, r.y + 16, 20, true, g.color("on_surface"));
        }
        if (ok > 0 && before > 0) {
            const int pct = static_cast<int>(std::lround(100.0 * (after - before) / before));
            text(cr, fit(conv::humanSize(before) + " → " + conv::humanSize(after) + "  " + std::to_string(pct) + "%", 14, false, r.w - 250), x, r.y + 50, 14, false, g.color("on_surface_variant"));
        }
        const double again = 138, folder = 56, by = r.y + (r.h - 56) / 2;
        pill(cr, {r.x + r.w - again - 16, by, again, 56}, "Again", "restart", true, kConvert, 0);
        pill(cr, {r.x + r.w - again - 16 - 8 - folder, by, folder, 56}, "", "folder-open", false, kOpenDir, 0, !eng.lastDir().empty());
    }

    Hit* hitAt(double x, double y) {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (!it->r.contains(x, y)) continue;
            const bool inList = it->kind == kRow || it->kind == kRemove;
            if (!inList || listRect.contains(x, y)) return &*it;
        }
        return nullptr;
    }
    void setQuality(double x) {
        const double f = std::clamp((x - sliderRect.x) / sliderRect.w, 0.0, 1.0);
        eng.opt.quality = 1 + static_cast<int>(std::lround(f * 99));
    }

    bool motion(double x, double y) override {
        if (dragging) { setQuality(x); return true; }
        const Hit* h = hitAt(x, y);
        const Kind k = h ? h->kind : kNone;
        const int a = h ? h->arg : 0;
        const bool changed = k != hotKind || a != hotArg;
        hotKind = k;
        hotArg = a;
        return changed;
    }
    bool leave() override { const bool c = hotKind != kNone; hotKind = kNone; return c; }
    Cursor cursor(double x, double y) const override {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->r.contains(x, y)) return Cursor::Pointer;
        return Cursor::Default;
    }
    bool scroll(double x, double y, double dy) override {
        if (!listRect.contains(x, y)) return false;
        scrollY = std::clamp(scrollY + dy * 0.6, 0.0, std::max(0.0, contentH - listRect.h));
        return true;
    }

    bool click(double x, double y, bool press) override {
        if (!press) { const bool d = dragging; dragging = false; return d; }
        const Hit* h = hitAt(x, y);
        if (!h) return false;
        const Hit hit = *h;
        switch (hit.kind) {
            case kChoose: case kAdd: startPick(); break;
            case kClear: eng.clear(); scrollY = 0; break;
            case kFormat: eng.opt.format = hit.arg; break;
            case kSize: eng.opt.size = hit.arg; break;
            case kStrip: eng.opt.strip = !eng.opt.strip; break;
            case kOut: eng.opt.nextToSource = hit.arg == 0; break;
            case kQuality: dragging = true; setQuality(x); break;
            case kConvert: eng.start(); break;
            case kCancel: eng.cancel(); break;
            case kOpenDir: openDir(eng.lastDir()); break;
            case kRow: rowClicked(hit.arg); break;
            case kRemove: eng.remove(static_cast<size_t>(hit.arg)); break;
            default: return false;
        }
        motion(x, y);
        return true;
    }
    void rowClicked(int i) {
        if (i < 0 || static_cast<size_t>(i) >= eng.items.size()) return;
        const conv::Item& it = *eng.items[static_cast<size_t>(i)];
        if (it.st == conv::St::Ok) openDir(conv::dirOf(it.out));
    }

    bool key(const KeyEvent& k) override {
        if (k.ctrl && (k.sym == XKB_KEY_v || k.sym == XKB_KEY_V)) { startPaste(); return true; }
        if (k.ctrl && (k.sym == XKB_KEY_o || k.sym == XKB_KEY_O)) { startPick(); return true; }
        if (k.sym == XKB_KEY_Return && !eng.running() && !eng.items.empty() && !eng.toolsMissing()) { eng.start(); return true; }
        if (k.sym == XKB_KEY_Escape && eng.running()) { eng.cancel(); return true; }
        return false;
    }

    bool tick() override {
        bool changed = eng.tick();
        if (pick.finished()) { onPickDone(); changed = true; }
        if (paste.finished()) { onPasteDone(); changed = true; }
        if (!note.empty() && Clock::now() > noteUntil) { note.clear(); changed = true; }
        return loadOneThumb() || changed;
    }
    bool animating() const override { return eng.running() || thumbsPending(); }
};
