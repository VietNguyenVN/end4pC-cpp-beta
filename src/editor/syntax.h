// Syntax highlighting for T3xt: a small scanner per family of languages (C-like, Python, shell, Lua, JSON, markup, CSS, Markdown, YAML, INI, SQL,
// Make, diff), one line at a time with a tiny state carried from line to line (block comments, triple quotes, fences). It only says which kind
// of token every byte is; the colours come from the window. No dependencies, so it is tested on its own (tests/editor/test_editor.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace t3 {

enum Tok : uint8_t { TPlain, TKeyword, TType, TFunction, TString, TNumber, TComment, TPreproc, TAttr, TTag, THeading, TEmphasis, TAdded, TRemoved, TCount };
enum class Style { Plain, C, Python, Shell, Lua, Json, Markup, Css, Markdown, Yaml, Ini, Sql, Make, Diff };

struct Lang {
    std::string name;
    Style style = Style::Plain;
    std::unordered_set<std::string> keywords, types;
    std::string lineComment, blockOpen, blockClose;  // "//", "/*", "*/"
    bool caseInsensitive = false, backtickStrings = false, preproc = false, firstWordIsFunction = false, macroBang = false;
};

// The state at the start of a line: what is still open from the lines before.
enum Mode : uint8_t { MNormal, MBlockComment, MTripleDouble, MTripleSingle, MTemplate, MLuaComment, MLuaString, MHtmlComment, MFence };
struct LineState {
    uint8_t mode = MNormal;
    uint8_t level = 0;  // Lua long brackets: the number of '='
    bool operator==(const LineState& o) const { return mode == o.mode && level == o.level; }
    bool operator!=(const LineState& o) const { return !(*this == o); }
};

inline std::unordered_set<std::string> words(const char* list) {
    std::unordered_set<std::string> out;
    std::istringstream in(list);
    std::string w;
    while (in >> w) out.insert(w);
    return out;
}

