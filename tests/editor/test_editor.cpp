// T3xt: the highlighter and the document, without a window. `meson test -C build`.
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "../../src/editor/document.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

using namespace t3;

static std::vector<uint8_t> tok(const char* lang, const std::string& line, LineState* st = nullptr) {
    std::vector<uint8_t> out;
    LineState in = st ? *st : LineState{};
    const LineState next = highlightLine(languages().at(lang), line, in, out);
    if (st) *st = next;
    return out;
}

static int T(const std::vector<uint8_t>& v, size_t i) { return i < v.size() ? v[i] : 255; }

static void testSyntax() {
    auto c = tok("cpp", "int x = 42; // note");
    CHECK(T(c, 0) == TType);       // int
    CHECK(T(c, 8) == TNumber);     // 42
    CHECK(T(c, 13) == TComment);   // // note
    c = tok("cpp", "return \"a // b\";");
    CHECK(T(c, 0) == TKeyword && T(c, 8) == TString);  // the // inside a string is not a comment
    c = tok("c", "#include <stdio.h>");
    CHECK(T(c, 0) == TPreproc && T(c, 10) == TString);
    LineState st;
    tok("cpp", "/* start", &st);
    CHECK(st.mode == MBlockComment);
    c = tok("cpp", "end */ int", &st);
    CHECK(T(c, 0) == TComment && T(c, 7) == TType && st.mode == MNormal);
    st = {};
    c = tok("python", "s = \"\"\"text", &st);
    CHECK(st.mode == MTripleDouble && T(c, 4) == TString);
    c = tok("python", "more\"\"\" + 1", &st);
    CHECK(st.mode == MNormal && T(c, 0) == TString && T(c, 10) == TNumber);
    c = tok("python", "def run(x):");
    CHECK(T(c, 0) == TKeyword && T(c, 4) == TFunction);
    st = {};
    tok("lua", "--[[ long", &st);
    CHECK(st.mode == MLuaComment);
    tok("lua", "]] x = 1", &st);
    CHECK(st.mode == MNormal);
    c = tok("json", "  \"key\": \"value\", \"n\": 3");
    CHECK(T(c, 2) == TAttr && T(c, 9) == TString && T(c, 23) == TNumber);
    c = tok("rust", "fn f<'a>(x: &'a str) {");
    CHECK(T(c, 0) == TKeyword);  // a lifetime does not swallow the line
    c = tok("markdown", "# Title");
    CHECK(T(c, 0) == THeading);
    c = tok("diff", "+added");
    CHECK(T(c, 0) == TAdded);
    CHECK(&detectLanguage("/x/main.cpp", "") == &languages().at("cpp"));
    CHECK(&detectLanguage("/x/Makefile", "") == &languages().at("make"));
    CHECK(&detectLanguage("/x/run", "#!/usr/bin/env python3") == &languages().at("python"));
    CHECK(&detectLanguage("/x/unknown.zzz", "") == &languages().at("plain"));
    CHECK(tok("cpp", std::string(5000, 'a')).size() == 5000);  // a huge line is left alone
}

static void testEditing() {
    Doc d;
    d.insertText("hello");
    CHECK(d.lines[0] == "hello" && d.cur == (Pos{0, 5}));
    d.insertText("\nworld");
    CHECK(d.lineCount() == 2 && d.lines[1] == "world");
    d.setCursor({0, 5}, false);
    d.replaceRange({0, 5}, {1, 0}, " ");
    CHECK(d.lineCount() == 1 && d.lines[0] == "hello world");
    CHECK(d.textRange({0, 6}, {0, 11}) == "world");
    d.setCursor({0, 0}, false);
    d.setCursor({0, 5}, true);
    CHECK(d.selectedText() == "hello");
    d.deleteSelection();
    CHECK(d.lines[0] == " world");
    CHECK(d.undo());
    CHECK(d.lines[0] == "hello world");
    CHECK(d.redo());
    CHECK(d.lines[0] == " world");
    // typing in a row is one undo step
    Doc t;
    for (char ch : std::string("abc")) t.insertText(std::string(1, ch), 1);
    CHECK(t.lines[0] == "abc");
    t.undo();
    CHECK(t.lines[0].empty());
    // multi-byte characters move and delete as one
    Doc u;
    u.insertText("aéb");
    u.setCursor({0, 3}, false);
    u.backspace(false);
    CHECK(u.lines[0] == "ab");
    CHECK(prevCol("aéb", 3) == 1 && nextCol("aéb", 1) == 3);
}

