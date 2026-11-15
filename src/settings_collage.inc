
constexpr size_t kCollageMaxDepth = 8;
constexpr double kCollageAspect = 1.6;
constexpr double kCgGap = 12, kCgMinTile = 100, kCgMaxTile = 200;
constexpr double kCgPreviewMax = 520, kCgInset = 6, kCgRadius = 6;
constexpr double kCgBadge = 26;
constexpr int kCgOptSub = 4, kCgOptBase = 100;

const std::vector<std::string> kCollagePresets = {
    "v500(L,L)", "h500(L,L)", "v620(L,h500(L,L))", "v380(h500(L,L),L)", "h600(L,v500(L,L))",
    "v333(L,v500(L,L))", "v500(h500(L,L),h500(L,L))", "v600(L,h333(L,h500(L,L)))",
    "v500(h500(L,v500(L,L)),h500(v500(L,L),L))", "v333(h500(L,L),v500(h500(L,L),h500(L,L)))",
    "h333(L,h500(L,L))", "v250(L,v667(L,L))", "h620(v500(L,L),L)", "v300(h333(L,h500(L,L)),L)",
    "h700(L,v333(L,v500(L,L)))", "v700(h500(L,L),h250(L,h333(L,h500(L,L))))",
    "h500(v333(L,v500(L,L)),v500(L,L))", "v500(L,h500(v500(L,L),v500(L,L)))",
    "v500(L,h500(v500(L,L),v333(L,v500(L,L))))", "v500(L,h500(L,v500(L,L)))", "h300(L,v500(L,L))",
    "v500(L,h700(L,L))", "v500(h350(L,L),L)", "v350(L,h333(L,h500(L,L)))", "v500(v500(L,L),v500(L,L))",
    "h500(h500(L,L),h500(L,L))", "v250(h500(L,L),v667(L,h500(L,L)))", "h550(L,h500(v500(L,L),v500(L,L)))",
    "v600(h600(L,v500(L,L)),h333(L,h500(L,L)))", "v500(h333(L,h500(L,L)),h500(L,L))",
};

struct CNode {
    bool leaf = true;
    int id = 0;
    std::string img;
    char d = 'v';
    double r = 0.5;
    std::vector<CNode> kids;
};

struct CRect {
    double x = 0, y = 0, w = 0, h = 0;
    int id = 0;
    std::string img;
};

struct CollageDims {
    double textH = 0, gridH = 0, tw = 0, th = 0, labelY = 0, pvY = 0, pvW = 0, pvH = 0, hintY = 0, hintH = 0, h = 0;
    int cols = 1;
};

std::string jsonQuote(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char esc[8]; std::snprintf(esc, sizeof esc, "\\u%04x", c); out += esc; }
        else out += c;
    }
    return out + "\"";
}

std::string treeJson(const CNode& n) {
    if (n.leaf) return "{\"t\":\"leaf\",\"id\":" + std::to_string(n.id) + ",\"img\":" + jsonQuote(n.img) + "}";
    char ratio[24];
    std::snprintf(ratio, sizeof ratio, "%.3f", n.r);
    return "{\"t\":\"split\",\"d\":\"" + std::string(1, n.d) + "\",\"r\":" + ratio + ",\"a\":" + treeJson(n.kids[0]) + ",\"b\":" + treeJson(n.kids[1]) + "}";
}

struct CTreeReader {
    const std::string& s;
    size_t i = 0;
    void skipWs() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; }
    bool eat(char c) {
        skipWs();
        if (i < s.size() && s[i] == c) { i++; return true; }
        return false;
    }
    bool readString(std::string& out) {
        out.clear();
        if (!eat('"')) return false;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) i++;
            out += s[i++];
        }
        return i < s.size() && s[i++] == '"';
    }
    bool readNumber(double& out) {
        skipWs();
        char* end = nullptr;
        out = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        i = static_cast<size_t>(end - s.c_str());
        return true;
    }
    bool readField(CNode& n, const std::string& key, bool& typed, size_t depth) {
        std::string value;
        if (key == "t") {
            if (!readString(value)) return false;
            n.leaf = value == "leaf";
            typed = true;
            return value == "leaf" || value == "split";
        }
        if (key == "img") return readString(n.img);
        if (key == "d") {
            if (!readString(value) || value.size() != 1) return false;
            n.d = value[0];
            return true;
        }
        if (key == "id") { double v = 0; if (!readNumber(v)) return false; n.id = static_cast<int>(v); return true; }
        if (key == "r") return readNumber(n.r);
        if (key == "a" || key == "b") {
            n.kids.resize(2);
            return readNode(n.kids[key == "b" ? 1 : 0], depth + 1);
        }
        return false;
    }
    bool readNode(CNode& n, size_t depth) {
        if (depth > kCollageMaxDepth || !eat('{')) return false;
        bool typed = false;
        for (;;) {
            std::string key;
            if (!readString(key) || !eat(':') || !readField(n, key, typed, depth)) return false;
            if (eat('}')) break;
            if (!eat(',')) return false;
        }
        return typed && (n.leaf ? n.kids.empty() : n.kids.size() == 2);
    }
};

