// The Freeform board of the Tools window: strokes and text boxes placed anywhere, with undo/redo and a text form to keep them.
// Pure data and geometry (no drawing, no Wayland), so tests/freeform can run it on its own.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ff {
constexpr size_t kMaxItems = 4000, kMaxPoints = 20000, kMaxText = 20000, kMaxHistory = 60;
constexpr int kShapeFlat = 0, kShapeRounded = 1, kShapeEllipse = 2;
constexpr double kMaxCoord = 100000, kMinSize = 1, kMaxSize = 200;

struct Pt { double x = 0, y = 0; };
struct Box {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(Pt p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
};

struct Item {
    enum Kind { Stroke, Text, Link, Shape } kind = Stroke;
    int id = 0;               // never 0 once on a board; what a link points at
    bool boxed = false;       // a text drawn as a concept node (a box around it, in `shape`)
    int shape = 1;            // of a node or a Shape: 0 a rectangle with flat corners, 1 with rounded corners, 2 an ellipse
    bool filled = false;      // a Shape: tinted inside, not only an outline
    std::string mshape;       // a Shape: the name of a Material shape (assets/shapes.txt); empty means `shape` above
    int from = 0, to = 0;     // a link: the ids of the two items it joins (an arrow runs from `from` to `to`); its text is the phrase on it
    unsigned rgb = 0xffffff;  // 0xRRGGBB
    double size = 4;          // a stroke's width, a text's height in pixels
    std::vector<Pt> pts;      // a stroke's path; a text's top-left corner is pts[0]; a Shape's two opposite corners
    std::string text;
    std::string font;         // a text's font family; empty is the shell's default
    double textW = 0, textH = 0;  // the outer size of a text or node, measured by the page once laid out (not stored)
};
using Items = std::vector<Item>;

inline bool finite(double v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoord; }

inline int indexOfId(const Items& items, int id) {
    if (id == 0) return -1;
    for (size_t i = 0; i < items.size(); i++) if (items[i].id == id) return static_cast<int>(i);
    return -1;
}
inline int nextId(const Items& items) {
    int m = 0;
    for (const Item& it : items) m = std::max(m, it.id);
    return m + 1;
}

inline Box bounds(const Item& it) {
    if (it.pts.empty()) return {};
    if (it.kind == Item::Text) return {it.pts[0].x, it.pts[0].y, it.textW, it.textH};
    double x0 = it.pts[0].x, y0 = it.pts[0].y, x1 = x0, y1 = y0;
    for (const Pt& p : it.pts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    const double r = it.size / 2;
    return {x0 - r, y0 - r, x1 - x0 + 2 * r, y1 - y0 + 2 * r};
}

// Where a line from the centre of `b` toward `toward` leaves the box.
inline Pt edgePoint(const Box& b, Pt toward, int shape = kShapeFlat) {
    const Pt c{b.x + b.w / 2, b.y + b.h / 2};
    const double dx = toward.x - c.x, dy = toward.y - c.y;
    if ((dx == 0 && dy == 0) || b.w <= 0 || b.h <= 0) return c;
    if (shape == kShapeEllipse) {
        const double k = 1 / std::hypot(dx / (b.w / 2), dy / (b.h / 2));
        return {c.x + dx * k, c.y + dy * k};
    }
    const double sx = dx != 0 ? (b.w / 2) / std::fabs(dx) : 1e18, sy = dy != 0 ? (b.h / 2) / std::fabs(dy) : 1e18;
    const double t = std::min(sx, sy);
    return {c.x + dx * t, c.y + dy * t};
}

// The two ends of a link, on the borders of the boxes it joins. False when one of them is gone.
inline bool linkEnds(const Items& items, const Item& link, Pt& a, Pt& b) {
    const int i = indexOfId(items, link.from), j = indexOfId(items, link.to);
    if (i < 0 || j < 0 || i == j || items[i].pts.empty() || items[j].pts.empty()) return false;
    const Box bi = bounds(items[i]), bj = bounds(items[j]);
    const Pt ci{bi.x + bi.w / 2, bi.y + bi.h / 2}, cj{bj.x + bj.w / 2, bj.y + bj.h / 2};
    auto shapeOf = [](const Item& it) { return it.kind == Item::Shape || it.boxed ? it.shape : kShapeFlat; };
    a = edgePoint(bi, cj, shapeOf(items[i]));
    b = edgePoint(bj, ci, shapeOf(items[j]));
    return true;
}

inline Box boundsIn(const Items& items, const Item& it) {
    if (it.kind != Item::Link) return bounds(it);
    Pt a, b;
    if (!linkEnds(items, it, a, b)) return {};
    const double r = it.size / 2;
    return {std::min(a.x, b.x) - r, std::min(a.y, b.y) - r, std::fabs(a.x - b.x) + 2 * r, std::fabs(a.y - b.y) + 2 * r};
}

inline bool contentBounds(const Items& items, Box& out) {
    bool any = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const Item& it : items) {
        const Box b = boundsIn(items, it);
        if (b.w <= 0 && b.h <= 0) continue;
        if (!any) { x0 = b.x; y0 = b.y; x1 = b.x + b.w; y1 = b.y + b.h; any = true; continue; }
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y); x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    if (any) out = {x0, y0, x1 - x0, y1 - y0};
    return any;
}

inline double segmentDistance(Pt p, Pt a, Pt b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Is p on the item, give or take `tol` pixels? (`items` is the board: a link needs the boxes it joins.)
inline bool hit(const Items& items, const Item& it, Pt p, double tol) {
    if (it.kind == Item::Link) {
        Pt a, b;
        return linkEnds(items, it, a, b) && segmentDistance(p, a, b) <= it.size / 2 + tol + 3;
    }
    if (it.pts.empty()) return false;
    if (it.kind == Item::Shape) {
        if (it.pts.size() < 2) return false;
        const Box b = bounds(it);
        if (it.filled || !it.mshape.empty()) return Box{b.x - tol, b.y - tol, b.w + 2 * tol, b.h + 2 * tol}.contains(p);  // (a Material shape is taken anywhere in its box)
        // an outline is only taken on its line (the empty inside stays free for what is drawn there)
        const double reach = it.size / 2 + tol + 2, x0 = b.x + it.size / 2, y0 = b.y + it.size / 2, x1 = b.x + b.w - it.size / 2, y1 = b.y + b.h - it.size / 2;
        if (it.shape == kShapeEllipse) {
            const double rx = std::max(1.0, (x1 - x0) / 2), ry = std::max(1.0, (y1 - y0) / 2);
            const double d = std::hypot((p.x - (x0 + x1) / 2) / rx, (p.y - (y0 + y1) / 2) / ry);
            return std::fabs(d - 1) * std::min(rx, ry) <= reach;
        }
        return segmentDistance(p, {x0, y0}, {x1, y0}) <= reach || segmentDistance(p, {x1, y0}, {x1, y1}) <= reach ||
               segmentDistance(p, {x1, y1}, {x0, y1}) <= reach || segmentDistance(p, {x0, y1}, {x0, y0}) <= reach;
    }
    if (it.kind == Item::Text) {
        Box b = bounds(it);
        b = {b.x - tol, b.y - tol, b.w + 2 * tol, b.h + 2 * tol};
        return b.contains(p);
    }
    const double reach = it.size / 2 + tol;
    if (it.pts.size() == 1) return std::hypot(p.x - it.pts[0].x, p.y - it.pts[0].y) <= reach;
    for (size_t i = 1; i < it.pts.size(); i++)
        if (segmentDistance(p, it.pts[i - 1], it.pts[i]) <= reach) return true;
    return false;
}
inline bool hit(const Item& it, Pt p, double tol) { return it.kind != Item::Link && hit(Items{}, it, p, tol); }

// The topmost item under p (the last one drawn), or -1. Nodes and texts win over the links that end at them.
inline int topAt(const Items& items, Pt p, double tol) {
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; i--)
        if (items[i].kind != Item::Link && hit(items, items[i], p, tol)) return i;
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; i--)
        if (items[i].kind == Item::Link && hit(items, items[i], p, tol)) return i;
    return -1;
}

