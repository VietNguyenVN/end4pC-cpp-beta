// The Freeform board (src/freeform.h): bounds, hit testing, concept links, shapes, undo/redo and the text file form.
#include "../../src/freeform.h"

#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace ff;

static Item stroke(std::initializer_list<Pt> pts, double size = 4, unsigned rgb = 0xff0000, int id = 1) {
    Item it;
    it.kind = Item::Stroke;
    it.id = id;
    it.size = size;
    it.rgb = rgb;
    it.pts = pts;
    return it;
}
static Item label(const char* s, double x, double y, double w, double h, int id = 2, bool boxed = false) {
    Item it;
    it.kind = Item::Text;
    it.id = id;
    it.text = s;
    it.boxed = boxed;
    it.pts = {{x, y}};
    it.size = 28;
    it.textW = w;
    it.textH = h;
    return it;
}
static Item shape(int kind, bool filled, Pt a, Pt b, int id = 3) {
    Item it;
    it.kind = Item::Shape;
    it.id = id;
    it.shape = kind;
    it.filled = filled;
    it.size = 4;
    it.pts = {a, b};
    return it;
}
static Item link(int from, int to, int id, const char* phrase = "") {
    Item it;
    it.kind = Item::Link;
    it.id = id;
    it.from = from;
    it.to = to;
    it.size = 3;
    it.text = phrase;
    return it;
}

static void geometry() {
    const Item s = stroke({{10, 10}, {50, 30}}, 6);
    const Box b = bounds(s);
    CHECK(b.x == 7 && b.y == 7 && b.w == 46 && b.h == 26);          // the width is part of the box
    CHECK(hit(s, {30, 20}, 0));                                      // on the line
    CHECK(!hit(s, {30, 40}, 0));                                     // well away
    CHECK(hit(s, {30, 24}, 2));                                      // tolerance reaches it
    CHECK(hit(stroke({{5, 5}}, 10), {8, 5}, 0));                    // a dot
    CHECK(!hit(stroke({{5, 5}}, 10), {20, 5}, 0));
    CHECK(!hit(Item{}, {0, 0}, 5));                                  // nothing to hit

    const Item t = label("hi", 100, 100, 40, 20);
    CHECK(hit(t, {120, 110}, 0) && !hit(t, {150, 110}, 0) && hit(t, {143, 110}, 4));

    Items all = {stroke({{0, 0}, {100, 100}}), label("x", 40, 40, 30, 30)};
    CHECK(topAt(all, {50, 50}, 0) == 1);                             // the one drawn last wins
    CHECK(topAt(all, {0, 0}, 0) == 0);
    CHECK(topAt(all, {300, 300}, 0) == -1);

    Box cb;
    CHECK(contentBounds(all, cb) && cb.x < 0 && cb.x + cb.w > 100);
    CHECK(!contentBounds({}, cb));

    const Item m = moved(s, 5, -5);
    CHECK(m.pts[0].x == 15 && m.pts[0].y == 5 && s.pts[0].x == 10);  // a copy: the original stays
}

static void shapes() {
    const Item flat = shape(kShapeFlat, false, {10, 10}, {110, 60});
    CHECK(hit(flat, {10, 30}, 0) && hit(flat, {60, 60}, 0));         // on the outline
    CHECK(!hit(flat, {60, 35}, 0));                                  // the empty inside is not taken
    const Item solid = shape(kShapeRounded, true, {10, 10}, {110, 60});
    CHECK(hit(solid, {60, 35}, 0) && !hit(solid, {200, 35}, 0));     // a filled one is taken everywhere inside
    const Item ell = shape(kShapeEllipse, false, {0, 0}, {100, 60});
    CHECK(hit(ell, {50, 2}, 0) && hit(ell, {2, 30}, 0));             // top and left of the curve
    CHECK(!hit(ell, {2, 2}, 0) && !hit(ell, {50, 30}, 0));           // the corner of the box and the middle are off the curve
    const Item backwards = shape(kShapeFlat, true, {110, 60}, {10, 10});
    CHECK(bounds(backwards).x < 11 && hit(backwards, {60, 35}, 0)); // dragged up and to the left
    CHECK(!hit(shape(kShapeFlat, true, {5, 5}, {5, 5}), {50, 50}, 0));
}

