// Notes: a small multi-line text editor (UTF-8 text, caret, selection, undo). It owns a Pango layout that is the one truth for
// where every character is, so mouse hits, up/down and the caret rectangle all come from the same wrapped layout.
// Positions are byte offsets that always sit on a code point boundary. Included by page_notes.inc.
namespace notes_ui {
struct Snapshot { std::string s; int cur = 0, anc = 0; };

struct TextEditor {
    std::string s;
    int cur = 0, anc = 0;
    double goalX = -1;
    double px = 18;
    double width = 600;
    std::vector<Snapshot> undo, redo;
    Clock::time_point lastEdit{};
    PangoLayout* lay = nullptr;
    bool stale = true;
    unsigned rev = 0;

    ~TextEditor() { if (lay) g_object_unref(lay); }

    PangoLayout* layout(double w) {
        if (!lay) lay = pango_layout_new(pango_layout_get_context(g_measure));
        if (stale || w != width) {
            width = w;
            setFont(lay, px, false);
            pango_layout_set_width(lay, static_cast<int>(w * PANGO_SCALE));
            pango_layout_set_wrap(lay, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_text(lay, s.data(), static_cast<int>(s.size()));
            stale = false;
        }
        return lay;
    }
    double contentHeight() { int w = 0, h = 0; pango_layout_get_pixel_size(layout(width), &w, &h); return h; }

    int next(int i) const { if (i >= static_cast<int>(s.size())) return i; i++; while (i < static_cast<int>(s.size()) && (s[i] & 0xC0) == 0x80) i++; return i; }
    int prev(int i) const { if (i <= 0) return 0; i--; while (i > 0 && (s[i] & 0xC0) == 0x80) i--; return i; }
    int lo() const { return std::min(cur, anc); }
    int hi() const { return std::max(cur, anc); }
    bool hasSel() const { return cur != anc; }
    std::string selected() const { return s.substr(lo(), hi() - lo()); }
    static bool wordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || (c & 0x80) || c == '_'; }

    void set(const std::string& text) {
        s = text;
        cur = anc = 0;
        goalX = -1;
        undo.clear();
        redo.clear();
        stale = true;
        rev++;
    }

    void remember(bool typing) {
        const auto now = Clock::now();
        const bool join = typing && !undo.empty() && now - lastEdit < std::chrono::milliseconds(900);
        if (!join) {
            undo.push_back({s, cur, anc});
            if (undo.size() > 200) undo.erase(undo.begin());
        }
        redo.clear();
        lastEdit = now;
    }
    void replace(int from, int to, const std::string& with, bool typing) {
        if (from == to && with.empty()) return;
        remember(typing);
        s = s.substr(0, from) + with + s.substr(to);
        cur = anc = from + static_cast<int>(with.size());
        goalX = -1;
        stale = true;
        rev++;
    }
    void insert(const std::string& t) { replace(lo(), hi(), t, true); }
    void backspace(bool word) {
        if (hasSel()) return replace(lo(), hi(), "", false);
        const int from = word ? wordLeft(cur) : prev(cur);
        replace(from, cur, "", !word);
    }
    void del(bool word) {
        if (hasSel()) return replace(lo(), hi(), "", false);
        replace(cur, word ? wordRight(cur) : next(cur), "", !word);
    }
    bool restoreFrom(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
        if (from.empty()) return false;
        to.push_back({s, cur, anc});
        s = from.back().s;
        cur = from.back().cur;
        anc = from.back().anc;
        from.pop_back();
        stale = true;
        rev++;
        goalX = -1;
        return true;
    }

    int wordLeft(int i) const {
        while (i > 0 && !wordChar(s[i - 1])) i = prev(i);
        while (i > 0 && wordChar(s[i - 1])) i = prev(i);
        return i;
    }
    int wordRight(int i) const {
        const int n = static_cast<int>(s.size());
        while (i < n && !wordChar(s[i])) i = next(i);
        while (i < n && wordChar(s[i])) i = next(i);
        return i;
    }
    void moveTo(int i, bool extend, bool keepGoal = false) {
        cur = std::clamp(i, 0, static_cast<int>(s.size()));
        if (!extend) anc = cur;
        if (!keepGoal) goalX = -1;
    }
    void caret(int index, double& x, double& y, double& h) {
        PangoRectangle r{};
        pango_layout_get_cursor_pos(layout(width), index, &r, nullptr);
        x = double(r.x) / PANGO_SCALE;
        y = double(r.y) / PANGO_SCALE;
        h = double(r.height) / PANGO_SCALE;
    }
    int indexAt(double x, double y) {
        int idx = 0, trailing = 0;
        pango_layout_xy_to_index(layout(width), static_cast<int>(x * PANGO_SCALE), static_cast<int>(y * PANGO_SCALE), &idx, &trailing);
        while (trailing-- > 0) idx = next(idx);
        return idx;
    }
    void vertical(double dy, bool extend) {
        double x = 0, y = 0, h = 0;
        caret(cur, x, y, h);
        if (goalX < 0) goalX = x;
        const double ty = y + h / 2 + dy * h;
        if (ty < 0) return moveTo(0, extend, true);
        if (ty > contentHeight()) return moveTo(static_cast<int>(s.size()), extend, true);
        moveTo(indexAt(goalX, ty), extend, true);
    }
    int lineEdge(bool end) {
        int line = 0, x = 0;
        pango_layout_index_to_line_x(layout(width), cur, 0, &line, &x);
        PangoLayoutLine* l = pango_layout_get_line_readonly(layout(width), line);
        if (!l) return cur;
        const int start = l->start_index, stop = l->start_index + l->length;
        if (!end) return start;
        const bool wrapped = stop < static_cast<int>(s.size()) && s[stop] != '\n' && stop > start && (s[stop - 1] == ' ' || s[stop - 1] == '\t');
        return wrapped ? stop - 1 : stop;
    }
    void selectWordAt(int i) {
        const int n = static_cast<int>(s.size());
        int a = i, b = i;
        while (a > 0 && wordChar(s[a - 1])) a = prev(a);
        while (b < n && wordChar(s[b])) b = next(b);
        if (a == b && b < n) b = next(b);
        anc = a;
        cur = b;
    }

    std::vector<Rect> selectionRects() {
        std::vector<Rect> out;
        if (!hasSel()) return out;
        PangoLayoutIter* it = pango_layout_get_iter(layout(width));
        do {
            PangoLayoutLine* line = pango_layout_iter_get_line_readonly(it);
            PangoRectangle ext{};
            pango_layout_iter_get_line_extents(it, nullptr, &ext);
            const int a = std::max(lo(), line->start_index), b = std::min(hi(), line->start_index + line->length);
            if (a < b) {
                int* ranges = nullptr;
                int n = 0;
                pango_layout_line_get_x_ranges(line, a, b, &ranges, &n);
                for (int i = 0; i < n; i++)
                    out.push_back({double(ranges[2 * i]) / PANGO_SCALE, double(ext.y) / PANGO_SCALE, double(ranges[2 * i + 1] - ranges[2 * i]) / PANGO_SCALE, double(ext.height) / PANGO_SCALE});
                g_free(ranges);
            } else if (line->length == 0 && line->start_index >= lo() && line->start_index < hi()) {
                out.push_back({0, double(ext.y) / PANGO_SCALE, 6, double(ext.height) / PANGO_SCALE});
            }
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
        return out;
    }
};
}
