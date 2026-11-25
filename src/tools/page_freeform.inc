// Tools page: Freeform. Included by src/tools.cpp (inside its anonymous namespace), after the text box helpers and the notes store (clipboardPaste).
// A board where strokes, texts, shapes and concept-map nodes are placed anywhere, with arrows that follow the nodes they join. The board is
// kept in ~/.local/share/end4pC/tools/freeform.txt; Copy and Save make a picture of what is on it. The model is src/freeform.h.

#include "freeform_draw.inc"

struct FreeformPage final : ToolPage {
    enum Tool { Select, Pen, TextTool, Node, Shape, LinkTool, Eraser, kToolCount };
    enum Id { None, ToolBtn, StyleBtn, FillBtn, ShapeBtn, ColorBtn, SizeBtn, FontBtn, UndoBtn, RedoBtn, ClearBtn, CopyBtn, SaveBtn };
    enum class Drag { None, Pen, Shape, Move, LinkFrom, Erase, Resize };
    struct Hit { Rect r; int id = None, arg = 0; };
    struct Entry { int id, arg; double w; };

    static constexpr unsigned kColors[8] = {0xf5f5f5, 0x212121, 0xe53935, 0xfb8c00, 0xfdd835, 0x43a047, 0x1e88e5, 0x8e24aa};
    static constexpr double kTextSizes[3] = {18, 26, 40}, kLineSizes[3] = {2, 4, 8};
    static constexpr double kToolbarH = 56, kGap = 12, kTapMs = 400, kReach = 5, kChildGap = 150, kNudge = 4;
    static constexpr size_t kMaxEditBytes = 4000;
    static constexpr double kRowH = 34, kPopPad = 8, kPopW = 280, kSearchH = 52, kTipSeconds = 0.18;  // (the tooltips animate like the ones of the screenshot tool)
    static constexpr int kPopRows = 9, kShapeCols = 6;
    static constexpr double kCell = 56, kShapePad = 12;
    static constexpr const char* kToolIcons[kToolCount] = {"cursor", "pen", "text-field", "widget-add", "pen-new-round", "link", "eraser-square"};
    static constexpr const char* kToolTips[kToolCount] = {"Select and move (V)", "Pen (P)", "Text (T)", "Concept node (N)", "Shape (R)", "Arrow between items (L)", "Eraser (E)"};

    ff::Items items;
    ff::History hist;
    int tool = Select, style = ff::kShapeRounded, sizeIdx = 1;
    bool fillShapes = false;
    unsigned color = 0xf5f5f5;
    int selId = 0;

    Drag drag = Drag::None;
    ff::Item draft;
    int dragId = 0, dragHandle = -1;
    ff::Item dragItem;  // a resize: the item and its box as they were when the handle was grabbed
    ff::Box dragBox;
    ff::Pt dragLast, pointer;
    ff::Items dragBefore;
    bool dragMoved = false, erased = false;

    int editId = 0;
    size_t caret = 0;
    ff::Items editBefore;
    std::string editOrig;
    bool editNew = false, editDirty = false, lastCaret = true;
    Clock::time_point lastKey = Clock::now();

    Clock::time_point lastClickAt{};
    ff::Pt lastClickPt{-99, -99};
    int lastClickId = 0;

    Rect canvas, bar, fontPop;
    std::vector<Hit> hits;
    Hit hover;
    Bubble tip;
    bool tipMoving = false;
    double W = 0, H = 0;
    std::string font;  // the family new texts get; empty is the default
    std::vector<std::string> families;
    std::vector<int> fontRows;  // the families the search lets through (indexes into `families`)
    std::string fontFilter;
    bool fontOpen = false;
    int fontHover = -1;
    Rect shapePop;
    std::vector<std::string> shapeNames;
    std::string mshape;  // the Material shape the Shape tool draws; empty is the one picked with the corner buttons
    bool shapesOpen = false;
    int shapeHover = -1;
    double shapeScroll = 0;
    double fontScroll = 0;
    std::string flashMsg;
    Clock::time_point flashUntil{};

    ~FreeformPage() override { commitEdit(); }
    void blur() override { commitEdit(); }
    bool animating() const override { return tipMoving; }
    bool grabsKeys() const override { return editId != 0 || fontOpen || shapesOpen; }
    const char* name() const override { return "Freeform"; }
    const char* icon() const override { return "pen-new-round"; }
    bool roundClip() const override { return false; }

    static std::string dataDir() { return configDir() + "/tools"; }
    static Color boardColor() { return g.color("surface_container"); }

