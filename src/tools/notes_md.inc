
namespace notes_ui {
enum class SpanKind { Bold, Italic, Code, Strike, Link };
struct MdSpan { int start = 0, end = 0; SpanKind kind = SpanKind::Bold; };
enum class BlockKind { Para, Heading, Bullet, Number, Task, Quote, Code, Rule };

struct MdBlock {
    BlockKind kind = BlockKind::Para;
    int level = 0;
    int number = 0;
    bool checked = false;
    int srcOff = -1;
    std::string raw;
    std::string text;
    std::vector<MdSpan> spans;
    PangoLayout* lay = nullptr;
    double x = 0, y = 0, w = 0, h = 0, lineH = 0;
    ~MdBlock() { if (lay) g_object_unref(lay); }
    MdBlock() = default;
    MdBlock(const MdBlock&) = delete;
    MdBlock& operator=(const MdBlock&) = delete;
};

struct MdStyle { Color fg, bg, accent, onAccent, codeBg; };

inline bool mdPunct(char c) { return std::ispunct(static_cast<unsigned char>(c)) != 0; }
inline bool mdWordCh(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || (c & 0x80); }

inline size_t mdFindClose(const std::string& s, const std::string& mark, size_t from) {
    for (size_t j = s.find(mark, from); j != std::string::npos; j = s.find(mark, j + 1)) {
        if (j > from && s[j - 1] != ' ' && s[j - 1] != '\n') return j;
    }
    return std::string::npos;
}

void mdInline(const std::string& s, std::string& out, std::vector<MdSpan>& spans);

inline bool mdPair(const std::string& s, size_t& i, const char* mark, SpanKind kind, std::string& out, std::vector<MdSpan>& spans) {
    const size_t n = std::strlen(mark);
    if (s.compare(i, n, mark) != 0 || i + n >= s.size() || s[i + n] == ' ') return false;
    const size_t j = mdFindClose(s, mark, i + n);
    if (j == std::string::npos) return false;
    if (n == 1 && mark[0] == '_' && ((i > 0 && mdWordCh(s[i - 1])) || (j + 1 < s.size() && mdWordCh(s[j + 1])))) return false;
    if (n == 1 && j + 1 < s.size() && s[j + 1] == mark[0]) return false;
    const int start = static_cast<int>(out.size());
    mdInline(s.substr(i + n, j - i - n), out, spans);
    spans.push_back({start, static_cast<int>(out.size()), kind});
    i = j + n;
    return true;
}

inline bool mdCode(const std::string& s, size_t& i, std::string& out, std::vector<MdSpan>& spans) {
    const size_t j = s.find('`', i + 1);
    if (s[i] != '`' || j == std::string::npos || j == i + 1) return false;
    const int start = static_cast<int>(out.size());
    out += s.substr(i + 1, j - i - 1);
    spans.push_back({start, static_cast<int>(out.size()), SpanKind::Code});
    i = j + 1;
    return true;
}

inline bool mdLink(const std::string& s, size_t& i, std::string& out, std::vector<MdSpan>& spans) {
    if (s[i] == '[') {
        const size_t close = s.find("](", i + 1);
        const size_t end = close == std::string::npos ? close : s.find(')', close + 2);
        if (end == std::string::npos || close == i + 1) return false;
        const int start = static_cast<int>(out.size());
        mdInline(s.substr(i + 1, close - i - 1), out, spans);
        spans.push_back({start, static_cast<int>(out.size()), SpanKind::Link});
        i = end + 1;
        return true;
    }
    if (s.compare(i, 7, "http://") != 0 && s.compare(i, 8, "https://") != 0) return false;
    if (i > 0 && mdWordCh(s[i - 1])) return false;
    size_t e = i;
    while (e < s.size() && s[e] != ' ' && s[e] != '\n' && s[e] != '\t') e++;
    while (e > i && std::strchr(".,;:!?)", s[e - 1])) e--;
    const int start = static_cast<int>(out.size());
    out += s.substr(i, e - i);
    spans.push_back({start, static_cast<int>(out.size()), SpanKind::Link});
    i = e;
    return true;
}

void mdInline(const std::string& s, std::string& out, std::vector<MdSpan>& spans) {
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size() && mdPunct(s[i + 1])) { out += s[i + 1]; i += 2; continue; }
        const bool done = mdCode(s, i, out, spans) || mdLink(s, i, out, spans) || mdPair(s, i, "**", SpanKind::Bold, out, spans) ||
                          mdPair(s, i, "__", SpanKind::Bold, out, spans) || mdPair(s, i, "~~", SpanKind::Strike, out, spans) ||
                          mdPair(s, i, "*", SpanKind::Italic, out, spans) || mdPair(s, i, "_", SpanKind::Italic, out, spans);
        if (!done) out += s[i++];
    }
}