bool parseCollageTree(const std::string& json, CNode& out) {
    CTreeReader rd{json};
    CNode parsed;
    if (!rd.readNode(parsed, 0)) return false;
    rd.skipWs();
    if (rd.i != json.size()) return false;
    out = parsed;
    return true;
}

CNode currentTree() {
    CNode t;
    const std::string json = g.val.count("collageTree") ? g.val["collageTree"] : std::string(kCollageDefaultTree);
    if (!parseCollageTree(json, t)) parseCollageTree(kCollageDefaultTree, t);
    return t;
}

void collectLeaves(CNode& n, std::vector<CNode*>& out) {
    if (n.leaf) { out.push_back(&n); return; }
    for (auto& k : n.kids) collectLeaves(k, out);
}

// The shape only (no pictures, no ids): two trees with the same signature lay out the same.
std::string collageSignature(const CNode& n) {
    if (n.leaf) return "L";
    char ratio[16];
    std::snprintf(ratio, sizeof ratio, "%d", static_cast<int>(std::lround(n.r * 100)));
    return std::string(1, n.d) + ratio + "(" + collageSignature(n.kids[0]) + "," + collageSignature(n.kids[1]) + ")";
}

void collageRects(const CNode& n, double x, double y, double w, double h, std::vector<CRect>& out) {
    if (n.leaf) { out.push_back({x, y, w, h, n.id, n.img}); return; }
    const bool vertical = n.d == 'v';
    const double a = vertical ? w * n.r : h * n.r;
    collageRects(n.kids[0], x, y, vertical ? a : w, vertical ? h : a, out);
    collageRects(n.kids[1], vertical ? x + a : x, vertical ? y : y + a, vertical ? w - a : w, vertical ? h : h - a, out);
}

CNode presetNode(const char*& p) {
    CNode n;
    if (*p == 'L') { p++; return n; }
    n.leaf = false;
    n.d = *p++;
    char* end = nullptr;
    n.r = std::strtol(p, &end, 10) / 1000.0;
    p = end + 1;
    n.kids.push_back(presetNode(p));
    p++;
    n.kids.push_back(presetNode(p));
    p++;
    return n;
}
const std::vector<CNode>& collagePresetTrees() {
    static const std::vector<CNode> trees = [] {
        std::vector<CNode> out;
        for (const auto& text : kCollagePresets) {
            const char* p = text.c_str();
            out.push_back(presetNode(p));
        }
        return out;
    }();
    return trees;
}

std::string valueOf(const char* key) { return g.val.count(key) ? g.val[key] : std::string(); }

CNode* primaryLeaf(CNode& t) {
    std::vector<CNode*> leaves;
    collectLeaves(t, leaves);
    const int want = std::atoi(valueOf("collagePrimary").c_str());
    for (CNode* l : leaves) if (l->id == want) return l;
    return leaves.front();
}

void numberLeaves(CNode& n, const std::vector<std::string>& images, int& next) {
    if (n.leaf) {
        n.id = next;
        n.img = images[static_cast<size_t>(next - 1) % images.size()];
        next++;
        return;
    }
    for (auto& k : n.kids) numberLeaves(k, images, next);
}

void applyCollagePreset(size_t index) {
    if (index >= collagePresetTrees().size()) return;
    CNode cur = currentTree();
    std::vector<CNode*> leaves;
    collectLeaves(cur, leaves);
    CNode* primary = primaryLeaf(cur);
    std::vector<std::string> images{primary->img};
    for (CNode* l : leaves) if (l != primary) images.push_back(l->img);
    CNode next = collagePresetTrees()[index];
    int nextId = 1;
    numberLeaves(next, images, nextId);
    sendSet("collageTree", treeJson(next));
    sendSet("collagePrimary", "1");
}

void resetCollage() {
    sendSet("collageTree", kCollageDefaultTree);
    sendSet("collagePrimary", "1");
}

void setCollageImage(int id, const std::string& path) {
    CNode t = currentTree();
    std::vector<CNode*> leaves;
    collectLeaves(t, leaves);
    for (CNode* l : leaves) if (l->id == id) l->img = path;
    sendSet("collageTree", treeJson(t));
}

pid_t g_pickPid = 0;
int g_pickLeaf = 0;

std::string pickFile() { return runtimeDir() + "/end4pC-collage-pick.txt"; }

bool collagePickBusy() { return g_pickPid > 0; }

