#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

enum class MdAct { Bold, Italic, Strike, Code, Heading, Bullet, Numbered, Task, Quote, Link, CodeBlock, Rule };

struct MdWords {
    std::string bold = "bold text", italic = "italic text", strike = "strikethrough text", code = "code", link = "link text", url = "url";
};

struct MdEditResult {
    bool changed = false;
    int from = 0, to = 0;
    std::string with;
    int anc = 0, cur = 0;
};

namespace mdedit {
inline int lineStart(const std::string& s, int i) {
    i = std::clamp(i, 0, static_cast<int>(s.size()));
    while (i > 0 && s[i - 1] != '\n') i--;
    return i;
}
inline int lineEnd(const std::string& s, int i) {
    i = std::clamp(i, 0, static_cast<int>(s.size()));
    const size_t p = s.find('\n', static_cast<size_t>(i));
    return p == std::string::npos ? static_cast<int>(s.size()) : static_cast<int>(p);
}
inline bool blank(const std::string& line) { return line.find_first_not_of(" \t\r") == std::string::npos; }
inline bool wordByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) || (c & 0x80) || c == '_'; }

enum class Kind { None, Heading, Bullet, Numbered, Task, Quote };

struct Prefix {
    Kind kind = Kind::None;
    int indent = 0;
    int length = 0;
    int level = 0;
    int number = 0;
    bool done = false;
    char marker = '-';
};

inline Prefix prefixOf(const std::string& line) {
    Prefix p;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    p.indent = static_cast<int>(i);
    p.length = static_cast<int>(i);
    if (i >= line.size()) return p;
    const char c = line[i];
    if (c == '#') {
        size_t j = i;
        while (j < line.size() && line[j] == '#') j++;
        if (j - i <= 6 && (j == line.size() || line[j] == ' ')) {
            p.kind = Kind::Heading;
            p.level = static_cast<int>(j - i);
            p.length = static_cast<int>(j < line.size() ? j + 1 : j);
        }
    } else if ((c == '-' || c == '*' || c == '+') && i + 1 < line.size() && line[i + 1] == ' ') {
        p.marker = c;
        p.kind = Kind::Bullet;
        p.length = static_cast<int>(i + 2);
        if (line.size() >= i + 6 && line[i + 2] == '[' && line[i + 4] == ']' && line[i + 5] == ' ' && (line[i + 3] == ' ' || line[i + 3] == 'x' || line[i + 3] == 'X')) {
            p.kind = Kind::Task;
            p.done = line[i + 3] != ' ';
            p.length = static_cast<int>(i + 6);
        }
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
        size_t j = i;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) j++;
        if (j < line.size() && (line[j] == '.' || line[j] == ')') && j + 1 < line.size() && line[j + 1] == ' ' && j - i <= 9) {
            p.kind = Kind::Numbered;
            p.number = std::atoi(line.substr(i, j - i).c_str());
            p.length = static_cast<int>(j + 2);
        }
    } else if (c == '>') {
        p.kind = Kind::Quote;
        p.length = static_cast<int>(i + 1 < line.size() && line[i + 1] == ' ' ? i + 2 : i + 1);
    }
    return p;
}

inline std::string markerFor(Kind k, int level, int number) {
    switch (k) {
        case Kind::Heading: return std::string(static_cast<size_t>(std::clamp(level, 1, 6)), '#') + " ";
        case Kind::Bullet: return "- ";
        case Kind::Numbered: return std::to_string(number) + ". ";
        case Kind::Task: return "- [ ] ";
        case Kind::Quote: return "> ";
        default: return "";
    }
}

inline Kind kindOf(MdAct a) {
    switch (a) {
        case MdAct::Heading: return Kind::Heading;
        case MdAct::Bullet: return Kind::Bullet;
        case MdAct::Numbered: return Kind::Numbered;
        case MdAct::Task: return Kind::Task;
        case MdAct::Quote: return Kind::Quote;
        default: return Kind::None;
    }
}

