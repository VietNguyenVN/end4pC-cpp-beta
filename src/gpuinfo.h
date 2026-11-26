// What the graphics card is doing, from the files the kernel and the driver tools give: no root and no extra program except nvidia-smi.
//   AMD:    /sys/class/drm/cardN/device/gpu_busy_percent (usage), mem_info_vram_used/total, hwmon temp1_input
//   Intel:  /sys/class/drm/cardN/gt_act_freq_mhz (or gt/gt0/rps_act_freq_mhz) against the maximum: there is no usage number without root,
//           so the bar shows how fast the card runs and the text says MHz
//   NVIDIA: `nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu --format=csv,noheader,nounits`
// Reading is split from running programs so tests/gpu can use a made-up directory tree.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace gpuinfo {
struct Sample {
    bool present = false;
    std::string vendor;                 // "amd", "intel" or "nvidia"
    double usage = -1;                  // 0..1 when the driver tells; for Intel: the clock against its maximum (see usageIsClock)
    bool usageIsClock = false;
    double freqMhz = -1, freqMaxMhz = -1;
    double vramUsedGb = -1, vramTotalGb = -1;
    double tempC = -1;
};

inline bool readNumber(const std::filesystem::path& p, double& out) {
    std::ifstream f(p);
    double v = 0;
    if (!(f >> v) || !(v == v)) return false;
    out = v;
    return true;
}

inline bool firstNumber(const std::filesystem::path& dir, std::initializer_list<const char*> names, double& out) {
    for (const char* n : names) if (readNumber(dir / n, out)) return true;
    return false;
}

inline double hwmonTemp(const std::filesystem::path& device) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(device / "hwmon", ec)) {
        double milli = 0;
        if (readNumber(e.path() / "temp1_input", milli)) return milli / 1000.0;
    }
    return -1;
}

// Reads one card directory (…/card1). Returns false when it has nothing to show.
inline bool readCard(const std::filesystem::path& card, Sample& s) {
    const auto device = card / "device";
    {  // a card that sleeps (a laptop's second card) is left alone: reading its files would wake it up
        std::ifstream status(device / "power/runtime_status");
        std::string state;
        if (status >> state && (state == "suspended" || state == "suspending")) return false;
    }
    double busy = 0;
    if (readNumber(device / "gpu_busy_percent", busy)) {  // AMD (amdgpu)
        s = {};
        s.present = true;
        s.vendor = "amd";
        s.usage = std::clamp(busy / 100.0, 0.0, 1.0);
        double used = 0, total = 0;
        if (readNumber(device / "mem_info_vram_used", used) && readNumber(device / "mem_info_vram_total", total) && total > 0) {
            s.vramUsedGb = used / 1073741824.0;
            s.vramTotalGb = total / 1073741824.0;
        }
        s.tempC = hwmonTemp(device);
        return true;
    }
    double act = 0, max = 0;
    const bool freq = firstNumber(card, {"gt_act_freq_mhz", "gt/gt0/rps_act_freq_mhz"}, act);
    if (freq && firstNumber(card, {"gt_max_freq_mhz", "gt_RP0_freq_mhz", "gt/gt0/rps_max_freq_mhz"}, max) && max > 0) {  // Intel (i915 / xe)
        s = {};
        s.present = true;
        s.vendor = "intel";
        s.freqMhz = act;
        s.freqMaxMhz = max;
        s.usage = std::clamp(act / max, 0.0, 1.0);
        s.usageIsClock = true;
        return true;
    }
    return false;
}

// The card that is worth showing: an AMD one first (it has a real usage), then an Intel one. `root` is /sys/class/drm.
inline Sample readSysfs(const std::filesystem::path& root = "/sys/class/drm") {
    Sample best;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue;  // card1 yes, card1-DP-1 no
        Sample s;
        if (!readCard(e.path(), s)) continue;
        if (!best.present || (s.vendor == "amd" && best.vendor != "amd")) best = s;
    }
    return best;
}

// "37, 2100, 8192, 54" (utilization %, memory used MiB, memory total MiB, temperature C). "[N/A]" fields stay unknown.
inline bool parseNvidia(const std::string& csv, Sample& s) {
    double v[4] = {-1, -1, -1, -1};
    size_t pos = 0;
    for (int i = 0; i < 4; i++) {
        const size_t comma = csv.find(',', pos);
        const std::string field = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        char* end = nullptr;
        const double x = std::strtod(field.c_str(), &end);
        if (end != field.c_str()) v[i] = x;
        if (comma == std::string::npos) { if (i < 3) return false; break; }
        pos = comma + 1;
    }
    if (v[0] < 0 && v[1] < 0) return false;
    s = {};
    s.present = true;
    s.vendor = "nvidia";
    if (v[0] >= 0) s.usage = std::clamp(v[0] / 100.0, 0.0, 1.0);
    if (v[1] >= 0 && v[2] > 0) { s.vramUsedGb = v[1] / 1024.0; s.vramTotalGb = v[2] / 1024.0; }
    if (v[3] >= 0) s.tempC = v[3];
    return true;
}

// The line the card gets in a list: "37%", "37% · 2.1 / 8.0 GB", or "350 MHz" for a card with no usage number.
inline std::string text(const Sample& s) {
    char b[64];
    if (s.usageIsClock) { std::snprintf(b, sizeof b, "%.0f MHz", s.freqMhz); return b; }
    if (s.usage < 0) return "-";
    const int pct = static_cast<int>(s.usage * 100 + 0.5);
    if (s.vramTotalGb > 0) std::snprintf(b, sizeof b, "%d%% · %.1f / %.1f GB", pct, s.vramUsedGb, s.vramTotalGb);
    else std::snprintf(b, sizeof b, "%d%%", pct);
    return b;
}
}  // namespace gpuinfo