void startPick(int leafId) {
    if (g_pickPid > 0) return;
    g_pickLeaf = leafId;
    std::remove(pickFile().c_str());
    const std::string cmd = "p=$(zenity --file-selection --title='Choose a picture' --file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif *.avif') && [ -n \"$p\" ] && printf '%s' \"$p\" > " + shq(pickFile());
    g_pickPid = spawn({"bash", "-c", cmd});
}

bool collagePickPoll() {
    if (g_pickPid <= 0) return false;
    int status = 0;
    if (waitpid(g_pickPid, &status, WNOHANG) == 0) return false;
    g_pickPid = 0;  // (-1 too: another reaper took it; the answer is in the file all the same)
    const std::string path = readFile(pickFile());
    std::remove(pickFile().c_str());
    if (!path.empty()) setCollageImage(g_pickLeaf, path);
    return true;
}

std::string collageWallpaper() {
    std::string w = readFile(stateDir() + "/wallpaper.txt");
    while (!w.empty() && (w.back() == '\n' || w.back() == '\r')) w.pop_back();
    return w;
}

cairo_surface_t* pixbufSurface(GdkPixbuf* pb) {
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < ph; y++) {
        const guchar* sp = px + y * rs;
        uint32_t* dp = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < pw; x++, sp += nch) {
            const uint32_t a = nch == 4 ? sp[3] : 255;
            dp[x] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(img);
    return img;
}

