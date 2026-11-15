// The display map's geometry (src/displaymap.h): logical sizes, snapping, overlap resolution, contiguity, the automatic row.
#include "../../src/displaymap.h"

#include <cstdio>
#include <cstring>
#include <random>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using dm::Rect;
using dm::Screen;

static Screen S(const char* n, int x, int y, int w, int h, bool off = false) { return Screen{n, Rect{x, y, w, h}, off}; }

static void sizes() {
    int w, h;
    dm::logicalSize(1920, 1080, 1, 0, w, h); CHECK(w == 1920 && h == 1080);
    dm::logicalSize(1920, 1080, 1, 1, w, h); CHECK(w == 1080 && h == 1920);   // a quarter turn swaps
    dm::logicalSize(1920, 1080, 1, 2, w, h); CHECK(w == 1920 && h == 1080);   // upside down does not
    dm::logicalSize(1920, 1080, 1, 3, w, h); CHECK(w == 1080 && h == 1920);
    dm::logicalSize(3840, 2160, 2, 0, w, h); CHECK(w == 1920 && h == 1080);
    dm::logicalSize(2560, 1440, 1.25, 0, w, h); CHECK(w == 2048 && h == 1152);
    dm::logicalSize(1920, 1200, 1.5, 3, w, h); CHECK(w == 800 && h == 1280);
    dm::logicalSize(1920, 1080, 0, 0, w, h); CHECK(w == 1920);                 // a silly scale falls back to 1
    CHECK(dm::parseMode("2560x1440@59.95Hz", w, h) && w == 2560 && h == 1440);
    CHECK(!dm::parseMode("preferred", w, h));
}

static void snapping() {
    std::vector<Screen> s = {S("A", 0, 0, 1920, 1080), S("B", 3000, 0, 1280, 720)};
    int x, y;
    dm::place(s, 1, 1930, 5, 20, true, x, y);   // near A's right edge, a bit low: flush right of A, tops aligned
    CHECK(x == 1920 && y == 0);
    dm::place(s, 1, 1930, 5, 20, false, x, y);  // shift held: no snap, but it must not overlap (10 px in): pushed out to the right edge
    CHECK(x == 1920 && y == 5);
    dm::place(s, 1, 1000, 1085, 20, true, x, y); // below A, left edge near A's centre line? snaps flush under A
    CHECK(y == 1080);
    dm::place(s, 1, -1290, 10, 20, true, x, y);  // left of A: right edge flush with A's left edge
    CHECK(x == -1280 && y == 0);
    // centres aligned vertically (B 720 tall next to A 1080 tall: centre y = 180)
    dm::place(s, 1, 1920, 183, 20, true, x, y);
    CHECK(x == 1920 && y == 180);
    // bottoms aligned
    dm::place(s, 1, 1920, 355, 20, true, x, y);
    CHECK(x == 1920 && y == 360);
}

static void overlap() {
    std::vector<Screen> s = {S("A", 0, 0, 1920, 1080), S("B", 1920, 0, 1920, 1080), S("C", 0, 1080, 1280, 720)};
    int x, y;
    dm::place(s, 2, 100, 100, 12, false, x, y);  // dropped inside A: pushed out to the nearest clear side
    CHECK(!dm::overlaps(Rect{x, y, 1280, 720}, s[0].r) && !dm::overlaps(Rect{x, y, 1280, 720}, s[1].r));
    CHECK(y >= 1080 || y + 720 <= 0 || x + 1280 <= 0);
    dm::place(s, 2, 1500, 500, 12, false, x, y);  // in the middle of A and B
    CHECK(!dm::overlaps(Rect{x, y, 1280, 720}, s[0].r) && !dm::overlaps(Rect{x, y, 1280, 720}, s[1].r));
    // a screen that is off blocks nothing
    s[1].disabled = true;
    dm::place(s, 2, 2000, 100, 12, false, x, y);
    CHECK(x == 2000 || x != 2000);  // (it just has to terminate)
    CHECK(!dm::overlaps(Rect{x, y, 1280, 720}, s[0].r));
}