inline std::string markOf(MdAct a) {
    switch (a) {
        case MdAct::Bold: return "**";
        case MdAct::Italic: return "*";
        case MdAct::Strike: return "~~";
        case MdAct::Code: return "`";
        default: return "";
    }
}
inline int runBefore(const std::string& s, int i, char c) { int n = 0; while (i - n - 1 >= 0 && s[i - n - 1] == c) n++; return n; }
inline int runAfter(const std::string& s, int i, char c) { int n = 0; while (i + n < static_cast<int>(s.size()) && s[i + n] == c) n++; return n; }

inline bool wrappedOutside(const std::string& s, int a, int b, MdAct act) {
    const std::string m = markOf(act);
    const int len = static_cast<int>(m.size());
    if (a < len || b + len > static_cast<int>(s.size())) return false;
    if (s.compare(static_cast<size_t>(a - len), static_cast<size_t>(len), m) != 0 || s.compare(static_cast<size_t>(b), static_cast<size_t>(len), m) != 0) return false;
    if (act == MdAct::Italic) return runBefore(s, a, '*') % 2 == 1 && runAfter(s, b, '*') % 2 == 1;
    if (act == MdAct::Bold) return runBefore(s, a, '*') >= 2 && runAfter(s, b, '*') >= 2;
    return true;
}
inline bool wrappedInside(const std::string& s, int a, int b, MdAct act) {
    const std::string m = markOf(act);
    const int len = static_cast<int>(m.size());
    if (b - a < 2 * len + 1) return false;
    if (s.compare(static_cast<size_t>(a), static_cast<size_t>(len), m) != 0 || s.compare(static_cast<size_t>(b - len), static_cast<size_t>(len), m) != 0) return false;
    if (act == MdAct::Italic) return runAfter(s, a, '*') % 2 == 1 && runBefore(s, b, '*') % 2 == 1;
    if (act == MdAct::Bold) return runAfter(s, a, '*') >= 2 && runBefore(s, b, '*') >= 2;
    return true;
}

inline MdEditResult replaceOf(int from, int to, std::string with, int anc, int cur) {
    MdEditResult r;
    r.changed = true;
    r.from = from;
    r.to = to;
    r.with = std::move(with);
    r.anc = anc;
    r.cur = cur;
    return r;
}

inline void wordAround(const std::string& s, int i, int& a, int& b) {
    a = b = std::clamp(i, 0, static_cast<int>(s.size()));
    while (a > 0 && wordByte(s[a - 1])) a--;
    while (b < static_cast<int>(s.size()) && wordByte(s[b])) b++;
}

inline MdEditResult inlineMark(const std::string& s, int anc, int cur, MdAct act, const MdWords& w) {
    int a = std::min(anc, cur), b = std::max(anc, cur);
    const std::string m = markOf(act);
    const int len = static_cast<int>(m.size());
    if (a == b) {
        int wa = 0, wb = 0;
        wordAround(s, a, wa, wb);
        if (wa == wb) {
            const bool off = wrappedOutside(s, a, b, act);
            if (off) return replaceOf(a - len, b + len, "", a - len, a - len);
            const std::string ph = act == MdAct::Bold ? w.bold : act == MdAct::Italic ? w.italic : act == MdAct::Strike ? w.strike : w.code;
            return replaceOf(a, b, m + ph + m, a + len, a + len + static_cast<int>(ph.size()));
        }
        a = wa;
        b = wb;
    }
    if (wrappedOutside(s, a, b, act)) return replaceOf(a - len, b + len, s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a)), a - len, b - len);
    if (wrappedInside(s, a, b, act)) {
        const std::string inner = s.substr(static_cast<size_t>(a + len), static_cast<size_t>(b - a - 2 * len));
        return replaceOf(a, b, inner, a, a + static_cast<int>(inner.size()));
    }
    const std::string inner = s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
    return replaceOf(a, b, m + inner + m, a + len, a + len + static_cast<int>(inner.size()));
}

