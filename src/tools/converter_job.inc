namespace conv {
struct Format { const char* ext; const char* label; bool lossy; };
constexpr Format kFormats[] = {{"png", "PNG", false}, {"jpg", "JPG", true},  {"webp", "WEBP", true}, {"avif", "AVIF", true}, {"heic", "HEIC", true},
                               {"jxl", "JXL", true},  {"bmp", "BMP", false}, {"tiff", "TIFF", false}, {"pdf", "PDF", false}};
constexpr int kFormatCount = 9, kPdf = 8, kJpg = 1;
constexpr int kSizeSteps[] = {0, 2048, 1080, 720};

struct Options {
    int format = 2, quality = 85, size = 0;
    bool strip = false, nextToSource = true;
};

inline std::string outputDir(const std::string& input, const Options& o) {
    return o.nextToSource ? dirOf(input) : homeDir() + "/Pictures/Converted";
}

inline std::string tempPath(const std::string& dir, const std::string& stem, const char* ext) {
    return dir + "/." + stem + ".e4p-part." + ext;
}

inline std::string reserveName(const std::string& dir, const std::string& stem, const char* ext) {
    for (int n = 1; n < 10000; n++) {
        const std::string p = dir + "/" + stem + "_converted" + (n > 1 ? "-" + std::to_string(n) : "") + "." + ext;
        const int fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (fd >= 0) { close(fd); return p; }
        if (errno != EEXIST) return "";
    }
    return "";
}

inline std::vector<std::string> magickArgs(const std::vector<std::string>& in, const std::string& out, const Options& o) {
    const Format& f = kFormats[o.format];
    std::vector<std::string> a = {"magick"};
    for (const std::string& p : in) a.push_back(extOf(p) == "gif" ? p + "[0]" : p);
    a.push_back("-auto-orient");
    if (o.strip) a.push_back("-strip");
    if (kSizeSteps[o.size] > 0) a.push_back("-resize"), a.push_back(std::to_string(kSizeSteps[o.size]) + "x" + std::to_string(kSizeSteps[o.size]) + ">");
    if (o.format == kJpg || o.format == kPdf) for (const char* s : {"-background", "white", "-alpha", "remove", "-alpha", "off"}) a.push_back(s);
    if (f.lossy) a.push_back("-quality"), a.push_back(std::to_string(o.quality));
    else if (o.format == 0) a.push_back("-define"), a.push_back("png:compression-level=9");
    else if (o.format == 7) a.push_back("-compress"), a.push_back("zip");
    a.push_back(out);
    return a;
}

inline bool ffmpegCan(const Options& o) { return o.format <= 3 || o.format == 6 || o.format == 7; }

inline std::vector<std::string> ffmpegArgs(const std::string& in, const std::string& out, const Options& o) {
    std::vector<std::string> a = {"ffmpeg", "-y", "-loglevel", "error", "-i", in};
    if (kSizeSteps[o.size] > 0) {
        const std::string m = std::to_string(kSizeSteps[o.size]);
        a.push_back("-vf"), a.push_back("scale='min(" + m + ",iw)':'min(" + m + ",ih)':force_original_aspect_ratio=decrease");
    }
    if (o.strip) a.push_back("-map_metadata"), a.push_back("-1");
    if (o.format == kJpg) a.push_back("-q:v"), a.push_back(std::to_string(2 + (100 - o.quality) * 29 / 100));
    else if (o.format == 2) a.push_back("-quality"), a.push_back(std::to_string(o.quality));
    else if (o.format == 3) a.push_back("-crf"), a.push_back(std::to_string(63 - o.quality * 63 / 100));
    a.push_back("-frames:v"), a.push_back("1");
    a.push_back(out);
    return a;
}

inline std::string firstLine(const std::string& s) {
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line))
        if (line.find_first_not_of(" \t\r") != std::string::npos) return line.size() > 160 ? line.substr(0, 160) : line;
    return "";
}
}
