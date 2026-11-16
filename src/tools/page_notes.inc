#include "notes_store.inc"
#include "notes_editor.inc"
#include "notes_draw.inc"
#include "notes_md.inc"
#include "../mdedit.h"
#include "notes_mdbar.inc"

namespace notes_ui {
constexpr double kGap = 12, kSearchH = 56, kTopH = 56, kCardR = 28, kFab = 64, kBtn = 36, kSaveDelayMs = 600, kChipMs = 8000;

struct Grid { int cols = 2; double cw = 0, ch = 0, top = 0, viewH = 0; };

struct NotesPage final : ToolPage {
    enum class View { List, Edit };
    enum Top { TopNone, TopBack, TopMode, TopPin, TopCopy, TopTrash };
    enum class Mode { Edit, Preview };
    struct CardDoc { std::string src; std::unique_ptr<MdDoc> doc; };

    NoteStore store;
    TextEditor ed;
    MdBar mdbar;
    Bubble tip;
    bool tipMoving = false;
    Mode mode = Mode::Edit, lastMode = Mode::Preview;
    MdDoc doc;
    unsigned docRev = 0;
    bool docOk = false;
    std::map<std::string, CardDoc> cardDocs;
    View view = View::List;
    int openIdx = -1;
    std::vector<int> order;
    std::string query;
    double W = 0, H = 0, scrollPos = 0, scrollTarget = 0, edScroll = 0, edViewH = 400;
    Elastic listEl, edEl;
    bool follow = false, blinkShown = true;
    Clock::time_point blinkBase = Clock::now();

    bool pendingSave = false;
    Clock::time_point editedAt;

    int hoverCard = -1, hoverBtn = 0;
    int hoverTop = TopNone;
    bool hoverFab = false, hoverClear = false, hoverUndo = false, dragging = false;
    Clock::time_point lastClick{};
    double lastClickX = -99, lastClickY = -99;

    bool hasTrashed = false, trashedPinned = false;
    std::string trashedId, status;
    Clock::time_point trashedAt, statusAt;

    const char* name() const override { return "Notes"; }
    const char* icon() const override { return "notes"; }
    ~NotesPage() override { flush(); }

    void init() override {
        store.importLegacy();
        store.load();
        rebuild();
    }

