// Tests of the single-line text selection model (src/textsel.h): moving and extending by character and word, Home/End,
// Backspace/Delete (also by word), replacing a selection, the clipboard hand-off, clicks (1, 2, 3 and shift), UTF-8 safety and
// that the offsets never leave the text or fall inside a character.
// Run with `meson test -C build` or tests/text/run.sh.
#include "../../src/textsel.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0, checks = 0;

// A field written with markers: "|" caret, "{" and "}" delimit a selection (caret at the "}", or at the "{" with "<" instead).
struct Doc {
    std::string text;
    size_t caret = 0, anchor = 0;
    textsel::Field field() { return textsel::Field(text, caret, anchor); }
};
Doc parse(const std::string& t) {
    Doc d;
    size_t lo = std::string::npos, hi = std::string::npos;
    bool backwards = false;
    for (char ch : t) {
        if (ch == '{') lo = d.text.size();
        else if (ch == '<') { lo = d.text.size(); backwards = true; }  // "<...>": the caret is at the start
        else if (ch == '}' || ch == '>') hi = d.text.size();
        else if (ch == '|') lo = hi = d.text.size();
        else d.text += ch;
    }
    if (lo == std::string::npos) lo = hi = 0;
    d.anchor = backwards ? hi : lo;
    d.caret = backwards ? lo : hi;
    return d;
}
std::string show(const Doc& d) {
    std::string out;
    const size_t lo = std::min(d.caret, d.anchor), hi = std::max(d.caret, d.anchor);
    for (size_t i = 0; i <= d.text.size(); i++) {
        if (lo != hi) {
            if (i == lo) out += d.caret < d.anchor ? "<" : "{";
            if (i == hi) out += d.caret < d.anchor ? ">" : "}";
        } else if (i == d.caret) out += "|";
        if (i < d.text.size()) out += d.text[i];
    }
    return out;
}
void expect(const std::string& what, const std::string& got, const std::string& want) {
    checks++;
    if (got != want) { failures++; std::printf("FAIL %s\n   got  %s\n   want %s\n", what.c_str(), got.c_str(), want.c_str()); }
}
void expectTrue(const std::string& what, bool v) { expect(what, v ? "true" : "false", "true"); }

// The state after pressing a key on a document, shown with markers.
std::string key(const std::string& in, textsel::Key k, bool ctrl = false, bool shift = false) {
    Doc d = parse(in);
    textsel::Field f = d.field();
    textsel::handleKey(f, k, ctrl, shift);
    return show(d);
}

void testArrows() {
    expect("left", key("ab|cd", textsel::Key::Left), "a|bcd");
    expect("left at start", key("|abc", textsel::Key::Left), "|abc");
    expect("right", key("ab|cd", textsel::Key::Right), "abc|d");
    expect("right at end", key("abc|", textsel::Key::Right), "abc|");
    expect("shift+left starts a selection", key("ab|cd", textsel::Key::Left, false, true), "a<b>cd");
    expect("shift+right starts a selection", key("ab|cd", textsel::Key::Right, false, true), "ab{c}d");
    expect("shift+right grows", key("a{b}cd", textsel::Key::Right, false, true), "a{bc}d");
    expect("shift+left shrinks", key("a{bc}d", textsel::Key::Left, false, true), "a{b}cd");
    expect("shift+left crosses the anchor", key("a{b}cd", textsel::Key::Left, false, true), "a|bcd");
    expect("left collapses to the start", key("a{bc}d", textsel::Key::Left), "a|bcd");
    expect("right collapses to the end", key("a{bc}d", textsel::Key::Right), "abc|d");
    expect("left collapses a backwards selection too", key("a<bc>d", textsel::Key::Left), "a|bcd");
    expect("home", key("ab|cd", textsel::Key::Home), "|abcd");
    expect("end", key("ab|cd", textsel::Key::End), "abcd|");
    expect("shift+home", key("ab|cd", textsel::Key::Home, false, true), "<ab>cd");
    expect("shift+end", key("ab|cd", textsel::Key::End, false, true), "ab{cd}");
    expect("home drops the selection", key("a{bc}d", textsel::Key::Home), "|abcd");
    expect("select all", key("ab|cd", textsel::Key::SelectAll), "{abcd}");
    expect("select all of nothing", key("|", textsel::Key::SelectAll), "|");
}

