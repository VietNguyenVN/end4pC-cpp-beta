namespace vid {
struct Info {
    double dur = 0;
    long long size = 0;
    int w = 0, h = 0;
    std::string vcodec, acodec;
    bool video = false, audio = false;
};

std::vector<std::string> probeArgs(const std::string& file) {
    return {"ffprobe", "-v", "error", "-show_entries", "format=duration,size:stream=codec_type,codec_name,width,height", "-of", "flat", "-i", file};
}

bool parseProbe(const std::string& flat, Info& info) {
    std::map<int, std::map<std::string, std::string>> streams;
    std::map<std::string, std::string> format;
    std::stringstream ss(flat);
    std::string line;
    while (std::getline(ss, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (val.size() >= 2 && val.front() == '"') val = val.substr(1, val.size() - 2);
        if (key.rfind("format.", 0) == 0) format[key.substr(7)] = val;
        else if (key.rfind("streams.stream.", 0) == 0) {
            const size_t dot = key.find('.', 15);
            if (dot != std::string::npos) streams[std::atoi(key.substr(15, dot - 15).c_str())][key.substr(dot + 1)] = val;
        }
    }
    info = Info{};
    info.dur = std::atof(format["duration"].c_str());
    info.size = std::atoll(format["size"].c_str());
    for (auto& [idx, s] : streams) {
        if (s["codec_type"] == "video" && !info.video && s["width"] != "") {
            info.video = true;
            info.vcodec = s["codec_name"];
            info.w = std::atoi(s["width"].c_str());
            info.h = std::atoi(s["height"].c_str());
        } else if (s["codec_type"] == "audio" && !info.audio) {
            info.audio = true;
            info.acodec = s["codec_name"];
        }
    }
    return info.dur > 0 && info.video;
}

enum Op { kTrim, kConvert, kAudio, kGif, kCompress, kMute, kOpCount };

const char* const kConvExt[] = {"mp4", "webm", "mkv", "mp4"};
const char* const kAudExt[] = {"opus", "mp3", "m4a"};
const int kGifWidth[] = {320, 480, 640};
const int kCrf[] = {28, 23, 18};
const int kMaxHeight[] = {0, 1080, 720, 480};

std::string outExt(Op op, const int* sel, const std::string& srcExt) {
    switch (op) {
        case kTrim: return sel[0] == 1 && !srcExt.empty() ? srcExt : "mp4";
        case kConvert: return kConvExt[sel[0]];
        case kAudio: return kAudExt[sel[0]];
        case kGif: return "gif";
        case kCompress: return "mp4";
        case kMute: return srcExt.empty() ? "mp4" : srcExt;
        default: return "mp4";
    }
}

std::string fmtSec(double s) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", s);
    return b;
}
void add(std::vector<std::string>& a, std::initializer_list<const char*> more) { for (const char* m : more) a.push_back(m); }

void convertArgs(std::vector<std::string>& a, int fmt) {
    if (fmt == 0) add(a, {"-c:v", "libx264", "-crf", "23", "-c:a", "aac", "-movflags", "+faststart"});
    else if (fmt == 1) add(a, {"-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-c:a", "libopus"});
    else if (fmt == 2) add(a, {"-c", "copy"});
    else add(a, {"-c:v", "libsvtav1", "-crf", "35", "-c:a", "aac", "-movflags", "+faststart"});
}
void audioArgs(std::vector<std::string>& a, int fmt, const Info& info) {
    add(a, {"-vn"});
    if (fmt == 0) add(a, {"-c:a", "libopus", "-b:a", "128k"});
    else if (fmt == 1) add(a, {"-c:a", "libmp3lame", "-q:a", "2"});
    else if (info.acodec == "aac") add(a, {"-c:a", "copy"});
    else add(a, {"-c:a", "aac", "-b:a", "192k"});
}
void gifArgs(std::vector<std::string>& a, int width) {
    add(a, {"-an", "-vf"});
    a.push_back("fps=10,scale=" + std::to_string(width) + ":-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse");
}
void compressArgs(std::vector<std::string>& a, const int* sel) {
    add(a, {"-c:v", "libx264", "-preset", "medium"});
    a.push_back("-crf");
    a.push_back(std::to_string(kCrf[sel[0]]));
    if (kMaxHeight[sel[1]] > 0) {
        a.push_back("-vf");
        a.push_back("scale=-2:'min(" + std::to_string(kMaxHeight[sel[1]]) + ",ih)'");
    }
    add(a, {"-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart"});
}

std::vector<std::string> ffmpegArgs(Op op, const int* sel, const Info& info, const std::string& in, const std::string& out, bool partial, double start, double len) {
    std::vector<std::string> a = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-n", "-progress", "pipe:1", "-nostats"};
    if (partial) { a.push_back("-ss"); a.push_back(fmtSec(start)); a.push_back("-t"); a.push_back(fmtSec(len)); }
    a.push_back("-i");
    a.push_back(in);
    switch (op) {
        case kTrim:
            if (sel[0] == 1) add(a, {"-c", "copy", "-avoid_negative_ts", "make_zero"});
            else add(a, {"-c:v", "libx264", "-crf", "20", "-c:a", "aac"});
            break;
        case kConvert: convertArgs(a, sel[0]); break;
        case kAudio: audioArgs(a, sel[0], info); break;
        case kGif: gifArgs(a, kGifWidth[sel[0]]); break;
        case kCompress: compressArgs(a, sel); break;
        case kMute:
            if (partial) add(a, {"-c:v", "libx264", "-crf", "20", "-an"});
            else add(a, {"-c", "copy", "-an"});
            break;
        default: break;
    }
    a.push_back(out);
    return a;
}
}