static void concepts() {
    Items b = {label("Plants", 0, 0, 100, 40, 1, true), label("Light", 300, 0, 100, 40, 2, true)};
    b.push_back(link(1, 2, 3, "need"));
    Pt p, q;
    CHECK(linkEnds(b, b[2], p, q) && p.x == 100 && p.y == 20 && q.x == 300 && q.y == 20);   // from edge to edge, not centre to centre
    CHECK(topAt(b, {200, 20}, 0) == 2);                              // the arrow can be picked
    CHECK(topAt(b, {50, 20}, 0) == 0);                               // a node wins over a link that touches it
    const Box lb = boundsIn(b, b[2]);
    CHECK(lb.x <= 100 && lb.x + lb.w >= 300);

    b[1].pts[0] = {300, 300};                                        // moving a node: the arrow follows without being touched
    CHECK(linkEnds(b, b[2], p, q) && q.y > 100);

    CHECK(!linkEnds(b, link(1, 99, 4), p, q) && !linkEnds(b, link(1, 1, 4), p, q));
    CHECK(linked(b, 1, 2) && linked(b, 2, 1) && !linked(b, 1, 3));
    CHECK(nextId(b) == 4 && indexOfId(b, 2) == 1 && indexOfId(b, 0) == -1 && indexOfId(b, 77) == -1);

    const Items less = without(b, 0);                                // removing a node removes its links
    CHECK(less.size() == 1 && less[0].id == 2);
    CHECK(without(b, 2).size() == 2);                                // removing the link alone leaves both nodes
    CHECK(without(b, 9).size() == 3 && without(b, -1).size() == 3);

    const Box e{0, 0, 100, 40};
    const Pt right = edgePoint(e, {500, 20}), ell = edgePoint(e, {500, 20}, kShapeEllipse), diag = edgePoint(e, {100, 100});
    CHECK(right.x == 100 && right.y == 20 && ell.x == 100 && ell.y == 20);
    CHECK(diag.y == 40 && diag.x > 50 && diag.x < 100);              // leaves through the bottom edge
    CHECK(edgePoint(e, {50, 20}).x == 50);                            // pointing at the centre: the centre

    // a free place for the child of a node: next to it, lower down when something is already there
    Items tree = {label("Root", 0, 0, 100, 40, 1, true)};
    Pt spot = freeSpot(tree, 100, 40, {160, 0});
    CHECK(spot.x == 160 && spot.y == 0);
    tree.push_back(label("A", spot.x, spot.y, 100, 40, 2, true));
    spot = freeSpot(tree, 100, 40, {160, 0});
    CHECK(spot.x == 160 && spot.y > 40);
    CHECK(!overlaps({0, 0, 10, 10}, {50, 50, 10, 10}, 4) && overlaps({0, 0, 10, 10}, {12, 0, 10, 10}, 4));
}

static void points() {
    Item s;
    CHECK(addPoint(s, {0, 0}, 2));
    CHECK(!addPoint(s, {0.5, 0.5}, 2));                              // too close
    CHECK(addPoint(s, {5, 0}, 2));
    CHECK(!addPoint(s, {NAN, 0}, 2));
    CHECK(!addPoint(s, {1e9, 0}, 2));
    CHECK(s.pts.size() == 2);
}

static void history() {
    History h;
    Items cur = {stroke({{0, 0}})};
    CHECK(!h.undo(cur) && !h.redo(cur));
    h.record(cur);
    cur.push_back(stroke({{1, 1}}, 4, 0xff0000, 2));
    CHECK(h.undo(cur) && cur.size() == 1);
    CHECK(h.redo(cur) && cur.size() == 2);
    CHECK(h.undo(cur));
    h.record(cur);                                                   // a new edit drops the redo branch
    cur.clear();
    CHECK(!h.redo(cur));
    for (size_t i = 0; i < kMaxHistory + 10; i++) h.record(cur);
    CHECK(h.past.size() == kMaxHistory);
}

static void roundTrip() {
    Items in = {stroke({{1.5, 2.5}, {30, 40}}, 8, 0x12ab34, 1), label("two\nlines \\ ok", 12, 34, 10, 10, 2), label("Node", 5, 6, 80, 30, 3, true),
                shape(kShapeEllipse, true, {1, 2}, {30, 40}, 4), link(2, 3, 5, "leads to\nit")};
    in[1].rgb = 0x00ff00;
    in[1].size = 44;
    in[2].shape = kShapeFlat;
    const Items out = parse(serialize(in));
    CHECK(out.size() == 5);
    CHECK(out[0].kind == Item::Stroke && out[0].id == 1 && out[0].rgb == 0x12ab34 && out[0].size == 8 && out[0].pts.size() == 2 && out[0].pts[1].y == 40);
    CHECK(out[1].kind == Item::Text && !out[1].boxed && out[1].text == "two\nlines \\ ok" && out[1].rgb == 0x00ff00 && out[1].size == 44 && out[1].pts[0].x == 12);
    CHECK(out[2].boxed && out[2].shape == kShapeFlat && out[2].text == "Node" && out[2].id == 3);
    CHECK(out[3].kind == Item::Shape && out[3].shape == kShapeEllipse && out[3].filled && out[3].pts[1].x == 30);
    CHECK(out[4].kind == Item::Link && out[4].from == 2 && out[4].to == 3 && out[4].text == "leads to\nit");
    CHECK(serialize({}).empty() && parse("").empty());
}

