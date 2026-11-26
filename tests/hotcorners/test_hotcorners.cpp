// The per-monitor hot corner rules (src/hotcorners.h): lookup, editing and validation of "connector/cornerKey=action,..." lists.
#include "../../src/hotcorners.h"

#include <cstdio>
#include <vector>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace hotcorners;

static const std::vector<std::string> kActions = {"none", "launcher", "overview", "lock"};

static void lookup() {
    CHECK(overrideAction("", "DP-1", "cornerTL").empty());                          // no rules
    CHECK(overrideAction("DP-1/cornerTL=overview", "", "cornerTL").empty());        // no monitor name: never an override
    CHECK(overrideAction("DP-1/cornerTL=overview", "DP-1", "cornerTL") == "overview");
    CHECK(overrideAction("DP-1/cornerTL=overview", "DP-1", "cornerTR").empty());    // same monitor, another corner
    CHECK(overrideAction("DP-1/cornerTL=overview", "HDMI-A-1", "cornerTL").empty());
    CHECK(overrideAction("DP-10/cornerTL=lock,DP-1/cornerTL=overview", "DP-1", "cornerTL") == "overview");   // DP-1 is not a prefix of DP-10 here
    CHECK(overrideAction("DP-1/cornerTL=lock,DP-10/cornerTL=overview", "DP-10", "cornerTL") == "overview");
    CHECK(overrideAction("DP-1/cornerTL=lock,DP-1/cornerTL=overview", "DP-1", "cornerTL") == "lock");        // the first one wins
    CHECK(overrideAction("A/cornerBR=launcher,B/cornerBR=lock", "B", "cornerBR") == "lock");                 // the last one, with no comma after it
    CHECK(overrideAction("DP-1/cornerTL=none", "DP-1", "cornerTL") == "none");                                // "none" is a real answer, not "absent"
}

static void editing() {
    CHECK(withAction("", "DP-1", "cornerTL", "overview") == "DP-1/cornerTL=overview");
    CHECK(withAction("DP-1/cornerTL=lock", "DP-1", "cornerTL", "overview") == "DP-1/cornerTL=overview");     // replaced
    CHECK(withAction("DP-1/cornerTL=lock", "DP-1", "cornerTR", "overview") == "DP-1/cornerTL=lock,DP-1/cornerTR=overview");
    CHECK(withAction("DP-1/cornerTL=lock,B/cornerBL=launcher", "DP-1", "cornerTL", "") == "B/cornerBL=launcher");
    CHECK(withAction("DP-1/cornerTL=lock", "DP-1", "cornerTL", "").empty());
    CHECK(withAction(",,A/cornerTL=lock,,", "B", "cornerTL", "none") == "A/cornerTL=lock,B/cornerTL=none");  // empty entries are dropped
    CHECK(withAction("A/cornerTL=lock,A/cornerTL=launcher", "A", "cornerTL", "overview") == "A/cornerTL=overview");  // a duplicate collapses
    const std::string rules = withAction(withAction("", "A", "cornerTL", "lock"), "B", "cornerTL", "none");
    CHECK(overrideAction(rules, "A", "cornerTL") == "lock" && overrideAction(rules, "B", "cornerTL") == "none");  // what was written can be read back
}

static void validation() {
    CHECK(validRules("", kActions));
    CHECK(validRules("DP-1/cornerTL=overview", kActions));
    CHECK(validRules("DP-1/cornerTL=overview,HDMI-A-1/cornerBR=none", kActions));
    CHECK(validRules("eDP-1/cornerTR=lock,", kActions));                              // a comma at the end is harmless
    CHECK(!validRules(",,", kActions));                                               // empty entries in the middle are not
    CHECK(!validRules("DP-1/cornerTL=", kActions));                                   // no action
    CHECK(!validRules("DP-1/cornerTL", kActions));                                    // no "="
    CHECK(!validRules("DP-1=cornerTL/overview", kActions));                           // "=" before "/"
    CHECK(!validRules("/cornerTL=overview", kActions));                               // no monitor
    CHECK(!validRules("DP-1/cornerXX=overview", kActions));                           // unknown corner
    CHECK(!validRules("DP-1/cornerTL=Overview", kActions));                           // actions are lower case
    CHECK(!validRules("DP-1/cornerTL=wallpapers", kActions));                         // not in this list
    CHECK(!validRules("D P/cornerTL=lock", kActions));                                // a monitor name has no spaces or slashes
    CHECK(!validRules("DP-1/cornerTL=lock,bad", kActions));                           // one bad entry spoils the whole list
}

static void keeping() {
    CHECK(keepValid("", kActions).empty());
    CHECK(keepValid("DP-1/cornerTL=overview", kActions) == "DP-1/cornerTL=overview");
    CHECK(keepValid("DP-1/cornerTL=overview,HDMI/cornerBR=Wallpapers", kActions) == "DP-1/cornerTL=overview");   // the typo goes, the good entry stays
    CHECK(keepValid("bad,A/cornerTL=lock,,B/cornerTR=nope,C/cornerBL=none", kActions) == "A/cornerTL=lock,C/cornerBL=none");
    CHECK(keepValid("x/cornerTL=", kActions).empty() && keepValid(",,,", kActions).empty());
    CHECK(validRules(keepValid("a/cornerTL=lock,junk", kActions), kActions));          // what it returns always passes the strict check
}

int main() {
    lookup();
    editing();
    validation();
    keeping();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
