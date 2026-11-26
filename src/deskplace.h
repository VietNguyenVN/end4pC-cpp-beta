#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace deskplace {
enum class Anchor { TopRight, BottomLeft, TopLeft, BottomRight };

struct Preset {
    const char* name;
    int priority;
    Anchor anchor;
};

inline Preset presetFor(const std::string& name) {
    static const Preset table[] = {
        {"weather", 10, Anchor::TopRight},   {"calendar", 20, Anchor::TopRight}, {"resources", 30, Anchor::TopRight},
        {"player", 40, Anchor::BottomLeft},  {"clock", 50, Anchor::BottomLeft},  {"usercard", 60, Anchor::TopLeft},
        {"todo", 70, Anchor::TopRight},      {"image", 80, Anchor::TopRight},
        {"text", 85, Anchor::TopLeft},
    };
    for (const Preset& p : table) if (name == p.name) return p;
    return {"", 90, Anchor::TopRight};
}

struct Rect {
    int l = 0, t = 0, w = 0, h = 0;
};

struct Item {
    std::string name;
    int w = 0, h = 0, minW = 0, minH = 0;
    bool resizable = false;
    bool pinned = false;
    bool keep = false;
    int l = -1, t = -1;
    bool placed() const { return l >= 0; }
};

inline bool touches(const Rect& a, const Rect& b, int gap) {
    return a.l < b.l + b.w + gap && b.l < a.l + a.w + gap && a.t < b.t + b.h + gap && b.t < a.t + a.h + gap;
}

inline bool blocked(const std::vector<Rect>& obstacles, const Rect& r, int gap) {
    for (const Rect& o : obstacles) if (touches(r, o, gap)) return true;
    return false;
}

inline bool findSpot(int W, int H, int g, const std::vector<Rect>& obstacles, Anchor anchor, int w, int h, int& outL, int& outT) {
    const int maxL = W - w - g, maxT = H - h - g;
    if (maxL < g || maxT < g) return false;
    std::vector<int> ls = {maxL, g}, ts = {g, maxT};
    for (const Rect& o : obstacles) {
        ls.push_back(o.l - g - w);
        ls.push_back(o.l + o.w + g);
        ts.push_back(o.t + o.h + g);
        ts.push_back(o.t - g - h);
    }
    const bool rightFirst = anchor == Anchor::TopRight || anchor == Anchor::BottomRight;
    const bool bottomFirst = anchor == Anchor::BottomLeft || anchor == Anchor::BottomRight;
    std::sort(ls.begin(), ls.end());
    ls.erase(std::unique(ls.begin(), ls.end()), ls.end());
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
    if (rightFirst) std::reverse(ls.begin(), ls.end());
    if (bottomFirst) std::reverse(ts.begin(), ts.end());
    constexpr int kSameColumn = 24;
    std::vector<std::vector<int>> columns;
    for (int l : ls) {
        if (l < g || l > maxL) continue;
        if (!columns.empty() && std::abs(columns.back().front() - l) <= kSameColumn) columns.back().push_back(l);
        else columns.push_back({l});
    }
    for (const std::vector<int>& column : columns) {
        for (int t : ts) {
            if (t < g || t > maxT) continue;
            for (int l : column)
                if (!blocked(obstacles, {l, t, w, h}, g)) { outL = l; outT = t; return true; }
        }
    }
    return false;
}

inline bool placeOne(int W, int H, int g, const std::vector<Rect>& obstacles, Item& it, int& overlapped) {
    const Anchor anchor = presetFor(it.name).anchor;
    const int steps = it.resizable ? 8 : 0;
    for (int s = 0; s <= steps; s++) {
        const int w = it.resizable ? it.w + (it.minW - it.w) * s / steps : it.w;
        const int h = it.resizable ? it.h + (it.minH - it.h) * s / steps : it.h;
        int l = 0, t = 0;
        if (findSpot(W, H, g, obstacles, anchor, w, h, l, t)) { it.w = w; it.h = h; it.l = l; it.t = t; return true; }
    }
    if (it.resizable) { it.w = it.minW; it.h = it.minH; }
    const bool right = anchor == Anchor::TopRight || anchor == Anchor::BottomRight, bottom = anchor == Anchor::BottomLeft || anchor == Anchor::BottomRight;
    const int off = 28 * overlapped++;
    const int maxL = std::max(g, W - it.w - g), maxT = std::max(g, H - it.h - g);
    it.l = std::clamp(right ? maxL - off : g + off, g, maxL);
    it.t = std::clamp(bottom ? maxT - off : g + off, g, maxT);
    return false;
}

inline void clampInside(int W, int H, int g, Item& it) {
    it.l = std::clamp(it.l, g, std::max(g, W - it.w - g));
    it.t = std::clamp(it.t, g, std::max(g, H - it.h - g));
}

