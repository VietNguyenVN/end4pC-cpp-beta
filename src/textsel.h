// Single-line text selection: the model every one-line text field of the shell shares (Settings, the launcher, the wallpaper
// selector's search). Pure code: no Wayland, no Pango, so tests/text exercises it directly.
//
// A field is the text plus two byte offsets into it: the caret and the selection's anchor (equal: no selection). Offsets always
// sit on a UTF-8 character boundary. The host owns the three variables (they live in each program's own state) and builds a
// Field that refers to them, then calls what it needs:
//
//   textsel::Field f(g.edit, g.caret, g.anchor);
//   textsel::Result r = textsel::handleKey(f, textsel::Key::Left, ctrl, shift);   // an arrow, Home/End, Backspace, Ctrl+A ...
//   f.insert(typed);                                                              // typing and pasting replace the selection
//   textsel::applyClick(f, indexUnderPointer, clicks.press(nowMs, x, y), shift);  // mouse: caret, word, whole text
//   f.dragTo(indexUnderPointer);                                                  // while the button is held
//
// The host does the clipboard itself (Result::copyText, Result::wantPaste) and draws the highlight (f.lo(), f.hi()).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace textsel {
inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

inline size_t prevChar(const std::string& s, size_t i) {
    i = std::min(i, s.size());
    if (i == 0) return 0;
    do { i--; } while (i > 0 && isContinuation(s[i]));
    return i;
}
inline size_t nextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do { i++; } while (i < s.size() && isContinuation(s[i]));
    return i;
}

inline bool isWordByte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u >= 0x80;
}
inline size_t prevWord(const std::string& s, size_t i) {
    i = std::min(i, s.size());
    while (i > 0 && !isWordByte(s[prevChar(s, i)])) i = prevChar(s, i);
    while (i > 0 && isWordByte(s[prevChar(s, i)])) i = prevChar(s, i);
    return i;
}
inline size_t nextWord(const std::string& s, size_t i) {
    i = std::min(i, s.size());
    while (i < s.size() && !isWordByte(s[i])) i = nextChar(s, i);
    while (i < s.size() && isWordByte(s[i])) i = nextChar(s, i);
    return i;
}
inline std::pair<size_t, size_t> wordAt(const std::string& s, size_t i) {
    if (s.empty()) return {0, 0};
    i = std::min(i, s.size());
    size_t at = i >= s.size() ? prevChar(s, s.size()) : i;
    const bool word = isWordByte(s[at]);
    size_t a = at, b = nextChar(s, at);
    while (a > 0 && isWordByte(s[prevChar(s, a)]) == word) a = prevChar(s, a);
    while (b < s.size() && isWordByte(s[b]) == word) b = nextChar(s, b);
    return {a, b};
}

inline std::string sanitize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\n' || c == '\r' || c == '\t') out += ' ';
        else if (u < 0x20 || u == 0x7f) continue;
        else out += c;
    }
    return out;
}

struct Field {
    std::string& text;
    size_t& caret;
    size_t& anchor;

    Field(std::string& t, size_t& c, size_t& a) : text(t), caret(c), anchor(a) { clamp(); }

    void clamp() {
        caret = std::min(caret, text.size());
        anchor = std::min(anchor, text.size());
        while (caret > 0 && caret < text.size() && isContinuation(text[caret])) caret--;
        while (anchor > 0 && anchor < text.size() && isContinuation(text[anchor])) anchor--;
    }
    bool hasSel() const { return caret != anchor; }
    size_t lo() const { return std::min(caret, anchor); }
    size_t hi() const { return std::max(caret, anchor); }
    std::string selected() const { return text.substr(lo(), hi() - lo()); }

    size_t snap(size_t at) const {
        at = std::min(at, text.size());
        while (at > 0 && at < text.size() && isContinuation(text[at])) at--;
        return at;
    }
    void collapse(size_t at) { caret = anchor = snap(at); }
    void set(const std::string& t) { text = t; caret = anchor = text.size(); }
    void selectAll() { anchor = 0; caret = text.size(); }
    void setCaret(size_t at, bool extend) {
        caret = snap(at);
        if (!extend) anchor = caret;
    }
    void dragTo(size_t at) { setCaret(at, true); }
    void selectWordAt(size_t at) {
        const auto [a, b] = wordAt(text, snap(at));
        anchor = a;
        caret = b;
    }