inline const std::map<std::string, Lang>& languages() {
    static const std::map<std::string, Lang> all = [] {
        std::map<std::string, Lang> m;
        auto add = [&](const std::string& key, const char* name, Style style, const char* kw, const char* ty, const char* line, const char* bo, const char* bc) {
            Lang l;
            l.name = name; l.style = style; l.keywords = words(kw); l.types = words(ty); l.lineComment = line; l.blockOpen = bo; l.blockClose = bc;
            m[key] = std::move(l);
        };
        add("c", "C", Style::C, "if else for while do switch case default break continue return goto sizeof typedef struct union enum static extern const volatile inline register auto restrict true false NULL",
            "int char short long float double void unsigned signed bool size_t ssize_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t FILE", "//", "/*", "*/");
        m["c"].preproc = true;
        add("cpp", "C++", Style::C, "if else for while do switch case default break continue return goto sizeof typedef struct union enum class namespace using template typename static extern const constexpr consteval constinit volatile inline virtual override final explicit public private protected friend operator new delete try catch throw noexcept this nullptr true false auto mutable static_assert decltype co_await co_return co_yield concept requires export import module",
            "int char short long float double void unsigned signed bool wchar_t size_t std string vector map set unique_ptr shared_ptr optional variant array uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t", "//", "/*", "*/");
        m["cpp"].preproc = true;
        add("java", "Java", Style::C, "abstract assert break case catch class const continue default do else enum extends final finally for goto if implements import instanceof interface native new package private protected public return static strictfp super switch synchronized this throw throws transient try volatile while true false null var record sealed permits yield",
            "int long short byte char float double boolean void String Object Integer List Map Set", "//", "/*", "*/");
        add("js", "JavaScript", Style::C, "break case catch class const continue debugger default delete do else export extends finally for function if import in instanceof let new return super switch this throw try typeof var void while with yield async await of static get set true false null undefined from as",
            "Object Array String Number Boolean Promise Map Set Symbol Date RegExp Error JSON Math console document window", "//", "/*", "*/");
        m["js"].backtickStrings = true;
        add("ts", "TypeScript", Style::C, "break case catch class const continue debugger default delete do else enum export extends finally for function if implements import in instanceof interface let new package private protected public return static super switch this throw try typeof var void while with yield async await of readonly abstract declare namespace type keyof as is from get set true false null undefined satisfies",
            "string number boolean any unknown never object void symbol bigint Array Record Partial Promise Map Set Date Error", "//", "/*", "*/");
        m["ts"].backtickStrings = true;
        add("go", "Go", Style::C, "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil iota",
            "int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 uintptr float32 float64 complex64 complex128 string bool byte rune error any", "//", "/*", "*/");
        m["go"].backtickStrings = true;
        add("rust", "Rust", Style::C, "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while union macro_rules",
            "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result Box Rc Arc HashMap HashSet", "//", "/*", "*/");
        m["rust"].macroBang = true;
        add("cs", "C#", Style::C, "abstract as base break case catch checked class const continue default delegate do else enum event explicit extern false finally fixed for foreach goto if implicit in interface internal is lock namespace new null operator out override params private protected public readonly ref return sealed sizeof stackalloc static struct switch this throw true try typeof unchecked unsafe using virtual volatile while var async await record init get set yield",
            "int long short byte char float double decimal bool void string object uint ulong ushort sbyte List Dictionary Task", "//", "/*", "*/");
        add("kotlin", "Kotlin", Style::C, "as break class continue do else false for fun if in interface is null object package return super this throw true try typealias val var when while by catch constructor delegate dynamic field file finally get import init param property receiver set setparam where abstract annotation companion const crossinline data enum external final infix inline inner internal lateinit noinline open operator out override private protected public reified sealed suspend tailrec vararg",
            "Int Long Short Byte Char Float Double Boolean String Unit Any Nothing List Map Set Array", "//", "/*", "*/");
        add("swift", "Swift", Style::C, "associatedtype class deinit enum extension fileprivate func import init inout internal let open operator private protocol public rethrows static struct subscript typealias var break case continue default defer do else fallthrough for guard if in repeat return switch where while as Any catch false is nil super self Self throw throws true try async await actor",
            "Int Double Float Bool String Character Array Dictionary Set Optional", "//", "/*", "*/");
        add("dart", "Dart", Style::C, "abstract as assert async await break case catch class const continue covariant default deferred do dynamic else enum export extends extension external factory false final finally for Function get hide if implements import in interface is late library mixin new null on operator part required rethrow return set show static super switch sync this throw true try typedef var void while with yield",
            "int double num bool String List Map Set Future Stream Object", "//", "/*", "*/");
        add("qml", "QML", Style::C, "import as property signal function readonly required default alias on pragma component enum if else for while do switch case break continue return var let const true false null undefined new typeof in of",
            "Item Rectangle Text Image MouseArea Column Row Grid Repeater ListView Loader Timer Window ApplicationWindow QtObject int real string bool color url date var list", "//", "/*", "*/");
        m["qml"].backtickStrings = true;
        add("php", "PHP", Style::C, "abstract and as break case catch class clone const continue declare default do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile extends final finally fn for foreach function global goto if implements include include_once instanceof insteadof interface isset list match namespace new or print private protected public readonly require require_once return static switch throw trait try unset use var while xor yield true false null",
            "int float string bool array object callable iterable mixed void never self parent", "//", "/*", "*/");
        add("glsl", "GLSL", Style::C, "if else for while do switch case default break continue return discard struct const uniform in out inout varying attribute layout precision highp mediump lowp flat smooth true false",
            "void int uint float double bool vec2 vec3 vec4 ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 bvec2 bvec3 bvec4 mat2 mat3 mat4 sampler2D samplerCube sampler3D", "//", "/*", "*/");
        m["glsl"].preproc = true;
        add("kdl", "KDL", Style::C, "true false null", "", "//", "/*", "*/");
        m["kdl"].firstWordIsFunction = true;
        add("python", "Python", Style::Python, "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield match case self cls",
            "int float str bool list dict set tuple bytes object type Exception range len print open super", "#", "", "");
        add("shell", "Shell", Style::Shell, "if then else elif fi for while until do done case esac in function select time return exit break continue local export readonly declare typeset unset shift set source alias end begin switch and or not",
            "echo printf cd ls cp mv rm mkdir rmdir cat grep sed awk find sort uniq head tail cut tr xargs tee chmod chown ln touch test read eval exec trap wait kill sudo git make", "#", "", "");
        add("lua", "Lua", Style::Lua, "and break do else elseif end false for function goto if in local nil not or repeat return then true until while", "string table math io os coroutine require print pairs ipairs type tostring tonumber setmetatable getmetatable", "--", "", "");
        add("json", "JSON", Style::Json, "true false null", "", "//", "/*", "*/");
        add("html", "HTML", Style::Markup, "", "", "", "<!--", "-->");
        add("xml", "XML", Style::Markup, "", "", "", "<!--", "-->");
        add("css", "CSS", Style::Css, "important", "", "", "/*", "*/");
        m["css"].caseInsensitive = true;
        add("scss", "SCSS", Style::Css, "important", "", "//", "/*", "*/");
        add("markdown", "Markdown", Style::Markdown, "", "", "", "", "");
        add("yaml", "YAML", Style::Yaml, "true false null yes no on off True False", "", "#", "", "");
        add("ini", "INI", Style::Ini, "true false yes no on off null", "", "#", "", "");
        add("toml", "TOML", Style::Ini, "true false", "", "#", "", "");
        add("conf", "Config", Style::Ini, "true false yes no on off", "", "#", "", "");
        add("sql", "SQL", Style::Sql, "select from where and or not in is null as join left right inner outer full cross on group by order having limit offset insert into values update set delete create alter drop table index view database schema primary key foreign references default unique check constraint distinct union all case when then else end exists between like ilike asc desc begin commit rollback transaction with returning if",
            "int integer bigint smallint serial text varchar char boolean bool date time timestamp timestamptz numeric decimal float real double json jsonb uuid blob", "--", "/*", "*/");
        m["sql"].caseInsensitive = true;
        add("make", "Makefile", Style::Make, "ifeq ifneq ifdef ifndef else endif include sinclude -include define endef export unexport override vpath", ".PHONY .DEFAULT .SUFFIXES .PRECIOUS", "#", "", "");
        add("cmake", "CMake", Style::Make, "if else elseif endif foreach endforeach while endwhile function endfunction macro endmacro return break continue", "", "#", "", "");
        m["cmake"].caseInsensitive = true;
        add("dockerfile", "Dockerfile", Style::Make, "FROM RUN CMD LABEL EXPOSE ENV ADD COPY ENTRYPOINT VOLUME USER WORKDIR ARG ONBUILD STOPSIGNAL HEALTHCHECK SHELL AS", "", "#", "", "");
        add("diff", "Diff", Style::Diff, "", "", "", "", "");
        add("plain", "Plain text", Style::Plain, "", "", "", "", "");
        return m;
    }();
    return all;
}

