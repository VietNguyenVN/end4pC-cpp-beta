// Tests of the Markdown formatting of the Notes editor (src/mdedit.h): every action, toggling off, lines and blocks, the placeholder
// when nothing is selected, links, code blocks, rules, the active state of the toolbar buttons and Enter inside lists.
// Run with `meson test -C build` or tests/md/run.sh.
#include "../../src/mdedit.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int failures = 0, checks = 0;

struct Doc {
    std::string s;
    int anc = 0, cur = 0;
};

// A document written with markers: "|" is the caret, "{" and "}" delimit a selection (caret at the end).
Doc parse(const std::string& t) {
    Doc d;
    int a = -1, c = -1;
    for (char ch : t) {
        if (ch == '{') a = static_cast<int>(d.s.size());
        else if (ch == '}') c = static_cast<int>(d.s.size());
        else if (ch == '|') a = c = static_cast<int>(d.s.size());
        else d.s += ch;
    }
    d.anc = a < 0 ? 0 : a;
    d.cur = c < 0 ? d.anc : c;
    return d;
}
std::string show(const Doc& d) {
    std::string o;
    const int lo = std::min(d.anc, d.cur), hi = std::max(d.anc, d.cur);
    for (int i = 0; i <= static_cast<int>(d.s.size()); i++) {
        if (lo == hi && i == lo) o += '|';
        else if (lo != hi && i == lo) o += '{';
        else if (lo != hi && i == hi) o += '}';
        if (i < static_cast<int>(d.s.size())) o += d.s[static_cast<size_t>(i)];
    }
    return o;
}
Doc apply(const Doc& d, const MdEditResult& r) {
    if (!r.changed) return d;
    Doc o = d;
    o.s.replace(static_cast<size_t>(r.from), static_cast<size_t>(r.to - r.from), r.with);
    o.anc = r.anc;
    o.cur = r.cur;
    return o;
}
void expect(const std::string& name, const std::string& in, MdAct act, const std::string& want) {
    checks++;
    const Doc d = parse(in);
    const Doc o = apply(d, mdApply(d.s, d.anc, d.cur, act));
    const std::string got = show(o);
    if (got != want) {
        failures++;
        std::printf("FAIL %s\n  in:   %s\n  want: %s\n  got:  %s\n", name.c_str(), in.c_str(), want.c_str(), got.c_str());
    }
}
void expectEnter(const std::string& name, const std::string& in, const std::string& want) {
    checks++;
    const Doc d = parse(in);
    const MdEditResult r = mdEnter(d.s, d.cur);
    const std::string got = r.changed ? show(apply(d, r)) : "(plain newline)";
    if (got != want) {
        failures++;
        std::printf("FAIL %s\n  in:   %s\n  want: %s\n  got:  %s\n", name.c_str(), in.c_str(), want.c_str(), got.c_str());
    }
}
void expectActive(const std::string& name, const std::string& in, MdAct act, bool want) {
    checks++;
    const Doc d = parse(in);
    if (mdActive(d.s, d.anc, d.cur, act) != want) {
        failures++;
        std::printf("FAIL %s (active): %s should be %d\n", name.c_str(), in.c_str(), want);
    }
}
}  // namespace