inline Item moved(Item it, double dx, double dy) {
    for (Pt& p : it.pts) { p.x += dx; p.y += dy; }
    return it;
}

// Adds a point to a stroke unless it is closer than minDist to the last one (a still hand makes no points).
inline bool addPoint(Item& it, Pt p, double minDist) {
    if (it.pts.size() >= kMaxPoints || !finite(p.x) || !finite(p.y)) return false;
    if (!it.pts.empty() && std::hypot(p.x - it.pts.back().x, p.y - it.pts.back().y) < minDist) return false;
    it.pts.push_back(p);
    return true;
}

// ---- resizing: handles on the corners and edges of the selection ---------------------------------------------------------------------
// 0 top-left, 1 top, 2 top-right, 3 right, 4 bottom-right, 5 bottom, 6 bottom-left, 7 left
constexpr double kMinBox = 10, kMinTextSize = 8;
inline Box grown(const Box& b, double d) { return {b.x - d, b.y - d, b.w + 2 * d, b.h + 2 * d}; }

inline Pt handlePoint(const Box& b, int h) {
    const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.x + b.w, bt = b.y + b.h;
    switch (h) {
        case 0: return {b.x, b.y};
        case 1: return {cx, b.y};
        case 2: return {r, b.y};
        case 3: return {r, cy};
        case 4: return {r, bt};
        case 5: return {cx, bt};
        case 6: return {b.x, bt};
        default: return {b.x, cy};
    }
}
// The handle under p (the corners win where handles overlap), or -1. cornersOnly: a text only scales, so it has no edge handles.
inline int handleAt(const Box& b, Pt p, double tol, bool cornersOnly) {
    static const int order[8] = {0, 2, 4, 6, 1, 3, 5, 7};
    for (int i = 0; i < (cornersOnly ? 4 : 8); i++) {
        const Pt q = handlePoint(b, order[i]);
        if (std::hypot(p.x - q.x, p.y - q.y) <= tol) return order[i];
    }
    return -1;
}