inline int placeAll(int W, int H, int g, std::vector<Item>& items) {
    std::vector<Rect> obstacles;
    for (Item& it : items)
        if ((it.pinned || it.keep) && it.placed()) { clampInside(W, H, g, it); obstacles.push_back({it.l, it.t, it.w, it.h}); }
    std::vector<Item*> order;
    for (Item& it : items) if (!(it.pinned || it.keep) || !it.placed()) order.push_back(&it);
    std::stable_sort(order.begin(), order.end(), [](const Item* a, const Item* b) { return presetFor(a->name).priority < presetFor(b->name).priority; });
    int overlapped = 0, over = 0;
    for (Item* it : order) {
        if (!placeOne(W, H, g, obstacles, *it, overlapped)) over++;
        obstacles.push_back({it->l, it->t, it->w, it->h});
    }
    return over;
}

inline bool layoutFits(int W, int H, const std::vector<Item>& items) {
    constexpr int kSliver = 32;
    for (size_t i = 0; i < items.size(); i++) {
        const Item& a = items[i];
        if (!a.placed()) continue;
        if (a.l < 0 || a.t < 0 || a.l + a.w > W || a.t + a.h > H) return false;
        for (size_t j = i + 1; j < items.size(); j++) {
            const Item& b = items[j];
            if (!b.placed()) continue;
            const int ix = std::min(a.l + a.w, b.l + b.w) - std::max(a.l, b.l), iy = std::min(a.t + a.h, b.t + b.h) - std::max(a.t, b.t);
            if (ix > 0 && iy > 0 && std::min(ix, iy) > kSliver) return false;
        }
    }
    return true;
}

// Magnetic moving: while a widget is dragged its edges are pulled to the gap beside another widget, to the screen margin and to the
// other widgets' edges and centres, and the gaps left around it are reported so they can be drawn with their size in pixels.
struct Guide { bool vertical = false; int pos = 0, from = 0, to = 0; };  // a line of aligned edges (vertical: x = pos, from..to is y)
struct Spacing { bool horizontal = true; int from = 0, to = 0, cross = 0; };  // the free space between two widgets (value = to - from)
struct Snap { int l = 0, t = 0; std::vector<Guide> guides; std::vector<Spacing> spaces; };

inline int snapAxis(int pos, int size, int lo, int hi, int g, const std::vector<std::pair<int, int>>& near, const std::vector<std::pair<int, int>>& across, int threshold, int grid = 0) {
    // near: [start, size] of the widgets that share rows (or columns) with the dragged one, so they can sit beside it;
    // across: every widget, for the edge and centre alignments.
    std::vector<int> candidates = {lo, hi - size};
    for (const auto& n : near) { candidates.push_back(n.first + n.second + g); candidates.push_back(n.first - g - size); }
    for (const auto& n : across) {
        candidates.push_back(n.first);
        candidates.push_back(n.first + n.second - size);
        candidates.push_back(n.first + (n.second - size) / 2);
    }
    int best = std::clamp(pos, lo, std::max(lo, hi - size)), bestDist = threshold + 1;
    bool found = false;
    for (int c : candidates) {
        if (c < lo || c > hi - size) continue;
        const int d = std::abs(c - pos);
        if (d < bestDist) { bestDist = d; best = c; found = true; }
    }
    if (!found && grid > 0) best = std::clamp((best + grid / 2) / grid * grid, lo, std::max(lo, hi - size));  // (nothing to stick to: the grid)
    return best;
}

// The free space beside a rectangle and the lines where its edges match another widget's (what is drawn while it moves).
inline void describe(Snap& s, const Rect& r, const std::vector<Rect>& obstacles, int reach = 160) {
    for (const Rect& o : obstacles) {
        const int yFrom = std::max(r.t, o.t), yTo = std::min(r.t + r.h, o.t + o.h), xFrom = std::max(r.l, o.l), xTo = std::min(r.l + r.w, o.l + o.w);
        if (yTo > yFrom) {
            const int cross = (yFrom + yTo) / 2;
            if (o.l + o.w <= r.l && r.l - (o.l + o.w) <= reach) s.spaces.push_back({true, o.l + o.w, r.l, cross});
            if (r.l + r.w <= o.l && o.l - (r.l + r.w) <= reach) s.spaces.push_back({true, r.l + r.w, o.l, cross});
        }
        if (xTo > xFrom) {
            const int cross = (xFrom + xTo) / 2;
            if (o.t + o.h <= r.t && r.t - (o.t + o.h) <= reach) s.spaces.push_back({false, o.t + o.h, r.t, cross});
            if (r.t + r.h <= o.t && o.t - (r.t + r.h) <= reach) s.spaces.push_back({false, r.t + r.h, o.t, cross});
        }
        const int tops[2][2] = {{r.l, o.l}, {r.l + r.w, o.l + o.w}};
        for (const auto& e : tops) if (e[0] == e[1]) s.guides.push_back({true, e[0], std::min(r.t, o.t), std::max(r.t + r.h, o.t + o.h)});
        const int rows[2][2] = {{r.t, o.t}, {r.t + r.h, o.t + o.h}};
        for (const auto& e : rows) if (e[0] == e[1]) s.guides.push_back({false, e[0], std::min(r.l, o.l), std::max(r.l + r.w, o.l + o.w)});
    }
}

