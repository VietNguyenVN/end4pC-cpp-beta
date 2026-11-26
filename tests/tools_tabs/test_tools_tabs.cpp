// The order and visibility of the Tools tabs (src/tools_tabs.h): reading and writing the saved order, moving a tab, hidden ids.
#include "../../src/tools_tabs.h"

#include <cstdio>
#include <set>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace tools_tabs;

static bool permutation(const Order& o) {
    std::set<int> seen(o.begin(), o.end());
    return seen.size() == static_cast<size_t>(Count) && *seen.begin() == 0 && *seen.rbegin() == Count - 1;
}
static Order defaults() { return parseOrder(""); }

static void table() {
    static_assert(tabs.size() == Count, "one entry for every tab");
    std::set<std::string> ids;
    for (const TabInfo& t : tabs) {
        const std::string id = t.id;
        CHECK(!id.empty() && id.find(',') == std::string::npos && id.find(' ') == std::string::npos);   // an id goes in a comma list
        CHECK(ids.insert(id).second);                                                                   // and is unique
        CHECK(t.label && *t.label);
    }
    CHECK(std::string(tabs[Translator].id) == "translator" && std::string(tabs[Phone].id) == "phone");  // the order of the old TOOLS_PAGE numbers
}

static void parsing() {
    const Order d = defaults();
    CHECK(permutation(d));
    for (int i = 0; i < Count; i++) CHECK(d[i] == i);                                   // nothing saved: the original order
    CHECK(permutation(parseOrder("garbage,,,x")) && parseOrder("garbage,,,x") == d);      // nothing known in it
    const Order partial = parseOrder("phone,notes");
    CHECK(permutation(partial) && partial[0] == Phone && partial[1] == Notes);            // a short list: the rest follow in the original order
    CHECK(partial[2] == Translator && partial[3] == Freeform);                            // (Notes is already placed, so the rest skip it)
    const Order dup = parseOrder("notes,notes,translator,notes");
    CHECK(permutation(dup) && dup[0] == Notes && dup[1] == Translator);                   // a repeated id counts once
    const Order empties = parseOrder(",,notes,");
    CHECK(permutation(empties) && empties[0] == Notes);                                   // empty entries are skipped
    CHECK(parseOrder("Notes,PHONE") == d);                                                // the ids are lower case
    CHECK(parseOrder("notes2,notesx") == d);                                              // an id must match whole
    CHECK(permutation(parseOrder(std::string(5000, ',') + "units")));                     // a hostile long value
    // a list from an older build with fewer tabs: the tabs that came later land at the end
    const Order old = parseOrder("translator,notes,converter,video,units,timers,stopwatch,reminders");
    CHECK(permutation(old) && old[2] == Converter && old[8] == Freeform && old[Count - 1] == Phone);
}

static void formatting() {
    CHECK(formatOrder(defaults()).rfind("translator,notes,freeform,", 0) == 0);
    for (const char* list : {"", "phone,notes", "autostart,phone,keybinds,notes,translator", "x,y"}) {
        const std::string once = formatOrder(parseOrder(list));
        CHECK(formatOrder(parseOrder(once)) == once);                                     // writing and reading again changes nothing
        CHECK(parseOrder(once) == parseOrder(list));
    }
    CHECK(formatOrder(parseOrder("phone")).rfind("phone,translator,notes,", 0) == 0);
}

static void moving() {
    Order o = defaults();
    moveTab(o, 0, 3);                                                                     // forward: the tab lands at the target place
    CHECK(permutation(o) && o[3] == Translator && o[0] == Notes && o[1] == Freeform && o[2] == Converter);
    moveTab(o, 3, 0);                                                                     // and back
    CHECK(o == defaults());
    moveTab(o, 4, 4);                                                                     // nowhere
    CHECK(o == defaults());
    moveTab(o, Count - 1, 0);                                                             // last to first
    CHECK(permutation(o) && o[0] == Phone && o[1] == Translator && o[Count - 1] == Autostart);
    moveTab(o, 0, Count - 1);                                                             // first to last
    CHECK(o == defaults());
    moveTab(o, 5, 6);                                                                     // neighbours swap
    CHECK(permutation(o) && o[5] == Timers && o[6] == Units);
    for (int from = 0; from < Count; from++)
        for (int to = 0; to < Count; to++) { Order p = defaults(); moveTab(p, from, to); CHECK(permutation(p) && p[to] == from); }
}

static void hidden() {
    CHECK(!isHidden("", "notes"));
    CHECK(isHidden("notes", "notes") && isHidden("converter,notes", "notes") && isHidden("notes,converter", "notes"));
    CHECK(!isHidden("notes2", "notes") && !isHidden("mynotes", "notes") && !isHidden("converter,video", "notes"));   // whole ids only
    CHECK(!isHidden("Notes", "notes"));
    CHECK(isHidden("a,,notes", "notes") && !isHidden(",,", "notes"));
    CHECK(!isHidden("notes", ""));                                                        // an empty id is not hidden by a list that has none
}

int main() {
    table();
    parsing();
    formatting();
    moving();
    hidden();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