static void hostileFiles() {
    CHECK(parse("garbage\nS\nS 1 zz\nT 1 000000\nL 1\nR 1\n").empty());
    CHECK(parse("S 1 ff0000 4 3 1 1 2 2\n").empty());                // fewer points than it says
    CHECK(parse("S 1 ff0000 4 1 1e999 0\n").empty());                // out of range
    CHECK(parse("S 1 ff0000 4 99999999 0 0\n").empty());             // far too many points
    CHECK(parse("S 1 ff0000 nan 1 0 0\n").empty());
    CHECK(parse("S 0 ff0000 4 1 0 0\n").empty());                    // an id must be positive
    CHECK(parse("R 1 ff0000 4 1 0 0 0 nan 5\n").empty());
    const Items big = parse("S 1 ff0000 9999 1 0 0\n");             // a silly width is clamped, not refused
    CHECK(big.size() == 1 && big[0].size == kMaxSize);
    const Items shapeClamp = parse("R 1 ff0000 4 9 1 0 0 5 5\n");
    CHECK(shapeClamp.size() == 1 && shapeClamp[0].shape == 2);
    const Items mixed = parse("junk\nS 1 0000ff 2 1 5 5\nT 2 00ff00 20 0 1 1 1 hi\n");
    CHECK(mixed.size() == 2);
    const Items dup = parse("S 1 0000ff 2 1 5 5\nS 1 0000ff 2 1 6 6\n");   // the same id twice: the second gets a new one
    CHECK(dup.size() == 2 && dup[0].id != dup[1].id);
    const Items dangling = parse("T 1 00ff00 20 1 1 1 1 a\nL 2 ff0000 3 1 9 x\nL 3 ff0000 3 1 1 y\n");   // links to nothing, and to itself
    CHECK(dangling.size() == 1);
    std::string many;
    for (size_t i = 0; i < kMaxItems + 50; i++) many += "S " + std::to_string(i + 1) + " ff0000 2 1 0 0\n";
    CHECK(parse(many).size() == kMaxItems);
    CHECK(parse("T 1 ff0000 20 0 1 1 1 trailing\\").size() == 1);    // a lone backslash at the end
}

static void fonts() {
    Item t = label("Hello", 1, 2, 10, 10, 1);
    t.font = "DejaVu Serif";
    const Items out = parse(serialize({t, label("Plain", 5, 6, 10, 10, 2)}));
    CHECK(out.size() == 2 && out[0].font == "DejaVu Serif" && out[0].text == "Hello");
    CHECK(out[1].font.empty() && out[1].text == "Plain");
    Item tabby = label("a\tb", 1, 2, 10, 10, 1);                      // a tab in the text itself stays in the text
    tabby.font = "Mono";
    const Items back = parse(serialize({tabby}));
    CHECK(back.size() == 1 && back[0].font == "Mono" && back[0].text == "a\tb");
    const Items old = parse("T 1 ff0000 20 0 1 1 1 no font here\n");  // a line from before fonts existed
    CHECK(old.size() == 1 && old[0].font.empty() && old[0].text == "no font here");
    CHECK(cleanFont(std::string(300, 'x')).size() == 100);
    CHECK(cleanFont("a\nb\tc") == "abc");
}

static void materialShapes() {
    Item m = shape(kShapeRounded, false, {10, 10}, {110, 60});
    m.mshape = "Cookie12Sided";
    CHECK(hit(m, {60, 35}, 0) && !hit(m, {200, 35}, 0));              // taken anywhere in its box, even unfilled
    const Items out = parse(serialize({m, shape(kShapeFlat, true, {0, 0}, {5, 5}, 4)}));
    CHECK(out.size() == 2 && out[0].mshape == "Cookie12Sided" && out[1].mshape.empty());
    CHECK(cleanName("Cookie 12/Sided!") == "Cookie12Sided" && cleanName(std::string(99, 'a')).size() == 40);
    CHECK(parse("R 1 ff0000 4 1 0 0 0 5 5\n")[0].mshape.empty());       // a line without a name
    CHECK(parse("R 1 ff0000 4 1 0 0 0 5 5   Heart\n")[0].mshape == "Heart");
}