// The box after dragging handle h of `start` to p. The opposite side stays put; the box never gets smaller than minSize;
// keepAspect (corners) keeps the proportions of `start`, growing toward whichever side the pointer went further.
inline Box resizeBox(const Box& start, int h, Pt p, bool keepAspect, double minSize = kMinBox) {
    double x0 = start.x, y0 = start.y, x1 = start.x + start.w, y1 = start.y + start.h;
    if (h == 0 || h == 6 || h == 7) x0 = std::min(p.x, x1 - minSize);
    if (h == 2 || h == 3 || h == 4) x1 = std::max(p.x, x0 + minSize);
    if (h == 0 || h == 1 || h == 2) y0 = std::min(p.y, y1 - minSize);
    if (h == 4 || h == 5 || h == 6) y1 = std::max(p.y, y0 + minSize);
    if (keepAspect && h % 2 == 0 && start.w > 0 && start.h > 0) {
        const double s = std::max(minSize / std::min(start.w, start.h), std::max((x1 - x0) / start.w, (y1 - y0) / start.h));
        const double w = start.w * s, ht = start.h * s;
        if (h == 0 || h == 6) x0 = x1 - w; else x1 = x0 + w;
        if (h == 0 || h == 2) y0 = y1 - ht; else y1 = y0 + ht;
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

// The item stretched from the box it occupied (`from`, its bounds) to `to`. A stroke keeps its pen width, a shape keeps its outline width,
// a text or node grows its letters by the change in height (the page measures it again).
inline Item resized(Item it, const Box& from, const Box& to) {
    if (it.pts.empty() || from.w <= 0 || from.h <= 0) return it;
    if (it.kind == Item::Text) {
        it.size = std::clamp(it.size * to.h / from.h, kMinTextSize, kMaxSize);
        it.pts[0] = {to.x, to.y};
        return it;
    }
    const double r = it.size / 2;
    const Box a = grown(from, -r), b = grown(to, -r);
    auto mapX = [&](double x) { return a.w > 0 ? b.x + (x - a.x) * b.w / a.w : b.x + b.w / 2; };
    auto mapY = [&](double y) { return a.h > 0 ? b.y + (y - a.y) * b.h / a.h : b.y + b.h / 2; };
    for (Pt& p : it.pts) p = {mapX(p.x), mapY(p.y)};
    return it;
}

// Takes an item off the board with the links that end at it.
inline Items without(Items items, int index) {
    if (index < 0 || index >= static_cast<int>(items.size())) return items;
    const int id = items[index].id;
    Items out;
    for (size_t i = 0; i < items.size(); i++) {
        if (static_cast<int>(i) == index) continue;
        if (items[i].kind == Item::Link && id != 0 && (items[i].from == id || items[i].to == id)) continue;
        out.push_back(std::move(items[i]));
    }
    return out;
}

inline bool linked(const Items& items, int a, int b) {
    for (const Item& it : items)
        if (it.kind == Item::Link && ((it.from == a && it.to == b) || (it.from == b && it.to == a))) return true;
    return false;
}

inline bool overlaps(const Box& a, const Box& b, double gap) {
    return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;
}

// A top-left corner near `want` where a box of w x h touches nothing else: tries lower and lower spots, then further right.
inline Pt freeSpot(const Items& items, double w, double h, Pt want, double gap = 16) {
    for (int col = 0; col < 6; col++) {
        for (int row = 0; row < 40; row++) {
            const Pt p{want.x + col * (w + gap), want.y + row * (h + gap)};
            bool clear = true;
            for (const Item& it : items) {
                if (it.kind == Item::Link) continue;
                if (overlaps({p.x, p.y, w, h}, bounds(it), gap / 2)) { clear = false; break; }
            }
            if (clear) return p;
        }
    }
    return want;
}

// ---- undo / redo: whole-board snapshots (a board is small) -----------------------------------------------------------------------
struct History {
    std::vector<Items> past, future;
    void record(const Items& before) {
        past.push_back(before);
        if (past.size() > kMaxHistory) past.erase(past.begin());
        future.clear();
    }
    bool undo(Items& cur) {
        if (past.empty()) return false;
        future.push_back(cur);
        cur = std::move(past.back());
        past.pop_back();
        return true;
    }
    bool redo(Items& cur) {
        if (future.empty()) return false;
        past.push_back(cur);
        cur = std::move(future.back());
        future.pop_back();
        return true;
    }
};

// ---- the file form: one item per line --------------------------------------------------------------------------------------------
//   S id rrggbb size n x y x y ...             a stroke
//   R id rrggbb size shape filled x0 y0 x1 y1 [name]   a rectangle, an ellipse or a Material shape
//   T id rrggbb size boxed shape x y font<TAB>text   a text or a node (a line with no tab has no font) (\n for a line break, \\ for a backslash)
//   L id rrggbb size from to text              a link and the phrase on it
// Text from the file is cut to kMaxText at a character boundary and anything that is not valid UTF-8 is dropped (Pango would warn on it).
inline std::string cleanUtf8(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size() && out.size() < kMaxText) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= in.size();
        for (size_t k = 1; ok && k < n; k++) ok = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (ok && n == 2) ok = c >= 0xC2;  // no overlong forms
        if (ok && n == 3) {
            const unsigned cp = (c & 0x0Fu) << 12 | (in[i + 1] & 0x3Fu) << 6 | (in[i + 2] & 0x3Fu);
            ok = cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF);
        }
        if (ok && n == 4) {
            const unsigned cp = (c & 0x07u) << 18 | (in[i + 1] & 0x3Fu) << 12 | (in[i + 2] & 0x3Fu) << 6 | (in[i + 3] & 0x3Fu);
            ok = cp >= 0x10000 && cp <= 0x10FFFF;
        }
        if (!ok) { i++; continue; }
        if (out.size() + n > kMaxText) break;
        out.append(in, i, n);
        i += n;
    }
    return out;
}

