// The text of T3xt: a document is a vector of lines (UTF-8, no line endings inside), a cursor and an anchor (the selection), an undo history, and the
// cache of the highlighter. Every change goes through replaceRange, so undo is one simple thing. No window code in here: it is tested on its own
// (tests/editor/test_editor.cpp).
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

#include "syntax.h"

namespace t3 {

struct Pos {
    int line = 0, col = 0;  // col: a byte offset in the line, always on the start of a character
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
};

constexpr int kTabWidth = 4;
constexpr size_t kMaxFile = 64u * 1024 * 1024;

// ---- UTF-8 -----------------------------------------------------------------------------------------------------------------------------
inline int prevCol(const std::string& s, int c) {
    if (c <= 0) return 0;
    do c--; while (c > 0 && (static_cast<unsigned char>(s[static_cast<size_t>(c)]) & 0xC0) == 0x80);
    return c;
}
inline int nextCol(const std::string& s, int c) {
    const int n = static_cast<int>(s.size());
    if (c >= n) return n;
    do c++; while (c < n && (static_cast<unsigned char>(s[static_cast<size_t>(c)]) & 0xC0) == 0x80);
    return c;
}
// Is the text valid UTF-8? When it is not, `fixed` gets a copy with every bad byte replaced by U+FFFD.
inline bool validUtf8(const std::string& in, std::string* fixed = nullptr) {
    bool ok = true;
    std::string out;
    if (fixed) out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool good = len > 0 && i + static_cast<size_t>(len) <= in.size();
        for (int k = 1; good && k < len; k++) good = (static_cast<unsigned char>(in[i + static_cast<size_t>(k)]) & 0xC0) == 0x80;
        if (good && len == 2 && c < 0xC2) good = false;
        if (good && len == 3) { const unsigned cp = ((c & 0xF) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 2]) & 0x3F); if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) good = false; }
        if (good && len == 4) { const unsigned cp = ((c & 7) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(in[i + 3]) & 0x3F); if (cp < 0x10000 || cp > 0x10FFFF) good = false; }
        if (good) { if (fixed) out.append(in, i, static_cast<size_t>(len)); i += static_cast<size_t>(len); }
        else { ok = false; if (fixed) out += "\xEF\xBF\xBD"; i++; }
    }
    if (fixed) *fixed = std::move(out);
    return ok;
}
inline bool isWordByte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

// The column on screen of a byte offset (tabs go to the next multiple of kTabWidth).
inline int visualColumn(const std::string& s, int col) {
    int v = 0;
    for (int i = 0; i < col && i < static_cast<int>(s.size());) {
        if (s[static_cast<size_t>(i)] == '\t') { v += kTabWidth - v % kTabWidth; i++; }
        else { v++; i = nextCol(s, i); }
    }
    return v;
}
// The byte offset whose column is the nearest to `vis` (not past the end of the line).
inline int colFromVisual(const std::string& s, int vis) {
    int v = 0, i = 0;
    while (i < static_cast<int>(s.size())) {
        const int w = s[static_cast<size_t>(i)] == '\t' ? kTabWidth - v % kTabWidth : 1;
        if (vis <= v) break;
        if (vis < v + w) return (vis - v) * 2 < w ? i : nextCol(s, i);  // inside a tab: the nearer edge
        v += w;
        i = nextCol(s, i);
    }
    return i;
}

struct Edit {
    Pos at, end;                    // the text `inserted` sits between at and end after the edit
    std::string removed, inserted;
    Pos curBefore, anchorBefore, curAfter;
    int group = 0;
};

struct Doc {
    std::vector<std::string> lines{""};
    std::string path;
    bool modified = false, crlf = false, readOnly = false;
    const Lang* lang = &languages().at("plain");
    Pos cur, anchor;
    int wantVis = -1;                   // the column the cursor tries to keep going up and down
    double scrollY = 0, scrollX = 0;
    time_t diskMtime = 0;
    std::vector<Edit> undoStack, redoStack;
    int groupId = 0;
    std::chrono::steady_clock::time_point lastEditAt{};
    uint64_t revision = 0;               // counts the changes: whatever was worked out from the text (matches, widths) is cached by it
    int lastKind = 0;                    // 1 typing, 2 backspace, 3 delete, 0 anything else: edits of the same kind in a row are one undo step
    std::vector<LineState> states{LineState{}};  // states[i]: the highlighter state at the start of line i (valid up to statesValid)
    size_t statesValid = 1;