static void resizing() {
    const Box s{100, 100, 200, 100};
    const Box tr = resizeBox(s, 2, {400, 50}, false);                    // top-right handle: bottom-left corner stays
    CHECK(tr.x == 100 && tr.y == 50 && tr.w == 300 && tr.h == 150);
    const Box left = resizeBox(s, 7, {40, 999}, false);                  // an edge handle moves only its own side
    CHECK(left.x == 40 && left.w == 260 && left.y == 100 && left.h == 100);
    const Box tiny = resizeBox(s, 4, {0, 0}, false, 10);                 // dragged past the opposite corner: stops at the minimum
    CHECK(tiny.x == 100 && tiny.y == 100 && tiny.w == 10 && tiny.h == 10);
    const Box asp = resizeBox(s, 4, {500, 150}, true);                   // proportions kept (2:1), top-left anchored
    CHECK(asp.x == 100 && asp.y == 100 && asp.w == 400 && asp.h == 200);
    const Box asp2 = resizeBox(s, 0, {0, 100}, true);                    // top-left: bottom-right anchored
    CHECK(asp2.x + asp2.w == 300 && asp2.y + asp2.h == 200 && asp2.w / asp2.h == 2);

    CHECK(handleAt(s, {100, 100}, 6, false) == 0 && handleAt(s, {300, 200}, 6, false) == 4 && handleAt(s, {200, 100}, 6, false) == 1);
    CHECK(handleAt(s, {200, 100}, 6, true) == -1 && handleAt(s, {101, 103}, 6, true) == 0);   // a text has corners only
    CHECK(handleAt(s, {200, 150}, 6, false) == -1);
    CHECK(handlePoint(s, 5).x == 200 && handlePoint(s, 5).y == 200 && handlePoint(s, 7).x == 100);

    const Item st = stroke({{10, 10}, {50, 30}}, 4);                     // bounds 8,8 .. 52,32 (44 x 24)
    const Box from = bounds(st);
    const Item big = resized(st, from, {8, 8, 88, 48});                   // the inner extent (without the pen width) goes 40x20 -> 84x44
    CHECK(big.size == 4 && big.pts[0].x == 10 && big.pts[1].x == 94 && big.pts[1].y == 54);
    const Item sh = resized(shape(kShapeFlat, false, {10, 10}, {110, 60}), bounds(shape(kShapeFlat, false, {10, 10}, {110, 60})), {0, 0, 200, 100});
    CHECK(bounds(sh).x == 0 && bounds(sh).w == 200 && bounds(sh).h == 100 && sh.size == 4);
    Item t = label("Hi", 50, 60, 100, 40);
    const Item tt = resized(t, bounds(t), {20, 30, 200, 80});             // twice as tall: letters twice as big, moved to the new corner
    CHECK(tt.size == 56 && tt.pts[0].x == 20 && tt.pts[0].y == 30);
    CHECK(resized(t, bounds(t), {0, 0, 1, 1}).size == kMinTextSize);
    CHECK(resized(t, {0, 0, 0, 0}, {0, 0, 10, 10}).size == t.size);       // nothing to scale from
    const Item flat = resized(stroke({{5, 5}, {5, 25}}, 2), bounds(stroke({{5, 5}, {5, 25}}, 2)), {0, 0, 40, 22});
    CHECK(std::isfinite(flat.pts[0].x) && std::isfinite(flat.pts[1].y));  // a perfectly vertical stroke has no width to stretch
}

static void utf8() {
    CHECK(cleanUtf8("caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80") == "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80");   // valid text is left alone
    CHECK(cleanUtf8("a\xff" "b") == "ab");                                                          // a stray byte goes
    CHECK(cleanUtf8("a\xc3") == "a");                                                            // a character cut short
    CHECK(cleanUtf8("\xc0\xaf") == "");                                                          // an overlong slash
    CHECK(cleanUtf8("\xed\xa0\x80x") == "x");                                                   // a surrogate
    const std::string cut(kMaxText - 1, 'a');
    CHECK(cleanUtf8(cut + "\xc3\xa9").size() == kMaxText - 1);                                   // the limit never splits a character
    const Items t = parse("T 1 ff0000 20 0 1 1 1 ok\xff\xfe!\n");
    CHECK(t.size() == 1 && t[0].text == "ok!");
}

int main() {
    geometry();
    shapes();
    concepts();
    points();
    history();
    roundTrip();
    hostileFiles();
    utf8();
    fonts();
    materialShapes();
    resizing();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