    void moveLeft(bool extend, bool word) {
        if (hasSel() && !extend) { collapse(lo()); return; }
        setCaret(word ? prevWord(text, caret) : prevChar(text, caret), extend);
    }
    void moveRight(bool extend, bool word) {
        if (hasSel() && !extend) { collapse(hi()); return; }
        setCaret(word ? nextWord(text, caret) : nextChar(text, caret), extend);
    }
    void home(bool extend) { setCaret(0, extend); }
    void end(bool extend) { setCaret(text.size(), extend); }

    bool insert(const std::string& what) {
        const std::string clean = sanitize(what);
        if (clean.empty() && !hasSel()) return false;
        const size_t from = lo();
        text.replace(from, hi() - from, clean);
        caret = anchor = from + clean.size();
        return true;
    }
    bool eraseSelection() {
        if (!hasSel()) return false;
        const size_t from = lo();
        text.erase(from, hi() - from);
        caret = anchor = from;
        return true;
    }
    bool backspace(bool word) {
        if (eraseSelection()) return true;
        if (caret == 0) return false;
        const size_t from = word ? prevWord(text, caret) : prevChar(text, caret);
        text.erase(from, caret - from);
        caret = anchor = from;
        return true;
    }
    bool del(bool word) {
        if (eraseSelection()) return true;
        if (caret >= text.size()) return false;
        const size_t to = word ? nextWord(text, caret) : nextChar(text, caret);
        text.erase(caret, to - caret);
        anchor = caret;
        return true;
    }
};

enum class Key { None, Left, Right, Home, End, Backspace, Delete, SelectAll, Copy, Cut, Paste };

struct Result {
    bool handled = false;
    bool changed = false;
    bool repeat = false;
    bool wantPaste = false;
    bool wantCopy = false;
    std::string copyText;
};

inline Result handleKey(Field& f, Key k, bool ctrl, bool shift) {
    Result r;
    r.handled = k != Key::None;
    switch (k) {
        case Key::Left: f.moveLeft(shift, ctrl); r.repeat = true; break;
        case Key::Right: f.moveRight(shift, ctrl); r.repeat = true; break;
        case Key::Home: f.home(shift); break;
        case Key::End: f.end(shift); break;
        case Key::Backspace: r.changed = f.backspace(ctrl); r.repeat = true; break;
        case Key::Delete: r.changed = f.del(ctrl); r.repeat = true; break;
        case Key::SelectAll: f.selectAll(); break;
        case Key::Copy:
            if (f.hasSel()) { r.wantCopy = true; r.copyText = f.selected(); }
            break;
        case Key::Cut:
            if (f.hasSel()) { r.wantCopy = true; r.copyText = f.selected(); r.changed = f.eraseSelection(); }
            break;
        case Key::Paste: r.wantPaste = true; break;
        case Key::None: break;
    }
    return r;
}

struct Clicks {
    int n = 0;
    int64_t at = 0;
    double x = 0, y = 0;
    int press(int64_t nowMs, double px, double py, int64_t intervalMs = 400, double slop = 5) {
        const bool again = n > 0 && nowMs - at <= intervalMs && std::fabs(px - x) <= slop && std::fabs(py - y) <= slop;
        n = again ? n % 3 + 1 : 1;
        at = nowMs;
        x = px;
        y = py;
        return n;
    }
    void reset() { n = 0; }
};

inline void applyClick(Field& f, size_t index, int clicks, bool shift) {
    if (clicks >= 3) f.selectAll();
    else if (clicks == 2) f.selectWordAt(index);
    else f.setCaret(index, shift);
}

struct Drag {
    int clicks = 1;
    size_t lo = 0, hi = 0;
};
inline Drag beginDrag(const Field& f, int clicks) { return {clicks, f.lo(), f.hi()}; }
inline void drag(Field& f, size_t at, const Drag& d) {
    at = f.snap(at);
    if (d.clicks >= 3) { f.selectAll(); return; }
    if (d.clicks == 2) {
        const auto [a, b] = wordAt(f.text, at);
        if (at < d.lo) { f.anchor = d.hi; f.caret = std::min(a, d.lo); }
        else { f.anchor = d.lo; f.caret = std::max(b, d.hi); }
        return;
    }
    f.dragTo(at);
}
}