    // ---- the board ----------------------------------------------------------------------------------------------------------------
    void init() override {
        items = ff::parse(readFile(dataDir() + "/freeform.txt"));
        ffdraw::fitAll(items);
        color = ffdraw::luma(boardColor()) > 0.5 ? kColors[1] : kColors[0];
    }
    void persist() const { trWriteAtomic(dataDir(), "freeform.txt", ff::serialize(items)); }
    void flash(const std::string& m) { flashMsg = m; flashUntil = Clock::now() + std::chrono::milliseconds(2200); }

    int indexOf(int id) const { return ff::indexOfId(items, id); }
    ff::Item* find(int id) { const int i = indexOf(id); return i < 0 ? nullptr : &items[i]; }
    bool linkable(const ff::Item& it) const { return it.kind == ff::Item::Text || it.kind == ff::Item::Shape; }

    bool hasRoom(size_t more = 1) {
        if (items.size() + more <= ff::kMaxItems) return true;
        flash("The board is full");
        return false;
    }
    int add(ff::Item it) {
        it.id = ff::nextId(items);
        ffdraw::fit(it);
        items.push_back(std::move(it));
        return items.back().id;
    }
    ff::Item newText(ff::Pt at, bool boxed) const {
        ff::Item t;
        t.kind = ff::Item::Text;
        t.rgb = color;
        t.size = kTextSizes[sizeIdx];
        t.boxed = boxed;
        t.shape = style;
        t.font = font;
        t.pts = {at};
        return t;
    }
    ff::Item newLink(int from, int to, unsigned rgb) const {
        ff::Item l;
        l.kind = ff::Item::Link;
        l.from = from;
        l.to = to;
        l.rgb = rgb;
        l.size = ffdraw::kLinkWidth;
        return l;
    }

    // ---- editing a text, a node or the phrase on an arrow ------------------------------------------------------------------------------
    void beginEdit(int id, bool isNew, ff::Items before, size_t at) {
        editId = id;
        selId = id;
        editNew = isNew;
        editDirty = false;
        editBefore = std::move(before);
        const ff::Item* it = find(id);
        editOrig = it ? it->text : "";
        caret = std::min(at, editOrig.size());
        lastKey = Clock::now();
    }
    void commitEdit() {
        if (!editId) return;
        const int id = editId, i = indexOf(id);
        editId = 0;
        if (i < 0) return;
        const ff::Item& it = items[i];
        const bool blank = it.text.find_first_not_of(" \t\n") == std::string::npos;
        if (it.kind != ff::Item::Link && blank) {  // a text with nothing in it goes away
            items = ff::without(items, i);
            if (selId == id) selId = 0;
            if (!editNew) hist.record(editBefore);
        } else if (editNew || editDirty || it.text != editOrig) {
            hist.record(editBefore);
        }
        persist();
    }
    void startEditOf(int id, size_t at) {
        const ff::Item* it = find(id);
        if (!it || (it->kind != ff::Item::Text && it->kind != ff::Item::Link)) return;
        beginEdit(id, false, items, at);
    }
    // A new empty node next to `parentId`, linked to it, ready to type in.
    void addChild(int parentId) {
        const ff::Item* parent = find(parentId);
        if (!parent || !linkable(*parent)) return;
        commitEdit();
        parent = find(parentId);
        if (!parent || !hasRoom(2)) return;
        const ff::Items before = items;
        ff::Item child = newText({0, 0}, true);
        child.rgb = parent->rgb;
        if (parent->kind == ff::Item::Text) child.font = parent->font;
        if (parent->boxed || parent->kind == ff::Item::Shape) child.shape = parent->shape;
        ffdraw::fit(child);
        const ff::Box pb = ff::boundsIn(items, *parent);
        const ff::Pt want{pb.x + pb.w + kChildGap, pb.y + (pb.h - child.textH) / 2};
        child.pts[0] = ff::freeSpot(items, child.textW, child.textH, want);
        const int childId = add(child);
        items.push_back(newLink(parentId, childId, child.rgb));
        items.back().id = ff::nextId(items);
        beginEdit(childId, true, before, 0);
    }