inline std::string lowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The language of a file: by its name, then its extension, then the first line (#!).
inline const Lang& detectLanguage(const std::string& path, const std::string& firstLine) {
    const auto& all = languages();
    const size_t slash = path.rfind('/');
    const std::string base = lowerStr(slash == std::string::npos ? path : path.substr(slash + 1));
    static const std::map<std::string, std::string> byName = {{"makefile", "make"}, {"gnumakefile", "make"}, {"cmakelists.txt", "cmake"}, {"dockerfile", "dockerfile"}, {"pkgbuild", "shell"}, {".bashrc", "shell"}, {".zshrc", "shell"},
                                                              {".profile", "shell"}, {".bash_profile", "shell"}, {"config.fish", "shell"}, {".gitconfig", "ini"}, {".editorconfig", "ini"}, {"meson.build", "python"}, {".gitignore", "conf"}};
    if (auto it = byName.find(base); it != byName.end()) return all.at(it->second);
    const size_t dot = base.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : base.substr(dot + 1);
    static const std::map<std::string, std::string> byExt = {
        {"c", "c"}, {"h", "c"}, {"cpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"}, {"inc", "cpp"}, {"ipp", "cpp"}, {"java", "java"}, {"js", "js"}, {"mjs", "js"}, {"cjs", "js"}, {"jsx", "js"},
        {"ts", "ts"}, {"tsx", "ts"}, {"go", "go"}, {"rs", "rust"}, {"cs", "cs"}, {"kt", "kotlin"}, {"kts", "kotlin"}, {"swift", "swift"}, {"dart", "dart"}, {"qml", "qml"}, {"php", "php"}, {"glsl", "glsl"}, {"vert", "glsl"},
        {"frag", "glsl"}, {"kdl", "kdl"}, {"py", "python"}, {"pyw", "python"}, {"sh", "shell"}, {"bash", "shell"}, {"zsh", "shell"}, {"fish", "shell"}, {"lua", "lua"}, {"json", "json"}, {"jsonc", "json"}, {"html", "html"},
        {"htm", "html"}, {"xml", "xml"}, {"svg", "xml"}, {"xsl", "xml"}, {"ui", "xml"}, {"css", "css"}, {"scss", "scss"}, {"less", "scss"}, {"md", "markdown"}, {"markdown", "markdown"}, {"yml", "yaml"}, {"yaml", "yaml"},
        {"ini", "ini"}, {"desktop", "ini"}, {"toml", "toml"}, {"conf", "conf"}, {"cfg", "conf"}, {"service", "ini"}, {"sql", "sql"}, {"mk", "make"}, {"cmake", "cmake"}, {"diff", "diff"}, {"patch", "diff"}, {"tsv", "plain"}, {"txt", "plain"}};
    if (auto it = byExt.find(ext); it != byExt.end()) return all.at(it->second);
    if (firstLine.rfind("#!", 0) == 0) {
        if (firstLine.find("python") != std::string::npos) return all.at("python");
        if (firstLine.find("node") != std::string::npos) return all.at("js");
        if (firstLine.find("lua") != std::string::npos) return all.at("lua");
        if (firstLine.find("sh") != std::string::npos || firstLine.find("fish") != std::string::npos) return all.at("shell");
    }
    return all.at("plain");
}

namespace detail {
inline bool identStart(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
inline bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80; }
inline bool startsWith(const std::string& s, size_t i, const std::string& p) { return !p.empty() && s.compare(i, p.size(), p) == 0; }
inline void paint(std::vector<uint8_t>& out, size_t from, size_t to, Tok t) { for (size_t i = from; i < to && i < out.size(); i++) out[i] = t; }
inline size_t skipSpaces(const std::string& s, size_t i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; return i; }
inline bool isWordKeyword(const Lang& l, const std::string& w) { return l.keywords.count(l.caseInsensitive ? lowerStr(w) : w) > 0; }
inline bool isWordType(const Lang& l, const std::string& w) { return l.types.count(l.caseInsensitive ? lowerStr(w) : w) > 0; }
// A number starting at i: digits, hex, floats, exponents, suffixes. Returns its end.
inline size_t numberEnd(const std::string& s, size_t i) {
    size_t j = i;
    while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '_' || s[j] == '\'')) {
        if ((s[j] == 'e' || s[j] == 'E') && j + 1 < s.size() && (s[j + 1] == '+' || s[j + 1] == '-') && !(s.size() > i + 1 && (s[i + 1] == 'x' || s[i + 1] == 'X'))) j++;
        if (s[j] == '.' && j + 1 < s.size() && s[j + 1] == '.') break;  // 1..2
        j++;
    }
    return j;
}
// A quoted string starting at i (the quote is s[i]); backslash escapes. Returns the end (after the closing quote), or npos when it does not close on this line.
inline size_t quoteEnd(const std::string& s, size_t i, bool escapes = true) {
    const char q = s[i];
    for (size_t j = i + 1; j < s.size(); j++) {
        if (escapes && s[j] == '\\') { j++; continue; }
        if (s[j] == q) return j + 1;
    }
    return std::string::npos;
}
}  // namespace detail

// ---- the scanners ----------------------------------------------------------------------------------------------------------------------

