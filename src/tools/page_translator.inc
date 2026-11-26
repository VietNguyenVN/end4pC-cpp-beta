// Tools page: Translator. Included by src/tools.cpp (inside its anonymous namespace).
// Two big cards (an editable source and a read-only result), language dropdowns, swap, copy, paste, clear and a history card.
// Backend: `trans -b -s SRC -t TGT -- TEXT` (TEXT is one argv element, no shell). Files: translator_util.inc (child process,
// JSON, UTF-8), translator_editor.inc (the text editor).
#include "translator_util.inc"
#include "translator_editor.inc"

struct TrLang { const char* code; const char* name; };
const TrLang kTrLangs[] = {
    {"auto", "Auto"}, {"en", "English"}, {"ar", "Arabic"}, {"be", "Belarusian"}, {"ca", "Catalan"}, {"cs", "Czech"}, {"de", "German"},
    {"es", "Spanish"}, {"fr", "French"}, {"gl", "Galician"}, {"he", "Hebrew"}, {"hu", "Hungarian"}, {"it", "Italian"}, {"ja", "Japanese"},
    {"ko", "Korean"}, {"nl", "Dutch"}, {"no", "Norwegian"}, {"pl", "Polish"}, {"pt", "Portuguese"}, {"ru", "Russian"}, {"sv", "Swedish"},
    {"tr", "Turkish"}, {"uk", "Ukrainian"}, {"vi", "Vietnamese"}, {"zh-CN", "Chinese (Simplified)"}, {"zh-TW", "Chinese (Traditional)"},
};
constexpr int kTrLangCount = static_cast<int>(sizeof kTrLangs / sizeof kTrLangs[0]);

int trLangIndex(const std::string& code) {
    for (int i = 0; i < kTrLangCount; i++) if (code == kTrLangs[i].code) return i;
    return -1;
}

struct TrHistory { std::string src, tgt, in, out; long long ts = 0; };

constexpr int kTrDebounceMs = 450, kTrTimeoutMs = 15000;

struct TranslatorPage final : ToolPage {
    enum class St { Idle, Waiting, Busy, Done, Failed };
    enum Id { None, SrcChip, TgtChip, Swap, Paste, Clear, Copy, Retry, HistClear, HistRow, PopItem };
    struct Hit { Rect r; int id, arg; };
    struct Hover { int id = None, arg = 0; };

    TrEditor ed;
    int srcLang = 0, tgtLang = 1;
    St st = St::Idle;
    std::string out, err, detCode, detName;
    TrJob jobT, jobD;
    Clock::time_point debounceAt, lastKey = Clock::now(), copiedUntil;
    std::string copiedLabel;
    bool lastCaret = true;

    std::vector<TrHistory> history;
    double histScroll = 0, tgtScroll = 0, histMax = 0, tgtMax = 0;
    Elastic histEl, tgtEl;
    int popup = 0;
    std::vector<Hit> hits;
    Hover hover;
    Rect srcCard, tgtCard, histCard, histList, tgtText;
    bool sourceFocused = true;
    bool dragging = false;
    Clock::time_point lastClick;
    size_t lastClickIdx = 0;

    const char* name() const override { return "Translator"; }
    const char* icon() const override { return "chat-line"; }

    static std::string dataDir() { return configDir() + "/tools"; }
    static std::string histPath() { return dataDir() + "/translator-history.json"; }
    static std::string pairPath() { return dataDir() + "/translator-state.json"; }

