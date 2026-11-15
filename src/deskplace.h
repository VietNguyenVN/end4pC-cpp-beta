#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
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
}