    void rebuild() {
        order.clear();
        const std::string q = lowered(query);
        for (size_t i = 0; i < store.notes.size(); i++)
            if (q.empty() || lowered(store.notes[i].content).find(q) != std::string::npos) order.push_back(static_cast<int>(i));
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const Note &x = store.notes[a], &y = store.notes[b];
            if (x.pinned != y.pinned) return x.pinned;
            return x.mtime != y.mtime ? x.mtime > y.mtime : x.id > y.id;
        });
        scrollTarget = std::clamp(scrollTarget, 0.0, maxScroll());
        pruneCards();
    }
    void pruneCards() {
        for (auto it = cardDocs.begin(); it != cardDocs.end();) {
            const bool alive = std::any_of(store.notes.begin(), store.notes.end(), [&](const Note& n) { return n.id == it->first; });
            it = alive ? std::next(it) : cardDocs.erase(it);
        }
    }
    void fail(const char* msg) { status = msg; statusAt = Clock::now(); }

    bool flush() {
        if (!pendingSave || openIdx < 0 || openIdx >= static_cast<int>(store.notes.size())) { pendingSave = false; return true; }
        Note& n = store.notes[openIdx];
        n.content = ed.s;
        if (n.content.empty() && access(NoteStore::pathOf(n.id).c_str(), F_OK) != 0) { pendingSave = false; return true; }
        if (!store.save(n)) {
            fail("Could not save the note");
            editedAt = Clock::now();
            return false;
        }
        pendingSave = false;
        return true;
    }
    void markEdited() {
        pendingSave = true;
        editedAt = blinkBase = Clock::now();
        follow = true;
    }

    void openNote(int idx, bool fresh = false) {
        flush();
        openIdx = idx;
        ed.set(store.notes[idx].content);
        ed.moveTo(static_cast<int>(ed.s.size()), false);
        view = View::Edit;
        mode = fresh ? Mode::Edit : lastMode;
        docOk = false;
        edScroll = 0;
        follow = true;
        blinkBase = Clock::now();
        clearHover();
    }
    void setMode(Mode m) {
        mode = lastMode = m;
        docOk = false;
        edScroll = 0;
        follow = true;
        blinkBase = Clock::now();
    }
    void closeEditor() {
        flush();
        if (openIdx >= 0 && ed.s.find_first_not_of(" \t\r\n") == std::string::npos) {
            const bool pinned = store.notes[openIdx].pinned;
            store.discardEmpty(openIdx);
            if (pinned) store.savePins();
        }
        pendingSave = false;
        openIdx = -1;
        view = View::List;
        rebuild();
        clearHover();
    }
    void newNote() {
        if (view == View::Edit) closeEditor();
        query.clear();
        rebuild();
        openNote(store.create(), true);
    }
    void togglePin(int idx) {
        store.notes[idx].pinned = !store.notes[idx].pinned;
        if (!store.savePins()) fail("Could not save the note");
        rebuild();
    }
    void deleteNote(int idx) {
        if (idx == openIdx) { flush(); pendingSave = false; }
        const Note n = store.notes[idx];
        const bool hadFile = access(NoteStore::pathOf(n.id).c_str(), F_OK) == 0;
        if (!store.trash(idx)) { fail("Could not delete the note"); return; }
        store.savePins();
        if (hadFile) { hasTrashed = true; trashedId = n.id; trashedPinned = n.pinned; trashedAt = Clock::now(); }
        openIdx = -1;
        view = View::List;
        rebuild();
        clearHover();
    }
    void undoDelete() {
        hasTrashed = false;
        if (store.restore(trashedId, trashedPinned) < 0) { fail("Could not restore the note"); return; }
        store.savePins();
        rebuild();
    }
    void copyText(const std::string& s) { if (!s.empty() && !clipboardCopy(s)) fail("Could not copy"); }
    void pasteInto(bool toQuery) {
        std::string t;
        if (!clipboardPaste(t)) return fail("Could not paste");
        if (!toQuery) { ed.insert(t); markEdited(); return; }
        query += t.substr(0, t.find('\n'));
        rebuild();
    }

    Grid grid() const {
        Grid gr;
        gr.top = kSearchH + kGap;
        gr.viewH = H - gr.top;
        gr.cols = std::clamp(static_cast<int>((W + kGap) / (250 + kGap)), 2, 6);
        const int need = (static_cast<int>(order.size()) + gr.cols - 1) / gr.cols;
        const int rows = std::clamp(need, 2, gr.viewH >= 520 ? 3 : 2);
        gr.cw = (W - (gr.cols - 1) * kGap) / gr.cols;
        gr.ch = (gr.viewH - (rows - 1) * kGap) / rows;
        return gr;
    }
    double maxScroll() const {
        const Grid gr = grid();
        const int rows = (static_cast<int>(order.size()) + gr.cols - 1) / gr.cols;
        const double content = rows * (gr.ch + kGap) - kGap;
        return content > gr.viewH ? content - gr.viewH + 80 : 0;
    }
    Rect cardRect(int pos, const Grid& gr) const {
        return {(pos % gr.cols) * (gr.cw + kGap), gr.top + (pos / gr.cols) * (gr.ch + kGap) - (scrollPos + listEl.over), gr.cw, gr.ch};
    }
    static Rect pinBtn(const Rect& c) { return {c.x + c.w - 2 * kBtn - 20, c.y + c.h - kBtn - 12, kBtn, kBtn}; }
    static Rect trashBtn(const Rect& c) { return {c.x + c.w - kBtn - 12, c.y + c.h - kBtn - 12, kBtn, kBtn}; }
    Rect fabRect() const { return {W - kFab - 20, H - kFab - 20, kFab, kFab}; }
    Rect clearRect() const { return {W - 48, (kSearchH - kBtn) / 2, kBtn, kBtn}; }
    Rect topRect(int which) const {
        const double y = (kTopH - 48) / 2;
        switch (which) {
            case TopBack: return {0, y, 48, 48};
            case TopTrash: return {W - 48, y, 48, 48};
            case TopCopy: return {W - 106, y, 48, 48};
            case TopPin: return {W - 164, y, 48, 48};
            default: return {W - 222, y, 48, 48};
        }
    }
    Rect editCard() const { return {0, kTopH + 8, W, H - kTopH - 8}; }
    Rect textRect() const {
        const Rect c = editCard();
        const double bottom = mode == Mode::Edit ? kMdBarH + kMdBottom + kMdAbove : 26;
        return {c.x + 30, c.y + 26, c.w - 70, c.h - 26 - bottom};
    }
    Rect chipRect() const {
        const double w = textW(status.empty() ? "Note deleted" : status, 15, false) + (status.empty() ? textW("Undo", 15, true) + 80 : 48);
        return {(W - w) / 2, H - 76, w, 52};
    }
    bool chipVisible() const { return !status.empty() || hasTrashed; }
    Rect undoRect() const { const Rect c = chipRect(); const double w = textW("Undo", 15, true) + 36; return {c.x + c.w - w - 6, c.y + 6, w, c.h - 12}; }
    void clearHover() { hoverCard = -1; hoverBtn = 0; hoverTop = TopNone; hoverFab = hoverClear = hoverUndo = dragging = false; mdbar.hover = mdbar.pressed = -1; bubbleHide(tip); }

    int cardAt(double x, double y) const {
        const Grid gr = grid();
        if (y < gr.top) return -1;
        for (int i = 0; i < static_cast<int>(order.size()); i++) if (cardRect(i, gr).contains(x, y)) return i;
        return -1;
    }

    void drawSearch(cairo_t* cr) {
        rrect(cr, 0, 0, W, kSearchH, kSearchH / 2);
        src(cr, g.color("surface_container_high"));
        cairo_fill(cr);
        ::icon(cr, "magnifer", 20, (kSearchH - 24) / 2, 24, g.color("on_surface_variant"));
        const bool empty = query.empty();
        const double tx = 56, ty = (kSearchH - 22) / 2;
        box(cr, empty ? "Search notes" : query, tx, ty, W - tx - 60, 0, 18, false, g.color(empty ? "on_surface_variant" : "on_surface"), empty ? 0.8 : 1, !empty);
        if (!empty) {
            const double cx = tx + std::min(textW(query, 18, false), W - tx - 60) + 2;
            if (blinkOn()) { src(cr, g.color("primary")); cairo_rectangle(cr, cx, ty + 1, 2, 22); cairo_fill(cr); }
            const Rect c = clearRect();
            ::icon(cr, "close-circle", c.x + 6, c.y + 6, 24, hoverClear ? g.color("on_surface") : g.color("on_surface_variant"));
        }
    }
    MdStyle styleOf(const Tone& t, const Color& bg) const {
        const Color fg = g.color(t.fg);
        return {fg, bg, g.color("primary"), g.color("on_primary"), mix(fg, bg, 0.14)};
    }
    void drawPreview(cairo_t* cr, const Note& n, const Tone& t, const Color& bg, const Rect& a) {
        CardDoc& cd = cardDocs[n.id];
        const std::string text = previewOf(n.content);
        if (!cd.doc || cd.src != text) { cd.doc = std::make_unique<MdDoc>(); cd.doc->build(text); cd.src = text; }
        const MdStyle st = styleOf(t, bg);
        cd.doc->layout(a.w, 14, st, static_cast<unsigned>(g.colorsStamp));
        cairo_save(cr);
        cairo_rectangle(cr, a.x, a.y, a.w, a.h);
        cairo_clip(cr);
        cd.doc->draw(cr, a.x, a.y, 0, a.h, 14, st);
        if (cd.doc->height > a.h) {
            cairo_pattern_t* fade = cairo_pattern_create_linear(0, a.y + a.h - 26, 0, a.y + a.h);
            cairo_pattern_add_color_stop_rgba(fade, 0, bg.r, bg.g, bg.b, 0);
            cairo_pattern_add_color_stop_rgba(fade, 1, bg.r, bg.g, bg.b, 1);
            cairo_set_source(cr, fade);
            cairo_rectangle(cr, a.x, a.y + a.h - 26, a.w, 26);
            cairo_fill(cr);
            cairo_pattern_destroy(fade);
        }
        cairo_restore(cr);
    }
    void drawCard(cairo_t* cr, const Rect& c, const Note& n, bool hot, int btn) {
        const Tone& t = toneOf(n.id);
        const Color bg = g.color(t.bg), fg = g.color(t.fg);
        rrect(cr, c.x, c.y, c.w, c.h, kCardR);
        src(cr, hot ? mix(fg, bg, 0.08) : bg);
        cairo_fill(cr);
        const std::string title = mdPlain(titleOf(n.content));
        const double tw = c.w - 40 - (n.pinned ? 30 : 0);
        box(cr, title.empty() ? "Untitled" : title, c.x + 20, c.y + 18, tw, 0, 19, true, fg, title.empty() ? 0.6 : 1, !title.empty());
        const double py = c.y + 56, ph = c.h - 56 - 52;
        if (ph > 20) drawPreview(cr, n, t, hot ? mix(fg, bg, 0.08) : bg, {c.x + 20, py, c.w - 40, ph});
        box(cr, dateOf(n.mtime), c.x + 20, c.y + c.h - 38, c.w - 120, 0, 13, false, fg, 0.65, false);
        if (n.pinned) ::icon(cr, "pin", c.x + c.w - 20 - 22, c.y + 18, 22, fg, true);
        if (!hot) return;
        roundButton(cr, pinBtn(c), "pin", bg, fg, btn == 1, n.pinned);
        roundButton(cr, trashBtn(c), "trash-bin-minimalistic", bg, fg, btn == 2);
    }
    void drawList(cairo_t* cr) {
        drawSearch(cr);
        const Grid gr = grid();
        if (order.empty()) { drawEmptyCentered(cr, gr); drawFab(cr); return; }
        cairo_save(cr);
        cairo_rectangle(cr, 0, gr.top, W, gr.viewH);
        cairo_clip(cr);
        for (int i = 0; i < static_cast<int>(order.size()); i++) {
            const Rect c = cardRect(i, gr);
            if (c.y + c.h < gr.top || c.y > H) continue;
            drawCard(cr, c, store.notes[order[i]], i == hoverCard, hoverBtn);
        }
        cairo_restore(cr);
        drawFab(cr);
    }
    void drawEmptyCentered(cairo_t* cr, const Grid& gr) {
        const char* msg = query.empty() ? "No notes yet" : "Nothing found";
        const double cy = gr.top + gr.viewH / 2, mw = textW(msg, 20, true);
        ::icon(cr, "notes", W / 2 - 36, cy - 80, 72, g.color("on_surface_variant"), true);
        box(cr, msg, (W - mw) / 2, cy + 6, mw + 4, 0, 20, true, g.color("on_surface_variant"), 1, false);
    }
    void drawFab(cairo_t* cr) {
        const Rect f = fabRect();
        rrect(cr, f.x, f.y + 3, f.w, f.h, 20);
        src(cr, Color{0, 0, 0}, 0.18);
        cairo_fill(cr);
        rrect(cr, f.x, f.y, f.w, f.h, 20);
        src(cr, hoverFab ? mix(g.color("on_primary_container"), g.color("primary_container"), 0.1) : g.color("primary_container"));
        cairo_fill(cr);
        plusMark(cr, f.x + f.w / 2, f.y + f.h / 2, 12, g.color("on_primary_container"));
    }

    bool blinkOn() const { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - blinkBase).count() / 530 % 2 == 0; }
    double contentH() { return mode == Mode::Preview ? doc.height : ed.contentHeight(); }
    double editMax() { return std::max(0.0, contentH() - edViewH + 40); }

    void drawEditTop(cairo_t* cr) {
        const Note& n = store.notes[openIdx];
        const Color bg = g.color("background"), fg = g.color("on_surface");
        roundButton(cr, topRect(TopBack), "arrow-left", bg, fg, hoverTop == TopBack);
        roundButton(cr, topRect(TopMode), mode == Mode::Preview ? "pen" : "eye", bg, fg, hoverTop == TopMode, mode == Mode::Preview);
        roundButton(cr, topRect(TopPin), "pin", bg, fg, hoverTop == TopPin, n.pinned);
        roundButton(cr, topRect(TopCopy), "copy", bg, fg, hoverTop == TopCopy);
        roundButton(cr, topRect(TopTrash), "trash-bin-minimalistic", bg, g.color(hoverTop == TopTrash ? "error" : "on_surface"), hoverTop == TopTrash);
        const std::string title = mdPlain(titleOf(ed.s));
        box(cr, title.empty() ? "Untitled" : title, 64, (kTopH - 26) / 2, W - 64 - 240, 0, 22, true, fg, title.empty() ? 0.5 : 1, !title.empty());
    }
    void drawPreviewMode(cairo_t* cr, const Rect& t, const Tone& tone) {
        edViewH = t.h;
        const MdStyle st = styleOf(tone, g.color(tone.bg));
        if (!docOk || docRev != ed.rev) { doc.build(ed.s); docRev = ed.rev; docOk = true; }
        doc.layout(t.w, 18, st, static_cast<unsigned>(g.colorsStamp));
        edScroll = std::clamp(edScroll, 0.0, editMax());
        cairo_save(cr);
        cairo_rectangle(cr, t.x - 6, t.y, t.w + 12, t.h);
        cairo_clip(cr);
        if (ed.s.empty()) box(cr, "Write something", t.x, t.y, t.w, 0, 18, false, st.fg, 0.5, false);
        doc.draw(cr, t.x, t.y - (edScroll + edEl.over), edScroll + edEl.over, edScroll + edEl.over + t.h, 18, st);
        cairo_restore(cr);
        drawScrollbar(cr, t);
    }
    void drawEditText(cairo_t* cr, const Rect& t, const Color& fg) {
        edViewH = t.h;
        PangoLayout* lay = ed.layout(t.w);
        if (follow) { followCaret(); follow = false; }
        edScroll = std::clamp(edScroll, 0.0, editMax());
        cairo_save(cr);
        cairo_rectangle(cr, t.x - 6, t.y, t.w + 12, t.h);
        cairo_clip(cr);
        cairo_translate(cr, t.x, t.y - (edScroll + edEl.over));
        for (const Rect& s : ed.selectionRects()) { src(cr, g.color("primary"), 0.3); cairo_rectangle(cr, s.x, s.y, s.w, s.h); cairo_fill(cr); }
        if (ed.s.empty()) box(cr, "Write something", 0, 0, t.w, 0, ed.px, false, fg, 0.5, false);
        pango_cairo_update_layout(cr, lay);
        src(cr, fg);
        cairo_move_to(cr, 0, 0);
        pango_cairo_show_layout(cr, lay);
        blinkShown = blinkOn();
        if (blinkShown) {
            double x = 0, y = 0, h = 0;
            ed.caret(ed.cur, x, y, h);
            src(cr, g.color("primary"));
            rrect(cr, x - 1, y, 2.5, h, 1.2);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        drawScrollbar(cr, t);
    }
    void drawScrollbar(cairo_t* cr, const Rect& t) {
        const double total = contentH() + 40;
        if (total <= t.h) return;
        const double bh = std::max(36.0, t.h * t.h / total), by = t.y + (t.h - bh) * (edScroll / std::max(1.0, editMax()));
        rrect(cr, t.x + t.w + 12, by, 5, bh, 2.5);
        src(cr, g.color("on_surface_variant"), 0.4 * edEl.thumbAlpha());  // (shown while the wheel moves, faded out soon after)
        cairo_fill(cr);
    }
    void followCaret() {
        double x = 0, y = 0, h = 0;
        ed.caret(ed.cur, x, y, h);
        if (y < edScroll) edScroll = y;
        else if (y + h > edScroll + edViewH) edScroll = y + h - edViewH;
    }
    void drawEditor(cairo_t* cr) {
        if (openIdx < 0 || openIdx >= static_cast<int>(store.notes.size())) { view = View::List; return; }
        drawEditTop(cr);
        const Tone& tone = toneOf(store.notes[openIdx].id);
        const Rect c = editCard();
        rrect(cr, c.x, c.y, c.w, c.h, kCardR);
        src(cr, g.color(tone.bg));
        cairo_fill(cr);
        if (mode == Mode::Preview) { drawPreviewMode(cr, textRect(), tone); return; }
        drawEditText(cr, textRect(), g.color(tone.fg));
        mdbar.layout(W, c);
        unsigned on = 0;
        for (int i = 0; i < kMdToolCount; i++) if (mdActive(ed.s, ed.anc, ed.cur, kMdTools[i].act)) on |= 1u << i;
        mdbar.draw(cr, g.color(tone.bg), g.color(tone.fg), on);
    }
    void drawChip(cairo_t* cr) {
        if (!chipVisible()) return;
        const Rect c = chipRect();
        const bool err = !status.empty();
        rrect(cr, c.x, c.y, c.w, c.h, c.h / 2);
        src(cr, g.color(err ? "error_container" : "inverse_surface"));
        cairo_fill(cr);
        const Color fg = g.color(err ? "on_error_container" : "inverse_on_surface");
        box(cr, err ? status : "Note deleted", c.x + 24, c.y + (c.h - 20) / 2, c.w - 40, 0, 15, false, fg, 1, false);
        if (err) return;
        const Rect u = undoRect();
        if (hoverUndo) { rrect(cr, u.x, u.y, u.w, u.h, u.h / 2); src(cr, g.color("inverse_primary"), 0.2); cairo_fill(cr); }
        box(cr, "Undo", u.x + 18, u.y + (u.h - 20) / 2, u.w, 0, 15, true, g.color("inverse_primary"), 1, false);
    }
    void draw(cairo_t* cr, Rect r) override {
        W = r.w;
        H = r.h;
        if (view == View::Edit) drawEditor(cr);
        else drawList(cr);
        drawChip(cr);
        const Color tb = g.color("inverse_surface"), tf = g.color("inverse_on_surface");
        tipMoving = bubbleDraw(cr, tip, {tb.r, tb.g, tb.b}, {tf.r, tf.g, tf.b});
    }

    bool motion(double x, double y) override {
        const int card = view == View::List ? cardAt(x, y) : -1;
        int btn = 0;
        if (card >= 0) {
            const Rect c = cardRect(card, grid());
            btn = pinBtn(c).contains(x, y) ? 1 : trashBtn(c).contains(x, y) ? 2 : 0;
        }
        int top = TopNone;
        if (view == View::Edit) for (int t : {TopBack, TopMode, TopPin, TopCopy, TopTrash}) if (topRect(t).contains(x, y)) top = t;
        const bool fab = view == View::List && fabRect().contains(x, y);
        const bool clr = view == View::List && !query.empty() && clearRect().contains(x, y);
        const bool und = hasTrashed && status.empty() && undoRect().contains(x, y);
        int md = -1;
        if (view == View::Edit && mode == Mode::Edit) md = mdbar.hit(x, y);
        const bool mdChanged = md != mdbar.hover;
        if (mdChanged) {
            mdbar.hover = md;
            if (md >= 0) {
                const MdTool& t = kMdTools[mdbar.btns[static_cast<size_t>(md)].tool];
                const Rect& r = mdbar.btns[static_cast<size_t>(md)].r;
                const std::string name = i18n::tr(t.tip), keys = t.keys;
                bubbleShow(tip, t.tip, keys.empty() ? name : name + " (" + keys + ")", r.x, r.y, r.w, r.h, W, H);
            } else bubbleHide(tip);
        }
        bool changed = mdChanged || card != hoverCard || btn != hoverBtn || top != hoverTop || fab != hoverFab || clr != hoverClear || und != hoverUndo;
        hoverCard = card; hoverBtn = btn; hoverTop = top; hoverFab = fab; hoverClear = clr; hoverUndo = und;
        if (dragging) changed = dragTo(x, y) || changed;
        return changed;
    }
    bool dragTo(double x, double y) {
        const Rect t = textRect();
        if (y < t.y) edScroll = std::max(0.0, edScroll - 20);
        else if (y > t.y + t.h) edScroll = std::min(editMax(), edScroll + 20);
        ed.moveTo(ed.indexAt(x - t.x, y - t.y + edScroll), true);
        return true;
    }
    bool leave() override {
        const bool changed = hoverCard >= 0 || hoverTop != TopNone || hoverFab || hoverClear || hoverUndo || mdbar.hover >= 0;
        flush();
        clearHover();
        return changed;
    }
    bool scroll(double, double, double dy) override {
        if (view == View::Edit) { elasticWheel(edEl, edScroll, editMax(), dy * 3); return true; }
        elasticWheel(listEl, scrollTarget, maxScroll(), dy * 3);
        return true;
    }
    Cursor cursor(double x, double y) const override {
        if (view == View::Edit) {
            for (int t : {TopBack, TopMode, TopPin, TopCopy, TopTrash}) if (topRect(t).contains(x, y)) return Cursor::Pointer;
            if (mode == Mode::Preview) { const Rect r = textRect(); return doc.taskAt(x - r.x, y - r.y + edScroll) >= 0 ? Cursor::Pointer : Cursor::Default; }
            if (mdbar.hit(x, y) >= 0) return Cursor::Pointer;
            if (mdbar.over(x, y)) return Cursor::Default;
            return editCard().contains(x, y) ? Cursor::Text : Cursor::Default;
        }
        if (hasTrashed && status.empty() && undoRect().contains(x, y)) return Cursor::Pointer;
        if (fabRect().contains(x, y) || cardAt(x, y) >= 0 || (!query.empty() && clearRect().contains(x, y))) return Cursor::Pointer;
        return y < kSearchH ? Cursor::Text : Cursor::Default;
    }

    bool click(double x, double y, bool press) override {
        if (!press) { const bool was = dragging || mdbar.pressed >= 0; dragging = false; mdbar.pressed = -1; return was; }
        if (hasTrashed && status.empty() && undoRect().contains(x, y)) { undoDelete(); return true; }
        if (view == View::Edit) return clickEditor(x, y);
        if (fabRect().contains(x, y)) { newNote(); return true; }
        if (!query.empty() && clearRect().contains(x, y)) { query.clear(); rebuild(); return true; }
        const int card = cardAt(x, y);
        if (card < 0) return false;
        const Rect c = cardRect(card, grid());
        const int idx = order[card];
        if (pinBtn(c).contains(x, y)) togglePin(idx);
        else if (trashBtn(c).contains(x, y)) deleteNote(idx);
        else openNote(idx);
        return true;
    }
    bool clickEditor(double x, double y) {
        for (int t : {TopBack, TopMode, TopPin, TopCopy, TopTrash}) {
            if (!topRect(t).contains(x, y)) continue;
            if (t == TopBack) closeEditor();
            else if (t == TopMode) setMode(mode == Mode::Edit ? Mode::Preview : Mode::Edit);
            else if (t == TopPin) togglePin(openIdx);
            else if (t == TopCopy) copyText(ed.hasSel() ? ed.selected() : ed.s);
            else deleteNote(openIdx);
            return true;
        }
        if (!editCard().contains(x, y)) return false;
        if (mode == Mode::Edit) {
            const int b = mdbar.hit(x, y);
            if (b >= 0) { mdbar.pressed = b; applyMd(kMdTools[mdbar.btns[static_cast<size_t>(b)].tool].act); return true; }
            if (mdbar.over(x, y)) return true;
        }
        const Rect t = textRect();
        const bool twice = doubleClick(x, y);
        if (mode == Mode::Preview) return clickPreview(x - t.x, y - t.y + edScroll, twice);
        const int idx = ed.indexAt(x - t.x, y - t.y + edScroll);
        if (twice) ed.selectWordAt(idx);
        else { ed.moveTo(idx, false); dragging = true; }
        blinkBase = Clock::now();
        return true;
    }
    bool doubleClick(double x, double y) {
        const auto now = Clock::now();
        const bool twice = now - lastClick < std::chrono::milliseconds(400) && std::abs(x - lastClickX) < 6 && std::abs(y - lastClickY) < 6;
        lastClick = twice ? Clock::time_point{} : now;
        lastClickX = x;
        lastClickY = y;
        return twice;
    }
    bool clickPreview(double x, double y, bool twice) {
        const int off = doc.taskAt(x, y);
        if (off >= 0) { toggleTask(off); return true; }
        if (twice) setMode(Mode::Edit);
        return twice;
    }
    void toggleTask(int off) {
        if (off < 0 || off >= static_cast<int>(ed.s.size())) return;
        ed.replace(off, off + 1, ed.s[off] == ' ' ? "x" : " ", false);
        markEdited();
        follow = false;
    }

    static xkb_keysym_t lowerSym(xkb_keysym_t s) { return s >= XKB_KEY_A && s <= XKB_KEY_Z ? s + 32 : s; }

    bool key(const KeyEvent& k) override { return view == View::Edit ? editKey(k) : listKey(k); }

    bool listKey(const KeyEvent& k) {
        const xkb_keysym_t s = lowerSym(k.sym);
        if (k.ctrl && s == XKB_KEY_n) { newNote(); return true; }
        if (k.ctrl && s == XKB_KEY_v) { pasteInto(true); return true; }
        if (k.ctrl && s == XKB_KEY_BackSpace) { query.clear(); rebuild(); return true; }
        if (k.ctrl || k.alt) return false;
        if (s == XKB_KEY_Escape && !query.empty()) { query.clear(); rebuild(); return true; }
        if (s == XKB_KEY_Return && !query.empty() && !order.empty()) { openNote(order[0]); return true; }
        if (s == XKB_KEY_BackSpace && !query.empty()) {
            size_t n = query.size() - 1;
            while (n > 0 && (query[n] & 0xC0) == 0x80) n--;
            query.resize(n);
            rebuild();
            return true;
        }
        if (k.text.empty()) return false;
        query += k.text;
        scrollTarget = 0;
        rebuild();
        blinkBase = Clock::now();
        return true;
    }

    bool editKey(const KeyEvent& k) {
        if (mode == Mode::Preview) return previewKey(k);
        const unsigned before = ed.rev;
        const bool used = k.ctrl ? ctrlKey(k) : plainKey(k);
        if (!used) return false;
        if (ed.rev != before) markEdited();
        follow = true;
        blinkBase = Clock::now();
        return true;
    }
    bool previewKey(const KeyEvent& k) {
        const xkb_keysym_t s = lowerSym(k.sym);
        if (k.ctrl) {
            if (s == XKB_KEY_e) setMode(Mode::Edit);
            else if (s == XKB_KEY_n) newNote();
            else if (s == XKB_KEY_c) copyText(ed.s);
            else return false;
            return true;
        }
        const double page = std::max(40.0, edViewH - 40);
        if (s == XKB_KEY_Escape) closeEditor();
        else if (s == XKB_KEY_Up) edScroll = std::max(0.0, edScroll - 60);
        else if (s == XKB_KEY_Down) edScroll = std::min(editMax(), edScroll + 60);
        else if (s == XKB_KEY_Page_Up) edScroll = std::max(0.0, edScroll - page);
        else if (s == XKB_KEY_Page_Down) edScroll = std::min(editMax(), edScroll + page);
        else if (s == XKB_KEY_Home) edScroll = 0;
        else if (s == XKB_KEY_End) edScroll = editMax();
        else if (s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) { setMode(Mode::Edit); ed.moveTo(static_cast<int>(ed.s.size()), false); }
        else if (!k.text.empty() || s == XKB_KEY_BackSpace || s == XKB_KEY_Delete) { setMode(Mode::Edit); return editKey(k); }
        else return false;
        return true;
    }
    static MdWords mdWords() {
        MdWords w;
        w.bold = i18n::tr("bold text");
        w.italic = i18n::tr("italic text");
        w.strike = i18n::tr("strikethrough text");
        w.code = i18n::tr("code");
        w.link = i18n::tr("link text");
        return w;
    }
    void applyResult(const MdEditResult& r) {
        if (!r.changed) return;
        ed.replace(r.from, r.to, r.with, false);
        ed.anc = r.anc;
        ed.cur = r.cur;
        ed.goalX = -1;
        markEdited();
        blinkBase = Clock::now();
    }
    void applyMd(MdAct act) { applyResult(mdApply(ed.s, ed.anc, ed.cur, act, mdWords())); }
    bool ctrlKey(const KeyEvent& k) {
        const xkb_keysym_t s = lowerSym(k.sym);
        const int n = static_cast<int>(ed.s.size());
        if (s == XKB_KEY_n) newNote();
        else if (s == XKB_KEY_e) setMode(Mode::Preview);
        else if (s == XKB_KEY_b) applyMd(MdAct::Bold);
        else if (s == XKB_KEY_i) applyMd(MdAct::Italic);
        else if (s == XKB_KEY_k) applyMd(MdAct::Link);
        else if (s == XKB_KEY_x && k.shift) applyMd(MdAct::Strike);
        else if (k.sym == XKB_KEY_grave) applyMd(MdAct::Code);
        else if (s == XKB_KEY_a) { ed.anc = 0; ed.cur = n; }
        else if (s == XKB_KEY_c) copyText(ed.selected());
        else if (s == XKB_KEY_x) { copyText(ed.selected()); ed.insert(""); }
        else if (s == XKB_KEY_v) pasteInto(false);
        else if (s == XKB_KEY_z && !k.shift) ed.restoreFrom(ed.undo, ed.redo);
        else if (s == XKB_KEY_y || s == XKB_KEY_z) ed.restoreFrom(ed.redo, ed.undo);
        else if (s == XKB_KEY_BackSpace) ed.backspace(true);
        else if (s == XKB_KEY_Delete) ed.del(true);
        else if (s == XKB_KEY_Home) ed.moveTo(0, k.shift);
        else if (s == XKB_KEY_End) ed.moveTo(n, k.shift);
        else return false;
        return true;
    }
    bool plainKey(const KeyEvent& k) {
        const xkb_keysym_t s = k.sym;
        if (s == XKB_KEY_Escape) { closeEditor(); return true; }
        if (moveKey(k)) return true;
        if (s == XKB_KEY_BackSpace) ed.backspace(false);
        else if (s == XKB_KEY_Delete) ed.del(false);
        else if (s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) {
            const MdEditResult r = ed.hasSel() ? MdEditResult{} : mdEnter(ed.s, ed.cur);
            if (r.changed) applyResult(r);
            else ed.insert("\n");
        }
        else if (s == XKB_KEY_Tab) ed.insert("  ");
        else if (!k.text.empty() && !k.alt) ed.insert(k.text);
        else return false;
        return true;
    }
    bool moveKey(const KeyEvent& k) {
        const xkb_keysym_t s = k.sym;
        const double lines = std::max(1.0, edViewH / (ed.px * 1.4) - 1);
        if (s == XKB_KEY_Left) {
            if (ed.hasSel() && !k.shift && !k.alt) ed.moveTo(ed.lo(), false);
            else ed.moveTo(k.alt ? ed.wordLeft(ed.cur) : ed.prev(ed.cur), k.shift);
        } else if (s == XKB_KEY_Right) {
            if (ed.hasSel() && !k.shift && !k.alt) ed.moveTo(ed.hi(), false);
            else ed.moveTo(k.alt ? ed.wordRight(ed.cur) : ed.next(ed.cur), k.shift);
        }
        else if (s == XKB_KEY_Up) ed.vertical(-1, k.shift);
        else if (s == XKB_KEY_Down) ed.vertical(1, k.shift);
        else if (s == XKB_KEY_Page_Up) ed.vertical(-lines, k.shift);
        else if (s == XKB_KEY_Page_Down) ed.vertical(lines, k.shift);
        else if (s == XKB_KEY_Home) ed.moveTo(ed.lineEdge(false), k.shift);
        else if (s == XKB_KEY_End) ed.moveTo(ed.lineEdge(true), k.shift);
        else return false;
        return true;
    }

    bool tick() override {
        const auto now = Clock::now();
        bool redraw = false;
        if (pendingSave && now - editedAt >= std::chrono::milliseconds(static_cast<int>(kSaveDelayMs))) flush();
        if (hasTrashed && now - trashedAt > std::chrono::milliseconds(static_cast<int>(kChipMs))) { hasTrashed = false; redraw = true; }
        if (!status.empty() && now - statusAt > std::chrono::milliseconds(4000)) { status.clear(); redraw = true; }
        if ((view == View::Edit || !query.empty()) && blinkOn() != blinkShown) { blinkShown = blinkOn(); redraw = true; }
        if (std::abs(scrollTarget - scrollPos) > 0.5) {
            scrollPos += (scrollTarget - scrollPos) * 0.35;
            redraw = true;
        } else if (scrollPos != scrollTarget) { scrollPos = scrollTarget; redraw = true; }
        if (listEl.step() | edEl.step()) redraw = true;
        return redraw;
    }
    bool animating() const override { return pendingSave || tipMoving || std::abs(scrollTarget - scrollPos) > 0.5 || listEl.over != 0 || edEl.busy(); }
};
}

using NotesPage = notes_ui::NotesPage;