    void init() override {
        signal(SIGPIPE, SIG_IGN);
        std::string a, b;
        const std::string pair = readFile(pairPath());
        if (jsonValue(pair, "src", a) && trLangIndex(a) >= 0) srcLang = trLangIndex(a);
        if (jsonValue(pair, "tgt", b) && trLangIndex(b) > 0) tgtLang = trLangIndex(b);
        for (auto& o : trJsonObjects(readFile(histPath()))) {
            if (!o.count("in") || !o.count("out") || history.size() >= kTrHistoryMax) continue;
            history.push_back({o["src"], o["tgt"], o["in"], o["out"], std::atoll(o["ts"].c_str())});
        }
    }
    void savePair() const {
        trWriteAtomic(dataDir(), "translator-state.json", "{\"src\": " + trJsonEscape(kTrLangs[srcLang].code) + ", \"tgt\": " + trJsonEscape(kTrLangs[tgtLang].code) + "}\n");
    }
    void saveHistory() const {
        std::string j = "[\n";
        for (size_t i = 0; i < history.size(); i++) {
            const TrHistory& h = history[i];
            j += " {\"src\": " + trJsonEscape(h.src) + ", \"tgt\": " + trJsonEscape(h.tgt) + ", \"in\": " + trJsonEscape(h.in) + ", \"out\": " + trJsonEscape(h.out) +
                 ", \"ts\": " + std::to_string(h.ts) + (i + 1 < history.size() ? "},\n" : "}\n");
        }
        trWriteAtomic(dataDir(), "translator-history.json", j + "]\n");
    }

    void cancelJobs() { jobT.kill(); jobD.kill(); }

    void edited() {
        lastKey = Clock::now();
        cancelJobs();
        err.clear();
        tgtScroll = 0;
        if (trTrim(ed.s).empty()) { out.clear(); detCode.clear(); detName.clear(); st = St::Idle; return; }
        st = St::Waiting;
        debounceAt = Clock::now() + std::chrono::milliseconds(kTrDebounceMs);
    }

    static std::string textArg(const std::string& t) { return t.compare(0, 5, "file:") == 0 ? " " + t : t; }

    void startJobs() {
        cancelJobs();
        const std::string text = ed.s;
        if (trTrim(text).empty()) { st = St::Idle; return; }
        const int timeout = kTrTimeoutMs + static_cast<int>(text.size()) * 5;
        if (!jobT.start({"trans", "-b", "-s", kTrLangs[srcLang].code, "-t", kTrLangs[tgtLang].code, "--", textArg(text)}, "", timeout)) {
            fail("Could not translate (offline?)");
            return;
        }
        if (srcLang == 0 && !jobD.start({"trans", "-id", "--", textArg(trHead(text, 300))}, "", timeout)) detCode.clear();
        st = St::Busy;
    }

    void fail(const std::string& why) { err = why; st = St::Failed; }

    void finishTranslate() {
        const std::string o = trTrim(trStripAnsi(jobT.out));
        if (jobT.timedOut) { fail("Timed out"); return; }
        if (!jobT.ok || o.empty()) { fail("Could not translate (offline?)"); return; }
        out = o;
        tgtScroll = 0;
        st = St::Done;
        addHistory();
    }

    void finishDetect() {
        detCode.clear();
        detName.clear();
        if (!jobD.ok) return;
        std::stringstream ss(trStripAnsi(jobD.out));
        std::string line;
        while (std::getline(ss, line)) {
            std::stringstream ls(line);
            std::string key, val;
            ls >> key >> val;
            if (key == "Code") detCode = val;
            else if (key == "Name") detName = trTrim(line.substr(line.find("Name") + 4));
        }
        const int i = trLangIndex(detCode);
        if (i > 0) detName = kTrLangs[i].name;
    }

    void addHistory() {
        TrHistory h{kTrLangs[srcLang].code, kTrLangs[tgtLang].code, ed.s, out, static_cast<long long>(time(nullptr))};
        history.erase(std::remove_if(history.begin(), history.end(), [&](const TrHistory& e) { return e.in == h.in && e.src == h.src && e.tgt == h.tgt; }), history.end());
        history.insert(history.begin(), h);
        if (history.size() > kTrHistoryMax) history.resize(kTrHistoryMax);
        saveHistory();
    }

    void setLang(bool source, int idx) {
        (source ? srcLang : tgtLang) = idx;
        popup = 0;
        savePair();
        detCode.clear();
        detName.clear();
        if (!trTrim(ed.s).empty()) startJobs();
    }

    void swapLangs() {
        const int fromDetect = srcLang == 0 ? trLangIndex(detCode) : srcLang;
        if (fromDetect <= 0) return;
        const bool moveOut = st == St::Done && !out.empty();
        srcLang = tgtLang;
        tgtLang = fromDetect;
        if (moveOut) ed.set(out);
        out.clear();
        detCode.clear();
        detName.clear();
        savePair();
        if (trTrim(ed.s).empty()) st = St::Idle; else startJobs();
    }