// A font family name as the file keeps it: printable UTF-8, no tabs or line breaks, at most 100 bytes.
inline std::string cleanFont(const std::string& in) {
    std::string out;
    for (char c : cleanUtf8(in.substr(0, 100))) if (static_cast<unsigned char>(c) >= 32 && c != 127) out += c;
    return out;
}

// A Material shape name as the file keeps it: letters and digits only, at most 40.
inline std::string cleanName(const std::string& in) {
    std::string out;
    for (char c : in) if (std::isalnum(static_cast<unsigned char>(c)) && out.size() < 40) out += c;
    return out;
}

inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c != '\r') o += c;
    }
    return o;
}
inline std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; o += s[i] == 'n' ? '\n' : s[i]; }
        else o += s[i];
    }
    return o;
}

inline std::string serialize(const Items& items) {
    std::string out;
    char buf[112];
    for (const Item& it : items) {
        if (it.kind == Item::Link) {
            std::snprintf(buf, sizeof buf, "L %d %06x %.1f %d %d ", it.id, it.rgb & 0xffffff, it.size, it.from, it.to);
            out += buf;
            out += escape(it.text);
        } else if (it.pts.empty()) {
            continue;
        } else if (it.kind == Item::Shape) {
            if (it.pts.size() < 2) continue;
            std::snprintf(buf, sizeof buf, "R %d %06x %.1f %d %d %.1f %.1f %.1f %.1f", it.id, it.rgb & 0xffffff, it.size, it.shape, it.filled ? 1 : 0,
                          it.pts[0].x, it.pts[0].y, it.pts[1].x, it.pts[1].y);
            out += buf;
            if (!it.mshape.empty()) out += " " + cleanName(it.mshape);
        } else if (it.kind == Item::Stroke) {
            std::snprintf(buf, sizeof buf, "S %d %06x %.1f %zu", it.id, it.rgb & 0xffffff, it.size, it.pts.size());
            out += buf;
            for (const Pt& p : it.pts) { std::snprintf(buf, sizeof buf, " %.1f %.1f", p.x, p.y); out += buf; }
        } else {
            std::snprintf(buf, sizeof buf, "T %d %06x %.1f %d %d %.1f %.1f ", it.id, it.rgb & 0xffffff, it.size, it.boxed ? 1 : 0, it.shape, it.pts[0].x, it.pts[0].y);
            out += buf;
            out += cleanFont(it.font);
            out += '\t';
            out += escape(it.text);
        }
        out += '\n';
    }
    return out;
}