inline bool looksLikeUrl(const std::string& t) {
    return t.compare(0, 7, "http://") == 0 || t.compare(0, 8, "https://") == 0 || t.compare(0, 4, "www.") == 0;
}
inline MdEditResult link(const std::string& s, int anc, int cur, const MdWords& w) {
    int a = std::min(anc, cur), b = std::max(anc, cur);
    if (a == b) {
        int wa = 0, wb = 0;
        wordAround(s, a, wa, wb);
        a = wa;
        b = wb;
    }
    const std::string sel = s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
    if (sel.empty()) {
        const std::string out = "[" + w.link + "](" + w.url + ")";
        return replaceOf(a, b, out, a + 1, a + 1 + static_cast<int>(w.link.size()));
    }
    if (looksLikeUrl(sel)) {
        const std::string out = "[" + w.link + "](" + sel + ")";
        return replaceOf(a, b, out, a + 1, a + 1 + static_cast<int>(w.link.size()));
    }
    const std::string out = "[" + sel + "](" + w.url + ")";
    const int ua = a + 1 + static_cast<int>(sel.size()) + 2;
    return replaceOf(a, b, out, ua, ua + static_cast<int>(w.url.size()));
}

struct Block {
    int from = 0, to = 0;
    std::vector<std::string> lines;
};
inline Block blockOf(const std::string& s, int a, int b) {
    Block k;
    k.from = lineStart(s, a);
    const int last = (b > a && b > 0 && s[b - 1] == '\n') ? b - 1 : b;
    k.to = lineEnd(s, last);
    size_t i = static_cast<size_t>(k.from);
    while (true) {
        const size_t nl = s.find('\n', i);
        const size_t e = (nl == std::string::npos || static_cast<int>(nl) > k.to) ? static_cast<size_t>(k.to) : nl;
        k.lines.push_back(s.substr(i, e - i));
        if (e >= static_cast<size_t>(k.to)) break;
        i = e + 1;
    }
    return k;
}
inline std::string join(const std::vector<std::string>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) { if (i) o += '\n'; o += v[i]; }
    return o;
}
inline std::string stripPrefix(const std::string& line, const Prefix& p) { return line.substr(static_cast<size_t>(p.length)); }

inline int mapCol(int col, int oldPrefix, int newPrefix, int newLen) {
    return std::clamp(col < oldPrefix ? newPrefix : col + (newPrefix - oldPrefix), 0, newLen);
}

inline MdEditResult lineKind(const std::string& s, int anc, int cur, MdAct act) {
    const int a = std::min(anc, cur), b = std::max(anc, cur);
    const Block k = blockOf(s, a, b);
    const Kind want = kindOf(act);
    const bool many = k.lines.size() > 1;
    std::vector<Prefix> pre;
    for (const auto& l : k.lines) pre.push_back(prefixOf(l));

    int level = 0;
    bool off = false;
    if (want == Kind::Heading) {
        const int now = pre[0].kind == Kind::Heading ? pre[0].level : 0;
        level = now >= 3 ? 0 : now + 1;
        off = level == 0;
    } else {
        bool any = false;
        off = true;
        for (size_t i = 0; i < k.lines.size(); i++) {
            if (many && blank(k.lines[i])) continue;
            any = true;
            if (pre[i].kind != want) off = false;
        }
        if (!any) off = false;
    }

    std::vector<std::string> out;
    std::vector<int> oldPre, newPre;
    int number = 0;
    for (size_t i = 0; i < k.lines.size(); i++) {
        const std::string& l = k.lines[i];
        const Prefix& p = pre[i];
        const int old = p.kind == Kind::None ? p.indent : p.length;
        const std::string rest = l.substr(static_cast<size_t>(old));
        std::string head;
        if (many && blank(l)) head = l.substr(0, static_cast<size_t>(old));
        else if (want == Kind::Heading) head = off ? "" : markerFor(Kind::Heading, level, 0);
        else head = l.substr(0, static_cast<size_t>(p.indent)) + (off ? "" : markerFor(want, 0, ++number));
        out.push_back(head + rest);
        oldPre.push_back(old);
        newPre.push_back(static_cast<int>(head.size()));
    }

    const std::string with = join(out);
    auto where = [&](int pos) {
        int ls = k.from;
        for (size_t i = 0; i < k.lines.size(); i++) {
            const int le = ls + static_cast<int>(k.lines[i].size());
            if (pos <= le || i + 1 == k.lines.size()) {
                int ns = k.from;
                for (size_t j = 0; j < i; j++) ns += static_cast<int>(out[j].size()) + 1;
                return ns + mapCol(pos - ls, oldPre[i], newPre[i], static_cast<int>(out[i].size()));
            }
            ls = le + 1;
        }
        return k.from;
    };
    if (many && a != b) return replaceOf(k.from, k.to, with, k.from, k.from + static_cast<int>(with.size()));
    return replaceOf(k.from, k.to, with, where(anc), where(cur));
}