    void restore(const TrHistory& h) {
        cancelJobs();
        if (trLangIndex(h.src) >= 0) srcLang = trLangIndex(h.src);
        if (trLangIndex(h.tgt) > 0) tgtLang = trLangIndex(h.tgt);
        ed.set(h.in);
        out = h.out;
        err.clear();
        detCode.clear();
        detName.clear();
        tgtScroll = 0;
        st = St::Done;
        savePair();
    }

    void pasteClipboard() {
        std::string t;
        if (!trRunSync({"wl-paste", "-n", "-t", "text"}, "", &t, 1500) || t.empty()) { flash("Nothing to paste"); return; }
        if (ed.insert(t)) edited();
    }
    void copyText(const std::string& t) {
        if (t.empty()) return;
        flash(trRunSync({"wl-copy"}, t, nullptr, 2000) ? "Copied" : "Copy failed");
    }
    void flash(const char* label) { copiedLabel = label; copiedUntil = Clock::now() + std::chrono::milliseconds(1600); }
    bool flashing() const { return Clock::now() < copiedUntil; }

    bool hot(Rect r, int id, int arg = 0) {
        hits.push_back({r, id, arg});
        return hover.id == id && hover.arg == arg;
    }
    static double pillWidth(const std::string& label, bool withIcon) {
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        return 28 + (withIcon ? 26 : 0) + tw;
    }
    static void pill(cairo_t* cr, Rect r, const std::string& label, const char* ic, const Color& bg, const Color& fg, bool hov, double alpha = 1) {
        src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
        rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        cairo_fill(cr);
        double tw = 0, th = 0;
        measure(label, 15, true, 0, tw, th);
        double x = r.x + (r.w - (ic ? 26 : 0) - tw) / 2;
        if (ic) { ::icon(cr, ic, x, r.y + (r.h - 18) / 2, 18, fg); x += 26; }
        text(cr, label, x, r.y + (r.h - th) / 2, 15, true, fg, 0, alpha);
    }
    void roundButton(cairo_t* cr, Rect r, const char* ic, const Color& bg, const Color& fg, bool hov, double alpha = 1) {
        src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
        cairo_new_sub_path(cr);
        cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
        cairo_fill(cr);
        ::icon(cr, ic, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, fg, false);
        src(cr, fg, 0);
    }
    Rect drawChip(cairo_t* cr, const Rect& card, int lang, bool source, double x, const Color& bg, const Color& fg) {
        double tw = 0, th = 0;
        measure(kTrLangs[lang].name, 16, true, 0, tw, th);
        const Rect r{x, card.y + 14, tw + 66, 44};
        const bool hov = hot(r, source ? SrcChip : TgtChip);
        src(cr, hov ? mix(fg, bg, 0.12) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        cairo_fill(cr);
        text(cr, kTrLangs[lang].name, r.x + 20, r.y + (r.h - th) / 2, 16, true, fg);
        ::icon(cr, "alt-arrow-down", r.x + r.w - 38, r.y + 10, 24, fg);
        return r;
    }
    static std::string upper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }

    void drawSource(cairo_t* cr) {
        const Rect c = srcCard;
        src(cr, g.color("surface_container_high"));
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        const Color fg = g.color("on_surface"), bg = g.color("surface_container_highest");
        const Rect chip = drawChip(cr, c, srcLang, true, c.x + 16, bg, fg);
        drawDetected(cr, c, chip);
        ed.box = {c.x + 22, c.y + 74, c.w - 44, c.h - 74 - 72};
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        lastCaret = caretOn;
        if (ed.s.empty() && ed.preedit.empty()) text(cr, "Type or paste text", ed.box.x, ed.box.y, kEdPx, false, g.color("on_surface_variant"), ed.box.w, 0.7);
        ed.draw(cr, ed.box, fg, g.color("primary"), sourceFocused && (caretOn || !ed.preedit.empty()) && popup == 0);
        drawSourceFooter(cr, c);
    }
    void drawDetected(cairo_t* cr, const Rect& c, const Rect& chip) {
        if (srcLang != 0 || detName.empty()) return;
        const double x = chip.x + chip.w + 12, room = c.x + c.w - 30 - x;
        std::string label = "Detected: " + detName;
        double tw = 0, th = 0;
        measure(label, 14, false, 0, tw, th);
        if (tw > room) { label = detName; measure(label, 14, false, 0, tw, th); }
        if (tw <= room) text(cr, label, x, chip.y + (chip.h - th) / 2, 14, false, g.color("on_surface_variant"));
    }
    void drawSourceFooter(cairo_t* cr, const Rect& c) {
        const Color bg = g.color("secondary_container"), fg = g.color("on_secondary_container");
        const double y = c.y + c.h - 60;
        Rect p{c.x + 16, y, pillWidth("Paste", true), 44};
        pill(cr, p, "Paste", "clipboard", bg, fg, hot(p, Paste));
        Rect k{p.x + p.w + 8, y, pillWidth("Clear", true), 44};
        pill(cr, k, "Clear", "eraser", bg, fg, hot(k, Clear), ed.s.empty() ? 0.45 : 1);
        if (ed.s.empty()) return;
        const std::string n = std::to_string(ed.s.size()) + "/" + std::to_string(kTrMaxText);
        double tw = 0, th = 0;
        measure(n, 13, false, 0, tw, th);
        const double x = c.x + c.w - 20 - tw;
        if (x > k.x + k.w + 10) trLine(cr, n, x, y + (44 - th) / 2, tw + 4, 13, false, g.color("on_surface_variant"), 0.7);
    }

    void drawTarget(cairo_t* cr) {
        const Rect c = tgtCard;
        const Color base = g.color("primary_container"), fg = g.color("on_primary_container");
        src(cr, base);
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        drawChip(cr, c, tgtLang, false, c.x + 40, mix(fg, base, 0.14), fg);
        if (st == St::Busy || st == St::Waiting) drawSpinner(cr, c.x + c.w - 36, c.y + 36, fg);
        tgtText = {c.x + 22, c.y + 74, c.w - 44, c.h - 74 - 72};
        if (st == St::Failed) drawError(cr, fg);
        else if (out.empty()) text(cr, "Translation", tgtText.x, tgtText.y, kEdPx, false, fg, tgtText.w, 0.5);
        else {
            tgtScroll = std::clamp(tgtScroll, 0.0, tgtMax);  // (with the last size: no empty band for a frame)
            const double h = trBlock(cr, out, tgtText, tgtScroll + tgtEl.over, fg, st == St::Done ? 1 : 0.5);
            tgtMax = std::max(0.0, h - tgtText.h);
            tgtScroll = std::clamp(tgtScroll, 0.0, tgtMax);
        }
        drawTargetFooter(cr, c);
    }
    static void drawSpinner(cairo_t* cr, double cx, double cy, const Color& fg) {
        const double a = std::chrono::duration<double>(Clock::now().time_since_epoch()).count() * 6;
        cairo_set_line_width(cr, 3.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, 10, a, a + 4.2);
        src(cr, fg, 0.8);
        cairo_stroke(cr);
    }
    void drawError(cairo_t* cr, const Color& fg) {
        const Color bg = g.color("primary"), onbg = g.color("on_primary");
        const double cx = tgtText.x + tgtText.w / 2;
        double y = tgtText.y + tgtText.h / 2 - 80;
        ::icon(cr, "danger-circle", cx - 24, y, 48, g.color("error"), true);
        double tw = 0, th = 0;
        measure(err, 17, true, tgtText.w, tw, th);
        y += 62;
        text(cr, err, cx - tw / 2, y, 17, true, fg, tgtText.w);
        const Rect b{cx - pillWidth("Retry", true) / 2, y + th + 16, pillWidth("Retry", true), 44};
        pill(cr, b, "Retry", "restart", bg, onbg, hot(b, Retry));
    }
    void drawTargetFooter(cairo_t* cr, const Rect& c) {
        const bool can = !out.empty() && st != St::Failed;
        const std::string label = flashing() ? copiedLabel : "Copy";
        Rect b{c.x + 16, c.y + c.h - 60, pillWidth("Copy", true) + 16, 44};
        pill(cr, b, label, flashing() && copiedLabel == "Copied" ? "check-circle" : "copy", g.color("primary"), g.color("on_primary"), can && hot(b, Copy), can ? 1 : 0.45);
    }