    void insertText(const std::string& s) {
        ff::Item* it = find(editId);
        if (!it || s.empty() || it->text.size() + s.size() > kMaxEditBytes) return;
        it->text.insert(caret, s);
        caret += s.size();
        ffdraw::fit(*it);
    }
    bool editKey(const KeyEvent& k) {
        ff::Item* it = find(editId);
        if (!it) { editId = 0; return false; }
        std::string& s = it->text;
        const bool isLink = it->kind == ff::Item::Link;
        lastKey = Clock::now();
        switch (k.sym) {
            case XKB_KEY_Escape: commitEdit(); return true;
            case XKB_KEY_Tab: if (!isLink) { const int id = editId; commitEdit(); addChild(id); } return true;
            case XKB_KEY_Return: case XKB_KEY_KP_Enter:
                if (isLink || k.ctrl || (it->boxed && !k.shift)) commitEdit();
                else insertText("\n");
                return true;
            case XKB_KEY_BackSpace: if (caret > 0) { const size_t p = trPrevChar(s, caret); s.erase(p, caret - p); caret = p; ffdraw::fit(*it); } return true;
            case XKB_KEY_Delete: if (caret < s.size()) { s.erase(caret, trNextChar(s, caret) - caret); ffdraw::fit(*it); } return true;
            case XKB_KEY_Left: caret = trPrevChar(s, caret); return true;
            case XKB_KEY_Right: caret = trNextChar(s, caret); return true;
            case XKB_KEY_Home: { const size_t nl = caret ? s.rfind('\n', caret - 1) : std::string::npos; caret = nl == std::string::npos ? 0 : nl + 1; return true; }
            case XKB_KEY_End: { const size_t e = s.find('\n', caret); caret = e == std::string::npos ? s.size() : e; return true; }
            case XKB_KEY_Up: case XKB_KEY_Down: {
                if (isLink) return true;
                double x = 0, y = 0, h = 0;
                ffdraw::caretAt(*it, caret, x, y, h);
                caret = ffdraw::indexAt(*it, x, y + (k.sym == XKB_KEY_Up ? -h * 0.5 : h * 1.5));
                return true;
            }
            default: break;
        }
        if (k.ctrl && (k.sym == XKB_KEY_v || k.sym == XKB_KEY_V)) {
            std::string t;
            if (notes_ui::clipboardPaste(t)) {
                if (isLink) std::replace(t.begin(), t.end(), '\n', ' ');
                insertText(t);
            }
            return true;
        }
        if (!k.ctrl && !k.alt && !k.text.empty() && !(isLink && k.text == "\n")) insertText(k.text);
        return true;
    }

    // ---- what the toolbar changes --------------------------------------------------------------------------------------------------
    template<class F> void changeSelected(F edit) {
        ff::Item* it = find(selId);
        if (!it) return;
        const ff::Items snapshot = items;
        const ff::Item was = *it;
        edit(*it);
        ffdraw::fit(*it);
        const bool same = was.rgb == it->rgb && was.size == it->size && was.shape == it->shape && was.filled == it->filled && was.font == it->font && was.mshape == it->mshape;
        if (same) return;  // nothing changed (a style for a stroke, say): no undo step either
        if (editId) editDirty = true;  // while typing, the whole edit is one step
        else hist.record(snapshot);
        persist();
    }
    void undo() {
        commitEdit();
        if (hist.undo(items)) { if (indexOf(selId) < 0) selId = 0; persist(); }
    }
    void redo() {
        commitEdit();
        if (hist.redo(items)) { if (indexOf(selId) < 0) selId = 0; persist(); }
    }
    void removeSelected() {
        const int i = indexOf(selId);
        if (i < 0) return;
        hist.record(items);
        items = ff::without(items, i);
        selId = 0;
        persist();
    }
    void clearBoard() {
        commitEdit();
        if (items.empty()) return;
        hist.record(items);
        items.clear();
        selId = 0;
        persist();
        flash("Board cleared");
    }
    static std::string picturesDir() {
        const char* x = std::getenv("XDG_PICTURES_DIR");
        return x && *x ? x : homeDir() + "/Pictures";
    }
    void copyImage() {
        commitEdit();
        if (items.empty()) { flash("Nothing to copy"); return; }
        const std::string tmp = runtimeDir() + "/end4pC-freeform.png";
        if (!ffdraw::renderPng(items, boardColor(), tmp)) { flash("Could not make the picture"); return; }
        const std::string png = readFile(tmp);
        unlink(tmp.c_str());
        flash(trRunSync({"wl-copy", "--type", "image/png"}, png, nullptr, 4000) ? "Copied to the clipboard" : "Copy failed");
    }
    void saveImage() {
        commitEdit();
        if (items.empty()) { flash("Nothing to save"); return; }
        const std::string dir = picturesDir() + "/Freeform";
        mkdir(picturesDir().c_str(), 0755);
        mkdir(dir.c_str(), 0755);
        char stamp[40];
        const time_t now = time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        std::string file = dir + "/freeform-" + stamp + ".png";
        for (int n = 2; access(file.c_str(), F_OK) == 0 && n < 1000; n++) file = dir + "/freeform-" + stamp + "-" + std::to_string(n) + ".png";
        flash(ffdraw::renderPng(items, boardColor(), file) ? "Saved in Pictures/Freeform" : "Could not save the picture");
    }