inline LineState highlightC(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    auto closeBlock = [&]() {
        const size_t e = s.find(l.blockClose, i);
        if (e == std::string::npos) { paint(out, i, s.size(), TComment); i = s.size(); return false; }
        paint(out, i, e + l.blockClose.size(), TComment);
        i = e + l.blockClose.size();
        return true;
    };
    if (st.mode == MBlockComment) { if (!closeBlock()) return st; st.mode = MNormal; }
    else if (st.mode == MTemplate) {
        size_t j = i;
        while (j < s.size() && s[j] != '`') { if (s[j] == '\\') j++; j++; }
        if (j >= s.size()) { paint(out, 0, s.size(), TString); return st; }
        paint(out, 0, j + 1, TString);
        i = j + 1;
        st.mode = MNormal;
    }
    bool first = true;
    if (l.preproc) {
        const size_t f = skipSpaces(s, 0);
        if (f < s.size() && s[f] == '#' && i == 0) {
            paint(out, f, s.size(), TPreproc);
            // <header> and "header" are strings; a comment after the directive is a comment
            for (size_t j = f; j < s.size(); j++) {
                if (s[j] == '<' && s.find('>', j) != std::string::npos && s.compare(f, 8, "#include") == 0) { const size_t e = s.find('>', j); paint(out, j, e + 1, TString); break; }
                if (s[j] == '"') { const size_t e = quoteEnd(s, j); if (e != std::string::npos) { paint(out, j, e, TString); j = e - 1; } }
                if (startsWith(s, j, "//")) { paint(out, j, s.size(), TComment); break; }
            }
            return st;
        }
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (startsWith(s, i, l.lineComment)) { paint(out, i, s.size(), TComment); break; }
        if (startsWith(s, i, l.blockOpen)) {
            const size_t e = s.find(l.blockClose, i + l.blockOpen.size());
            if (e == std::string::npos) { paint(out, i, s.size(), TComment); st.mode = MBlockComment; return st; }
            paint(out, i, e + l.blockClose.size(), TComment);
            i = e + l.blockClose.size();
            continue;
        }
        if (c == '"' || (c == '\'' )) {
            const size_t e = quoteEnd(s, i);
            if (e == std::string::npos) {
                if (c == '"') { paint(out, i, s.size(), TString); break; }  // an open string: coloured to the end of the line while typing
                i++;                                                          // a lone ' (a lifetime, an apostrophe): plain
                continue;
            }
            if (c == '\'' && e - i > 12) { i++; continue; }  // 'a long thing' is not a character
            paint(out, i, e, TString);
            i = e;
            continue;
        }
        if (c == '`' && l.backtickStrings) {
            size_t j = i + 1;
            while (j < s.size() && s[j] != '`') { if (s[j] == '\\') j++; j++; }
            if (j >= s.size()) { paint(out, i, s.size(), TString); st.mode = MTemplate; return st; }
            paint(out, i, j + 1, TString);
            i = j + 1;
            continue;
        }
        if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1]))))) {
            if (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1]))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        }
        if (identStart(c)) {
            size_t e = i;
            while (e < s.size() && identChar(static_cast<unsigned char>(s[e]))) e++;
            const std::string w = s.substr(i, e - i);
            const size_t next = skipSpaces(s, e);
            Tok t = TPlain;
            if (isWordKeyword(l, w)) t = TKeyword;
            else if (isWordType(l, w)) t = TType;
            else if (l.macroBang && e < s.size() && s[e] == '!' && (e + 1 >= s.size() || s[e + 1] != '=')) t = TFunction;
            else if (next < s.size() && s[next] == '(') t = TFunction;
            else if (first && l.firstWordIsFunction) t = TFunction;
            else if (std::isupper(static_cast<unsigned char>(w[0])) && w.size() > 1 && std::any_of(w.begin(), w.end(), [](char ch) { return std::islower(static_cast<unsigned char>(ch)); })) t = TType;
            paint(out, i, e, t);
            if (l.firstWordIsFunction) first = false;
            i = e;
            continue;
        }
        if (c != ' ' && c != '\t') first = false;
        i++;
    }
    return st;
}