inline Snap snapMove(int W, int H, int g, const std::vector<Rect>& obstacles, Rect r, int threshold = 10, int reach = 160, int grid = 0) {
    auto overlap = [](int a, int as, int b, int bs) { return a < b + bs && b < a + as; };
    std::vector<std::pair<int, int>> nearX, acrossX, nearY, acrossY;
    for (const Rect& o : obstacles) {
        acrossX.push_back({o.l, o.w});
        if (overlap(r.t, r.h, o.t, o.h)) nearX.push_back({o.l, o.w});
    }
    Snap s;
    r.l = snapAxis(r.l, r.w, g, W - g, g, nearX, acrossX, threshold, grid);
    for (const Rect& o : obstacles) {
        acrossY.push_back({o.t, o.h});
        if (overlap(r.l, r.w, o.l, o.w)) nearY.push_back({o.t, o.h});
    }
    r.t = snapAxis(r.t, r.h, g, H - g, g, nearY, acrossY, threshold, grid);
    s.l = r.l;
    s.t = r.t;
    describe(s, r, obstacles, reach);
    return s;
}

// Resizing: the edges that move are pulled to the gap beside a neighbour (or to its edge) when they come within `threshold`.
// L/R/T/B tell which edges move; the others stay.
inline void snapResize(int g, const std::vector<Rect>& obstacles, double& left, double& right, double& top, double& bottom, bool L, bool R, bool T, bool B, int threshold = 12, int grid = 0) {
    auto pull = [&](double& v, const std::vector<int>& candidates) {
        int best = -1, bestDist = threshold + 1;
        for (int c : candidates) { const int d = std::abs(c - static_cast<int>(v)); if (d < bestDist) { bestDist = d; best = c; } }
        if (best >= 0) v = best;
        return best >= 0;
    };
    auto overlap = [](double a, double b, int c, int cs) { return a < c + cs && c < b; };
    std::vector<int> ls, rs, ts, bs;
    for (const Rect& o : obstacles) {
        if (overlap(top, bottom, o.t, o.h)) { ls.push_back(o.l + o.w + g); rs.push_back(o.l - g); }
        ls.push_back(o.l); rs.push_back(o.l + o.w);
        if (overlap(left, right, o.l, o.w)) { ts.push_back(o.t + o.h + g); bs.push_back(o.t - g); }
        ts.push_back(o.t); bs.push_back(o.t + o.h);
    }
    if (L && !pull(left, ls) && grid > 0) left = (static_cast<int>(left) + grid / 2) / grid * grid;
    if (R) pull(right, rs);
    if (T && !pull(top, ts) && grid > 0) top = (static_cast<int>(top) + grid / 2) / grid * grid;
    if (B) pull(bottom, bs);
}

// "Tidy": the widgets that share a column with rects[pivot] are stacked from the top with exactly `g` between them, all on the pivot's
// side (right edges together when the pivot is on the right half of the screen, left edges otherwise). The result keeps the order.
inline std::vector<Rect> tidyColumn(int W, int H, int g, std::vector<Rect> rects, size_t pivot) {
    if (pivot >= rects.size()) return rects;
    const Rect p = rects[pivot];
    std::vector<size_t> column;
    for (size_t i = 0; i < rects.size(); i++)
        if (i == pivot || (rects[i].l < p.l + p.w && p.l < rects[i].l + rects[i].w)) column.push_back(i);
    std::stable_sort(column.begin(), column.end(), [&](size_t a, size_t b) { return rects[a].t < rects[b].t; });
    const bool right = p.l + p.w / 2 > W / 2;
    int total = 0;
    for (size_t i : column) total += rects[i].h;
    total += g * static_cast<int>(column.size() - 1);
    int y = std::clamp(rects[column.front()].t, g, std::max(g, H - g - total));
    for (size_t i : column) {
        Rect& r = rects[i];
        r.t = y;
        r.l = std::clamp(right ? p.l + p.w - r.w : p.l, g, std::max(g, W - g - r.w));
        y += r.h + g;
    }
    return rects;
}
}
