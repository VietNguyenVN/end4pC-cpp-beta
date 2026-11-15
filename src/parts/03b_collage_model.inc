

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    double number = 0;
    std::string text;
    std::vector<std::string> keys;
    std::vector<JsonValue> items;
    const JsonValue* find(const std::string& key) const {
        for (size_t i = 0; i < keys.size(); i++)
            if (keys[i] == key) return &items[i];
        return nullptr;
    }
};

constexpr int kJsonMaxDepth = 32;

void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class JsonReader {
public:
    JsonReader(const std::string& text, size_t pos) : s_(text), p_(pos) {}

    bool document(JsonValue& out) {
        return value(out, 0) && skipSpace() && p_ == s_.size();
    }
    bool stringAt(std::string& out) { return str(out); }

private:
    const std::string& s_;
    size_t p_;

    bool skipSpace() {
        while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) p_++;
        return true;
    }
    bool consume(char c) {
        skipSpace();
        if (p_ >= s_.size() || s_[p_] != c) return false;
        p_++;
        return true;
    }
    bool literal(const char* word) {
        const size_t n = std::strlen(word);
        if (s_.compare(p_, n, word) != 0) return false;
        p_ += n;
        return true;
    }
    bool value(JsonValue& out, int depth) {
        skipSpace();
        if (depth > kJsonMaxDepth || p_ >= s_.size()) return false;
        const char c = s_[p_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.kind = JsonValue::Kind::String;
            return str(out.text);
        }
        if (literal("true") || literal("false")) {
            out.kind = JsonValue::Kind::Bool;
            return true;
        }
        if (literal("null")) return true;
        return number(out);
    }
    bool object(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::Object;
        p_++;
        if (consume('}')) return true;
        do {
            std::string key;
            JsonValue member;
            if (!str(key) || !consume(':') || !value(member, depth + 1)) return false;
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(member));
        } while (consume(','));
        return consume('}');
    }
    bool array(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::Array;
        p_++;
        if (consume(']')) return true;
        do {
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            out.items.push_back(std::move(item));
        } while (consume(','));
        return consume(']');
    }
    bool number(JsonValue& out) {
        const std::string_view digits = "+-.0123456789eE";
        const size_t start = p_;
        while (p_ < s_.size() && digits.find(s_[p_]) != std::string_view::npos) p_++;
        if (p_ == start) return false;
        const char* first = s_.data() + start;
        const auto [end, ec] = std::from_chars(first, s_.data() + p_, out.number);
        if (ec != std::errc() || end != s_.data() + p_) return false;
        out.kind = JsonValue::Kind::Number;
        return true;
    }
    bool str(std::string& out) {
        skipSpace();
        if (p_ >= s_.size() || s_[p_] != '"') return false;
        out.clear();
        for (p_++; p_ < s_.size(); p_++) {
            const unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c == '"') {
                p_++;
                return true;
            }
            if (c < 0x20) return false;
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (++p_ >= s_.size() || !escape(out)) return false;
        }
        return false;
    }
    bool escape(std::string& out) {
        switch (s_[p_]) {
            case '"': out += '"'; return true;
            case '\\': out += '\\'; return true;
            case '/': out += '/'; return true;
            case 'b': out += '\b'; return true;
            case 'f': out += '\f'; return true;
            case 'n': out += '\n'; return true;
            case 'r': out += '\r'; return true;
            case 't': out += '\t'; return true;
            case 'u': return unicode(out);
            default: return false;
        }
    }
    bool unicode(std::string& out) {  // \uXXXX of the basic plane, as UTF-8 (surrogate pairs are not needed here)
        if (p_ + 4 >= s_.size()) return false;
        unsigned code = 0;
        for (size_t i = 1; i <= 4; i++) {
            const unsigned char h = static_cast<unsigned char>(s_[p_ + i]);
            if (!std::isxdigit(h)) return false;
            code = code * 16 + (std::isdigit(h) ? h - '0' : std::tolower(h) - 'a' + 10);
        }
        if (code >= 0xD800 && code <= 0xDFFF) return false;
        p_ += 4;
        appendUtf8(out, code);
        return true;
    }
};

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (const char ch : in) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

bool readJsonStringKey(const std::string& text, const char* key, std::string& out) {
    const std::string quoted = std::string("\"") + key + "\"";
    const size_t k = text.find(quoted);
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k + quoted.size());
    if (colon == std::string::npos) return false;
    return JsonReader(text, colon + 1).stringAt(out);
}

constexpr int kCollageMaxTiles = 6;
constexpr int kCollageMaxDepth = 8;
constexpr double kCollageMinRatio = 0.15, kCollageMaxRatio = 0.85;

struct CollageNode {
    bool leaf = true;
    int id = 0;
    std::string img;
    bool vertical = true;
    double ratio = 0.5;
    std::unique_ptr<CollageNode> a, b;
};

struct CollageBuild {
    int leaves = 0;
    std::set<int> ids;
    std::string error;
};

