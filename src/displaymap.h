#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace dm {
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

struct Screen {
    std::string name;
    Rect r;
    bool disabled = false;
};

inline void logicalSize(int pixelsW, int pixelsH, double scale, int transform, int& w, int& h) {
    if (scale < 0.1) scale = 1;
    int lw = static_cast<int>(std::lround(pixelsW / scale)), lh = static_cast<int>(std::lround(pixelsH / scale));
    if (transform & 1) std::swap(lw, lh);
    w = std::max(1, lw);
    h = std::max(1, lh);
}

inline bool parseMode(const std::string& mode, int& w, int& h) {
    int a = 0, b = 0;
    if (std::sscanf(mode.c_str(), "%dx%d", &a, &b) != 2 || a <= 0 || b <= 0) return false;
    w = a;
    h = b;
    return true;
}

inline bool overlaps(const Rect& a, const Rect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

inline bool touches(const Rect& a, const Rect& b) {
    const bool sideBySide = (a.right() == b.x || b.right() == a.x) && a.y < b.bottom() && b.y < a.bottom();
    const bool stacked = (a.bottom() == b.y || b.bottom() == a.y) && a.x < b.right() && b.x < a.right();
    return sideBySide || stacked;
}

namespace detail {
inline bool blocked(const std::vector<Screen>& s, size_t self, const Rect& r) {
    for (size_t i = 0; i < s.size(); i++)
        if (i != self && !s[i].disabled && overlaps(r, s[i].r)) return true;
    return false;
}

inline int nearest(int want, const std::vector<int>& candidates, int threshold) {
    int best = INT_MIN, bestD = threshold + 1;
    for (int c : candidates) {
        const int d = std::abs(c - want);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
}
}

inline void place(const std::vector<Screen>& screens, size_t self, int wantX, int wantY, int threshold, bool snap, int& outX, int& outY) {
    const Rect me = screens[self].r;
    int x = wantX, y = wantY;
    std::vector<size_t> others;
    for (size_t i = 0; i < screens.size(); i++)
        if (i != self && !screens[i].disabled) others.push_back(i);
    if (others.empty() || screens[self].disabled) { outX = x; outY = y; return; }

    if (snap) {
        std::vector<int> xs, ys;
        for (size_t i : others) {
            const Rect& o = screens[i].r;
            xs.insert(xs.end(), {o.right(), o.x - me.w, o.x, o.right() - me.w, o.x + o.w / 2 - me.w / 2});
            ys.insert(ys.end(), {o.bottom(), o.y - me.h, o.y, o.bottom() - me.h, o.y + o.h / 2 - me.h / 2});
        }
        const int sx = detail::nearest(x, xs, threshold), sy = detail::nearest(y, ys, threshold);
        if (sx != INT_MIN) x = sx;
        if (sy != INT_MIN) y = sy;
    }

    for (size_t pass = 0; pass < others.size() + 2; pass++) {
        const Rect cur{x, y, me.w, me.h};
        size_t hit = SIZE_MAX;
        for (size_t i : others)
            if (overlaps(cur, screens[i].r)) { hit = i; break; }
        if (hit == SIZE_MAX) break;
        const Rect& o = screens[hit].r;
        const int cand[4][2] = {{o.right(), y}, {o.x - me.w, y}, {x, o.bottom()}, {x, o.y - me.h}};
        long best = LONG_MAX;
        int bx = x, by = y;
        for (const auto& c : cand) {
            if (detail::blocked(screens, self, Rect{c[0], c[1], me.w, me.h})) continue;
            const long d = std::labs(static_cast<long>(c[0]) - x) + std::labs(static_cast<long>(c[1]) - y);
            if (d < best) { best = d; bx = c[0]; by = c[1]; }
        }
        if (best == LONG_MAX) {
            bx = cand[0][0];
            by = cand[0][1];
        }
        x = bx;
        y = by;
    }

    bool touching = false;
    for (size_t i : others)
        if (touches(Rect{x, y, me.w, me.h}, screens[i].r)) { touching = true; break; }
    if (!touching) {
        long best = LONG_MAX;
        int bx = x, by = y;
        for (size_t i : others) {
            const Rect& o = screens[i].r;
            const int yMin = o.y - me.h + 1, yMax = o.bottom() - 1, xMin = o.x - me.w + 1, xMax = o.right() - 1;
            const int cand[4][2] = {{o.right(), std::clamp(y, yMin, yMax)}, {o.x - me.w, std::clamp(y, yMin, yMax)},
                                    {std::clamp(x, xMin, xMax), o.bottom()}, {std::clamp(x, xMin, xMax), o.y - me.h}};
            for (const auto& c : cand) {
                if (detail::blocked(screens, self, Rect{c[0], c[1], me.w, me.h})) continue;
                const long d = std::labs(static_cast<long>(c[0]) - x) + std::labs(static_cast<long>(c[1]) - y);
                if (d < best) { best = d; bx = c[0]; by = c[1]; }
            }
        }
        x = bx;
        y = by;
    }
    outX = x;
    outY = y;
}

inline void autoLayout(std::vector<Screen>& screens) {
    int x = 0;
    for (auto& s : screens) {
        if (s.disabled) continue;
        s.r.x = x;
        s.r.y = 0;
        x += s.r.w;
    }
}

inline Rect bounds(const std::vector<Screen>& screens) {
    Rect b;
    bool any = false;
    for (int pass = 0; pass < 2 && !any; pass++)
        for (const auto& s : screens) {
            if (pass == 0 && s.disabled) continue;
            if (!any) { b = s.r; any = true; continue; }
            const int x2 = std::max(b.right(), s.r.right()), y2 = std::max(b.bottom(), s.r.bottom());
            b.x = std::min(b.x, s.r.x);
            b.y = std::min(b.y, s.r.y);
            b.w = x2 - b.x;
            b.h = y2 - b.y;
        }
    return b;
}
}