    void drawSwap(cairo_t* cr) {
        const double cx = (srcCard.x + srcCard.w + tgtCard.x) / 2;
        const Rect r{cx - 24, srcCard.y + 12, 48, 48};
        const bool usable = srcLang != 0 || trLangIndex(detCode) > 0;
        src(cr, g.color("background"));
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, r.y + 24, 29, 0, 2 * M_PI);
        cairo_fill(cr);
        roundButton(cr, r, "transfer-horizontal", g.color("tertiary_container"), g.color("on_tertiary_container"), usable && hot(r, Swap), usable ? 1 : 0.5);
    }

    static constexpr double kRowH = 76, kRowGap = 8;
    void drawHistory(cairo_t* cr) {
        const Rect c = histCard;
        src(cr, g.color("surface_container"));
        rrect(cr, c.x, c.y, c.w, c.h, 28);
        cairo_fill(cr);
        ::icon(cr, "history", c.x + 20, c.y + 22, 26, g.color("on_surface_variant"));
        text(cr, "History", c.x + 54, c.y + 22, 20, true, g.color("on_surface"));
        const Rect t{c.x + c.w - 60, c.y + 14, 44, 44};
        roundButton(cr, t, "trash-bin-minimalistic", g.color("surface_container_high"), g.color("on_surface_variant"), !history.empty() && hot(t, HistClear), history.empty() ? 0.45 : 1);
        histList = {c.x + 10, c.y + 70, c.w - 20, c.h - 80};
        if (history.empty()) { drawHistoryEmpty(cr); return; }
        const double total = history.size() * (kRowH + kRowGap) - kRowGap;
        histMax = std::max(0.0, total - histList.h);
        histScroll = std::clamp(histScroll, 0.0, histMax);
        cairo_save(cr);
        cairo_rectangle(cr, histList.x, histList.y, histList.w, histList.h);
        cairo_clip(cr);
        for (size_t i = 0; i < history.size(); i++) drawHistoryRow(cr, i);
        cairo_restore(cr);
    }
    void drawHistoryEmpty(cairo_t* cr) {
        double tw = 0, th = 0;
        measure("No history yet", 15, false, 0, tw, th);
        ::icon(cr, "history-2", histList.x + histList.w / 2 - 24, histList.y + histList.h / 2 - 50, 48, g.color("on_surface_variant"));
        text(cr, "No history yet", histList.x + (histList.w - tw) / 2, histList.y + histList.h / 2 + 12, 15, false, g.color("on_surface_variant"), 0, 0.8);
    }
    void drawHistoryRow(cairo_t* cr, size_t i) {
        const TrHistory& h = history[i];
        const Rect r{histList.x, histList.y + i * (kRowH + kRowGap) - (histScroll + histEl.over), histList.w, kRowH};
        if (r.y + r.h < histList.y || r.y > histList.y + histList.h) return;
        const bool hov = hot(r, HistRow, static_cast<int>(i));
        const Color bg = g.color("surface_container_high");
        src(cr, hov ? mix(g.color("on_surface"), bg, 0.08) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, 20);
        cairo_fill(cr);
        const double w = r.w - 28;
        trLine(cr, upper(h.src) + "  >  " + upper(h.tgt), r.x + 14, r.y + 8, w, 12, true, g.color("on_surface_variant"), 0.8);
        trLine(cr, h.in, r.x + 14, r.y + 27, w, 15, false, g.color("on_surface"));
        trLine(cr, h.out, r.x + 14, r.y + 49, w, 15, true, g.color("primary"));
    }

    std::vector<int> popupItems() const {
        std::vector<int> v;
        for (int i = popup == 1 ? 0 : 1; i < kTrLangCount; i++) v.push_back(i);
        return v;
    }
    void drawPopup(cairo_t* cr) {
        if (!popup) return;
        const Rect card = popup == 1 ? srcCard : tgtCard;
        const Rect area{card.x + 10, card.y + 66, card.w - 20, card.h - 76};
        src(cr, g.color("surface_container_highest"));
        rrect(cr, area.x, area.y, area.w, area.h, 22);
        cairo_fill(cr);
        const auto items = popupItems();
        const int cols = 2, rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        const double gap = 5, pad = 10, cw = (area.w - 2 * pad - gap) / cols, ch = std::min(40.0, (area.h - 2 * pad - (rows - 1) * gap) / rows);
        const int cur = popup == 1 ? srcLang : tgtLang;
        for (size_t k = 0; k < items.size(); k++) {
            const Rect r{area.x + pad + (k % cols) * (cw + gap), area.y + pad + (k / cols) * (ch + gap), cw, ch};
            const bool on = items[k] == cur;
            const Color bg = on ? g.color("primary") : g.color("surface_container_high"), fg = on ? g.color("on_primary") : g.color("on_surface");
            pill(cr, r, kTrLangs[items[k]].name, nullptr, bg, fg, hot(r, PopItem, items[k]));
        }
    }

    void draw(cairo_t* cr, Rect r) override {
        hits.clear();
        constexpr double gap = 10;
        const double histW = std::clamp(r.w * 0.27, 210.0, 270.0), paneW = (r.w - histW - 2 * gap) / 2;
        srcCard = {0, 0, paneW, r.h};
        tgtCard = {paneW + gap, 0, paneW, r.h};
        histCard = {2 * (paneW + gap), 0, histW, r.h};
        drawSource(cr);
        drawTarget(cr);
        drawHistory(cr);
        drawSwap(cr);
        drawPopup(cr);
    }

    Hover hitAt(double x, double y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (popup && it->id != PopItem) continue;
            if (!it->r.contains(x, y)) continue;
            if (it->id == HistRow && !histList.contains(x, y)) continue;
            return {it->id, it->arg};
        }
        return {};
    }
    bool motion(double x, double y) override {
        const Hover h = hitAt(x, y);
        const bool changed = h.id != hover.id || h.arg != hover.arg;
        hover = h;
        if (dragging) { ed.moveTo(ed.indexAt(x, y), true); return true; }
        return changed;
    }
    bool leave() override {
        const bool changed = hover.id != None;
        hover = {};
        return changed;
    }
    Cursor cursor(double x, double y) const override {
        if (hitAt(x, y).id != None) return Cursor::Pointer;
        return !popup && ed.box.contains(x, y) ? Cursor::Text : Cursor::Default;
    }

    bool act(const Hover& h) {
        switch (h.id) {
            case SrcChip: popup = popup == 1 ? 0 : 1; break;
            case TgtChip: popup = popup == 2 ? 0 : 2; break;
            case PopItem: setLang(popup == 1, h.arg); break;
            case Swap: swapLangs(); break;
            case Paste: pasteClipboard(); break;
            case Clear: if (ed.s.empty()) return false; ed.set(""); edited(); break;
            case Copy: copyText(out); break;
            case Retry: startJobs(); break;
            case HistClear: history.clear(); saveHistory(); break;
            case HistRow: restore(history[static_cast<size_t>(h.arg)]); break;
            default: return false;
        }
        return true;
    }

    void placeCaret(double x, double y) {
        const size_t i = ed.indexAt(x, y);
        const auto now = Clock::now();
        const bool twice = i == lastClickIdx && now - lastClick < std::chrono::milliseconds(400);
        lastClick = now;
        lastClickIdx = i;
        lastKey = now;
        dragging = true;
        if (!twice) { ed.moveTo(i, false); return; }
        size_t a = i, b = i;
        while (a > 0 && !std::isspace(static_cast<unsigned char>(ed.s[a - 1]))) a--;
        while (b < ed.s.size() && !std::isspace(static_cast<unsigned char>(ed.s[b]))) b++;
        ed.moveTo(a, false);
        ed.moveTo(b, true);
    }

    bool click(double x, double y, bool press) override {
        if (!press) { const bool was = dragging; dragging = false; return was; }
        const Hover h = hitAt(x, y);
        sourceFocused = !popup && h.id == None && ed.box.contains(x, y);
        if (popup && h.id != PopItem) { popup = 0; return true; }
        if (h.id != None) return act(h);
        if (ed.box.contains(x, y)) { placeCaret(x, y); return true; }
        return false;
    }

    bool scroll(double x, double y, double dy) override {
        const double step = dy * 3;
        if (histCard.contains(x, y)) elasticWheel(histEl, histScroll, histMax, step);
        else if (tgtCard.contains(x, y)) elasticWheel(tgtEl, tgtScroll, tgtMax, step);
        else if (srcCard.contains(x, y)) ed.scroll += step;
        else return false;
        return true;
    }

    bool ctrlKey(const KeyEvent& k) {
        if (k.sym == XKB_KEY_a) { ed.moveTo(0, false); ed.moveTo(ed.s.size(), true); }
        else if (k.sym == XKB_KEY_c) copyText(ed.hasSel() ? ed.selected() : out);
        else if (k.sym == XKB_KEY_x) { copyText(ed.selected()); if (ed.insert("")) edited(); }
        else if (k.sym == XKB_KEY_v) pasteClipboard();
        else return false;
        return true;
    }

    bool editKey(const KeyEvent& k) {
        switch (k.sym) {
            case XKB_KEY_BackSpace: if (ed.erase(false)) edited(); return true;
            case XKB_KEY_Delete: if (ed.erase(true)) edited(); return true;
            case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (ed.insert("\n")) edited(); return true;
            case XKB_KEY_Left: ed.moveTo(!k.shift && ed.hasSel() ? ed.selBegin() : trPrevChar(ed.s, ed.cur), k.shift); return true;
            case XKB_KEY_Right: ed.moveTo(!k.shift && ed.hasSel() ? ed.selEnd() : trNextChar(ed.s, ed.cur), k.shift); return true;
            case XKB_KEY_Up: ed.vertical(-1, k.shift); return true;
            case XKB_KEY_Down: ed.vertical(1, k.shift); return true;
            case XKB_KEY_Home: ed.home(k.shift); return true;
            case XKB_KEY_End: ed.end(k.shift); return true;
            default: break;
        }
        if (k.text.empty() || k.alt) return false;
        if (ed.insert(k.text)) edited();
        return true;
    }

    bool key(const KeyEvent& k) override {
        if (popup && k.sym == XKB_KEY_Escape) { popup = 0; return true; }
        lastKey = Clock::now();
        popup = 0;
        if (!k.ctrl && !k.alt && !k.text.empty()) sourceFocused = true;
        const bool used = k.ctrl ? ctrlKey(k) : editKey(k);
        return used;
    }

    bool imeContext(ImeContext& context) override {
        if (!sourceFocused || popup || ed.box.w <= 0) return false;
        context = {ed.s, ed.cur, ed.anc, ed.imeCaret, &ed.s};
        return true;
    }

    bool imeInlinePreedit() const override { return true; }

    void imeCancel() override { ed.preedit.clear(); }

    void imeEdit(const ImeEdit& edit) override {
        bool changed = ed.deleteSurrounding(edit.before, edit.after);
        if (!edit.commit.empty()) changed |= ed.insert(edit.commit);
        if (changed) edited();
        ed.preedit = edit.preedit;
        ed.preeditCursor = edit.preeditCursor;
        ed.reveal = true;
        lastKey = Clock::now();
    }

    bool tick() override {
        bool changed = false;
        if (st == St::Waiting && Clock::now() >= debounceAt) { startJobs(); changed = true; }
        if (jobT.running() && jobT.poll()) { finishTranslate(); changed = true; }
        if (jobD.running() && jobD.poll()) { finishDetect(); changed = true; }
        if (copiedUntil != Clock::time_point{} && !flashing()) { copiedUntil = Clock::time_point{}; changed = true; }
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        if (histEl.step() | tgtEl.step()) changed = true;
        return changed || caretOn != lastCaret;
    }
    bool animating() const override { return st == St::Waiting || st == St::Busy || histEl.over != 0 || tgtEl.over != 0; }
};
