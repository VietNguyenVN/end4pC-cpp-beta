// The equalizer of the media panel. Ten bands and a preamp are written as an EasyEffects preset (its Equalizer plugin) and loaded
// into the running EasyEffects, which does the filtering for the whole system output. Without EasyEffects the page only says so.
constexpr int kEqBands = 10;
constexpr double kEqRange = 12;  // dB, either way
constexpr double kEqFreq[kEqBands] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr const char* kEqFreqLabel[kEqBands] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
constexpr int kEqPresetCount = 8;
struct EqPreset { const char* name; double gain[kEqBands]; };
constexpr EqPreset kEqPresets[kEqPresetCount] = {
    {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Bass", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"Treble", {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
    {"Vocal", {-2, -3, -2, 1, 3, 4, 3, 1, 0, -1}},
    {"Pop", {-1, 1, 3, 4, 3, 0, -1, -1, 1, 2}},
    {"Rock", {5, 4, 3, 1, -1, -1, 1, 3, 4, 5}},
    {"Jazz", {4, 3, 1, 2, -2, -2, 0, 1, 3, 4}},
    {"Classic", {5, 4, 3, 2, -1, -1, 0, 2, 3, 4}},
};

std::string eqStatePath() { return stateDir() + "/equalizer.txt"; }

bool eqAvailable() {
    static int cached = -1;
    if (cached < 0) cached = captureArgv({"sh", "-c", "command -v easyeffects"}).empty() ? 0 : 1;
    return cached == 1;
}

void eqLoad(EqState& E) {
    if (E.loaded) return;
    E.loaded = true;
    std::ifstream f(eqStatePath());
    for (std::string line; std::getline(f, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "enabled") E.enabled = v == "1";
        else if (k == "preset") E.preset = std::clamp(std::atoi(v.c_str()), -1, kEqPresetCount - 1);
        else if (k == "preamp") E.preamp = std::clamp(std::atof(v.c_str()), -kEqRange, kEqRange);
        else if (k.rfind("gain", 0) == 0) {
            const int i = std::atoi(k.c_str() + 4);
            if (i >= 0 && i < kEqBands) E.gain[i] = std::clamp(std::atof(v.c_str()), -kEqRange, kEqRange);
        }
    }
}

void eqSave(const EqState& E) {
    mkdir(stateDir().c_str(), 0755);
    std::ofstream f(eqStatePath(), std::ios::trunc);
    f << "enabled=" << (E.enabled ? 1 : 0) << "\npreset=" << E.preset << "\npreamp=" << E.preamp << "\n";
    for (int i = 0; i < kEqBands; i++) f << "gain" << i << "=" << E.gain[i] << "\n";
}

// EasyEffects 8 keeps its presets in the data directory, the older ones in the config directory.
std::string eqEnvDir(const char* var, const std::string& fallback) {
    const char* x = std::getenv(var);
    return x && *x ? x : fallback;
}
std::string eqPresetDir() {
    const std::string data = eqEnvDir("XDG_DATA_HOME", homeDir() + "/.local/share") + "/easyeffects/output";
    const std::string conf = eqEnvDir("XDG_CONFIG_HOME", homeDir() + "/.config") + "/easyeffects/output";
    struct stat st{};
    if (stat(data.c_str(), &st) == 0) return data;
    if (stat(conf.c_str(), &st) == 0) return conf;
    return data;
}

// The merge into the preset EasyEffects has loaded (or the creation of a preset of our own) is done by scripts/eq_apply.py.
void eqApply(const EqState& E) {
    if (!eqAvailable()) return;
    const std::string script = assetsDir() + "/../scripts/eq_apply.py", dir = eqPresetDir(), enabled = E.enabled ? "1" : "0";
    std::vector<std::string> args = {"python3", script, dir, enabled, std::to_string(E.preamp)};
    for (int i = 0; i < kEqBands; i++) args.push_back(std::to_string(E.gain[i]));
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    runDetachedArgs(argv);
}