inline std::string mdPlain(const std::string& s) {
    std::string out;
    std::vector<MdSpan> spans;
    mdInline(s, out, spans);
    return out;
}

struct ListLine { int indent = 0, number = 0, checkOff = -1; BlockKind kind = BlockKind::Bullet; bool checked = false; size_t content = 0; };

inline bool mdListLine(const std::string& l, ListLine& li) {
    size_t i = 0;
    while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) li.indent += l[i++] == '\t' ? 4 : 1;
    auto skipSpaces = [&](size_t j) { while (j < l.size() && l[j] == ' ') j++; return j; };
    if (i + 1 < l.size() && std::strchr("-*+", l[i]) && l[i + 1] == ' ') {
        const size_t j = skipSpaces(i + 2);
        const bool task = j + 2 < l.size() && l[j] == '[' && (l[j + 1] == ' ' || l[j + 1] == 'x' || l[j + 1] == 'X') && l[j + 2] == ']' && (j + 3 == l.size() || l[j + 3] == ' ');
        li.kind = task ? BlockKind::Task : BlockKind::Bullet;
        li.checked = task && l[j + 1] != ' ';
        li.checkOff = task ? static_cast<int>(j + 1) : -1;
        li.content = task ? skipSpaces(j + 3) : j;
        return true;
    }
    size_t j = i;
    while (j < l.size() && std::isdigit(static_cast<unsigned char>(l[j])) && j - i < 6) j++;
    if (j == i || j + 1 >= l.size() || (l[j] != '.' && l[j] != ')') || l[j + 1] != ' ') return false;
    li.kind = BlockKind::Number;
    li.number = std::atoi(l.substr(i, j - i).c_str());
    li.content = skipSpaces(j + 2);
    return true;
}

inline bool mdRule(const std::string& l) {
    char c = 0;
    int n = 0;
    for (char ch : l) {
        if (ch == ' ') continue;
        if (ch != '-' && ch != '*' && ch != '_') return false;
        if (c && ch != c) return false;
        c = ch;
        n++;
    }
    return n >= 3;
}
inline bool mdFence(const std::string& l) {
    size_t i = 0;
    while (i < l.size() && l[i] == ' ' && i < 3) i++;
    return l.compare(i, 3, "```") == 0 || l.compare(i, 3, "~~~") == 0;
}

struct MdDoc {
    std::vector<std::unique_ptr<MdBlock>> blocks;
    double height = 0, laidW = -1, laidPx = -1;
    unsigned laidStamp = 0;
    bool laidOk = false;

    MdBlock& add(BlockKind k, const std::string& raw) {
        blocks.push_back(std::make_unique<MdBlock>());
        blocks.back()->kind = k;
        blocks.back()->raw = raw;
        return *blocks.back();
    }

