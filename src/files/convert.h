// The conversions, the archives and the extractions of the right-click menu: each one is a list of commands for the worker (ffmpeg, magick,
// zip, 7z, tar, bsdtar...). One click makes the file next to the original; the window shows a progress bar while it works.
// Included once by fil3s.cpp, inside its anonymous namespace, after model.h.
#pragma once

enum class Group { Image, Video, Audio };
struct Target { Group group; const char* ext; const char* label; };

// Every format the menu offers. The index in this table is what a menu item carries.
const std::vector<Target>& targets() {
    static const std::vector<Target> t = {
        {Group::Image, "png", "PNG"}, {Group::Image, "jpg", "JPG"}, {Group::Image, "webp", "WebP"}, {Group::Image, "avif", "AVIF"},
        {Group::Image, "gif", "GIF"}, {Group::Image, "bmp", "BMP"}, {Group::Image, "tiff", "TIFF"}, {Group::Image, "ico", "ICO"}, {Group::Image, "pdf", "PDF"},
        {Group::Video, "mp4", "MP4 (H.264)"}, {Group::Video, "webm", "WebM (VP9)"}, {Group::Video, "mkv", "MKV (no re-encode)"}, {Group::Video, "gif", "GIF"},
        {Group::Audio, "mp3", "MP3"}, {Group::Audio, "flac", "FLAC"}, {Group::Audio, "opus", "Opus"}, {Group::Audio, "ogg", "OGG Vorbis"}, {Group::Audio, "m4a", "M4A (AAC)"}, {Group::Audio, "wav", "WAV"},
    };
    return t;
}

struct ArchiveFormat { const char* ext; const char* label; const char* tool; };
const std::vector<ArchiveFormat>& archiveFormats() {
    static const std::vector<ArchiveFormat> f = {
        {"zip", "ZIP", "zip"}, {"7z", "7z", "7z"}, {"tar.gz", "TAR.GZ", "tar"}, {"tar.xz", "TAR.XZ", "tar"}, {"tar.zst", "TAR.ZST", "tar"},
        {"tar.bz2", "TAR.BZ2", "tar"}, {"rar", "RAR", "rar"},  // RAR can only be written with the (non-free) `rar` program: the entry shows when it is installed
    };
    return f;
}