static void testLines() {
    Doc d;
    d.lang = &languages().at("cpp");
    d.insertText("  if (x) {");
    d.newline();
    CHECK(d.lines[1] == "      " && d.cur.col == 6);  // indentation of the line plus a level
    Doc e;
    e.insertText("a\nb\nc");
    e.setCursor({0, 0}, false);
    e.setCursor({2, 1}, true);
    e.indentLines(false);
    CHECK(e.lines[0] == "    a" && e.lines[2] == "    c");
    e.undo();
    CHECK(e.lines[0] == "a" && e.lines[1] == "b" && e.lines[2] == "c");  // all the lines were one step
    e.lang = &languages().at("cpp");
    e.toggleComment();
    CHECK(e.lines[0] == "// a" && e.lines[2] == "// c");
    e.toggleComment();
    CHECK(e.lines[0] == "a");
    CHECK(visualColumn("\tx", 1) == 4 && colFromVisual("\tx", 4) == 1);
}

static void testFind() {
    Doc d;
    d.insertText("Foo bar\nfoo baz\nFOO");
    Pos p;
    CHECK(d.find("foo", false, {0, 0}, true, p) && p == (Pos{0, 0}));
    CHECK(d.find("foo", false, {0, 1}, true, p) && p == (Pos{1, 0}));
    CHECK(d.find("foo", true, {0, 0}, true, p) && p == (Pos{1, 0}));
    CHECK(d.find("foo", false, {1, 0}, false, p) && p == (Pos{0, 0}));
    CHECK(!d.find("zzz", false, {0, 0}, true, p));
    CHECK(d.countMatches("foo", false) == 3 && d.countMatches("foo", true) == 1);
    CHECK(d.replaceAll("foo", "x", false) == 3);
    CHECK(d.lines[0] == "x bar" && d.lines[2] == "x");
    d.undo();
    CHECK(d.lines[0] == "Foo bar" && d.lines[2] == "FOO");  // one step brings every one back
}

static void testFiles() {
    const std::string dir = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/t3xt-test-" + std::to_string(getpid());
    mkdir(dir.c_str(), 0700);
    const std::string file = dir + "/a.txt";
    { std::ofstream f(file, std::ios::binary); f << "one\r\ntwo\r\n"; }
    Doc d;
    std::string err;
    CHECK(d.load(file, err));
    CHECK(d.crlf && d.lineCount() == 3 && d.lines[1] == "two" && d.lines[2].empty());
    d.insertText("x");
    CHECK(d.modified && d.save("", err));
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    CHECK(ss.str() == "xone\r\ntwo\r\n");  // the line endings of the file are kept
    { std::ofstream f(dir + "/bad.txt", std::ios::binary); f << "ok \xff\xfe end"; }
    Doc b;
    CHECK(b.load(dir + "/bad.txt", err) && b.readOnly);
    CHECK(!b.save("", err));
    { std::ofstream f(dir + "/bin.dat", std::ios::binary); f.write("ab\0cd", 5); }
    Doc n;
    CHECK(!n.load(dir + "/bin.dat", err));
    CHECK(!n.load(dir + "/missing", err));
    unlink(file.c_str()); unlink((dir + "/bad.txt").c_str()); unlink((dir + "/bin.dat").c_str()); rmdir(dir.c_str());
}

int main() {
    testSyntax();
    testEditing();
    testLines();
    testFind();
    testFiles();
    if (failures) { std::fprintf(stderr, "%d checks failed\n", failures); return 1; }
    std::puts("editor: all checks passed");
    return 0;
}