void testWords() {
    expect("ctrl+left to the start of the word", key("hello wor|ld", textsel::Key::Left, true), "hello |world");
    expect("ctrl+left from a word start goes to the previous word", key("hello |world", textsel::Key::Left, true), "|hello world");
    expect("ctrl+left skips the separators", key("hello,  |world", textsel::Key::Left, true), "|hello,  world");
    expect("ctrl+right to the end of the word", key("hel|lo world", textsel::Key::Right, true), "hello| world");
    expect("ctrl+right from a word end goes to the next word end", key("hello| world", textsel::Key::Right, true), "hello world|");
    expect("ctrl+shift+right", key("|hello world", textsel::Key::Right, true, true), "{hello} world");
    expect("ctrl+shift+left", key("hello world|", textsel::Key::Left, true, true), "hello <world>");
    expect("ctrl+backspace", key("hello wor|ld", textsel::Key::Backspace, true), "hello |ld");
    expect("ctrl+backspace after a space", key("hello |world", textsel::Key::Backspace, true), "|world");
    expect("ctrl+delete", key("hel|lo world", textsel::Key::Delete, true), "hel| world");
    expect("underscore is part of a word", key("snake_case| x", textsel::Key::Left, true), "|snake_case x");
    expect("ctrl+left at start", key("|abc", textsel::Key::Left, true), "|abc");
    expect("ctrl+right at end", key("abc|", textsel::Key::Right, true), "abc|");
}

void testDelete() {
    expect("backspace", key("ab|cd", textsel::Key::Backspace), "a|cd");
    expect("backspace at start", key("|abc", textsel::Key::Backspace), "|abc");
    expect("delete", key("ab|cd", textsel::Key::Delete), "ab|d");
    expect("delete at end", key("abc|", textsel::Key::Delete), "abc|");
    expect("backspace deletes the selection", key("a{bc}d", textsel::Key::Backspace), "a|d");
    expect("delete deletes the selection", key("a{bc}d", textsel::Key::Delete), "a|d");
    expect("backspace deletes a backwards selection", key("a<bc>d", textsel::Key::Backspace), "a|d");
    expect("ctrl+backspace deletes only the selection", key("a{b c}d", textsel::Key::Backspace, true), "a|d");
    {
        Doc d = parse("ab|cd");
        textsel::Field f = d.field();
        expectTrue("backspace reports a change", textsel::handleKey(f, textsel::Key::Backspace, false, false).changed);
    }
    {
        Doc d = parse("|abc");
        textsel::Field f = d.field();
        expectTrue("backspace at the start reports nothing", !textsel::handleKey(f, textsel::Key::Backspace, false, false).changed);
    }
}

// Inserts `what` in a document and shows the result.
std::string typed(const std::string& in, const std::string& what, bool* changed = nullptr) {
    Doc d = parse(in);
    textsel::Field f = d.field();
    const bool c = f.insert(what);
    if (changed) *changed = c;
    return show(d);
}

void testInsert() {
    bool changed = false;
    expect("insert at the caret", typed("ab|cd", "XY", &changed), "abXY|cd");
    expectTrue("insert changes", changed);
    expect("typing replaces the selection", typed("a{bc}d", "Z"), "aZ|d");
    expect("typing replaces a backwards selection", typed("a<bc>d", "Z"), "aZ|d");
    expect("line breaks and tabs become spaces", typed("a|b", "1\n2\t3\r4"), "a1 2 3 4|b");
    expect("control characters are dropped", typed("a|b", std::string("x\x01y\x7fz")), "axyz|b");
    typed("a|b", "", &changed);
    expectTrue("inserting nothing changes nothing", !changed);
    expect("inserting nothing over a selection deletes it", typed("a{b}c", "", &changed), "a|c");
    expectTrue("... and reports a change", changed);
}