inline MdEditResult codeBlock(const std::string& s, int anc, int cur) {
    const int a = std::min(anc, cur), b = std::max(anc, cur);
    const Block k = blockOf(s, a, b);
    const int up = k.from > 0 ? lineStart(s, k.from - 1) : -1;
    const int dn = k.to < static_cast<int>(s.size()) ? lineEnd(s, k.to + 1) : -1;
    if (up >= 0 && dn >= 0) {
        const std::string above = s.substr(static_cast<size_t>(up), static_cast<size_t>(k.from - 1 - up)), below = s.substr(static_cast<size_t>(k.to + 1), static_cast<size_t>(dn - k.to - 1));
        if (above.compare(0, 3, "```") == 0 && below.compare(0, 3, "```") == 0) {
            const std::string body = join(k.lines);
            return replaceOf(up, dn, body, up, up + static_cast<int>(body.size()));
        }
    }
    if (a == b && blank(k.lines[0])) {
        const std::string out = "```\n\n```";
        return replaceOf(k.from, k.to, out, k.from + 4, k.from + 4);
    }
    const std::string body = join(k.lines);
    return replaceOf(k.from, k.to, "```\n" + body + "\n```", k.from + 4, k.from + 4 + static_cast<int>(body.size()));
}

inline MdEditResult rule(const std::string& s, int cur) {
    const int ls = lineStart(s, cur), le = lineEnd(s, cur);
    const bool empty = blank(s.substr(static_cast<size_t>(ls), static_cast<size_t>(le - ls)));
    if (empty) {
        const bool prevBlank = ls == 0 || blank(s.substr(static_cast<size_t>(lineStart(s, ls - 1)), static_cast<size_t>(ls - 1 - lineStart(s, ls - 1))));
        const std::string with = std::string(prevBlank ? "" : "\n") + "---\n";
        return replaceOf(ls, le, with, ls + static_cast<int>(with.size()), ls + static_cast<int>(with.size()));
    }
    const std::string with = "\n\n---\n";
    return replaceOf(le, le, with, le + static_cast<int>(with.size()), le + static_cast<int>(with.size()));
}
}

inline MdEditResult mdApply(const std::string& s, int anc, int cur, MdAct act, const MdWords& w = MdWords()) {
    anc = std::clamp(anc, 0, static_cast<int>(s.size()));
    cur = std::clamp(cur, 0, static_cast<int>(s.size()));
    switch (act) {
        case MdAct::Bold:
        case MdAct::Italic:
        case MdAct::Strike:
        case MdAct::Code: return mdedit::inlineMark(s, anc, cur, act, w);
        case MdAct::Link: return mdedit::link(s, anc, cur, w);
        case MdAct::CodeBlock: return mdedit::codeBlock(s, anc, cur);
        case MdAct::Rule: return mdedit::rule(s, cur);
        default: return mdedit::lineKind(s, anc, cur, act);
    }
}