    // ---- text ------------------------------------------------------------------------------------------------------------------------
    int lineCount() const { return static_cast<int>(lines.size()); }
    Pos clampPos(Pos p) const {
        p.line = std::clamp(p.line, 0, lineCount() - 1);
        p.col = std::clamp(p.col, 0, static_cast<int>(lines[static_cast<size_t>(p.line)].size()));
        while (p.col > 0 && p.col < static_cast<int>(lines[static_cast<size_t>(p.line)].size()) && (static_cast<unsigned char>(lines[static_cast<size_t>(p.line)][static_cast<size_t>(p.col)]) & 0xC0) == 0x80) p.col--;
        return p;
    }
    Pos endPos() const { return {lineCount() - 1, static_cast<int>(lines.back().size())}; }
    std::string textRange(Pos a, Pos b) const {
        if (b < a) std::swap(a, b);
        a = clampPos(a); b = clampPos(b);
        if (a.line == b.line) return lines[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col), static_cast<size_t>(b.col - a.col));
        std::string out = lines[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col));
        for (int l = a.line + 1; l < b.line; l++) { out += '\n'; out += lines[static_cast<size_t>(l)]; }
        out += '\n';
        out += lines[static_cast<size_t>(b.line)].substr(0, static_cast<size_t>(b.col));
        return out;
    }
    std::string allText(const char* eol = "\n") const {
        std::string out;
        for (size_t i = 0; i < lines.size(); i++) { if (i) out += eol; out += lines[i]; }
        return out;
    }
    bool hasSelection() const { return cur != anchor; }
    Pos selStart() const { return cur < anchor ? cur : anchor; }
    Pos selEnd() const { return cur < anchor ? anchor : cur; }
    std::string selectedText() const { return hasSelection() ? textRange(selStart(), selEnd()) : std::string(); }

    // The one way text changes: [a, b) is replaced by `text` (which may hold '\n'). Returns where the new text ends.
    Pos replaceRange(Pos a, Pos b, const std::string& text, bool record = true, int kind = 0) {
        if (b < a) std::swap(a, b);
        a = clampPos(a); b = clampPos(b);
        Edit e;
        e.at = a;
        e.removed = textRange(a, b);
        e.inserted = text;
        e.curBefore = cur;
        e.anchorBefore = anchor;
        const std::string tail = lines[static_cast<size_t>(b.line)].substr(static_cast<size_t>(b.col));
        const std::string head = lines[static_cast<size_t>(a.line)].substr(0, static_cast<size_t>(a.col));
        std::vector<std::string> parts;
        size_t from = 0;
        for (size_t nl; (nl = text.find('\n', from)) != std::string::npos; from = nl + 1) parts.push_back(text.substr(from, nl - from));
        parts.push_back(text.substr(from));
        Pos end{a.line + static_cast<int>(parts.size()) - 1, 0};
        end.col = (parts.size() == 1 ? static_cast<int>(head.size()) : 0) + static_cast<int>(parts.back().size());
        parts.front() = head + parts.front();
        parts.back() += tail;
        lines.erase(lines.begin() + a.line, lines.begin() + b.line + 1);
        lines.insert(lines.begin() + a.line, parts.begin(), parts.end());
        e.end = end;
        invalidateFrom(a.line);
        modified = true;
        revision++;
        if (record) {
            const auto now = std::chrono::steady_clock::now();
            const bool join = kind != 0 && kind == lastKind && now - lastEditAt < std::chrono::milliseconds(900) && !undoStack.empty() && redoStack.empty();
            if (!join) groupId++;
            e.group = groupId;
            lastKind = kind;
            lastEditAt = now;
            redoStack.clear();
            undoStack.push_back(std::move(e));
            if (undoStack.size() > 5000) undoStack.erase(undoStack.begin(), undoStack.begin() + 1000);
        }
        return end;
    }
    void breakUndoGroup() { lastKind = 0; }

    // ---- the cursor and the selection -------------------------------------------------------------------------------------------------
    void setCursor(Pos p, bool extend) {
        cur = clampPos(p);
        if (!extend) anchor = cur;
        wantVis = -1;
        breakUndoGroup();
    }
    void deleteSelection(int kind = 0) {
        if (!hasSelection()) return;
        const Pos a = selStart(), b = selEnd();
        replaceRange(a, b, "", true, kind);
        cur = anchor = a;
        wantVis = -1;
        fixCursorAfter();
    }
    void fixCursorAfter() { if (!undoStack.empty()) undoStack.back().curAfter = cur; }
    void insertText(const std::string& text, int kind = 0) {
        if (readOnly) return;
        if (hasSelection()) { const Pos a = selStart(), b = selEnd(); const Pos end = replaceRange(a, b, text, true, 0); cur = anchor = end; }
        else { const Pos end = replaceRange(cur, cur, text, true, kind); cur = anchor = end; }
        wantVis = -1;
        fixCursorAfter();
    }
    static std::string leadingSpace(const std::string& s) { size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; return s.substr(0, i); }
    // Enter: a new line with the indentation of this one (a level deeper after an opening bracket or a colon in Python).
    void newline() {
        if (readOnly) return;
        const std::string& line = lines[static_cast<size_t>(cur.line)];
        std::string indent = leadingSpace(line.substr(0, static_cast<size_t>(cur.col)));
        const std::string before = line.substr(0, static_cast<size_t>(cur.col));
        size_t last = before.find_last_not_of(" \t");
        const bool open = last != std::string::npos && (before[last] == '{' || before[last] == '(' || before[last] == '[' || (before[last] == ':' && lang->style == Style::Python));
        if (open) indent += std::string(kTabWidth, ' ');
        const std::string after = line.substr(static_cast<size_t>(cur.col));
        const bool closes = open && !after.empty() && (after[0] == '}' || after[0] == ')' || after[0] == ']');
        std::string text = "\n" + indent;
        if (closes) text += "\n" + leadingSpace(before);
        const Pos at = hasSelection() ? selStart() : cur;
        if (hasSelection()) replaceRange(selStart(), selEnd(), "", true, 0);
        const Pos end = replaceRange(at, at, text, true, 0);
        cur = anchor = closes ? Pos{at.line + 1, static_cast<int>(indent.size())} : end;  // between the brackets: the cursor stays on the indented line
        wantVis = -1;
        fixCursorAfter();
    }
    void backspace(bool word) {
        if (readOnly) return;
        if (hasSelection()) { deleteSelection(0); return; }
        Pos a = cur;
        if (cur.col == 0) { if (cur.line == 0) return; a = {cur.line - 1, static_cast<int>(lines[static_cast<size_t>(cur.line - 1)].size())}; }
        else if (word) a = wordLeft(cur);
        else {
            const std::string& s = lines[static_cast<size_t>(cur.line)];
            // in the indentation, one step removes a whole level of spaces
            const std::string before = s.substr(0, static_cast<size_t>(cur.col));
            if (cur.col > 0 && before.find_first_not_of(' ') == std::string::npos && cur.col % kTabWidth == 0) a = {cur.line, cur.col - kTabWidth};
            else a = {cur.line, prevCol(s, cur.col)};
        }
        replaceRange(a, cur, "", true, 2);
        cur = anchor = a;
        wantVis = -1;
        fixCursorAfter();
    }
    void deleteForward(bool word) {
        if (readOnly) return;
        if (hasSelection()) { deleteSelection(0); return; }
        const std::string& s = lines[static_cast<size_t>(cur.line)];
        Pos b = cur;
        if (cur.col >= static_cast<int>(s.size())) { if (cur.line + 1 >= lineCount()) return; b = {cur.line + 1, 0}; }
        else b = word ? wordRight(cur) : Pos{cur.line, nextCol(s, cur.col)};
        replaceRange(cur, b, "", true, 3);
        wantVis = -1;
        fixCursorAfter();
    }

    // ---- movement --------------------------------------------------------------------------------------------------------------------
    Pos left(Pos p) const {
        if (p.col > 0) return {p.line, prevCol(lines[static_cast<size_t>(p.line)], p.col)};
        return p.line > 0 ? Pos{p.line - 1, static_cast<int>(lines[static_cast<size_t>(p.line - 1)].size())} : p;
    }
    Pos right(Pos p) const {
        const std::string& s = lines[static_cast<size_t>(p.line)];
        if (p.col < static_cast<int>(s.size())) return {p.line, nextCol(s, p.col)};
        return p.line + 1 < lineCount() ? Pos{p.line + 1, 0} : p;
    }
    Pos wordLeft(Pos p) const {
        p = left(p);
        while (!(p.col == 0 && p.line == 0)) {
            const std::string& s = lines[static_cast<size_t>(p.line)];
            if (p.col == 0) break;
            if (isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(p.col - 1)])) || !std::isspace(static_cast<unsigned char>(s[static_cast<size_t>(p.col - 1)]))) { if (!std::isspace(static_cast<unsigned char>(s[static_cast<size_t>(p.col)]))) break; }
            p = left(p);
        }
        const std::string& s = lines[static_cast<size_t>(p.line)];
        if (p.col < static_cast<int>(s.size()) && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(p.col)])))
            while (p.col > 0 && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(p.col - 1)]))) p.col = prevCol(s, p.col);
        return p;
    }
    Pos wordRight(Pos p) const {
        const std::string& s = lines[static_cast<size_t>(p.line)];
        if (p.col >= static_cast<int>(s.size())) return right(p);
        int c = p.col;
        if (isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(c)]))) while (c < static_cast<int>(s.size()) && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(c)]))) c = nextCol(s, c);
        else if (!std::isspace(static_cast<unsigned char>(s[static_cast<size_t>(c)]))) c = nextCol(s, c);
        while (c < static_cast<int>(s.size()) && (s[static_cast<size_t>(c)] == ' ' || s[static_cast<size_t>(c)] == '\t')) c++;
        return {p.line, c};
    }
    Pos vertical(Pos p, int dy) {
        if (wantVis < 0) wantVis = visualColumn(lines[static_cast<size_t>(p.line)], p.col);
        const int line = std::clamp(p.line + dy, 0, lineCount() - 1);
        if (line == p.line && dy < 0) return {0, 0};
        if (line == p.line && dy > 0) return endPos();
        return {line, colFromVisual(lines[static_cast<size_t>(line)], wantVis)};
    }
    Pos homePos(Pos p) const {  // Home goes to the first character that is not a space, and to the start of the line the second time
        const std::string& s = lines[static_cast<size_t>(p.line)];
        const int first = static_cast<int>(leadingSpace(s).size());
        return {p.line, p.col == first ? 0 : first};
    }
    void selectWordAt(Pos p) {
        p = clampPos(p);
        const std::string& s = lines[static_cast<size_t>(p.line)];
        int a = p.col, b = p.col;
        if (a < static_cast<int>(s.size()) && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(a)])) ) {
            while (a > 0 && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(a - 1)]))) a = prevCol(s, a);
            while (b < static_cast<int>(s.size()) && isWordByte(static_cast<unsigned char>(s[static_cast<size_t>(b)]))) b = nextCol(s, b);
        } else if (a < static_cast<int>(s.size())) b = nextCol(s, a);
        anchor = {p.line, a};
        cur = {p.line, b};
        wantVis = -1;
    }
    void selectLine(int line) {
        line = std::clamp(line, 0, lineCount() - 1);
        anchor = {line, 0};
        cur = line + 1 < lineCount() ? Pos{line + 1, 0} : Pos{line, static_cast<int>(lines[static_cast<size_t>(line)].size())};
        wantVis = -1;
    }
    void selectAll() { anchor = {0, 0}; cur = endPos(); wantVis = -1; }

    // ---- lines: indent, comment ------------------------------------------------------------------------------------------------------
    void selectedLines(int& first, int& last) const {
        first = selStart().line;
        last = selEnd().line;
        if (hasSelection() && selEnd().col == 0 && last > first) last--;  // a selection that ends at the start of a line does not include it
    }
    void indentLines(bool outdent) {
        if (readOnly) return;
        int first, last;
        selectedLines(first, last);
        const Pos c0 = cur, a0 = anchor;
        const int g = ++groupId;
        lastKind = 0;
        int dCur = 0, dAnchor = 0;
        for (int l = first; l <= last; l++) {
            const std::string& s = lines[static_cast<size_t>(l)];
            if (!outdent) { replaceRange({l, 0}, {l, 0}, std::string(kTabWidth, ' '), true, 0); if (l == c0.line) dCur = kTabWidth; if (l == a0.line) dAnchor = kTabWidth; }
            else {
                int n = 0;
                if (!s.empty() && s[0] == '\t') n = 1;
                else while (n < kTabWidth && n < static_cast<int>(s.size()) && s[static_cast<size_t>(n)] == ' ') n++;
                if (n) { replaceRange({l, 0}, {l, n}, "", true, 0); if (l == c0.line) dCur = -std::min(n, c0.col); if (l == a0.line) dAnchor = -std::min(n, a0.col); }
            }
            if (!undoStack.empty()) undoStack.back().group = g;  // (all the lines are one step)
        }
        cur = {c0.line, std::max(0, c0.col + dCur)};
        anchor = {a0.line, std::max(0, a0.col + dAnchor)};
        lastKind = 0;
    }
    void toggleComment() {
        if (readOnly) return;
        std::string marker = lang->lineComment;
        if (marker.empty()) marker = lang->style == Style::Markup ? "" : "#";
        if (marker.empty()) return;
        int first, last;
        selectedLines(first, last);
        bool allCommented = true;
        for (int l = first; l <= last; l++) {
            const std::string& s = lines[static_cast<size_t>(l)];
            const size_t f = s.find_first_not_of(" \t");
            if (f == std::string::npos) continue;
            if (s.compare(f, marker.size(), marker) != 0) allCommented = false;
        }
        size_t indent = std::string::npos;  // the comment marks line up at the shallowest indentation
        for (int l = first; l <= last; l++) { const size_t f = lines[static_cast<size_t>(l)].find_first_not_of(" \t"); if (f != std::string::npos) indent = std::min(indent, f); }
        if (indent == std::string::npos) return;
        const Pos c0 = cur, a0 = anchor;
        const int g = ++groupId;
        lastKind = 0;
        for (int l = first; l <= last; l++) {
            const std::string s = lines[static_cast<size_t>(l)];
            if (s.find_first_not_of(" \t") == std::string::npos) continue;
            if (allCommented) {
                const size_t f = s.find_first_not_of(" \t");
                size_t n = marker.size();
                if (f + n < s.size() && s[f + n] == ' ') n++;
                replaceRange({l, static_cast<int>(f)}, {l, static_cast<int>(f + n)}, "", true, 0);
            } else replaceRange({l, static_cast<int>(indent)}, {l, static_cast<int>(indent)}, marker + " ", true, 0);
            undoStack.back().group = g;
        }
        const int shift = allCommented ? -static_cast<int>(marker.size() + 1) : static_cast<int>(marker.size() + 1);
        cur = clampPos({c0.line, std::max(0, c0.col + shift)});
        anchor = clampPos({a0.line, std::max(0, a0.col + shift)});
    }

    // ---- undo ------------------------------------------------------------------------------------------------------------------------
    bool undo() {
        if (undoStack.empty()) return false;
        const int g = undoStack.back().group;
        Pos selA = undoStack.back().anchorBefore, selC = undoStack.back().curBefore;
        while (!undoStack.empty() && undoStack.back().group == g) {
            Edit e = std::move(undoStack.back());
            undoStack.pop_back();
            replaceRange(e.at, e.end, e.removed, false);
            selA = e.anchorBefore;
            selC = e.curBefore;
            redoStack.push_back(std::move(e));
        }
        cur = clampPos(selC);
        anchor = clampPos(selA);
        wantVis = -1;
        lastKind = 0;
        return true;
    }
    bool redo() {
        if (redoStack.empty()) return false;
        const int g = redoStack.back().group;
        Pos after = cur;
        while (!redoStack.empty() && redoStack.back().group == g) {
            Edit e = std::move(redoStack.back());
            redoStack.pop_back();
            replaceRange(e.at, e.at.line == e.end.line && e.removed.find('\n') == std::string::npos ? Pos{e.at.line, e.at.col + static_cast<int>(e.removed.size())} : endOfText(e.at, e.removed), e.inserted, false);
            after = e.end;
            undoStack.push_back(std::move(e));
        }
        cur = anchor = clampPos(after);
        wantVis = -1;
        lastKind = 0;
        return true;
    }
    // Where text that starts at `at` and holds `text` ends.
    static Pos endOfText(Pos at, const std::string& text) {
        Pos p = at;
        for (const char c : text) { if (c == '\n') { p.line++; p.col = 0; } else p.col++; }
        return p;
    }

    // ---- finding ---------------------------------------------------------------------------------------------------------------------
    // The next match of `needle` from `from` (forward or back), wrapping round the document. False when there is none.
    bool find(const std::string& needle, bool matchCase, Pos from, bool forward, Pos& found) const {
        if (needle.empty()) return false;
        const std::string n = matchCase ? needle : lowerStr(needle);
        const int total = lineCount();
        for (int k = 0; k <= total; k++) {
            const int l = forward ? (from.line + k) % total : ((from.line - k) % total + total) % total;
            const std::string& raw = lines[static_cast<size_t>(l)];
            const std::string s = matchCase ? raw : lowerStr(raw);
            if (forward) {
                size_t start = k == 0 ? static_cast<size_t>(from.col) : 0;
                size_t p = s.find(n, start);
                if (k == total) { if (p != std::string::npos && static_cast<int>(p) < from.col) { found = {l, static_cast<int>(p)}; return true; } continue; }
                if (p != std::string::npos) { found = {l, static_cast<int>(p)}; return true; }
            } else {
                size_t p = std::string::npos;
                if (k == 0) { if (from.col >= static_cast<int>(n.size())) p = s.rfind(n, static_cast<size_t>(from.col) - n.size()); }
                else p = s.rfind(n);
                if (k == total) { if (p != std::string::npos && static_cast<int>(p) >= from.col) { found = {l, static_cast<int>(p)}; return true; } continue; }
                if (p != std::string::npos) { found = {l, static_cast<int>(p)}; return true; }
            }
        }
        return false;
    }
    int countMatches(const std::string& needle, bool matchCase) const {
        if (needle.empty()) return 0;
        const std::string n = matchCase ? needle : lowerStr(needle);
        int count = 0;
        for (const std::string& raw : lines) {
            const std::string s = matchCase ? raw : lowerStr(raw);
            for (size_t p = s.find(n); p != std::string::npos; p = s.find(n, p + n.size())) count++;
        }
        return count;
    }
    // Every match is replaced; one undo step. Returns how many.
    int replaceAll(const std::string& needle, const std::string& with, bool matchCase) {
        if (needle.empty() || readOnly) return 0;
        const std::string n = matchCase ? needle : lowerStr(needle);
        const int g = ++groupId;
        lastKind = 0;
        int count = 0;
        for (int l = 0; l < lineCount(); l++) {
            int from = 0;
            while (true) {
                const std::string s = matchCase ? lines[static_cast<size_t>(l)] : lowerStr(lines[static_cast<size_t>(l)]);
                const size_t p = s.find(n, static_cast<size_t>(from));
                if (p == std::string::npos) break;
                replaceRange({l, static_cast<int>(p)}, {l, static_cast<int>(p + n.size())}, with, true, 0);
                undoStack.back().group = g;
                from = static_cast<int>(p + with.size());
                count++;
            }
        }
        cur = clampPos(cur);
        anchor = clampPos(anchor);
        return count;
    }

    // ---- the highlighter cache -------------------------------------------------------------------------------------------------------
    void invalidateFrom(int line) {
        states.resize(static_cast<size_t>(lineCount()) + 1);
        statesValid = std::min(statesValid, static_cast<size_t>(line) + 1);
    }
    void ensureStates(int upToLine) {
        states.resize(static_cast<size_t>(lineCount()) + 1);
        std::vector<uint8_t> scratch;
        while (statesValid <= static_cast<size_t>(upToLine) && statesValid <= static_cast<size_t>(lineCount())) {
            states[statesValid] = highlightLine(*lang, lines[statesValid - 1], states[statesValid - 1], scratch);
            statesValid++;
        }
    }
    void tokens(int line, std::vector<uint8_t>& out) {
        ensureStates(line);
        highlightLine(*lang, lines[static_cast<size_t>(line)], states[static_cast<size_t>(line)], out);
    }
    void detect() { lang = &detectLanguage(path, lines.empty() ? "" : lines[0]); invalidateFrom(0); }

    // ---- files -----------------------------------------------------------------------------------------------------------------------
    // Loads a file into this document. On failure returns false and says why in `error`.
    bool load(const std::string& file, std::string& error) {
        struct stat st{};
        if (stat(file.c_str(), &st) != 0) { error = std::strerror(errno); return false; }
        if (!S_ISREG(st.st_mode)) { error = "not a regular file"; return false; }
        if (static_cast<size_t>(st.st_size) > kMaxFile) { error = "file is too big (64 MB at most)"; return false; }
        std::ifstream f(file, std::ios::binary);
        if (!f) { error = "cannot read the file"; return false; }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string data = ss.str();
        if (data.find('\0', 0) != std::string::npos && data.find('\0') < 8000) { error = "this looks like a binary file"; return false; }
        std::string fixed;
        readOnly = !validUtf8(data, &fixed);  // not UTF-8: shown with replacement marks and not saveable (saving would damage it)
        if (readOnly) data = std::move(fixed);
        crlf = data.find("\r\n") != std::string::npos;
        lines.clear();
        size_t from = 0;
        for (size_t nl; (nl = data.find('\n', from)) != std::string::npos; from = nl + 1) {
            std::string line = data.substr(from, nl - from);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
        }
        lines.push_back(data.substr(from));
        path = file;
        modified = false;
        diskMtime = st.st_mtime;
        undoStack.clear();
        redoStack.clear();
        cur = anchor = {0, 0};
        statesValid = 1;
        states.assign(1, LineState{});
        detect();
        return true;
    }
    // Saves to `target` (this document's path when empty): written beside it and renamed, so a failure never leaves half a file.
    bool save(const std::string& target, std::string& error) {
        const std::string file = target.empty() ? path : target;
        if (file.empty()) { error = "no file name"; return false; }
        if (readOnly) { error = "this file is not valid UTF-8: it is open read-only"; return false; }
        std::string real = file;
        char resolved[4096];
        if (realpath(file.c_str(), resolved)) real = resolved;  // (through a symbolic link: the file it points to is the one that changes)
        const std::string dir = real.substr(0, real.rfind('/') == std::string::npos ? 0 : real.rfind('/'));
        std::string tmp = (dir.empty() ? "." : dir) + "/.t3xt-XXXXXX";
        std::vector<char> tmpl(tmp.begin(), tmp.end());
        tmpl.push_back('\0');
        const int fd = mkstemp(tmpl.data());
        if (fd < 0) { error = std::strerror(errno); return false; }
        const std::string data = allText(crlf ? "\r\n" : "\n");
        bool ok = true;
        for (size_t done = 0; ok && done < data.size();) {
            const ssize_t n = write(fd, data.data() + done, data.size() - done);
            if (n <= 0) { if (errno == EINTR) continue; ok = false; error = std::strerror(errno); }
            else done += static_cast<size_t>(n);
        }
        struct stat old{};
        if (ok && stat(real.c_str(), &old) == 0) fchmod(fd, old.st_mode & 07777);
        else if (ok) fchmod(fd, 0644);
        if (ok && fsync(fd) != 0) { ok = false; error = std::strerror(errno); }
        close(fd);
        if (ok && rename(tmpl.data(), real.c_str()) != 0) { ok = false; error = std::strerror(errno); }
        if (!ok) { unlink(tmpl.data()); return false; }
        path = file;
        modified = false;
        struct stat now{};
        if (stat(real.c_str(), &now) == 0) diskMtime = now.st_mtime;
        detect();
        return true;
    }
    std::string title() const { const size_t s = path.rfind('/'); return path.empty() ? "Untitled" : path.substr(s == std::string::npos ? 0 : s + 1); }
};

}  // namespace t3