void testClipboard() {
    Doc d = parse("a{bc}d");
    {
        textsel::Field f = d.field();
        const textsel::Result r = textsel::handleKey(f, textsel::Key::Copy, true, false);
        expectTrue("copy asks the host", r.wantCopy);
        expect("copy text", r.copyText, "bc");
        expect("copy keeps the text", show(d), "a{bc}d");
    }
    {
        textsel::Field f = d.field();
        const textsel::Result r = textsel::handleKey(f, textsel::Key::Cut, true, false);
        expect("cut text", r.copyText, "bc");
        expect("cut removes it", show(d), "a|d");
        expectTrue("cut reports a change", r.changed);
    }
    d = parse("a|d");
    textsel::Field f = d.field();
    expectTrue("copy with nothing selected does nothing", !textsel::handleKey(f, textsel::Key::Copy, true, false).wantCopy);
    expectTrue("paste asks the host", textsel::handleKey(f, textsel::Key::Paste, true, false).wantPaste);
    expectTrue("a key that is not ours is not handled", !textsel::handleKey(f, textsel::Key::None, false, false).handled);
    expectTrue("arrows repeat", textsel::handleKey(f, textsel::Key::Left, false, false).repeat);
    expectTrue("home does not repeat", !textsel::handleKey(f, textsel::Key::Home, false, false).repeat);
    expectTrue("paste does not repeat", !textsel::handleKey(f, textsel::Key::Paste, true, false).repeat);
}

void testClicks() {
    Doc d = parse("hello big|world");
    textsel::Field f = d.field();
    textsel::applyClick(f, 2, 1, false);
    expect("click puts the caret", show(d), "he|llo bigworld");
    textsel::applyClick(f, 9, 1, true);
    expect("shift+click extends", show(d), "he{llo big}world");
    textsel::applyClick(f, 1, 1, false);
    expect("click drops the selection", show(d), "h|ello bigworld");
    textsel::applyClick(f, 7, 2, false);
    expect("double click selects the word", show(d), "hello {bigworld}");
    textsel::applyClick(f, 5, 2, false);
    expect("double click on a space selects the spaces", show(d), "hello{ }bigworld");
    textsel::applyClick(f, 0, 3, false);
    expect("triple click selects all", show(d), "{hello bigworld}");
    textsel::applyClick(f, 99, 2, false);
    expect("double click past the end picks the last word", show(d), "hello {bigworld}");
    textsel::applyClick(f, 3, 1, false);
    f.dragTo(9);
    expect("drag selects from the press", show(d), "hel{lo big}world");
    f.dragTo(1);
    expect("drag back crosses the anchor", show(d), "h<el>lo bigworld");
    f.dragTo(99);
    expect("drag past the end stops at the end", show(d), "hel{lo bigworld}");

    // a drag after a double click grows by words from the picked one; resting on it keeps it
    d = parse("hello big world|");
    textsel::Field g = d.field();
    textsel::applyClick(g, 7, 2, false);
    textsel::Drag byWord = textsel::beginDrag(g, 2);
    expect("double click picks the word", show(d), "hello {big} world");
    textsel::drag(g, 7, byWord);
    expect("resting on the word keeps it", show(d), "hello {big} world");
    textsel::drag(g, 14, byWord);
    expect("dragging right adds whole words", show(d), "hello {big world}");
    textsel::drag(g, 1, byWord);
    expect("dragging left adds whole words", show(d), "<hello big> world");
    textsel::applyClick(g, 7, 3, false);
    textsel::Drag all = textsel::beginDrag(g, 3);
    textsel::drag(g, 2, all);
    expect("after a triple click everything stays", show(d), "{hello big world}");
    textsel::applyClick(g, 3, 1, false);
    textsel::Drag byChar = textsel::beginDrag(g, 1);
    textsel::drag(g, 6, byChar);
    expect("after one click the drag is by character", show(d), "hel{lo }big world");

    textsel::Clicks c;
    expectTrue("first click is 1", c.press(1000, 10, 10) == 1);
    expectTrue("quick second is 2", c.press(1200, 11, 10) == 2);
    expectTrue("quick third is 3", c.press(1350, 10, 11) == 3);
    expectTrue("a fourth starts again", c.press(1500, 10, 10) == 1);
    expectTrue("a slow click starts again", (c.press(1600, 10, 10), c.press(2300, 10, 10)) == 1);
    expectTrue("a click elsewhere starts again", (c.press(3000, 10, 10), c.press(3100, 80, 10)) == 1);
}