inline bool mdActive(const std::string& s, int anc, int cur, MdAct act) {
    using namespace mdedit;
    anc = std::clamp(anc, 0, static_cast<int>(s.size()));
    cur = std::clamp(cur, 0, static_cast<int>(s.size()));
    int a = std::min(anc, cur), b = std::max(anc, cur);
    switch (act) {
        case MdAct::Bold:
        case MdAct::Italic:
        case MdAct::Strike:
        case MdAct::Code: {
            if (a == b) {
                int wa = 0, wb = 0;
                wordAround(s, a, wa, wb);
                if (wa == wb) return wrappedOutside(s, a, b, act);
                a = wa;
                b = wb;
            }
            return wrappedOutside(s, a, b, act) || wrappedInside(s, a, b, act);
        }
        case MdAct::Heading:
        case MdAct::Bullet:
        case MdAct::Numbered:
        case MdAct::Task:
        case MdAct::Quote: {
            const Block k = blockOf(s, a, b);
            const Kind want = kindOf(act);
            bool any = false;
            for (const auto& l : k.lines) {
                if (blank(l) && k.lines.size() > 1) continue;
                any = true;
                if (prefixOf(l).kind != want) return false;
            }
            return any;
        }
        case MdAct::Link: {
            const int ls = lineStart(s, a), le = lineEnd(s, a);
            const std::string line = s.substr(static_cast<size_t>(ls), static_cast<size_t>(le - ls));
            const int c = a - ls;
            for (size_t i = 0; i < line.size(); i++) {
                if (line[i] != '[') continue;
                const size_t close = line.find("](", i);
                const size_t end = close == std::string::npos ? close : line.find(')', close);
                if (close != std::string::npos && end != std::string::npos && c >= static_cast<int>(i) && c <= static_cast<int>(end) + 1) return true;
            }
            return false;
        }
        case MdAct::CodeBlock: {
            const Block k = blockOf(s, a, b);
            const int up = k.from > 0 ? lineStart(s, k.from - 1) : -1;
            const int dn = k.to < static_cast<int>(s.size()) ? lineEnd(s, k.to + 1) : -1;
            return up >= 0 && dn >= 0 && s.compare(static_cast<size_t>(up), 3, "```") == 0 && s.compare(static_cast<size_t>(k.to + 1), 3, "```") == 0;
        }
        default: return false;
    }
}

inline MdEditResult mdEnter(const std::string& s, int cur) {
    using namespace mdedit;
    cur = std::clamp(cur, 0, static_cast<int>(s.size()));
    const int ls = lineStart(s, cur), le = lineEnd(s, cur);
    const std::string line = s.substr(static_cast<size_t>(ls), static_cast<size_t>(le - ls));
    const Prefix p = prefixOf(line);
    if (p.kind != Kind::Bullet && p.kind != Kind::Numbered && p.kind != Kind::Task && p.kind != Kind::Quote) return {};
    if (cur - ls < p.length) return {};
    const std::string rest = line.substr(static_cast<size_t>(p.length));
    if (blank(rest) && cur == le) return replaceOf(ls, le, "", ls, ls);
    std::string marker = line.substr(0, static_cast<size_t>(p.indent));
    if (p.kind == Kind::Bullet) marker += std::string(1, p.marker) + " ";
    else if (p.kind == Kind::Task) marker += std::string(1, p.marker) + " [ ] ";
    else if (p.kind == Kind::Numbered) marker += std::to_string(p.number + 1) + (line[static_cast<size_t>(p.length) - 2] == ')' ? ") " : ". ");
    else marker += "> ";
    const std::string with = "\n" + marker;
    return replaceOf(cur, cur, with, cur + static_cast<int>(with.size()), cur + static_cast<int>(with.size()));
}
