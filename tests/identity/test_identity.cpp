// The display name and the host name (src/identity.h): what is kept of a typed name, the fallback to the user name, and what a host name may be.
#include "../../src/identity.h"

#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace identity;

static void cleaning() {
    CHECK(cleanDisplayName("Ana") == "Ana");
    CHECK(cleanDisplayName("  Ana  María ") == "Ana María");                 // spaces at the ends go, a run becomes one
    CHECK(cleanDisplayName("Ana\tMaría") == "Ana María");                    // a tab is a space
    CHECK(cleanDisplayName("A\nB\rC\x01") == "ABC");                          // control characters go
    CHECK(cleanDisplayName("Say \"hi\" \\o/") == "Say hi o/");               // what would break the config file goes
    CHECK(cleanDisplayName("   ").empty() && cleanDisplayName("").empty() && cleanDisplayName("\"\"").empty());
    CHECK(cleanDisplayName("Ñandú 日本語 😀") == "Ñandú 日本語 😀");           // other scripts and emoji stay
    CHECK(cleanDisplayName("a\xff" "b") == "ab");                             // invalid UTF-8 goes
    CHECK(cleanDisplayName("caf\xc3") == "caf");                              // a cut character goes
    const std::string longName(200, 'x');
    CHECK(cleanDisplayName(longName).size() == kMaxDisplayName);              // cut to the limit
    std::string accents;
    for (int i = 0; i < 100; i++) accents += "é";                             // 2 bytes each: the limit falls between bytes of one letter or not at all
    const std::string cut = cleanDisplayName(accents);
    CHECK(cut.size() <= kMaxDisplayName && cut.size() % 2 == 0 && validUtf8(cut) == cut);
    const std::string spaced = std::string(kMaxDisplayName - 1, 'x') + " yyyy";
    CHECK(cleanDisplayName(spaced).back() != ' ');                            // a cut never leaves a space at the end
    CHECK(cleanDisplayName(cleanDisplayName("  A \"b\"  ")) == cleanDisplayName("  A \"b\"  "));   // cleaning twice changes nothing
}

static void fallback() {
    CHECK(displayName("", "pc") == "pc");                                     // empty: the user name, always
    CHECK(displayName("   ", "pc") == "pc");                                  // only spaces: the same
    CHECK(displayName("\"\\", "pc") == "pc");                                 // nothing left after cleaning: the same
    CHECK(displayName("Ana", "pc") == "Ana");
    CHECK(displayName("  Ana ", "pc") == "Ana");
    CHECK(displayName("", "") == "");
}

static void hostnames() {
    for (const char* ok : {"pc", "my-pc", "PC1", "a", "host.example.org", "a-b.c-d", "1host", "x1"}) CHECK(validHostname(ok));
    for (const char* bad : {"", "-pc", "pc-", "my pc", "pc_1", "pc!", ".pc", "pc.", "a..b", "a.-b", "a-.b", "ñandú", "pc\n", "a/b", "pc;rm"}) CHECK(!validHostname(bad));
    CHECK(validHostname(std::string(63, 'a')) && !validHostname(std::string(64, 'a')));          // a label has 63 at most
    CHECK(validHostname(std::string(30, 'a') + "." + std::string(33, 'b')) && !validHostname(std::string(30, 'a') + "." + std::string(34, 'b')));  // and the whole name 64
}

int main() {
    cleaning();
    fallback();
    hostnames();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