bool collageFail(CollageBuild& st, const char* message) {
    st.error = message;
    return false;
}

bool checkCollageFields(const JsonValue& obj, std::initializer_list<const char*> allowed, std::initializer_list<const char*> required, CollageBuild& st) {
    for (const std::string& key : obj.keys) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* name) { return key == name; });
        if (!known) {
            st.error = "unknown field \"" + key + "\"";
            return false;
        }
    }
    for (const char* key : required) {
        if (obj.find(key)) continue;
        st.error = std::string("missing field \"") + key + "\"";
        return false;
    }
    return true;
}

bool buildCollageLeaf(const JsonValue& v, CollageBuild& st, CollageNode& node) {
    if (!checkCollageFields(v, {"t", "id", "img"}, {"t", "id"}, st)) return false;
    const JsonValue& id = *v.find("id");
    if (id.kind != JsonValue::Kind::Number || id.number != std::floor(id.number) || id.number < 1 || id.number > 999999)
        return collageFail(st, "a tile id is a whole number from 1");
    if (!st.ids.insert(static_cast<int>(id.number)).second) return collageFail(st, "two tiles have the same id");
    if (++st.leaves > kCollageMaxTiles) return collageFail(st, "a collage has at most 6 tiles");
    const JsonValue* img = v.find("img");
    if (img && img->kind != JsonValue::Kind::String) return collageFail(st, "\"img\" is the path of a picture, as text (or empty)");
    node.leaf = true;
    node.id = static_cast<int>(id.number);
    node.img = img ? img->text : "";
    return true;
}

bool buildCollageNode(const JsonValue& v, int depth, CollageBuild& st, CollageNode& node);

bool buildCollageSplit(const JsonValue& v, int depth, CollageBuild& st, CollageNode& node) {
    if (!checkCollageFields(v, {"t", "d", "r", "a", "b"}, {"t", "d", "r", "a", "b"}, st)) return false;
    const JsonValue& d = *v.find("d");
    const JsonValue& r = *v.find("r");
    if (d.kind != JsonValue::Kind::String || (d.text != "v" && d.text != "h"))
        return collageFail(st, "\"d\" is \"v\" (a left, b right) or \"h\" (a on top, b below)");
    if (r.kind != JsonValue::Kind::Number || r.number < kCollageMinRatio || r.number > kCollageMaxRatio)
        return collageFail(st, "\"r\" is a ratio from 0.15 to 0.85");
    node.leaf = false;
    node.vertical = d.text == "v";
    node.ratio = r.number;
    node.a = std::make_unique<CollageNode>();
    node.b = std::make_unique<CollageNode>();
    return buildCollageNode(*v.find("a"), depth + 1, st, *node.a) && buildCollageNode(*v.find("b"), depth + 1, st, *node.b);
}

bool buildCollageNode(const JsonValue& v, int depth, CollageBuild& st, CollageNode& node) {
    if (depth > kCollageMaxDepth) return collageFail(st, "the tree is nested too deep");
    if (v.kind != JsonValue::Kind::Object) return collageFail(st, "every tile is an object");
    const JsonValue* t = v.find("t");
    if (!t || t->kind != JsonValue::Kind::String) return collageFail(st, "every tile needs \"t\": \"leaf\" or \"split\"");
    if (t->text == "leaf") return buildCollageLeaf(v, st, node);
    if (t->text == "split") return buildCollageSplit(v, depth, st, node);
    return collageFail(st, "\"t\" is \"leaf\" or \"split\"");
}

std::unique_ptr<CollageNode> parseCollageTree(const std::string& json, std::string& error) {
    JsonValue doc;
    if (!JsonReader(json, 0).document(doc)) {
        error = "not valid JSON";
        return nullptr;
    }
    CollageBuild st;
    auto root = std::make_unique<CollageNode>();
    if (!buildCollageNode(doc, 0, st, *root)) {
        error = st.error;
        return nullptr;
    }
    return root;
}

std::unique_ptr<CollageNode> collageTreeToDraw(const std::string& json) {
    std::string error;
    if (auto root = parseCollageTree(json, error)) return root;
    auto fallback = std::make_unique<CollageNode>();
    fallback->id = 1;
    return fallback;
}

void collectCollageLeaves(const CollageNode& n, std::vector<const CollageNode*>& out) {
    if (n.leaf) {
        out.push_back(&n);
        return;
    }
    collectCollageLeaves(*n.a, out);
    collectCollageLeaves(*n.b, out);
}

std::vector<std::string> collageImagePaths(const CollageSpec& spec) {
    const auto root = collageTreeToDraw(spec.tree);
    std::vector<const CollageNode*> leaves;
    collectCollageLeaves(*root, leaves);
    std::vector<std::string> paths;
    for (const CollageNode* leaf : leaves)
        if (!leaf->img.empty()) paths.push_back(leaf->img);
    return paths;
}

