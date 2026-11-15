// Checks src/deskplace.h (where the desktop widgets go) on many screen sizes. Build and run: tests/desk/run.sh
// (or `meson test -C build`). It prints each layout so the result can be read, and fails when a widget leaves the screen, two touch
// without need, or a pinned widget moves.
#include "../../src/deskplace.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskplace;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int kGap = 12, kBarArea = 52;  // the gap between widgets; the bar's thickness plus its gap, taken off the screen's height

static Item widget(const char* name, int w, int h, int minW, int minH, bool resizable) {
    Item it;
    it.name = name; it.w = w; it.h = h; it.minW = minW; it.minH = minH; it.resizable = resizable;
    return it;
}
// The widgets as the user has them today (sizes from ~/.local/state/end4pC), plus the others at their default sizes.
static std::vector<Item> userFour() {
    return {widget("clock", 420, 200, 180, 120, true), widget("weather", 360, 212, 360, 212, false), widget("calendar", 358, 300, 280, 300, true),
            widget("resources", 357, 467, 200, 200, true)};
}
static std::vector<Item> all8() {
    auto v = userFour();
    v.push_back(widget("player", 400, 420, 340, 340, true));
    v.push_back(widget("usercard", 525, 286, 300, 240, true));
    v.push_back(widget("todo", 280, 300, 280, 300, true));
    v.push_back(widget("image", 953, 463, 80, 80, true));
    return v;
}

static void draw(int W, int H, const std::vector<Item>& items) {  // a small map: one letter per widget (the first letter of its name, upper case when it is on top)
    const int cols = 64, rows = std::max(6, 64 * H / W / 2);
    std::vector<std::string> map(rows, std::string(cols, '.'));
    for (const Item& it : items) {
        if (!it.placed()) continue;
        for (int y = it.t * rows / H; y <= std::min(rows - 1, (it.t + it.h - 1) * rows / H); y++)
            for (int x = it.l * cols / W; x <= std::min(cols - 1, (it.l + it.w - 1) * cols / W); x++) map[y][x] = it.name[0];
    }
    for (const auto& r : map) std::printf("    %s\n", r.c_str());
}

static void report(const char* title, int sw, int sh, int W, int H, const std::vector<Item>& items, int over) {
    std::printf("\n== %s: screen %dx%d, usable %dx%d, %d placed over others\n", title, sw, sh, W, H, over);
    for (const Item& it : items) std::printf("  %-10s %4dx%-4d at (%4d,%4d)%s%s\n", it.name.c_str(), it.w, it.h, it.l, it.t, it.pinned ? "  pinned" : "", it.resizable && false ? "" : "");
    draw(W, H, items);
}

static void checkInside(int W, int H, const std::vector<Item>& items) {
    for (const Item& it : items)
        CHECK(it.l >= 0 && it.t >= 0 && it.l + it.w <= W && it.t + it.h <= H, "%s is outside the %dx%d screen at (%d,%d) %dx%d", it.name.c_str(), W, H, it.l, it.t, it.w, it.h);
}
static int touching(const std::vector<Item>& items, int gap) {
    int n = 0;
    for (size_t i = 0; i < items.size(); i++)
        for (size_t j = i + 1; j < items.size(); j++)
            if (blocked({{items[j].l, items[j].t, items[j].w, items[j].h}}, {items[i].l, items[i].t, items[i].w, items[i].h}, gap)) n++;
    return n;
}

static void run(const char* title, int sw, int sh, std::vector<Item> items, bool expectNoOverlap) {
    const int W = sw, H = sh - kBarArea;
    const int over = placeAll(W, H, kGap, items);
    report(title, sw, sh, W, H, items, over);
    checkInside(W, H, items);
    if (expectNoOverlap) {
        CHECK(over == 0, "%s: %d widgets had to go over others", title, over);
        CHECK(touching(items, kGap) == 0, "%s: some widgets are closer than the gap", title);
    }
}

int main() {
    // 1. The user's screen and widgets: the same arrangement as today (a right column from the top, the clock at the bottom left).
    {
        std::vector<Item> items = userFour();
        run("1920x1080, the user's four", 1920, 1080, items, true);
        const int W = 1920, H = 1080 - kBarArea;
        std::vector<Item> v = userFour();
        placeAll(W, H, kGap, v);
        auto find = [&](const char* n) -> const Item& { for (const Item& it : v) if (it.name == n) return it; return v[0]; };
        CHECK(find("weather").l == W - kGap - 360 && find("weather").t == kGap, "weather is not at the top right");
        CHECK(find("calendar").l + find("calendar").w == W - kGap && find("calendar").t == find("weather").t + 212 + kGap, "calendar is not right under the weather");
        CHECK(find("resources").t == find("calendar").t + find("calendar").h + kGap, "resources is not under the calendar");
        CHECK(find("clock").l == kGap && find("clock").t + find("clock").h == H - kGap, "clock is not at the bottom left");
        // The positions saved today count as valid on this screen, and not on a smaller one.
        std::vector<Item> saved = userFour();
        const int pos[4][2] = {{24, 428}, {1540, 12}, {1542, 237}, {1543, 553}};
        for (int i = 0; i < 4; i++) { saved[i].l = pos[i][0]; saved[i].t = pos[i][1]; }
        CHECK(layoutFits(1920, 1080 - kBarArea, saved), "the saved positions of today should fit 1920x1080");
        CHECK(!layoutFits(1440, 900 - kBarArea, saved), "the saved positions of today should not fit 1440x900");
    }
    // 2. Smaller screens: nothing outside, nothing over anything.
    run("1440x900", 1440, 900, userFour(), true);
    run("1366x768", 1366, 768, userFour(), true);
    run("1280x720", 1280, 720, userFour(), true);
    // 3. Bigger and wider screens.
    run("2560x1080 ultrawide", 2560, 1080, userFour(), true);
    run("3840x2160", 3840, 2160, userFour(), true);
    // 4. All eight widgets at their default sizes.
    run("1920x1080, all eight", 1920, 1080, all8(), false);
    run("2560x1440, all eight", 2560, 1440, all8(), true);
    // 5. A widget placed by hand stays where it is; the others go around it.
    {
        std::vector<Item> items = userFour();
        for (Item& it : items) if (it.name == "calendar") { it.pinned = true; it.l = 100; it.t = 100; }
        run("1440x900, calendar pinned at (100,100)", 1440, 900, items, true);
        const int W = 1440, H = 900 - kBarArea;
        std::vector<Item> v = items;
        placeAll(W, H, kGap, v);
        for (const Item& it : v) if (it.name == "calendar") CHECK(it.l == 100 && it.t == 100, "the pinned calendar moved to (%d,%d)", it.l, it.t);
        // A pinned widget that does not fit the screen is only kept inside it.
        std::vector<Item> w = userFour();
        for (Item& it : w) if (it.name == "resources") { it.pinned = true; it.l = 1543; it.t = 553; }
        placeAll(1280, 720 - kBarArea, kGap, w);
        for (const Item& it : w) if (it.name == "resources") CHECK(it.l + it.w <= 1280 && it.t + it.h <= 720 - kBarArea, "the pinned resources is not inside the screen");
    }
    // 6. A tiny screen: the widgets that fit nowhere go over the others, offset, but stay on the screen.
    run("800x600, all eight (overlap allowed)", 800, 600, all8(), false);
    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