cairo_surface_t* coverSurface(const std::string& path, int w, int h) {
    int iw = 0, ih = 0;
    if (w <= 0 || h <= 0 || !gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(w) / iw, double(h) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(w, int(iw * scale + 0.5)), std::max(h, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* pic = pixbufSurface(pb);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, pic, (w - cairo_image_surface_get_width(pic)) / 2.0, (h - cairo_image_surface_get_height(pic)) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(pic);
    g_object_unref(pb);
    return out;
}

cairo_surface_t* collageImage(const std::string& path, int w, int h) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = path + "|" + std::to_string(w) + "x" + std::to_string(h);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    return cache[key] = coverSurface(path, w, h);
}

int collageColumns(double w) {
    const int n = static_cast<int>(collagePresetTrees().size());
    int best = 1;
    double bestEmpty = 2, bestTile = 0;
    for (int c = 1; c <= n; c++) {
        const double tile = (w - (c - 1) * kCgGap) / c;
        if (tile < kCgMinTile) break;
        if (tile > kCgMaxTile) continue;
        const int rows = (n + c - 1) / c;
        const double empty = double(rows * c - n) / (rows * c);
        if (empty < bestEmpty - 1e-9 || (std::fabs(empty - bestEmpty) < 1e-9 && tile > bestTile)) {
            best = c;
            bestEmpty = empty;
            bestTile = tile;
        }
    }
    return best;
}

constexpr const char* kCollageHint = "Click a tile to pick its picture. Star: blurred backdrop. ×: back to the wallpaper.";

CollageDims collageDims(double w, const std::string& desc) {
    CollageDims d;
    double tw = 0;
    measure(desc, 12.5, false, w, tw, d.textH);
    const int n = static_cast<int>(collagePresetTrees().size());
    d.cols = collageColumns(w);
    d.tw = (w - (d.cols - 1) * kCgGap) / d.cols;
    d.th = d.tw / kCollageAspect;
    d.gridH = ((n + d.cols - 1) / d.cols) * (d.th + kCgGap) - kCgGap;
    d.labelY = d.gridH + 26;
    d.pvY = d.labelY + 24;
    d.pvW = std::min(w, kCgPreviewMax);
    d.pvH = d.pvW / kCollageAspect;
    d.hintY = d.pvY + d.pvH + 10;
    measure(kCollageHint, 12.5, false, w, tw, d.hintH);
    d.h = 22 + d.textH + 16 + d.hintY + d.hintH + 22;
    return d;
}

void drawCollageGrid(cairo_t* cr, const CollageDims& d, double gy, double oy, bool live, int rowIdx) {
    const std::string sig = collageSignature(currentTree());
    const auto& presets = collagePresetTrees();
    const Color primary = g.color("primary"), chip = g.color("surface_container_highest"), onSurface = g.color("on_surface");
    for (size_t k = 0; k < presets.size(); k++) {
        const double x = kPadX + (k % d.cols) * (d.tw + kCgGap), y = gy + (k / d.cols) * (d.th + kCgGap);
        const bool sel = collageSignature(presets[k]) == sig;
        bool hot = false;
        if (live) {
            g.hits.push_back({{x, y, d.tw, d.th}, rowIdx, static_cast<int>(k), CollageLayout});
            hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
        }
        const double cy = y + oy;
        rrect(cr, x, cy, d.tw, d.th, 10);
        src(cr, sel ? mix(g.color("secondary_container"), chip, 0.55) : hot ? mix(primary, chip, 0.12) : chip);
        cairo_fill(cr);
        if (sel) { rrect(cr, x + 1, cy + 1, d.tw - 2, d.th - 2, 19); src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
        std::vector<CRect> tiles;
        collageRects(presets[k], x + kCgInset, cy + kCgInset, d.tw - 2 * kCgInset, d.th - 2 * kCgInset, tiles);
        for (const CRect& t : tiles) {
            rrect(cr, t.x + 1.5, t.y + 1.5, t.w - 3, t.h - 3, kCgRadius);
            src(cr, sel ? primary : onSurface, sel ? 1 : 0.4);
            cairo_fill(cr);
        }
    }
}

void drawCollageTile(cairo_t* cr, const CRect& t, double oy, bool live, int rowIdx, int primaryId) {
    const Color primary = g.color("primary"), onPrimary = g.color("on_primary"), onSurface = g.color("on_surface");
    const Color chip = g.color("surface_container_highest");
    const double ux = t.x + 4, uy = t.y + 4, w = t.w - 8, h = t.h - 8, dy = uy + oy;
    const int iw = static_cast<int>(w), ih = static_cast<int>(h);
    if (live) g.hits.push_back({{ux, uy, w, h}, rowIdx, kCgOptBase + t.id * kCgOptSub, CollageLayout});
    cairo_save(cr);
    rrect(cr, ux, dy, w, h, 14);
    cairo_clip(cr);
    const std::string path = t.img.empty() ? collageWallpaper() : t.img;
    if (cairo_surface_t* im = collageImage(path, iw, ih)) { cairo_set_source_surface(cr, im, ux, dy); cairo_paint(cr); }
    else { src(cr, chip); cairo_paint(cr); }
    cairo_restore(cr);
    const bool isPrimary = t.id == primaryId;
    const double badgeX = ux + 8, badgeY = dy + 8;
    if (live) g.hits.push_back({{badgeX, uy + 8, kCgBadge, kCgBadge}, rowIdx, kCgOptBase + t.id * kCgOptSub + 1, CollageLayout});
    cairo_new_sub_path(cr);
    cairo_arc(cr, badgeX + kCgBadge / 2, badgeY + kCgBadge / 2, kCgBadge / 2, 0, 2 * M_PI);
    src(cr, isPrimary ? primary : chip, isPrimary ? 1 : 0.9);
    cairo_fill(cr);
    icon(cr, "star", badgeX + 5, badgeY + 5, 16, isPrimary ? onPrimary : onSurface, isPrimary);
    if (t.img.empty()) return;
    const double xX = ux + w - 8 - kCgBadge, xY = dy + 8;
    if (live) g.hits.push_back({{xX, uy + 8, kCgBadge, kCgBadge}, rowIdx, kCgOptBase + t.id * kCgOptSub + 2, CollageLayout});
    cairo_new_sub_path(cr);
    cairo_arc(cr, xX + kCgBadge / 2, xY + kCgBadge / 2, kCgBadge / 2, 0, 2 * M_PI);
    src(cr, chip, 0.9);
    cairo_fill(cr);
    icon(cr, "close-circle", xX + 5, xY + 5, 16, onSurface, false);
}

void drawCollagePreview(cairo_t* cr, const CollageDims& d, double x0, double y0, double oy, bool live, int rowIdx) {
    CNode cur = currentTree();
    std::vector<CRect> tiles;
    collageRects(cur, x0, y0, d.pvW, d.pvH, tiles);
    const int primaryId = primaryLeaf(cur)->id;
    for (const CRect& t : tiles) drawCollageTile(cr, t, oy, live, rowIdx, primaryId);
}

void drawCollageRow(cairo_t* cr, int rowIdx, const Row& r, const RowLayout& rl, double oy, bool live, double w) {
    const CollageDims d = collageDims(w, descOf(r));
    const Color variant = g.color("on_surface_variant"), onSurface = g.color("on_surface");
    text(cr, descOf(r), kPadX, rl.y + oy + 22, 12.5, false, variant, w);
    const double gy = rl.y + 22 + d.textH + 16;
    drawCollageGrid(cr, d, gy, oy, live, rowIdx);
    text(cr, "Current layout", kPadX, gy + oy + d.labelY, 14, true, onSurface);
    drawCollagePreview(cr, d, kPadX, gy + d.pvY, oy, live, rowIdx);
    text(cr, kCollageHint, kPadX, gy + oy + d.hintY, 12.5, false, variant, w);
}

void clickCollage(int opt) {
    if (opt < kCgOptBase) { applyCollagePreset(static_cast<size_t>(opt)); return; }
    const int id = (opt - kCgOptBase) / kCgOptSub, what = (opt - kCgOptBase) % kCgOptSub;
    if (what == 0) startPick(id);
    else if (what == 1) sendSet("collagePrimary", std::to_string(id));
    else setCollageImage(id, "");
}