void testUtf8() {
    // "añb€c" : a (1) ñ (2) b (1) € (3) c (1)
    const std::string s = "a\xC3\xB1" "b\xE2\x82\xAC" "c";
    std::string text = s;
    size_t caret = text.size(), anchor = text.size();
    textsel::Field f(text, caret, anchor);
    f.moveLeft(false, false);
    expectTrue("left over c", caret == 7);
    f.moveLeft(false, false);
    expectTrue("left over the euro sign (3 bytes)", caret == 4);
    f.moveLeft(false, false);
    expectTrue("left over b", caret == 3);
    f.moveLeft(true, false);
    expectTrue("shift+left over n-tilde (2 bytes)", caret == 1 && anchor == 3);
    expect("selected text", f.selected(), "\xC3\xB1");
    f.backspace(false);
    expect("backspace deletes the whole character", text, std::string("a") + "b\xE2\x82\xAC" "c");
    text = s;
    caret = anchor = 2;  // in the middle of n-tilde
    textsel::Field g(text, caret, anchor);
    expectTrue("an offset inside a character moves to its start", caret == 1 && anchor == 1);
    text = "xyz";
    caret = 10;
    anchor = 7;
    textsel::Field h(text, caret, anchor);
    expectTrue("offsets past the end come back", caret == 3 && anchor == 3);
    // words with accents
    text = "caf\xC3\xA9 ni\xC3\xB1o";
    caret = text.size();
    anchor = caret;
    textsel::Field w(text, caret, anchor);
    w.moveLeft(false, true);
    expect("ctrl+left over an accented word", show(Doc{text, caret, anchor}), "caf\xC3\xA9 |ni\xC3\xB1o");
    w.moveLeft(false, true);
    expect("... and the one before", show(Doc{text, caret, anchor}), "|caf\xC3\xA9 ni\xC3\xB1o");
    // a double click inside the accented word selects all of it
    textsel::applyClick(w, 4, 2, false);
    expect("double click on an accented word", w.selected(), "caf\xC3\xA9");
    // an insert of multi-byte text puts the caret after it
    text = "ab";
    caret = anchor = 1;
    textsel::Field m(text, caret, anchor);
    m.insert("\xE2\x82\xAC");
    expectTrue("insert of multi-byte text", caret == 4 && text == "a\xE2\x82\xAC" "b");
}

void testSet() {
    std::string text = "old value";
    size_t caret = 3, anchor = 0;
    textsel::Field f(text, caret, anchor);
    f.set("brand new");
    expect("set puts the caret at the end", show(Doc{text, caret, anchor}), "brand new|");
    f.selectAll();
    f.eraseSelection();
    expect("select all and delete empties it", show(Doc{text, caret, anchor}), "|");
}

// A random walk of keys and clicks: whatever happens the offsets stay in the text and on character boundaries.
void testInvariants() {
    std::string text = "caf\xC3\xA9 \xE2\x82\xAC uro  w\xC3\xB6rds_here, ok";
    size_t caret = 0, anchor = 0;
    textsel::Field f(text, caret, anchor);
    unsigned seed = 12345;
    auto next = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    const textsel::Key keys[] = {textsel::Key::Left, textsel::Key::Right, textsel::Key::Home, textsel::Key::End, textsel::Key::Backspace,
                                 textsel::Key::Delete, textsel::Key::SelectAll, textsel::Key::Copy, textsel::Key::Cut};
    bool ok = true;
    for (int i = 0; i < 20000 && ok; i++) {
        const unsigned op = next() % 12;
        if (op < 9) textsel::handleKey(f, keys[op], next() % 2, next() % 2);
        else if (op == 9) f.insert(next() % 2 ? "\xE2\x82\xAC" : "ab c");
        else if (op == 10) textsel::applyClick(f, next() % (text.size() + 4), 1 + next() % 3, next() % 2);
        else f.dragTo(next() % (text.size() + 4));
        if (text.size() > 80) { text.resize(40); while (!text.empty() && textsel::isContinuation(text.back())) text.pop_back(); f.clamp(); }
        ok = caret <= text.size() && anchor <= text.size() && (caret == text.size() || !textsel::isContinuation(text[caret])) &&
             (anchor == text.size() || !textsel::isContinuation(text[anchor]));
    }
    expectTrue("offsets stay valid through 20000 random operations", ok);
}

}  // namespace

int main() {
    testArrows();
    testWords();
    testDelete();
    testInsert();
    testClipboard();
    testClicks();
    testUtf8();
    testSet();
    testInvariants();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