inline LineState highlightPython(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    auto tripleEnd = [&](char q) {
        const std::string close(3, q);
        const size_t e = s.find(close, i);
        if (e == std::string::npos) { paint(out, i, s.size(), TString); i = s.size(); return false; }
        paint(out, i, e + 3, TString);
        i = e + 3;
        return true;
    };
    if (st.mode == MTripleDouble) { if (!tripleEnd('"')) return st; st.mode = MNormal; }
    else if (st.mode == MTripleSingle) { if (!tripleEnd('\'')) return st; st.mode = MNormal; }
    std::string prev;  // the word before the current one (def NAME, class NAME)
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '#') { paint(out, i, s.size(), TComment); break; }
        if (c == '"' || c == '\'') {
            size_t start = i;
            while (start > 0 && std::isalpha(static_cast<unsigned char>(s[start - 1])) && i - start < 3) start--;  // r"", f"", b"", rb""
            if (startsWith(s, i, std::string(3, static_cast<char>(c)))) {
                const size_t from = start;
                i += 3;
                paint(out, from, i, TString);
                const char q = static_cast<char>(c);
                if (!tripleEnd(q)) { st.mode = q == '"' ? MTripleDouble : MTripleSingle; return st; }
                continue;
            }
            const size_t e = quoteEnd(s, i);
            paint(out, start, e == std::string::npos ? s.size() : e, TString);
            i = e == std::string::npos ? s.size() : e;
            continue;
        }
        if (c == '@' && (i == 0 || skipSpaces(s, 0) == i)) {
            size_t e = i + 1;
            while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '.')) e++;
            paint(out, i, e, TPreproc);
            i = e;
            continue;
        }
        if (std::isdigit(c) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        if (identStart(c)) {
            size_t e = i;
            while (e < s.size() && identChar(static_cast<unsigned char>(s[e]))) e++;
            const std::string w = s.substr(i, e - i);
            const size_t next = skipSpaces(s, e);
            Tok t = TPlain;
            if (prev == "def") t = TFunction;
            else if (prev == "class") t = TType;
            else if (isWordKeyword(l, w)) t = TKeyword;
            else if (isWordType(l, w)) t = TType;
            else if (next < s.size() && s[next] == '(') t = std::isupper(static_cast<unsigned char>(w[0])) ? TType : TFunction;
            else if (std::isupper(static_cast<unsigned char>(w[0])) && w.size() > 1 && std::any_of(w.begin(), w.end(), [](char ch) { return std::islower(static_cast<unsigned char>(ch)); })) t = TType;
            paint(out, i, e, t);
            prev = w;
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightShell(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    bool wordStart = true, command = true;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '#' && wordStart) { paint(out, i, s.size(), TComment); break; }
        if (c == '"' || c == '\'') {
            const size_t e = quoteEnd(s, i, c == '"');
            const size_t end = e == std::string::npos ? s.size() : e;
            paint(out, i, end, TString);
            if (c == '"') for (size_t j = i; j < end; j++) if (s[j] == '$' && j + 1 < end && (identStart(static_cast<unsigned char>(s[j + 1])) || s[j + 1] == '{' || s[j + 1] == '(')) {
                size_t k = j + 1;
                if (s[k] == '{') { while (k < end && s[k] != '}') k++; k++; }
                else while (k < end && identChar(static_cast<unsigned char>(s[k]))) k++;
                paint(out, j, std::min(k, end), TAttr);
            }
            i = end;
            wordStart = false;
            continue;
        }
        if (c == '$') {
            size_t e = i + 1;
            if (e < s.size() && s[e] == '{') { while (e < s.size() && s[e] != '}') e++; e++; }
            else if (e < s.size() && s[e] == '(') { e++; }
            else if (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || std::strchr("?!$@*#-", s[e]))) e++;
            else while (e < s.size() && identChar(static_cast<unsigned char>(s[e]))) e++;
            paint(out, i, std::min(e, s.size()), TAttr);
            i = std::min(e, s.size());
            wordStart = false;
            continue;
        }
        if (identStart(c) || (c == '-' && i + 1 < s.size() && std::isalpha(static_cast<unsigned char>(s[i + 1])) && wordStart && !command)) {
            size_t e = i;
            if (c == '-') { e++; while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '-')) e++; paint(out, i, e, TAttr); i = e; wordStart = false; continue; }
            while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '-' || s[e] == '.')) e++;
            const std::string w = s.substr(i, e - i);
            Tok t = TPlain;
            if (isWordKeyword(l, w)) t = TKeyword;
            else if (isWordType(l, w) || (command && w.find('/') == std::string::npos)) t = command ? TFunction : TType;
            if (e < s.size() && s[e] == '=' && command) t = TAttr;  // VAR=value
            paint(out, i, e, t);
            command = false;
            wordStart = false;
            i = e;
            continue;
        }
        if (std::isdigit(c) && wordStart) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; wordStart = false; continue; }
        if (c == ';' || c == '|' || c == '&' || c == '(' || c == '{') command = true;
        wordStart = c == ' ' || c == '\t' || c == ';' || c == '|' || c == '&' || c == '(';
        i++;
    }
    return st;
}