    void build(const std::string& src) {
        blocks.clear();
        laidOk = false;
        std::vector<int> depths;
        MdBlock* cont = nullptr;
        bool fenced = false;
        size_t pos = 0;
        while (pos <= src.size()) {
            size_t e = src.find('\n', pos);
            if (e == std::string::npos) e = src.size();
            std::string line = src.substr(pos, e - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            parseLine(line, static_cast<int>(pos), fenced, depths, cont);
            pos = e + 1;
        }
        for (auto& b : blocks) if (b->kind != BlockKind::Code) mdInline(b->raw, b->text, b->spans);
        for (auto& b : blocks) if (b->kind == BlockKind::Code) b->text = b->raw;
    }

    void parseLine(const std::string& line, int off, bool& fenced, std::vector<int>& depths, MdBlock*& cont) {
        if (fenced) {
            if (mdFence(line)) fenced = false;
            else { MdBlock& c = *blocks.back(); c.raw += (c.raw.empty() ? "" : "\n") + line; }
            return;
        }
        if (mdFence(line)) { add(BlockKind::Code, ""); fenced = true; cont = nullptr; return; }
        if (line.find_first_not_of(" \t") == std::string::npos) { cont = nullptr; return; }
        size_t h = 0;
        while (h < line.size() && line[h] == '#') h++;
        if (h >= 1 && h <= 6 && h < line.size() && line[h] == ' ') {
            std::string t = line.substr(h + 1);
            while (!t.empty() && (t.back() == '#' || t.back() == ' ')) t.pop_back();
            add(BlockKind::Heading, t).level = static_cast<int>(std::min<size_t>(h, 3));
            cont = nullptr;
            depths.clear();
            return;
        }
        if (mdRule(line)) { add(BlockKind::Rule, ""); cont = nullptr; depths.clear(); return; }
        ListLine li;
        if (mdListLine(line, li)) return addListItem(line, li, off, depths, cont);
        depths.clear();
        const size_t q = line.find_first_not_of(' ');
        if (q <= 3 && line[q] == '>') {
            std::string t = line.substr(q + 1);
            if (!t.empty() && t[0] == ' ') t.erase(0, 1);
            if (cont && cont->kind == BlockKind::Quote) cont->raw += "\n" + t;
            else cont = &add(BlockKind::Quote, t);
            return;
        }
        const std::string t = line.substr(q);
        if (cont && cont->kind == BlockKind::Para) cont->raw += "\n" + t;
        else cont = &add(BlockKind::Para, t);
    }

    void addListItem(const std::string& line, const ListLine& li, int off, std::vector<int>& depths, MdBlock*& cont) {
        while (!depths.empty() && li.indent < depths.back()) depths.pop_back();
        if (depths.empty() || li.indent > depths.back()) depths.push_back(li.indent);
        MdBlock& b = add(li.kind, line.substr(li.content));
        b.level = static_cast<int>(std::min<size_t>(depths.size() - 1, 5));
        b.number = li.number;
        b.checked = li.checked;
        b.srcOff = li.checkOff >= 0 ? off + li.checkOff : -1;
        cont = nullptr;
    }

    static void setMono(PangoLayout* l, double px) {
        PangoFontDescription* d = pango_font_description_from_string("Monospace");
        pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
        pango_layout_set_font_description(l, d);
        pango_font_description_free(d);
    }
    static void attr(PangoAttrList* al, PangoAttribute* a, int s, int e) {
        a->start_index = static_cast<guint>(s);
        a->end_index = static_cast<guint>(e);
        pango_attr_list_insert(al, a);
    }
    static guint16 c16(double v) { return static_cast<guint16>(std::clamp(v, 0.0, 1.0) * 65535); }
    static void applySpans(PangoAttrList* al, const MdBlock& b, const MdStyle& st) {
        for (const MdSpan& s : b.spans) {
            if (s.kind == SpanKind::Bold) attr(al, pango_attr_weight_new(PANGO_WEIGHT_BOLD), s.start, s.end);
            else if (s.kind == SpanKind::Italic) attr(al, pango_attr_style_new(PANGO_STYLE_ITALIC), s.start, s.end);
            else if (s.kind == SpanKind::Strike) attr(al, pango_attr_strikethrough_new(TRUE), s.start, s.end);
            else if (s.kind == SpanKind::Link) {
                attr(al, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), s.start, s.end);
                attr(al, pango_attr_foreground_new(c16(st.accent.r), c16(st.accent.g), c16(st.accent.b)), s.start, s.end);
            } else {
                attr(al, pango_attr_family_new("monospace"), s.start, s.end);
                attr(al, pango_attr_background_new(c16(st.codeBg.r), c16(st.codeBg.g), c16(st.codeBg.b)), s.start, s.end);
            }
        }
        const int n = static_cast<int>(b.text.size());
        if (b.kind == BlockKind::Quote) attr(al, pango_attr_style_new(PANGO_STYLE_ITALIC), 0, n);
        if (b.kind == BlockKind::Task && b.checked) attr(al, pango_attr_strikethrough_new(TRUE), 0, n);
    }

