// What the graphics card is doing (src/gpuinfo.h): a made-up /sys/class/drm for AMD and Intel, and the nvidia-smi line.
#include "../../src/gpuinfo.h"

#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

namespace fs = std::filesystem;
static void put(const fs::path& p, const std::string& v) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << v << "\n";
}
static bool near(double a, double b) { return a - b < 1e-6 && b - a < 1e-6; }

static void amd() {
    const fs::path root = fs::temp_directory_path() / "gpuinfo-amd";
    fs::remove_all(root);
    put(root / "card0/device/gpu_busy_percent", "37");
    put(root / "card0/device/mem_info_vram_used", "2147483648");
    put(root / "card0/device/mem_info_vram_total", "8589934592");
    put(root / "card0/device/hwmon/hwmon3/temp1_input", "54000");
    put(root / "card0-DP-1/device/gpu_busy_percent", "99");   // a connector, not a card: ignored
    const gpuinfo::Sample s = gpuinfo::readSysfs(root);
    CHECK(s.present && s.vendor == "amd" && near(s.usage, 0.37) && !s.usageIsClock);
    CHECK(near(s.vramUsedGb, 2.0) && near(s.vramTotalGb, 8.0) && near(s.tempC, 54));
    CHECK(gpuinfo::text(s) == "37% · 2.0 / 8.0 GB");
    fs::remove_all(root);
}

static void intel() {
    const fs::path root = fs::temp_directory_path() / "gpuinfo-intel";
    fs::remove_all(root);
    put(root / "card1/gt_act_freq_mhz", "350");
    put(root / "card1/gt_max_freq_mhz", "1150");
    const gpuinfo::Sample s = gpuinfo::readSysfs(root);
    CHECK(s.present && s.vendor == "intel" && s.usageIsClock && near(s.freqMhz, 350) && near(s.freqMaxMhz, 1150));
    CHECK(s.usage > 0.30 && s.usage < 0.31);
    CHECK(gpuinfo::text(s) == "350 MHz");
    fs::remove_all(root);

    put(root / "card0/gt/gt0/rps_act_freq_mhz", "1200");   // the newer layout, and a clock above the stored maximum stays at 100 %
    put(root / "card0/gt/gt0/rps_max_freq_mhz", "1000");
    const gpuinfo::Sample n = gpuinfo::readSysfs(root);
    CHECK(n.present && n.usage == 1.0);
    fs::remove_all(root);
}

static void choosing() {
    const fs::path root = fs::temp_directory_path() / "gpuinfo-both";
    fs::remove_all(root);
    put(root / "card0/gt_act_freq_mhz", "300");
    put(root / "card0/gt_max_freq_mhz", "1000");
    put(root / "card1/device/gpu_busy_percent", "12");
    CHECK(gpuinfo::readSysfs(root).vendor == "amd");                       // the card with a real usage wins over the integrated one
    fs::remove_all(root);
    CHECK(!gpuinfo::readSysfs(root).present);                              // no directory at all
    put(root / "card0/device/gpu_busy_percent", "abc");                    // junk in the file
    CHECK(!gpuinfo::readSysfs(root).present);
    fs::remove_all(root);
}

static void sleeping() {
    const fs::path root = fs::temp_directory_path() / "gpuinfo-sleep";
    fs::remove_all(root);
    put(root / "card0/device/gpu_busy_percent", "80");
    put(root / "card0/device/power/runtime_status", "suspended");     // asleep: not read, not shown
    put(root / "card1/gt_act_freq_mhz", "300");
    put(root / "card1/gt_max_freq_mhz", "1000");
    put(root / "card1/device/power/runtime_status", "active");
    CHECK(gpuinfo::readSysfs(root).vendor == "intel");
    put(root / "card1/device/power/runtime_status", "unsupported");   // some drivers say so: that card is still shown
    CHECK(gpuinfo::readSysfs(root).vendor == "intel");
    put(root / "card0/device/power/runtime_status", "active");
    CHECK(gpuinfo::readSysfs(root).vendor == "amd");
    fs::remove_all(root);
}

static void nvidia() {
    gpuinfo::Sample s;
    CHECK(gpuinfo::parseNvidia("37, 2100, 8192, 54", s) && s.vendor == "nvidia" && near(s.usage, 0.37) && near(s.tempC, 54));
    CHECK(near(s.vramUsedGb, 2100 / 1024.0) && near(s.vramTotalGb, 8.0));
    CHECK(gpuinfo::parseNvidia("0, [N/A], [N/A], 40\n", s) && s.vramTotalGb < 0 && near(s.tempC, 40));
    CHECK(gpuinfo::text(s) == "0%");
    CHECK(!gpuinfo::parseNvidia("", s));
    CHECK(!gpuinfo::parseNvidia("[N/A], [N/A], [N/A], [N/A]", s));
    CHECK(!gpuinfo::parseNvidia("37, 2100", s));                           // short line
    CHECK(gpuinfo::parseNvidia("250, 1, 1, 1", s) && s.usage == 1.0);      // never above 100 %
}

int main() {
    amd();
    intel();
    choosing();
    sleeping();
    nvidia();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