int main() {
    // bold, italic, strike, code: wrap, take off, word under the caret, placeholders
    expect("bold wraps", "hello {world}", MdAct::Bold, "hello **{world}**");
    expect("bold off outside", "hello **{world}**", MdAct::Bold, "hello {world}");
    expect("bold off inside", "hello {**world**}", MdAct::Bold, "hello {world}");
    expect("bold word", "hel|lo world", MdAct::Bold, "**{hello}** world");
    expect("bold off word", "**hel|lo** world", MdAct::Bold, "{hello} world");
    expect("bold placeholder", "a |", MdAct::Bold, "a **{bold text}**");
    expect("bold empty doc", "|", MdAct::Bold, "**{bold text}**");
    expect("bold between empty marks", "a **|**", MdAct::Bold, "a |");
    expect("italic wraps", "{word}", MdAct::Italic, "*{word}*");
    expect("italic off", "*{word}*", MdAct::Italic, "{word}");
    expect("italic on bold keeps bold", "**{word}**", MdAct::Italic, "***{word}***");
    expect("italic off of bold italic", "***{word}***", MdAct::Italic, "**{word}**");
    expect("bold off of bold italic", "***{word}***", MdAct::Bold, "*{word}*");
    expect("italic placeholder", "|", MdAct::Italic, "*{italic text}*");
    expect("strike wraps", "{old}", MdAct::Strike, "~~{old}~~");
    expect("strike off", "~~{old}~~", MdAct::Strike, "{old}");
    expect("code wraps", "run {ls -la} now", MdAct::Code, "run `{ls -la}` now");
    expect("code off", "run `{ls}` now", MdAct::Code, "run {ls} now");
    expect("code placeholder", "x |", MdAct::Code, "x `{code}`");
    expect("bold multi-byte", "{caf\xC3\xA9}", MdAct::Bold, "**{caf\xC3\xA9}**");
    expect("bold selection keeps spaces", "{two words}", MdAct::Bold, "**{two words}**");

    // headings: # -> ## -> ### -> none, on every line of a selection, a caret keeps its place
    expect("heading 1", "Title|", MdAct::Heading, "# Title|");
    expect("heading 1 caret mid", "Ti|tle", MdAct::Heading, "# Ti|tle");
    expect("heading 2", "# Title|", MdAct::Heading, "## Title|");
    expect("heading 3", "## Title|", MdAct::Heading, "### Title|");
    expect("heading off", "### Title|", MdAct::Heading, "Title|");
    expect("heading on empty line", "|", MdAct::Heading, "# |");
    expect("heading caret in marker", "#| Title", MdAct::Heading, "## |Title");
    expect("heading replaces list", "- item|", MdAct::Heading, "# item|");
    expect("heading block", "{one\ntwo}", MdAct::Heading, "{# one\n# two}");
    expect("heading second line", "one\ntw|o", MdAct::Heading, "one\n# tw|o");

    // lists and quotes
    expect("bullet", "item|", MdAct::Bullet, "- item|");
    expect("bullet off", "- item|", MdAct::Bullet, "item|");
    expect("bullet empty line", "|", MdAct::Bullet, "- |");
    expect("bullet block", "{a\nb\nc}", MdAct::Bullet, "{- a\n- b\n- c}");
    expect("bullet block off", "{- a\n- b}", MdAct::Bullet, "{a\nb}");
    expect("bullet partial adds", "{- a\nb}", MdAct::Bullet, "{- a\n- b}");
    expect("bullet skips blank lines", "{a\n\nb}", MdAct::Bullet, "{- a\n\n- b}");
    expect("bullet keeps indent", "  item|", MdAct::Bullet, "  - item|");
    expect("numbered", "a\nb\nc", MdAct::Numbered, "1. |a\nb\nc");  // (the caret is at 0: only the first line; it stays before the text)
    expect("numbered block", "{a\nb\nc}", MdAct::Numbered, "{1. a\n2. b\n3. c}");
    expect("numbered off", "{1. a\n2. b}", MdAct::Numbered, "{a\nb}");
    expect("numbered from bullets", "{- a\n- b}", MdAct::Numbered, "{1. a\n2. b}");
    expect("numbered skips blank lines", "{a\n\nb}", MdAct::Numbered, "{1. a\n\n2. b}");
    expect("task", "todo|", MdAct::Task, "- [ ] todo|");
    expect("task off ticked", "- [x] todo|", MdAct::Task, "todo|");
    expect("task from bullet", "- todo|", MdAct::Task, "- [ ] todo|");
    expect("task block", "{a\nb}", MdAct::Task, "{- [ ] a\n- [ ] b}");
    expect("quote", "wise|", MdAct::Quote, "> wise|");
    expect("quote off", "> wise|", MdAct::Quote, "wise|");
    expect("quote block", "{a\nb}", MdAct::Quote, "{> a\n> b}");
    expect("selection ending at line start excludes the next line", "{a\n}b", MdAct::Bullet, "- {a}\nb");

    // links
    expect("link selection", "see {docs} now", MdAct::Link, "see [docs]({url}) now");
    expect("link placeholder", "|", MdAct::Link, "[{link text}](url)");
    expect("link word", "go to goo|gle", MdAct::Link, "go to [google]({url})");
    expect("link url selection", "{https://example.com}", MdAct::Link, "[{link text}](https://example.com)");
    expect("link www selection", "{www.example.com}", MdAct::Link, "[{link text}](www.example.com)");

    // code blocks and rules
    expect("code block selection", "{a\nb}", MdAct::CodeBlock, "```\n{a\nb}\n```");
    expect("code block empty line", "|", MdAct::CodeBlock, "```\n|\n```");
    expect("code block line", "x = 1|", MdAct::CodeBlock, "```\n{x = 1}\n```");
    expect("code block off", "```\n{a\nb}\n```", MdAct::CodeBlock, "{a\nb}");
    expect("rule after text", "hello|", MdAct::Rule, "hello\n\n---\n|");
    expect("rule on empty line after text", "hello\n|", MdAct::Rule, "hello\n\n---\n|");
    expect("rule on blank line after blank", "hello\n\n|", MdAct::Rule, "hello\n\n---\n|");
    expect("rule at start", "|", MdAct::Rule, "---\n|");

    // active state of the buttons
    expectActive("bold on", "**he|llo**", MdAct::Bold, true);
    expectActive("bold off", "he|llo", MdAct::Bold, false);
    expectActive("italic not on bold", "**he|llo**", MdAct::Italic, false);
    expectActive("italic on", "*he|llo*", MdAct::Italic, true);
    expectActive("heading on", "## He|llo", MdAct::Heading, true);
    expectActive("bullet on", "- he|llo", MdAct::Bullet, true);
    expectActive("bullet not on task", "- [ ] he|llo", MdAct::Bullet, false);
    expectActive("task on", "- [ ] he|llo", MdAct::Task, true);
    expectActive("quote on", "> he|llo", MdAct::Quote, true);
    expectActive("link on", "[te|xt](url)", MdAct::Link, true);
    expectActive("link off", "te|xt", MdAct::Link, false);
    expectActive("code block on", "```\n{a}\n```", MdAct::CodeBlock, true);

    // Enter in lists
    expectEnter("enter bullet", "- one|", "- one\n- |");
    expectEnter("enter bullet mid", "- on|e", "- on\n- |e");
    expectEnter("enter numbered", "3. three|", "3. three\n4. |");
    expectEnter("enter numbered paren", "1) one|", "1) one\n2) |");
    expectEnter("enter task", "- [x] done|", "- [x] done\n- [ ] |");
    expectEnter("enter quote", "> wise|", "> wise\n> |");
    expectEnter("enter indent", "  - sub|", "  - sub\n  - |");
    expectEnter("enter empty item ends list", "- one\n- |", "- one\n|");
    expectEnter("enter empty number ends list", "1. a\n2. |", "1. a\n|");
    expectEnter("enter plain line", "hello|", "(plain newline)");
    expectEnter("enter heading", "# hello|", "(plain newline)");
    expectEnter("enter in marker", "-| one", "(plain newline)");
    expectEnter("enter empty doc", "|", "(plain newline)");

    // the selection never leaves the text
    const std::string all[] = {"", "a", "\n", "a\nb", "- x\n\n1. y", "```\n```"};
    const MdAct acts[] = {MdAct::Bold, MdAct::Italic, MdAct::Strike, MdAct::Code, MdAct::Heading, MdAct::Bullet, MdAct::Numbered, MdAct::Task, MdAct::Quote, MdAct::Link, MdAct::CodeBlock, MdAct::Rule};
    for (const auto& t : all)
        for (int a = 0; a <= static_cast<int>(t.size()); a++)
            for (int c = 0; c <= static_cast<int>(t.size()); c++)
                for (MdAct act : acts) {
                    checks++;
                    const MdEditResult r = mdApply(t, a, c, act);
                    if (!r.changed) continue;
                    const Doc o = apply({t, a, c}, r);
                    const int n = static_cast<int>(o.s.size());
                    if (r.from < 0 || r.to < r.from || r.to > static_cast<int>(t.size()) || o.anc < 0 || o.cur < 0 || o.anc > n || o.cur > n) {
                        failures++;
                        std::printf("FAIL bounds: act %d text '%s' sel %d..%d\n", static_cast<int>(act), t.c_str(), a, c);
                    }
                }

    std::printf("mdedit: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