    // ---- the pointer ----------------------------------------------------------------------------------------------------------------
    ff::Pt toBoard(double x, double y) const { return {x - canvas.x, y - canvas.y}; }
    int targetFor(ff::Pt p, int exceptId) const {
        for (int i = static_cast<int>(items.size()) - 1; i >= 0; i--)
            if (linkable(items[i]) && items[i].id != exceptId && ff::hit(items, items[i], p, kReach)) return i;
        return -1;
    }

    Hit hitAt(double x, double y) const {
        for (const Hit& h : hits) if (h.r.contains(x, y)) return h;
        return Hit{};
    }
    bool toolbarClick() {
        const Hit h = hover;
        if (h.id == None) return false;
        switch (h.id) {
            case ToolBtn: commitEdit(); tool = h.arg; break;
            case StyleBtn:
                style = h.arg;
                mshape.clear();
                changeSelected([&](ff::Item& it) { if (it.kind == ff::Item::Shape || it.boxed) it.shape = style; if (it.kind == ff::Item::Shape) it.mshape.clear(); });
                break;
            case ShapeBtn: openShapes(!shapesOpen); break;
            case FillBtn: fillShapes = !fillShapes; changeSelected([&](ff::Item& it) { if (it.kind == ff::Item::Shape) it.filled = fillShapes; }); break;
            case ColorBtn: color = kColors[h.arg]; changeSelected([&](ff::Item& it) { it.rgb = color; }); break;
            case SizeBtn:
                sizeIdx = h.arg;
                changeSelected([&](ff::Item& it) { it.size = it.kind == ff::Item::Text ? kTextSizes[sizeIdx] : it.kind == ff::Item::Link ? it.size : kLineSizes[sizeIdx]; });
                break;
            case FontBtn: openFonts(!fontOpen); break;
            case UndoBtn: undo(); break;
            case RedoBtn: redo(); break;
            case ClearBtn: clearBoard(); break;
            case CopyBtn: copyImage(); break;
            case SaveBtn: saveImage(); break;
        }
        return true;
    }

