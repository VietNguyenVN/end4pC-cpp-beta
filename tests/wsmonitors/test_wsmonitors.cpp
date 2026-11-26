// The workspaces a bar shows (src/wsmonitors.h): the first id of a group, and the workspace in use per monitor from hyprctl's JSON.
#include "../../src/wsmonitors.h"

#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace wsmon;

static void groups() {
    for (int ws = 1; ws <= 8; ws++) CHECK(groupBase(ws, 8) == 1);          // inside the first group the bar is what it always was
    CHECK(groupBase(9, 8) == 9 && groupBase(11, 8) == 9 && groupBase(16, 8) == 9 && groupBase(17, 8) == 17);
    CHECK(groupBase(10, 10) == 1 && groupBase(11, 10) == 11 && groupBase(20, 10) == 11 && groupBase(21, 10) == 21);
    CHECK(groupBase(11, 20) == 1 && groupBase(20, 20) == 1 && groupBase(21, 20) == 21);   // 20 shown: one group of twenty, no reset at 11
    CHECK(groupBase(12, 12) == 1 && groupBase(13, 12) == 13);                              // (niri: as many as the output has)
    CHECK(groupBase(0, 8) == 1 && groupBase(-5, 8) == 1);                                  // no workspace in use / a special one
    CHECK(groupBase(5, 0) == 1 && groupBase(5, -3) == 1);                                  // nothing shown
    CHECK(groupBase(1, 1) == 1 && groupBase(4, 1) == 4);                                   // one slot: the bar follows the workspace
    for (int shown = 1; shown <= 20; shown++)
        for (int ws = 1; ws <= 60; ws++) {
            const int base = groupBase(ws, shown);
            CHECK(base >= 1 && base <= ws && ws < base + shown && (base - 1) % shown == 0);  // the workspace in use is always in the window
        }
}

static const char* kOne = R"([{
    "id": 0,
    "name": "VGA-1",
    "description": "Ancor Communications Inc MX259H HBLMRS015261",
    "width": 1920,
    "activeWorkspace": {
        "address": "1",
        "id": 3,
        "type": "normal",
        "name": "3"
    },
    "specialWorkspace": {
        "address": "",
        "type": "",
        "name": ""
    },
    "reserved": [4, 44, 4, 4],
    "focused": true
}])";

static void parsing() {
    const auto one = activeWorkspaces(kOne);
    CHECK(one.size() == 1 && one.at("VGA-1") == 3);

    const std::string two = std::string("[") + R"({"id": 0, "name": "DP-1", "description": "a", "activeWorkspace": {"address": "1", "id": 2, "type": "normal", "name": "2"}, "specialWorkspace": {"address": "", "type": "", "name": ""}},)" +
                            R"({"id": 1, "name": "DP-2", "description": "b", "activeWorkspace": {"address": "2", "id": 12, "type": "normal", "name": "12"}, "specialWorkspace": {"address": "x", "id": -98, "type": "special", "name": "special:s"}}])";
    const auto m = activeWorkspaces(two);
    CHECK(m.size() == 2 && m.at("DP-1") == 2 && m.at("DP-2") == 12);                      // the special workspace's id is not taken for the workspace in use

    CHECK(activeWorkspaces("").empty() && activeWorkspaces("[]").empty() && activeWorkspaces("not json").empty());
    CHECK(activeWorkspaces("[{\"name\": \"A\"}]").empty());                                // no workspace
    CHECK(activeWorkspaces("[{\"activeWorkspace\": {\"id\": 4}}]").empty());               // no name
    CHECK(activeWorkspaces("[{\"name\": \"A\", \"activeWorkspace\": {\"id\": 0}}]").empty());   // 0 is no workspace
    CHECK(activeWorkspaces("[{\"name\": \"A\", \"activeWorkspace\": {\"id\": -3}}]").empty());
    CHECK(activeWorkspaces("[{\"name\": \"A\", \"activeWorkspace\": {\"id\": \"x\"}}]").empty());
    CHECK(activeWorkspaces("[{\"name\": \"A\", \"activeWorkspace\": {\"id\": 7").empty());  // cut off in the middle
    // a description with braces, quotes and an escaped quote must not confuse the object boundaries
    const auto tricky = activeWorkspaces(R"([{"name": "HDMI-A-1", "description": "Acme } \" { 24\" {x}", "activeWorkspace": {"id": 9}}, {"name": "B", "activeWorkspace": {"id": 10}}])");
    CHECK(tricky.size() == 2 && tricky.at("HDMI-A-1") == 9 && tricky.at("B") == 10);
    const auto escaped = activeWorkspaces(R"([{"name": "we\"ird", "activeWorkspace": {"id": 5}}])");
    CHECK(escaped.size() == 1 && escaped.count("we\"ird") == 1);
    CHECK(activeWorkspaces(std::string(100000, '[')).empty());                              // hostile input: no crash
}

int main() {
    groups();
    parsing();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