std::string stemOf(const std::string& name) {
    for (const char* e : {".tar.gz", ".tar.xz", ".tar.zst", ".tar.bz2", ".tar.lz4"}) {
        const std::string ext = e;
        if (name.size() > ext.size() && lowerCase(name).compare(name.size() - ext.size(), ext.size(), ext) == 0) return name.substr(0, name.size() - ext.size());
    }
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

// Is the file one the target can be made from? (Audio and video both give audio; images give images.)
bool offers(Kind kind, const Target& t, const std::string& ext = "") {
    if (kind == Kind::Image) return t.group == Group::Image && ext != "svg" && ext != t.ext && !(ext == "jpeg" && std::string(t.ext) == "jpg");
    if (kind == Kind::Video) return t.group == Group::Video || t.group == Group::Audio;
    if (kind == Kind::Audio) return t.group == Group::Audio;
    return false;
}

Cmd convertCommand(const Entry& e, const Target& t) {
    Cmd c;
    const std::string dir = fs::path(e.path).parent_path().string();
    c.output = freeName(dir, stemOf(e.name) + "." + t.ext);
    c.label = e.name;
    if (t.group == Group::Image) {
        // ImageMagick guesses a lot from the content of a file (and has coders that read other files): the format is fixed from the extension on
        // both sides, svg is not accepted, and the work is bounded.
        static const std::map<std::string, std::string> coder = {{"png", "png"}, {"jpg", "jpeg"}, {"jpeg", "jpeg"}, {"gif", "gif"}, {"webp", "webp"}, {"bmp", "bmp"}, {"tif", "tiff"}, {"tiff", "tiff"},
                                                                  {"avif", "avif"}, {"heic", "heic"}, {"ico", "ico"}, {"jxl", "jxl"}};
        const std::string ext = extensionOf(e.name), x = t.ext;
        const auto in = coder.find(ext);
        if (in == coder.end()) { c.argv = {"false"}; return c; }
        const bool animated = (ext == "gif" || ext == "webp") && (x == "gif" || x == "webp");
        const std::string outCoder = x == "jpg" ? "jpeg" : x == "tif" ? "tiff" : x;
        c.argv = {"magick", "-limit", "memory", "1GiB", "-limit", "map", "2GiB", "-limit", "time", "300", "-limit", "disk", "4GiB", in->second + ":" + e.path + (animated ? "" : "[0]")};
        if (x == "jpg") for (const char* a : {"-background", "white", "-alpha", "remove", "-alpha", "off"}) c.argv.push_back(a);
        if (x == "ico") for (const char* a : {"-resize", "256x256>", "-define", "icon:auto-resize=256,128,64,48,32,16"}) c.argv.push_back(a);
        c.argv.push_back(outCoder + ":" + c.output);
        return c;
    }
    c.probe = e.path;
    // -protocol_whitelist file: a playlist or a "concat" inside the file cannot make ffmpeg read the network or another path; -n: never overwrite.
    c.argv = {"ffmpeg", "-nostdin", "-n", "-loglevel", "error", "-progress", "pipe:1", "-protocol_whitelist", "file", "-i", e.path};
    const std::string x = t.ext;
    auto add = [&](std::initializer_list<const char*> a) { for (const char* s : a) c.argv.push_back(s); };
    if (t.group == Group::Audio) {
        add({"-vn"});
        if (x == "mp3") add({"-c:a", "libmp3lame", "-q:a", "2"});
        else if (x == "flac") add({"-c:a", "flac"});
        else if (x == "opus") add({"-c:a", "libopus", "-b:a", "160k"});
        else if (x == "ogg") add({"-c:a", "libvorbis", "-q:a", "5"});
        else if (x == "m4a") add({"-c:a", "aac", "-b:a", "192k"});
        else add({"-c:a", "pcm_s16le"});
    } else if (x == "mp4") add({"-c:v", "libx264", "-preset", "medium", "-crf", "23", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k", "-movflags", "+faststart"});
    else if (x == "webm") add({"-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-row-mt", "1", "-deadline", "good", "-cpu-used", "4", "-c:a", "libopus"});
    else if (x == "mkv") add({"-c", "copy"});
    else add({"-vf", "fps=12,scale=480:-2:flags=lanczos", "-loop", "0"});  // gif
    c.argv.push_back(c.output);
    return c;
}

// An archive of the selection, next to it. One item is named after it, several after the folder they are in.
Cmd archiveCommand(const std::vector<const Entry*>& sel, const ArchiveFormat& f, const std::string& dir) {
    Cmd c;
    c.cwd = dir;
    const std::string base = sel.size() == 1 ? (sel[0]->dir ? sel[0]->name : stemOf(sel[0]->name)) : (fs::path(dir).filename().empty() ? "Archive" : fs::path(dir).filename().string());
    c.output = freeName(dir, base + "." + f.ext);
    c.label = fs::path(c.output).filename().string();
    const std::string ext = f.ext, out = c.output;
    if (ext == "zip") c.argv = {"zip", "-r", "-q", out};
    else if (ext == "7z") c.argv = {"7z", "a", "-bd", "-y", out};
    else if (ext == "rar") c.argv = {"rar", "a", "-idq", out};
    else if (ext == "tar.gz") c.argv = {"tar", "-czf", out};
    else if (ext == "tar.xz") c.argv = {"tar", "-cJf", out};
    else if (ext == "tar.bz2") c.argv = {"tar", "-cjf", out};
    else c.argv = {"tar", "--zstd", "-cf", out};
    for (const Entry* e : sel) c.argv.push_back("./" + e->name);  // ("./": a file called -rf is a file, not an option)
    return c;
}

// Extracting: into a folder named after the archive (the default), or straight into the folder it is in.
Cmd extractCommand(const Entry& e, bool intoFolder) {
    Cmd c;
    const std::string dir = fs::path(e.path).parent_path().string();
    c.label = e.name;
    const std::string dest = intoFolder ? freeName(dir, stemOf(e.name)) : dir;
    if (intoFolder) c.output = dest;  // removed if it fails (a half-extracted folder helps nobody); "here" never deletes anything
    c.argv = {"sh", "-c", "mkdir -p \"$2\" && { bsdtar -xkf \"$1\" -C \"$2\" || 7z x -aos -y -bd -o\"$2\" \"$1\" >/dev/null; }", "sh", e.path, dest};
    return c;
}