inline LineState highlightLua(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    auto closeLong = [&](Tok t) {
        const std::string close = "]" + std::string(st.level, '=') + "]";
        const size_t e = s.find(close, i);
        if (e == std::string::npos) { paint(out, i, s.size(), t); i = s.size(); return false; }
        paint(out, i, e + close.size(), t);
        i = e + close.size();
        return true;
    };
    if (st.mode == MLuaComment) { if (!closeLong(TComment)) return st; st.mode = MNormal; }
    else if (st.mode == MLuaString) { if (!closeLong(TString)) return st; st.mode = MNormal; }
    auto longOpen = [&](size_t at, uint8_t& level) {  // [[ or [==[ at `at`
        size_t j = at + 1;
        level = 0;
        while (j < s.size() && s[j] == '=') { level++; j++; }
        return j < s.size() && s[j] == '[' ? j + 1 : std::string::npos;
    };
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (startsWith(s, i, "--")) {
            uint8_t level;
            if (i + 2 < s.size() && s[i + 2] == '[') {
                const size_t after = longOpen(i + 2, level);
                if (after != std::string::npos) {
                    st.mode = MLuaComment; st.level = level;
                    paint(out, i, after, TComment);
                    i = after;
                    if (!closeLong(TComment)) return st;
                    st.mode = MNormal;
                    continue;
                }
            }
            paint(out, i, s.size(), TComment);
            break;
        }
        if (c == '[') {
            uint8_t level;
            const size_t after = longOpen(i, level);
            if (after != std::string::npos) {
                st.mode = MLuaString; st.level = level;
                paint(out, i, after, TString);
                i = after;
                if (!closeLong(TString)) return st;
                st.mode = MNormal;
                continue;
            }
        }
        if (c == '"' || c == '\'') {
            const size_t e = quoteEnd(s, i);
            paint(out, i, e == std::string::npos ? s.size() : e, TString);
            i = e == std::string::npos ? s.size() : e;
            continue;
        }
        if (std::isdigit(c) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        if (identStart(c)) {
            size_t e = i;
            while (e < s.size() && identChar(static_cast<unsigned char>(s[e]))) e++;
            const std::string w = s.substr(i, e - i);
            const size_t next = skipSpaces(s, e);
            Tok t = isWordKeyword(l, w) ? TKeyword : isWordType(l, w) ? TType : (next < s.size() && (s[next] == '(' || s[next] == '"' || s[next] == '{')) ? TFunction : TPlain;
            paint(out, i, e, t);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightJson(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    if (st.mode == MBlockComment) {
        const size_t e = s.find("*/");
        if (e == std::string::npos) { paint(out, 0, s.size(), TComment); return st; }
        paint(out, 0, e + 2, TComment);
        i = e + 2;
        st.mode = MNormal;
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (startsWith(s, i, "//")) { paint(out, i, s.size(), TComment); break; }
        if (startsWith(s, i, "/*")) {
            const size_t e = s.find("*/", i + 2);
            if (e == std::string::npos) { paint(out, i, s.size(), TComment); st.mode = MBlockComment; return st; }
            paint(out, i, e + 2, TComment);
            i = e + 2;
            continue;
        }
        if (c == '"') {
            const size_t e = quoteEnd(s, i);
            const size_t end = e == std::string::npos ? s.size() : e;
            const size_t after = skipSpaces(s, end);
            paint(out, i, end, after < s.size() && s[after] == ':' ? TAttr : TString);  // a key, or a value
            i = end;
            continue;
        }
        if (std::isdigit(c) || (c == '-' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) { const size_t e = numberEnd(s, i + 1); paint(out, i, e, TNumber); i = e; continue; }
        if (std::isalpha(c)) {
            size_t e = i;
            while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) e++;
            if (isWordKeyword(l, s.substr(i, e - i))) paint(out, i, e, TKeyword);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightMarkup(const Lang&, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    if (st.mode == MHtmlComment) {
        const size_t e = s.find("-->");
        if (e == std::string::npos) { paint(out, 0, s.size(), TComment); return st; }
        paint(out, 0, e + 3, TComment);
        i = e + 3;
        st.mode = MNormal;
    }
    bool inTag = false;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (!inTag && startsWith(s, i, "<!--")) {
            const size_t e = s.find("-->", i + 4);
            if (e == std::string::npos) { paint(out, i, s.size(), TComment); st.mode = MHtmlComment; return st; }
            paint(out, i, e + 3, TComment);
            i = e + 3;
            continue;
        }
        if (!inTag && c == '<' && i + 1 < s.size() && (std::isalpha(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '/' || s[i + 1] == '!' || s[i + 1] == '?')) {
            size_t e = i + 1;
            while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '/' || s[e] == '!' || s[e] == '?' || s[e] == ':' || s[e] == '-')) e++;
            paint(out, i, e, TTag);
            i = e;
            inTag = true;
            continue;
        }
        if (inTag) {
            if (c == '>' || (c == '/' && i + 1 < s.size() && s[i + 1] == '>') || (c == '?' && i + 1 < s.size() && s[i + 1] == '>')) {
                const size_t e = c == '>' ? i + 1 : i + 2;
                paint(out, i, e, TTag);
                i = e;
                inTag = false;
                continue;
            }
            if (c == '"' || c == '\'') {
                const size_t e = quoteEnd(s, i, false);
                paint(out, i, e == std::string::npos ? s.size() : e, TString);
                i = e == std::string::npos ? s.size() : e;
                continue;
            }
            if (identStart(c)) {
                size_t e = i;
                while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '-' || s[e] == ':' || s[e] == '.')) e++;
                paint(out, i, e, TAttr);
                i = e;
                continue;
            }
        }
        if (c == '&') {
            const size_t e = s.find(';', i);
            if (e != std::string::npos && e - i < 10) { paint(out, i, e + 1, TPreproc); i = e + 1; continue; }
        }
        i++;
    }
    return st;
}

inline LineState highlightCss(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    if (st.mode == MBlockComment) {
        const size_t e = s.find("*/");
        if (e == std::string::npos) { paint(out, 0, s.size(), TComment); return st; }
        paint(out, 0, e + 2, TComment);
        i = e + 2;
        st.mode = MNormal;
    }
    const bool hasBrace = s.find('{') != std::string::npos;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (!l.lineComment.empty() && startsWith(s, i, l.lineComment)) { paint(out, i, s.size(), TComment); break; }
        if (startsWith(s, i, "/*")) {
            const size_t e = s.find("*/", i + 2);
            if (e == std::string::npos) { paint(out, i, s.size(), TComment); st.mode = MBlockComment; return st; }
            paint(out, i, e + 2, TComment);
            i = e + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const size_t e = quoteEnd(s, i);
            paint(out, i, e == std::string::npos ? s.size() : e, TString);
            i = e == std::string::npos ? s.size() : e;
            continue;
        }
        if (c == '@') { size_t e = i + 1; while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '-')) e++; paint(out, i, e, TKeyword); i = e; continue; }
        if (c == '#' && i + 1 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1]))) {
            size_t e = i + 1;
            while (e < s.size() && std::isxdigit(static_cast<unsigned char>(s[e]))) e++;
            if (e - i == 4 || e - i == 5 || e - i == 7 || e - i == 9) { paint(out, i, e, TNumber); i = e; continue; }
        }
        if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            size_t e = i;
            while (e < s.size() && (std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '.' || s[e] == '%')) e++;
            paint(out, i, e, TNumber);
            i = e;
            continue;
        }
        if (c == '!' ) { size_t e = i + 1; while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) e++; paint(out, i, e, TKeyword); i = e; continue; }
        if (identStart(c) || c == '-' || c == '.' || c == '#') {
            size_t e = i + (c == '.' || c == '#' ? 1 : 0);
            while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '-')) e++;
            if (e == i) { i++; continue; }
            const size_t next = skipSpaces(s, e);
            Tok t = TPlain;
            if (next < s.size() && s[next] == ':' && !(next + 1 < s.size() && s[next + 1] == ':') && (!hasBrace || s.find('{') < i) && c != '.' && c != '#') t = TAttr;  // property: value
            else if (c == '.' || c == '#') t = TType;
            else if (next < s.size() && s[next] == '(') t = TFunction;
            else if (!hasBrace || s.find('{') > i) t = TTag;
            if (t == TTag && s.find(':', e) != std::string::npos && s.find('{') == std::string::npos && s.find(';') != std::string::npos) t = TPlain;  // a value
            paint(out, i, e, t);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightMarkdown(const Lang&, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    const size_t f = skipSpaces(s, 0);
    if (s.compare(f, 3, "```") == 0 || s.compare(f, 3, "~~~") == 0) {
        paint(out, 0, s.size(), TString);
        st.mode = st.mode == MFence ? MNormal : MFence;
        return st;
    }
    if (st.mode == MFence) { paint(out, 0, s.size(), TString); return st; }
    if (f < s.size() && s[f] == '#') { size_t e = f; while (e < s.size() && s[e] == '#') e++; if (e < s.size() && s[e] == ' ') { paint(out, 0, s.size(), THeading); return st; } }
    if (f < s.size() && s[f] == '>') paint(out, f, s.size(), TComment);
    if (f + 1 < s.size() && (s[f] == '-' || s[f] == '*' || s[f] == '+') && s[f + 1] == ' ') paint(out, f, f + 1, TKeyword);
    else { size_t e = f; while (e < s.size() && std::isdigit(static_cast<unsigned char>(s[e]))) e++; if (e > f && e + 1 < s.size() && s[e] == '.' && s[e + 1] == ' ') paint(out, f, e + 1, TKeyword); }
    for (size_t i = f; i < s.size(); i++) {
        if (s[i] == '`') { const size_t e = s.find('`', i + 1); if (e != std::string::npos) { paint(out, i, e + 1, TString); i = e; } }
        else if ((s[i] == '*' || s[i] == '_') && i + 1 < s.size() && s[i + 1] == s[i]) { const size_t e = s.find(std::string(2, s[i]), i + 2); if (e != std::string::npos && e > i + 2) { paint(out, i, e + 2, TEmphasis); i = e + 1; } }
        else if (s[i] == '[') {
            const size_t e = s.find(']', i + 1);
            if (e != std::string::npos) {
                paint(out, i, e + 1, TFunction);
                if (e + 1 < s.size() && s[e + 1] == '(') { const size_t u = s.find(')', e + 2); if (u != std::string::npos) { paint(out, e + 1, u + 1, TString); i = u; continue; } }
                i = e;
            }
        }
    }
    return st;
}