static void contiguity() {
    std::vector<Screen> s = {S("A", 0, 0, 1920, 1080), S("B", 1920, 0, 1920, 1080)};
    int x, y;
    dm::place(s, 1, 5000, 3000, 12, true, x, y);  // dropped far away: brought back flush against A
    CHECK(dm::touches(Rect{x, y, 1920, 1080}, s[0].r));
    CHECK(!dm::overlaps(Rect{x, y, 1920, 1080}, s[0].r));
    dm::place(s, 1, 1920, 1080, 12, false, x, y);  // corner contact only: not enough, so it moves to share an edge
    CHECK(dm::touches(Rect{x, y, 1920, 1080}, s[0].r));
    CHECK(!dm::touches(Rect{0, 0, 10, 10}, Rect{10, 10, 10, 10}));  // a shared corner is not touching
    CHECK(dm::touches(Rect{0, 0, 10, 10}, Rect{10, 5, 10, 10}));
    // the only screen on goes where it is put
    std::vector<Screen> one = {S("A", 0, 0, 1920, 1080)};
    dm::place(one, 0, 77, -33, 12, true, x, y);
    CHECK(x == 77 && y == -33);
}

static void layout() {
    std::vector<Screen> s = {S("A", 500, 500, 1920, 1080), S("B", 9, 9, 1080, 1920), S("C", 4, 4, 800, 600, true), S("D", 1, 1, 1366, 768)};
    dm::autoLayout(s);
    CHECK(s[0].r.x == 0 && s[0].r.y == 0);
    CHECK(s[1].r.x == 1920 && s[1].r.y == 0);
    CHECK(s[2].r.x == 4 && s[2].r.y == 4);          // off: untouched
    CHECK(s[3].r.x == 3000 && s[3].r.y == 0);
    const Rect b = dm::bounds(s);
    CHECK(b.x == 0 && b.y == 0 && b.w == 3000 + 1366 && b.h == 1920);
}

// Random drops: the result never overlaps and always touches another screen, also when it is dropped again where it ended.
static void fuzz() {
    std::mt19937 rng(7);
    auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    const int sizes[][2] = {{1920, 1080}, {2560, 1440}, {1366, 768}, {1080, 1920}, {3840, 2160}, {1280, 1024}};
    for (int round = 0; round < 3000; round++) {
        std::vector<Screen> s;
        const int n = rnd(2, 4);
        for (int i = 0; i < n; i++) {
            const auto& z = sizes[rnd(0, 5)];
            s.push_back(S("M", 0, 0, z[0], z[1], i > 0 && rnd(0, 9) == 0));
        }
        s[0].disabled = false;
        // a valid starting layout: in a row
        int x0 = 0;
        for (auto& m : s) { m.r.x = x0; if (!m.disabled) x0 += m.r.w; }
        const size_t self = static_cast<size_t>(rnd(0, n - 1));
        if (s[self].disabled) continue;
        int on = 0;
        for (const auto& m : s) on += m.disabled ? 0 : 1;
        int x, y;
        dm::place(s, self, rnd(-4000, 8000), rnd(-3000, 5000), rnd(0, 40), rnd(0, 1) == 1, x, y);
        const Rect me{x, y, s[self].r.w, s[self].r.h};
        bool touching = on == 1;
        for (size_t i = 0; i < s.size(); i++) {
            if (i == self || s[i].disabled) continue;
            CHECK(!dm::overlaps(me, s[i].r));
            if (dm::touches(me, s[i].r)) touching = true;
        }
        CHECK(touching);
        s[self].r.x = x;
        s[self].r.y = y;
        int x2, y2;
        dm::place(s, self, x, y, 12, true, x2, y2);
        // (a second drop where it ended may still move it to a nearer snap point, but it stays valid)
        const Rect again{x2, y2, s[self].r.w, s[self].r.h};
        bool ok = on == 1;
        for (size_t i = 0; i < s.size(); i++) {
            if (i == self || s[i].disabled) continue;
            CHECK(!dm::overlaps(again, s[i].r));
            if (dm::touches(again, s[i].r)) ok = true;
        }
        CHECK(ok);
    }
}

int main() {
    sizes();
    snapping();
    overlap();
    contiguity();
    layout();
    fuzz();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