    static double indentOf(const MdBlock& b) {
        switch (b.kind) {
            case BlockKind::Bullet: case BlockKind::Number: case BlockKind::Task: return b.level * 24 + (b.kind == BlockKind::Number ? 30 : 26);
            case BlockKind::Quote: return 18;
            case BlockKind::Code: return 14;
            default: return 0;
        }
    }
    void layoutBlock(MdBlock& b, double width, double px, const MdStyle& st) {
        if (b.lay) { g_object_unref(b.lay); b.lay = nullptr; }
        b.x = indentOf(b);
        b.w = std::max(40.0, width - b.x - (b.kind == BlockKind::Code ? 14 : 0));
        if (b.kind == BlockKind::Rule) { b.h = 20; return; }
        const double size = b.kind == BlockKind::Heading ? px * (b.level == 1 ? 1.7 : b.level == 2 ? 1.4 : 1.18) : px;
        b.lay = pango_layout_new(pango_layout_get_context(g_measure));
        if (b.kind == BlockKind::Code) setMono(b.lay, px * 0.92);
        else setFont(b.lay, size, b.kind == BlockKind::Heading);
        pango_layout_set_width(b.lay, static_cast<int>(b.w * PANGO_SCALE));
        pango_layout_set_wrap(b.lay, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(b.lay, b.text.data(), static_cast<int>(b.text.size()));
        PangoAttrList* al = pango_attr_list_new();
        applySpans(al, b, st);
        pango_layout_set_attributes(b.lay, al);
        pango_attr_list_unref(al);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(b.lay, &w, &h);
        PangoLayoutLine* first = pango_layout_get_line_readonly(b.lay, 0);
        PangoRectangle ext{};
        if (first) pango_layout_line_get_pixel_extents(first, nullptr, &ext);
        b.lineH = first ? ext.height : h;
        b.h = h + (b.kind == BlockKind::Code ? 20 : 0);
    }
    static double gapAfter(const MdBlock& a, const MdBlock* next, double px) {
        if (!next) return 0;
        const bool list = a.kind == BlockKind::Bullet || a.kind == BlockKind::Number || a.kind == BlockKind::Task;
        const bool nextList = next->kind == BlockKind::Bullet || next->kind == BlockKind::Number || next->kind == BlockKind::Task;
        if (list && nextList) return px * 0.28;
        if (next->kind == BlockKind::Heading) return px * 0.7;
        return a.kind == BlockKind::Heading ? px * 0.35 : px * 0.55;
    }
    void layout(double width, double px, const MdStyle& st, unsigned stamp) {
        if (laidOk && width == laidW && px == laidPx && stamp == laidStamp) return;
        double y = 0;
        for (size_t i = 0; i < blocks.size(); i++) {
            MdBlock& b = *blocks[i];
            layoutBlock(b, width, px, st);
            b.y = y;
            y += b.h + gapAfter(b, i + 1 < blocks.size() ? blocks[i + 1].get() : nullptr, px);
        }
        height = y;
        laidW = width;
        laidPx = px;
        laidStamp = stamp;
        laidOk = true;
    }

    static void drawCheck(cairo_t* cr, double x, double y, bool on, const MdStyle& st) {
        rrect(cr, x + 1, y + 1, 16, 16, 5);
        if (on) { src(cr, st.accent); cairo_fill(cr); }
        else { src(cr, st.fg, 0.75); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        if (!on) return;
        src(cr, st.onAccent);
        cairo_set_line_width(cr, 2.2);
        cairo_move_to(cr, x + 5, y + 9.5);
        cairo_line_to(cr, x + 8, y + 12.5);
        cairo_line_to(cr, x + 13, y + 6);
        cairo_stroke(cr);
    }
    void drawMarker(cairo_t* cr, const MdBlock& b, double ox, double oy, double px, const MdStyle& st) const {
        const double mx = ox + b.level * 24, my = oy + b.y;
        if (b.kind == BlockKind::Bullet) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, mx + 9, my + b.lineH / 2, 3, 0, 2 * M_PI);
            src(cr, st.fg, 0.85);
            cairo_fill(cr);
        } else if (b.kind == BlockKind::Number) {
            box(cr, std::to_string(b.number) + ".", mx, my, 30, 0, px, false, st.fg, 0.8, true);
        } else if (b.kind == BlockKind::Task) {
            drawCheck(cr, mx + 2, my + (b.lineH - 18) / 2, b.checked, st);
        }
    }
    void draw(cairo_t* cr, double ox, double oy, double top, double bottom, double px, const MdStyle& st) const {
        for (const auto& bp : blocks) {
            const MdBlock& b = *bp;
            if (b.y + b.h < top || b.y > bottom) continue;
            const double y = oy + b.y;
            if (b.kind == BlockKind::Rule) { src(cr, st.fg, 0.3); cairo_rectangle(cr, ox, y + 9, b.w + b.x, 2); cairo_fill(cr); continue; }
            if (b.kind == BlockKind::Code) { rrect(cr, ox, y, b.x + b.w + 14, b.h, 12); src(cr, st.codeBg); cairo_fill(cr); }
            if (b.kind == BlockKind::Quote) { rrect(cr, ox, y, 4, b.h, 2); src(cr, st.fg, 0.4); cairo_fill(cr); }
            drawMarker(cr, b, ox, oy, px, st);
            const double alpha = b.kind == BlockKind::Quote ? 0.85 : (b.kind == BlockKind::Task && b.checked ? 0.55 : 1);
            src(cr, st.fg, alpha);
            cairo_move_to(cr, ox + b.x, y + (b.kind == BlockKind::Code ? 10 : 0));
            pango_cairo_update_layout(cr, b.lay);
            pango_cairo_show_layout(cr, b.lay);
        }
    }

    int taskAt(double x, double y) const {
        for (const auto& bp : blocks) {
            const MdBlock& b = *bp;
            if (b.kind != BlockKind::Task || b.srcOff < 0) continue;
            const double left = b.level * 24;
            if (x >= left - 4 && x <= left + 30 && y >= b.y - 3 && y <= b.y + std::max(b.lineH, 22.0) + 3) return b.srcOff;
        }
        return -1;
    }
};
}