    bool click(double x, double y, bool press) override {
        if (!press) return release();
        bubbleHide(tip);
        if (shapesOpen) {  // the shape grid works like the font list: it takes its own clicks, any other click closes it
            if (shapePop.contains(x, y)) { pickShape(x, y); return true; }
            shapesOpen = false;
            if (hitAt(x, y).id == ShapeBtn) return true;
        }
        if (fontOpen) {  // the font list takes the click; a click anywhere else closes it (on its own button that is all it does)
            if (fontPop.contains(x, y)) { pickFont(y); return true; }
            fontOpen = false;
            if (hitAt(x, y).id == FontBtn) return true;
        }
        if (!canvas.contains(x, y)) { hover = hitAt(x, y); return toolbarClick(); }
        const ff::Pt p = toBoard(x, y);
        const auto now = Clock::now();
        const bool again = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickAt).count() < kTapMs && std::hypot(p.x - lastClickPt.x, p.y - lastClickPt.y) < 8;
        lastClickAt = now;
        lastClickPt = p;
        if (editId) {  // a click inside the text being edited puts the caret there; anywhere else ends the edit
            ff::Item* e = find(editId);
            if (e && e->kind == ff::Item::Text && ff::hit(items, *e, p, 0)) {
                const double off = e->boxed ? ffdraw::kPadX : ffdraw::kPlainPad, offY = e->boxed ? ffdraw::kPadY : ffdraw::kPlainPad;
                caret = ffdraw::indexAt(*e, p.x - e->pts[0].x - off, p.y - e->pts[0].y - offY);
                lastKey = now;
                return true;
            }
            commitEdit();
        }
        drag = Drag::None;  // a release that went to another page must not leave a drag behind
        pointer = p;
        dragLast = p;
        dragBefore = items;
        dragMoved = erased = false;
        const int hitIdx = ff::topAt(items, p, kReach);
        switch (tool) {
            case Select: return pressSelect(p, hitIdx, again);
            case Pen:
                if (!hasRoom()) return true;
                draft = ff::Item{};
                draft.kind = ff::Item::Stroke;
                draft.rgb = color;
                draft.size = kLineSizes[sizeIdx];
                draft.pts = {p};
                drag = Drag::Pen;
                selId = 0;
                return true;
            case TextTool: case Node: return pressText(p, hitIdx);
            case Shape:
                if (!hasRoom()) return true;
                draft = ff::Item{};
                draft.kind = ff::Item::Shape;
                draft.rgb = color;
                draft.size = kLineSizes[sizeIdx];
                draft.shape = style;
                draft.mshape = mshape;
                draft.filled = fillShapes;
                draft.pts = {p, p};
                drag = Drag::Shape;
                selId = 0;
                return true;
            case LinkTool:
                if (hitIdx >= 0 && linkable(items[hitIdx])) { dragId = items[hitIdx].id; selId = dragId; drag = Drag::LinkFrom; }
                else selId = 0;
                return true;
            case Eraser: drag = Drag::Erase; return eraseAt(p);
        }
        return false;
    }
    bool pressSelect(ff::Pt p, int hitIdx, bool again) {
        if (const int h = handleUnder(p); h >= 0) {  // a handle of the selection: resize instead of moving or picking
            dragItem = *find(selId);
            dragBox = ff::boundsIn(items, dragItem);
            dragHandle = h;
            dragId = selId;
            drag = Drag::Resize;
            return true;
        }
        if (hitIdx < 0) {
            selId = 0;
            if (again) {  // a double click on an empty place makes a node there
                newNodeAt(p);
                return true;
            }
            return true;
        }
        const int id = items[hitIdx].id;
        const bool sameAgain = again && id == lastClickId;
        lastClickId = id;
        selId = id;
        if (sameAgain && (items[hitIdx].kind == ff::Item::Text || items[hitIdx].kind == ff::Item::Link)) {
            const ff::Item& it = items[hitIdx];
            size_t at = it.text.size();
            if (it.kind == ff::Item::Text) at = ffdraw::indexAt(it, p.x - it.pts[0].x - (it.boxed ? ffdraw::kPadX : ffdraw::kPlainPad), p.y - it.pts[0].y - (it.boxed ? ffdraw::kPadY : ffdraw::kPlainPad));
            startEditOf(id, at);
            return true;
        }
        if (items[hitIdx].kind != ff::Item::Link) { drag = Drag::Move; dragId = id; }
        return true;
    }
    bool pressText(ff::Pt p, int hitIdx) {
        if (hitIdx >= 0 && items[hitIdx].kind == ff::Item::Text) {
            const ff::Item& it = items[hitIdx];
            startEditOf(it.id, ffdraw::indexAt(it, p.x - it.pts[0].x - (it.boxed ? ffdraw::kPadX : ffdraw::kPlainPad), p.y - it.pts[0].y - (it.boxed ? ffdraw::kPadY : ffdraw::kPlainPad)));
            return true;
        }
        if (tool == Node) { newNodeAt(p); return true; }
        if (!hasRoom()) return true;
        const ff::Items before = items;
        ff::Item t = newText({p.x, p.y}, false);
        ffdraw::fit(t);
        t.pts[0] = {p.x - ffdraw::kPlainPad, p.y - t.textH / 2};
        beginEdit(add(t), true, before, 0);
        return true;
    }
    // A node centred on p, ready for typing.
    void newNodeAt(ff::Pt p) {
        if (!hasRoom()) return;
        const ff::Items before = items;
        ff::Item n = newText({0, 0}, true);
        ffdraw::fit(n);
        n.pts[0] = {p.x - n.textW / 2, p.y - n.textH / 2};
        beginEdit(add(n), true, before, 0);
    }
    bool eraseAt(ff::Pt p) {
        const int i = ff::topAt(items, p, kReach);
        if (i < 0) return false;
        if (!erased) { hist.record(items); erased = true; }
        items = ff::without(items, i);
        selId = 0;
        return true;
    }

    bool motion(double x, double y) override {
        const ff::Pt p = toBoard(x, y);
        pointer = p;
        const Hit now = hitAt(x, y);
        const bool hoverChanged = now.id != hover.id || now.arg != hover.arg;
        hover = now;
        if (hoverChanged) {
            const char* label = now.id != None && !fontOpen && !shapesOpen && drag == Drag::None ? tipOf(now) : nullptr;
            for (const Hit& h : hits)
                if (label && h.id == now.id && h.arg == now.arg)
                    bubbleShow(tip, std::to_string(h.id) + ":" + std::to_string(h.arg), i18n::tr(label), h.r.x, h.r.y, h.r.w, h.r.h, W, H);
            if (!label) bubbleHide(tip);
        }
        if (shapesOpen) {
            const int cell = shapeAt(x, y);
            if (cell != shapeHover && cell >= 0) {
                const ff::Box c = cellBox(cell);
                bubbleShow(tip, "shape:" + shapeNames[cell], shapeNames[cell], shapePop.x + c.x, shapePop.y + c.y - shapeScroll, c.w, c.h, W, H);
            } else if (cell < 0) bubbleHide(tip);
            const bool changed = cell != shapeHover || hoverChanged;
            shapeHover = cell;
            return changed;
        }
        if (fontOpen) {
            const int row = fontPop.contains(x, y) && y > fontPop.y + kSearchH ? static_cast<int>((y - fontPop.y - kSearchH - kPopPad + fontScroll) / kRowH) : -1;
            const int shown = row >= 0 && row < static_cast<int>(fontRows.size()) ? row : -1;
            const bool rowChanged = shown != fontHover;
            fontHover = shown;
            return hoverChanged || rowChanged;
        }
        switch (drag) {
            case Drag::None: return hoverChanged;
            case Drag::Pen: ff::addPoint(draft, p, 1.5); return true;
            case Drag::Shape: draft.pts[1] = p; return true;
            case Drag::Move: {
                if (ff::Item* it = find(dragId)) {
                    *it = ff::moved(*it, p.x - dragLast.x, p.y - dragLast.y);
                    dragMoved = true;
                }
                dragLast = p;
                return true;
            }
            case Drag::Erase: eraseAt(p); return true;
            case Drag::Resize: resizeTo(p); return true;
            case Drag::LinkFrom: return true;
        }
        return hoverChanged;
    }
    bool leave() override {
        const bool changed = hover.id != None || tip.key != "";
        hover = Hit{};
        bubbleHide(tip);
        return changed;
    }
    // ---- resizing the selection ----------------------------------------------------------------------------------------------------
    static constexpr double kSelPad = 6, kHandleReach = 9;
    // The dashed box round the selected item; its corners and edges carry the handles.
    ff::Box selectionBox(const ff::Item& it) const { return ff::grown(ff::boundsIn(items, it), kSelPad); }
    int handleUnder(ff::Pt p) const {
        if (tool != Select || editId) return -1;
        const int i = indexOf(selId);
        if (i < 0 || items[i].kind == ff::Item::Link) return -1;
        return ff::handleAt(selectionBox(items[i]), p, kHandleReach, items[i].kind == ff::Item::Text);
    }
    void resizeTo(ff::Pt p) {
        ff::Item* it = find(dragId);
        if (!it) return;
        const bool text = dragItem.kind == ff::Item::Text;
        const ff::Box sel = ff::resizeBox(ff::grown(dragBox, kSelPad), dragHandle, p, text, ff::kMinBox + 2 * kSelPad);
        const ff::Box to = ff::grown(sel, -kSelPad);
        *it = ff::resized(dragItem, dragBox, to);
        if (text) {  // the letters changed size, so the box is measured again: it stays anchored on the side opposite to the handle
            ffdraw::fit(*it);
            if (dragHandle == 0 || dragHandle == 6) it->pts[0].x = to.x + to.w - it->textW;
            if (dragHandle == 0 || dragHandle == 2) it->pts[0].y = to.y + to.h - it->textH;
        }
        dragMoved = true;
    }

    // ---- the grid of Material shapes ---------------------------------------------------------------------------------------------------
    ff::Box cellBox(int i) const {
        return {kShapePad + (i % kShapeCols) * kCell, kShapePad + (i / kShapeCols) * kCell, kCell, kCell};
    }
    int shapeAt(double x, double y) const {
        if (!shapePop.contains(x, y)) return -1;
        const int col = static_cast<int>((x - shapePop.x - kShapePad) / kCell), row = static_cast<int>((y - shapePop.y - kShapePad + shapeScroll) / kCell);
        if (col < 0 || col >= kShapeCols || row < 0 || x - shapePop.x < kShapePad || y - shapePop.y < kShapePad) return -1;
        const int i = row * kShapeCols + col;
        return i < static_cast<int>(shapeNames.size()) ? i : -1;
    }
    void openShapes(bool open) {
        shapesOpen = open;
        shapeHover = -1;
        bubbleHide(tip);
        if (!open) return;
        fontOpen = false;
        if (shapeNames.empty()) {  // the names, in the order shapes.txt lists them (the plain ones come first)
            const auto known = loadShapes();
            std::ifstream f(assetsDir() + "/shapes.txt");
            std::string line;
            while (std::getline(f, line)) {
                std::stringstream ss(line);
                std::string n;
                ss >> n;
                if (known.count(n) && std::find(shapeNames.begin(), shapeNames.end(), n) == shapeNames.end()) shapeNames.push_back(n);
            }
        }
    }
    void pickShape(double x, double y) {
        const int i = shapeAt(x, y);
        if (i < 0) return;
        mshape = shapeNames[i];
        shapesOpen = false;
        commitEdit();
        tool = Shape;
        changeSelected([&](ff::Item& it) { if (it.kind == ff::Item::Shape) it.mshape = mshape; });
    }
    bool scroll(double x, double y, double delta) override {
        if (shapesOpen && shapePop.contains(x, y)) {
            const double rows = std::ceil(shapeNames.size() / double(kShapeCols)), max = std::max(0.0, rows * kCell + 2 * kShapePad - shapePop.h);
            shapeScroll = std::clamp(shapeScroll + delta * 3, 0.0, max);
            return true;
        }
        if (!fontOpen || !fontPop.contains(x, y)) return false;
        const double max = std::max(0.0, static_cast<double>(fontRows.size()) * kRowH - (fontPop.h - kSearchH - 2 * kPopPad));
        fontScroll = std::clamp(fontScroll + delta * 3, 0.0, max);
        return true;
    }
    void openFonts(bool open) {
        fontOpen = open;
        fontHover = -1;
        if (!open) return;
        shapesOpen = false;
        loadFamilies();
        fontFilter.clear();
        filterFonts();
        for (size_t i = 0; i < fontRows.size(); i++)  // the list opens with the current font in view
            if (families[fontRows[i]] == font) fontScroll = std::max(0.0, (static_cast<double>(i) - 3) * kRowH);
    }
    void filterFonts() {
        auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        const std::string needle = lower(fontFilter);
        fontRows.clear();
        for (size_t i = 0; i < families.size(); i++)
            if (needle.empty() || (!families[i].empty() && lower(families[i]).find(needle) != std::string::npos)) fontRows.push_back(static_cast<int>(i));
        fontScroll = 0;
        fontHover = -1;
    }
    bool fontKey(const KeyEvent& k) {
        if (k.sym == XKB_KEY_Escape) {
            if (fontFilter.empty()) fontOpen = false;
            else { fontFilter.clear(); filterFonts(); }
        } else if (k.sym == XKB_KEY_Return || k.sym == XKB_KEY_KP_Enter) {
            if (!fontRows.empty()) chooseFont(fontRows[0]);
        } else if (k.sym == XKB_KEY_BackSpace) {
            if (!fontFilter.empty()) { fontFilter.erase(trPrevChar(fontFilter, fontFilter.size())); filterFonts(); }
        } else if (!k.ctrl && !k.alt && !k.text.empty() && fontFilter.size() < 60) {
            fontFilter += k.text;
            filterFonts();
        }
        return true;
    }
    void loadFamilies() {
        if (!families.empty()) return;
        PangoFontFamily** all = nullptr;
        int n = 0;
        pango_font_map_list_families(pango_cairo_font_map_get_default(), &all, &n);
        std::vector<std::string> names;
        PangoFontMap* map = pango_cairo_font_map_get_default();
        PangoContext* ctx = pango_font_map_create_context(map);
        PangoLanguage* en = pango_language_from_string("en");
        for (int i = 0; i < n; i++) {  // only families that can write letters (not the symbol and dingbat ones)
            const char* name = pango_font_family_get_name(all[i]);
            PangoFontDescription* d = pango_font_description_new();
            pango_font_description_set_family(d, name);
            pango_font_description_set_absolute_size(d, 14 * PANGO_SCALE);
            PangoFont* f = pango_font_map_load_font(map, ctx, d);
            bool letters = false;
            if (f) {
                PangoCoverage* cov = pango_font_get_coverage(f, en);
                letters = pango_coverage_get(cov, 'a') == PANGO_COVERAGE_EXACT && pango_coverage_get(cov, 'Z') == PANGO_COVERAGE_EXACT;
                g_object_unref(cov);
                g_object_unref(f);
            }
            pango_font_description_free(d);
            if (letters) names.push_back(name);
        }
        g_object_unref(ctx);
        g_free(all);
        auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        names.erase(std::unique(names.begin(), names.end()), names.end());
        if (names.size() > 600) names.resize(600);
        families.push_back("");  // the first row is the default font
        families.insert(families.end(), names.begin(), names.end());
    }
    void chooseFont(int family) {
        font = families[family];
        fontOpen = false;
        changeSelected([&](ff::Item& it) { if (it.kind == ff::Item::Text) it.font = font; });
    }
    void pickFont(double y) {
        if (y <= fontPop.y + kSearchH) return;  // the search field
        const int row = static_cast<int>((y - fontPop.y - kSearchH - kPopPad + fontScroll) / kRowH);
        if (row >= 0 && row < static_cast<int>(fontRows.size())) chooseFont(fontRows[row]);
    }

    bool release() {
        const Drag d = drag;
        drag = Drag::None;
        switch (d) {
            case Drag::None: return false;
            case Drag::Pen:
                hist.record(items);
                selId = 0;
                add(draft);
                break;
            case Drag::Shape: {
                const ff::Box b = ffdraw::shapeBox(draft);
                if (b.w < 6 && b.h < 6) return true;
                hist.record(items);
                selId = add(draft);
                break;
            }
            case Drag::Move: if (!dragMoved) return true; hist.record(dragBefore); break;
            case Drag::Resize: if (!dragMoved) return true; hist.record(dragBefore); break;
            case Drag::Erase: break;
            case Drag::LinkFrom: finishLink(); return true;
        }
        persist();
        return true;
    }
    void finishLink() {
        const ff::Item* from = find(dragId);
        if (!from) return;
        const unsigned rgb = from->rgb;
        const int to = targetFor(pointer, dragId);
        if (to >= 0) {
            const int toId = items[to].id;
            if (ff::linked(items, dragId, toId) || !hasRoom()) return;
            const ff::Items before = items;
            items.push_back(newLink(dragId, toId, color));
            items.back().id = ff::nextId(items);
            beginEdit(items.back().id, true, before, 0);  // the phrase on the arrow comes first
            return;
        }
        if (!hasRoom(2)) return;
        const ff::Box fb = ff::boundsIn(items, *from);
        if (std::hypot(pointer.x - (fb.x + fb.w / 2), pointer.y - (fb.y + fb.h / 2)) < std::max(fb.w, fb.h) / 2 + 24) return;  // a click, not a drag
        const ff::Items before = items;  // dropped on an empty place: a new node there, already linked
        const int fromId = dragId;
        ff::Item n = newText({0, 0}, true);
        n.rgb = rgb;
        if (from->boxed || from->kind == ff::Item::Shape) n.shape = from->shape;
        ffdraw::fit(n);
        n.pts[0] = {pointer.x - n.textW / 2, pointer.y - n.textH / 2};
        const int nodeId = add(n);
        items.push_back(newLink(fromId, nodeId, rgb));
        items.back().id = ff::nextId(items);
        beginEdit(nodeId, true, before, 0);
    }

    // ---- keys ---------------------------------------------------------------------------------------------------------------------
    bool key(const KeyEvent& k) override {
        if (shapesOpen) { if (k.sym == XKB_KEY_Escape) shapesOpen = false; return true; }
        if (fontOpen) return fontKey(k);
        if (editId) return editKey(k);
        if (k.ctrl) {
            if (k.sym == XKB_KEY_z || k.sym == XKB_KEY_Z) { if (k.shift) redo(); else undo(); return true; }
            if (k.sym == XKB_KEY_y || k.sym == XKB_KEY_Y) { redo(); return true; }
            if (k.sym == XKB_KEY_c || k.sym == XKB_KEY_C) { copyImage(); return true; }
            if (k.sym == XKB_KEY_s || k.sym == XKB_KEY_S) { saveImage(); return true; }
            return false;
        }
        if (k.alt) return false;
        const ff::Item* sel = find(selId);
        switch (k.sym) {
            case XKB_KEY_Delete: case XKB_KEY_BackSpace: if (sel) { removeSelected(); return true; } return false;
            case XKB_KEY_Tab: if (sel && linkable(*sel)) { addChild(selId); return true; } return false;
            case XKB_KEY_Return: case XKB_KEY_KP_Enter:
                if (sel && (sel->kind == ff::Item::Text || sel->kind == ff::Item::Link)) { startEditOf(selId, sel->text.size()); return true; }
                return false;
            case XKB_KEY_Escape: if (sel) { selId = 0; return true; } return false;
            case XKB_KEY_Left: case XKB_KEY_Right: case XKB_KEY_Up: case XKB_KEY_Down: {
                if (!sel || sel->kind == ff::Item::Link) return false;
                const double step = k.shift ? 1 : kNudge * 2.5;
                const double dx = k.sym == XKB_KEY_Left ? -step : k.sym == XKB_KEY_Right ? step : 0, dy = k.sym == XKB_KEY_Up ? -step : k.sym == XKB_KEY_Down ? step : 0;
                hist.record(items);
                *find(selId) = ff::moved(*sel, dx, dy);
                persist();
                return true;
            }
            default: break;
        }
        const char* letters = "vptnrle";
        for (int i = 0; i < kToolCount; i++)
            if (k.sym == static_cast<xkb_keysym_t>(letters[i]) && !k.shift) { tool = i; return true; }
        return false;
    }

    bool tick() override {
        bool changed = false;
        if (flashUntil != Clock::time_point{} && Clock::now() >= flashUntil) { flashUntil = Clock::time_point{}; changed = true; }
        const bool caretOn = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastKey).count() / 530 % 2 == 0;
        if (editId && caretOn != lastCaret) changed = true;
        return changed;
    }
    Cursor cursor(double x, double y) const override {
        if (!canvas.contains(x, y)) return hover.id != None ? Cursor::Pointer : Cursor::Default;
        if (tool == TextTool || tool == Node) return Cursor::Text;
        if (handleUnder(toBoard(x, y)) >= 0) return Cursor::Pointer;
        return tool == Select && ff::topAt(items, toBoard(x, y), kReach) >= 0 ? Cursor::Pointer : Cursor::Default;
    }

#include "freeform_view.inc"
};