inline LineState highlightYaml(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = skipSpaces(s, 0);
    if (s.compare(i, 3, "---") == 0 || s.compare(i, 3, "...") == 0) { paint(out, i, i + 3, TKeyword); i += 3; }
    if (i + 1 < s.size() && s[i] == '-' && s[i + 1] == ' ') { paint(out, i, i + 1, TKeyword); i = skipSpaces(s, i + 1); }
    // key:
    {
        size_t e = i;
        if (e < s.size() && (s[e] == '"' || s[e] == '\'')) { const size_t q = quoteEnd(s, e); if (q != std::string::npos) e = q; }
        else while (e < s.size() && s[e] != ':' && s[e] != '#' && !(s[e] == ' ' && e + 1 < s.size() && s[e + 1] == '#')) e++;
        if (e < s.size() && s[e] == ':' && (e + 1 >= s.size() || s[e + 1] == ' ')) { paint(out, i, e, TAttr); i = e + 1; }
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) { paint(out, i, s.size(), TComment); break; }
        if (c == '"' || c == '\'') { const size_t e = quoteEnd(s, i, c == '"'); paint(out, i, e == std::string::npos ? s.size() : e, TString); i = e == std::string::npos ? s.size() : e; continue; }
        if ((c == '&' || c == '*' || c == '!') && (i == 0 || s[i - 1] == ' ')) { size_t e = i + 1; while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e]))) e++; paint(out, i, e, TPreproc); i = e; continue; }
        if (std::isdigit(c) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        if (std::isalpha(c)) {
            size_t e = i;
            while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) e++;
            if (isWordKeyword(l, s.substr(i, e - i)) && (i == 0 || s[i - 1] == ' ')) paint(out, i, e, TKeyword);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightIni(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    const size_t f = skipSpaces(s, 0);
    if (f >= s.size()) return st;
    if (s[f] == '#' || s[f] == ';') { paint(out, f, s.size(), TComment); return st; }
    if (s[f] == '[') { const size_t e = s.find(']', f); paint(out, f, e == std::string::npos ? s.size() : e + 1, THeading); return st; }
    size_t i = f;
    const size_t eq = s.find('=', f);
    const size_t colon = s.find(':', f);
    const size_t sep = std::min(eq, colon);
    if (sep != std::string::npos && s.find(' ', f) >= std::min(sep, s.find(' ', f))) { paint(out, f, sep, TAttr); i = sep + 1; }
    else if (sep == std::string::npos && eq == std::string::npos) {  // a line of a conf file: "word value value"
        size_t e = f;
        while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e]))) e++;
        if (e < s.size()) { paint(out, f, e, TAttr); i = e; }
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c == '#' || c == ';') && (i == 0 || s[i - 1] == ' ')) { paint(out, i, s.size(), TComment); break; }
        if (c == '"' || c == '\'') { const size_t e = quoteEnd(s, i); paint(out, i, e == std::string::npos ? s.size() : e, TString); i = e == std::string::npos ? s.size() : e; continue; }
        if (std::isdigit(c) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        if (std::isalpha(c)) {
            size_t e = i;
            while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e]))) e++;
            if (isWordKeyword(l, lowerStr(s.substr(i, e - i))) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) paint(out, i, e, TKeyword);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightSql(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = 0;
    if (st.mode == MBlockComment) {
        const size_t e = s.find("*/");
        if (e == std::string::npos) { paint(out, 0, s.size(), TComment); return st; }
        paint(out, 0, e + 2, TComment);
        i = e + 2;
        st.mode = MNormal;
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (startsWith(s, i, "--")) { paint(out, i, s.size(), TComment); break; }
        if (startsWith(s, i, "/*")) {
            const size_t e = s.find("*/", i + 2);
            if (e == std::string::npos) { paint(out, i, s.size(), TComment); st.mode = MBlockComment; return st; }
            paint(out, i, e + 2, TComment);
            i = e + 2;
            continue;
        }
        if (c == '\'' || c == '"') { const size_t e = quoteEnd(s, i, false); paint(out, i, e == std::string::npos ? s.size() : e, c == '\'' ? TString : TAttr); i = e == std::string::npos ? s.size() : e; continue; }
        if (std::isdigit(c) && (i == 0 || !identChar(static_cast<unsigned char>(s[i - 1])))) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        if (identStart(c)) {
            size_t e = i;
            while (e < s.size() && identChar(static_cast<unsigned char>(s[e]))) e++;
            const std::string w = s.substr(i, e - i);
            const size_t next = skipSpaces(s, e);
            Tok t = isWordKeyword(l, w) ? TKeyword : isWordType(l, w) ? TType : (next < s.size() && s[next] == '(') ? TFunction : TPlain;
            paint(out, i, e, t);
            i = e;
            continue;
        }
        i++;
    }
    return st;
}