std::string collagePrimaryImage(const CollageSpec& spec) {
    const auto root = collageTreeToDraw(spec.tree);
    std::vector<const CollageNode*> leaves;
    collectCollageLeaves(*root, leaves);
    for (const CollageNode* leaf : leaves)
        if (leaf->id == spec.primary) return leaf->img;
    return leaves.front()->img;
}

struct CollageRect {
    double x = 0, y = 0, w = 0, h = 0;
};
struct CollageTile {
    CollageRect rect;
    std::string img;
};
struct CollageArea {
    double x0 = 0, y0 = 0, aw = 1, ah = 1, gap = 30;
};

constexpr double kCollageEdgeEps = 0.0005;

void walkCollage(const CollageNode& n, const CollageRect& f, const CollageArea& area, std::vector<CollageTile>& out) {
    if (!n.leaf) {
        if (n.vertical) {
            walkCollage(*n.a, {f.x, f.y, f.w * n.ratio, f.h}, area, out);
            walkCollage(*n.b, {f.x + f.w * n.ratio, f.y, f.w * (1 - n.ratio), f.h}, area, out);
        } else {
            walkCollage(*n.a, {f.x, f.y, f.w, f.h * n.ratio}, area, out);
            walkCollage(*n.b, {f.x, f.y + f.h * n.ratio, f.w, f.h * (1 - n.ratio)}, area, out);
        }
        return;
    }
    const double half = area.gap / 2;
    const double left = f.x < kCollageEdgeEps ? 0 : half;
    const double right = f.x + f.w > 1 - kCollageEdgeEps ? 0 : half;
    const double top = f.y < kCollageEdgeEps ? 0 : half;
    const double bottom = f.y + f.h > 1 - kCollageEdgeEps ? 0 : half;
    CollageRect rect{area.x0 + f.x * area.aw + left, area.y0 + f.y * area.ah + top,
                     std::max(1.0, f.w * area.aw - left - right), std::max(1.0, f.h * area.ah - top - bottom)};
    out.push_back({rect, n.img});
}

std::vector<CollageTile> collageTiles(const CollageSpec& spec, int w, int h) {
    const auto root = collageTreeToDraw(spec.tree);
    const CollageInsets& in = spec.insets;
    const double margin = spec.margin;
    CollageArea area;
    area.x0 = margin + in.left;
    area.y0 = margin + in.top;
    area.aw = std::max(1.0, w - area.x0 - margin - in.right);
    area.ah = std::max(1.0, h - area.y0 - margin - in.bottom);
    area.gap = spec.gap;
    std::vector<CollageTile> out;
    walkCollage(*root, {0, 0, 1, 1}, area, out);
    return out;
}

CollageSpec collageNow(const App& app) {
    CollageSpec spec;
    spec.on = app.cfg.collage != 0;
    spec.gap = app.cfg.collageGap;
    spec.margin = app.cfg.collageMargin;
    spec.radius = app.cfg.collageRadius;
    spec.blur = app.cfg.collageBlur;
    spec.primary = app.cfg.collagePrimary;
    spec.tree = app.cfg.collageTree;
    return spec;
}

CollageSpec withBarInsets(CollageSpec spec, const Bar& bar, const App& app) {
    const double frame = app.cfg.frame;
    spec.insets = {frame, frame, frame, frame};
    if (bar.layerSurface) {
        const double span = barThickness(app) + ((app.cfg.barStyle == "float" || app.cfg.barStyle == "island") ? kBarGap : 0);
        if (sideBar(app)) (barAtEnd(app) ? spec.insets.right : spec.insets.left) += span;
        else (barAtEnd(app) ? spec.insets.bottom : spec.insets.top) += span;
    }
    return spec;
}

std::atomic<bool> g_collageDecoded{false};

bool isCollageKey(const std::string& key) {
    return key == "collage" || key == "collageGap" || key == "collageMargin" || key == "collageRadius" || key == "collageBlur" || key == "collagePrimary" || key == "collageTree";
}

std::string setCollageKey(Config& next, const std::string& key, const std::string& value) {
    if (key == "collageTree") {
        std::string error;
        if (!parseCollageTree(value, error)) return "collageTree: " + error;
        next.collageTree = value;
        return "";
    }
    struct CollageIntKey { const char* name; int* field; int lo, hi; };
    const CollageIntKey table[] = {{"collage", &next.collage, 0, 1}, {"collageGap", &next.collageGap, 0, 80},
                                   {"collageMargin", &next.collageMargin, 0, 120}, {"collageRadius", &next.collageRadius, 0, 80}, {"collageBlur", &next.collageBlur, 0, 100},
                                   {"collagePrimary", &next.collagePrimary, 1, 999999}};
    for (const CollageIntKey& k : table) {
        if (key != k.name) continue;
        int v = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
        if (ec != std::errc() || end != value.data() + value.size()) return "value must be an integer";
        if (v < k.lo || v > k.hi) return "value must be from " + std::to_string(k.lo) + " to " + std::to_string(k.hi);
        *k.field = v;
        return "";
    }
    return "unknown key";
}