// Reads what serialize wrote. Lines it does not understand are skipped; numbers are bounded (the file is outside our control).
// Ids are made unique, and links whose ends are missing are dropped.
inline Items parse(const std::string& data) {
    Items items;
    size_t pos = 0;
    while (pos < data.size() && items.size() < kMaxItems) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        const std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        if (line.size() < 3 || (line[0] != 'S' && line[0] != 'T' && line[0] != 'L' && line[0] != 'R') || line[1] != ' ') continue;
        const char* p = line.c_str() + 2;
        char* e = nullptr;
        Item it;
        it.kind = line[0] == 'S' ? Item::Stroke : line[0] == 'T' ? Item::Text : line[0] == 'R' ? Item::Shape : Item::Link;
        auto num = [&](double& v) { v = std::strtod(p, &e); if (e == p || !std::isfinite(v)) return false; p = e; return true; };
        double v = 0;
        if (!num(v) || v < 1 || v > 1e9) continue;
        it.id = static_cast<int>(v);
        it.rgb = static_cast<unsigned>(std::strtoul(p, &e, 16)) & 0xffffff;
        if (e == p) continue;
        p = e;
        if (!num(it.size)) continue;
        it.size = std::clamp(it.size, kMinSize, kMaxSize);
        bool ok = true;
        if (it.kind == Item::Stroke) {
            double n = 0;
            if (!num(n) || n < 1 || n > kMaxPoints) continue;
            for (double i = 0; i < n && ok; i++) {
                Pt pt;
                if (!num(pt.x) || !num(pt.y) || !finite(pt.x) || !finite(pt.y)) { ok = false; break; }
                it.pts.push_back(pt);
            }
        } else if (it.kind == Item::Shape) {
            double shape = 0, filled = 0;
            Pt a, b;
            if (!num(shape) || !num(filled) || !num(a.x) || !num(a.y) || !num(b.x) || !num(b.y)) continue;
            if (!finite(a.x) || !finite(a.y) || !finite(b.x) || !finite(b.y)) continue;
            it.shape = std::clamp(static_cast<int>(shape), 0, 2);
            it.filled = filled != 0;
            it.pts = {a, b};
            while (*p == ' ') p++;
            it.mshape = cleanName(p);
        } else if (it.kind == Item::Text) {
            double boxed = 0, shape = 0;
            Pt pt;
            if (!num(boxed) || !num(shape) || !num(pt.x) || !num(pt.y) || !finite(pt.x) || !finite(pt.y)) continue;
            it.boxed = boxed != 0;
            it.shape = std::clamp(static_cast<int>(shape), 0, 2);
            it.pts.push_back(pt);
            if (*p == ' ') p++;
            const std::string rest = p;
            const size_t tab = rest.find('\t');
            if (tab != std::string::npos) it.font = cleanFont(rest.substr(0, tab));
            it.text = cleanUtf8(unescape(tab == std::string::npos ? rest : rest.substr(tab + 1)));
        } else {
            double a = 0, b = 0;
            if (!num(a) || !num(b) || a < 1 || b < 1 || a > 1e9 || b > 1e9) continue;
            it.from = static_cast<int>(a);
            it.to = static_cast<int>(b);
            if (*p == ' ') p++;
            it.text = cleanUtf8(unescape(std::string(p)));
        }
        if (!ok || (it.kind != Item::Link && it.pts.empty())) continue;
        items.push_back(std::move(it));
    }
    // unique ids (a hand-edited file may repeat one), then drop the links that lost an end
    for (size_t i = 0; i < items.size(); i++)
        for (size_t j = 0; j < i; j++)
            if (items[j].id == items[i].id) { items[i].id = nextId(items); break; }
    Items out;
    for (Item& it : items)
        if (it.kind != Item::Link || (indexOfId(items, it.from) >= 0 && indexOfId(items, it.to) >= 0 && it.from != it.to)) out.push_back(std::move(it));
    return out;
}
}  // namespace ff