inline LineState highlightMake(const Lang& l, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    size_t i = skipSpaces(s, 0);
    if (i >= s.size()) return st;
    const bool cmake = l.name == "CMake", docker = l.name == "Dockerfile";
    if (!cmake && !docker && s[0] != '\t' && s[0] != '#') {  // target: deps
        size_t e = i;
        while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e])) && s[e] != ':' && s[e] != '=') e++;
        if (e < s.size() && s[e] == ':' && !(e + 1 < s.size() && s[e + 1] == '=')) { if (!isWordKeyword(l, s.substr(i, e - i))) { paint(out, i, e, TFunction); i = e + 1; } }
        else if (e < s.size() && (s[e] == '=' || (s[e] == ':' && e + 1 < s.size() && s[e + 1] == '='))) { paint(out, i, e, TAttr); }
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) { paint(out, i, s.size(), TComment); break; }
        if (c == '"' || c == '\'') { const size_t e = quoteEnd(s, i, c == '"'); paint(out, i, e == std::string::npos ? s.size() : e, TString); i = e == std::string::npos ? s.size() : e; continue; }
        if (c == '$' && i + 1 < s.size() && (s[i + 1] == '(' || s[i + 1] == '{')) {
            const char close = s[i + 1] == '(' ? ')' : '}';
            const size_t e = s.find(close, i + 2);
            paint(out, i, e == std::string::npos ? s.size() : e + 1, TAttr);
            i = e == std::string::npos ? s.size() : e + 1;
            continue;
        }
        if (c == '$' || c == '@') { paint(out, i, std::min(i + 2, s.size()), TAttr); i += 2; continue; }
        if (identStart(c) || c == '.' || c == '-') {
            size_t e = i;
            while (e < s.size() && (identChar(static_cast<unsigned char>(s[e])) || s[e] == '.' || (s[e] == '-' && e > i))) e++;
            if (e == i) { i++; continue; }
            const std::string w = s.substr(i, e - i);
            const size_t next = skipSpaces(s, e);
            Tok t = isWordKeyword(l, w) ? TKeyword : isWordType(l, w) ? TType : (cmake && next < s.size() && s[next] == '(') ? TFunction : TPlain;
            if (docker && i == skipSpaces(s, 0) && t == TPlain) t = TPlain;
            paint(out, i, e, t);
            i = e;
            continue;
        }
        if (std::isdigit(c)) { const size_t e = numberEnd(s, i); paint(out, i, e, TNumber); i = e; continue; }
        i++;
    }
    return st;
}

inline LineState highlightDiff(const Lang&, const std::string& s, LineState st, std::vector<uint8_t>& out) {
    using namespace detail;
    if (s.empty()) return st;
    if (s.compare(0, 2, "@@") == 0) paint(out, 0, s.size(), THeading);
    else if (s.compare(0, 3, "+++") == 0 || s.compare(0, 3, "---") == 0 || s.compare(0, 4, "diff") == 0 || s.compare(0, 5, "index") == 0) paint(out, 0, s.size(), TComment);
    else if (s[0] == '+') paint(out, 0, s.size(), TAdded);
    else if (s[0] == '-') paint(out, 0, s.size(), TRemoved);
    return st;
}

// Highlights one line: fills `out` (one Tok per byte) and returns the state the next line starts in.
inline LineState highlightLine(const Lang& l, const std::string& line, LineState in, std::vector<uint8_t>& out) {
    out.assign(line.size(), TPlain);
    if (line.size() > 4000) return in;  // a minified file: not worth scanning (and the state is kept as it was)
    switch (l.style) {
        case Style::C: return highlightC(l, line, in, out);
        case Style::Python: return highlightPython(l, line, in, out);
        case Style::Shell: return highlightShell(l, line, in, out);
        case Style::Lua: return highlightLua(l, line, in, out);
        case Style::Json: return highlightJson(l, line, in, out);
        case Style::Markup: return highlightMarkup(l, line, in, out);
        case Style::Css: return highlightCss(l, line, in, out);
        case Style::Markdown: return highlightMarkdown(l, line, in, out);
        case Style::Yaml: return highlightYaml(l, line, in, out);
        case Style::Ini: return highlightIni(l, line, in, out);
        case Style::Sql: return highlightSql(l, line, in, out);
        case Style::Make: return highlightMake(l, line, in, out);
        case Style::Diff: return highlightDiff(l, line, in, out);
        default: return in;
    }
}

}  // namespace t3
