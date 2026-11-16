// Unit and currency data and the parser for conversions written as text ("1usd to eur", "cuanto es 100 dolares a pesos mexicanos").
// Shared by the Tools page (src/tools/page_units.inc) and the launcher (src/parts/09_launcher.inc).
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <cstring>
#include <vector>

namespace unitconv {

constexpr int kUnTemperature = 3;  // its index in unStaticCategories()

struct UnUnit { std::string id, name, sym; double factor = 1; };  // factor: how many base units one of this is (temperature is special)
struct UnCategory { const char* name; const char* icon; std::vector<UnUnit> units; };

inline const std::vector<UnCategory>& unStaticCategories() {
    static const std::vector<UnCategory> cats = {
        {"Currency", "dollar-minimalistic", {}},
        {"Length", "ruler", {{"m", "Meter", "m", 1}, {"km", "Kilometer", "km", 1000}, {"cm", "Centimeter", "cm", 0.01}, {"mm", "Millimeter", "mm", 0.001},
                             {"mi", "Mile", "mi", 1609.344}, {"yd", "Yard", "yd", 0.9144}, {"ft", "Foot", "ft", 0.3048}, {"in", "Inch", "in", 0.0254}, {"nmi", "Nautical mile", "nmi", 1852}}},
        {"Weight", "weigher", {{"kg", "Kilogram", "kg", 1}, {"g", "Gram", "g", 0.001}, {"mg", "Milligram", "mg", 1e-6}, {"t", "Tonne", "t", 1000},
                               {"lb", "Pound", "lb", 0.45359237}, {"oz", "Ounce", "oz", 0.028349523125}, {"st", "Stone", "st", 6.35029318}}},
        {"Temperature", "thermometer", {{"c", "Celsius", "°C", 1}, {"f", "Fahrenheit", "°F", 1}, {"k", "Kelvin", "K", 1}}},
        {"Volume", "waterdrop", {{"l", "Liter", "L", 1}, {"ml", "Milliliter", "mL", 0.001}, {"m3", "Cubic meter", "m³", 1000}, {"gal", "US gallon", "gal", 3.785411784},
                                 {"qt", "US quart", "qt", 0.946352946}, {"pt", "US pint", "pt", 0.473176473}, {"cup", "US cup", "cup", 0.2365882365}, {"floz", "US fluid ounce", "fl oz", 0.0295735295625},
                                 {"tbsp", "Tablespoon", "tbsp", 0.01478676478125}, {"tsp", "Teaspoon", "tsp", 0.00492892159375}, {"igal", "Imperial gallon", "imp gal", 4.54609}}},
        {"Speed", "rocket-2", {{"kmh", "Kilometers per hour", "km/h", 1.0 / 3.6}, {"ms", "Meters per second", "m/s", 1}, {"mph", "Miles per hour", "mph", 0.44704},
                                  {"kn", "Knot", "kn", 1852.0 / 3600}, {"fts", "Feet per second", "ft/s", 0.3048}}},
        {"Area", "map", {{"m2", "Square meter", "m²", 1}, {"km2", "Square kilometer", "km²", 1e6}, {"ha", "Hectare", "ha", 10000}, {"acre", "Acre", "ac", 4046.8564224},
                         {"ft2", "Square foot", "ft²", 0.09290304}, {"yd2", "Square yard", "yd²", 0.83612736}, {"in2", "Square inch", "in²", 0.00064516}, {"cm2", "Square centimeter", "cm²", 0.0001}, {"mi2", "Square mile", "mi²", 2589988.110336}}},
        {"Time", "clock-circle", {{"s", "Second", "s", 1}, {"ms", "Millisecond", "ms", 0.001}, {"min", "Minute", "min", 60}, {"h", "Hour", "h", 3600},
                                  {"d", "Day", "d", 86400}, {"w", "Week", "wk", 604800}, {"mo", "Month", "mo", 2629746}, {"y", "Year", "yr", 31556952}}},
        {"Data", "database", {{"b", "Byte", "B", 1}, {"kb", "Kilobyte", "KB", 1e3}, {"mb", "Megabyte", "MB", 1e6}, {"gb", "Gigabyte", "GB", 1e9}, {"tb", "Terabyte", "TB", 1e12},
                              {"pb", "Petabyte", "PB", 1e15}, {"kib", "Kibibyte", "KiB", 1024}, {"mib", "Mebibyte", "MiB", 1048576}, {"gib", "Gibibyte", "GiB", 1073741824.0},
                              {"tib", "Tebibyte", "TiB", 1099511627776.0}, {"bit", "Bit", "bit", 0.125}}},
    };
    return cats;
}

// The currencies people look for first, and names for the common ones (the rest show their code).
inline const char* const kUnPopular[] = {"USD", "EUR", "GBP", "JPY", "MXN", "CAD", "AUD", "CHF", "CNY", "BRL", "INR", "COP", "ARS", "CLP", "PEN", "KRW", "RUB", "TRY", "SEK", "NOK", "DKK", "PLN", "NZD", "ZAR", "AED", "SAR", "SGD", "HKD", "THB", "VES", "UYU", "BOB", "PYG", "DOP", "CRC", "GTQ", "ILS", "CZK", "HUF", "UAH"};
inline const std::map<std::string, std::string>& unCurrencyNames() {
    static const std::map<std::string, std::string> n = {
        {"USD", "US dollar"}, {"EUR", "Euro"}, {"GBP", "British pound"}, {"JPY", "Japanese yen"}, {"MXN", "Mexican peso"}, {"CAD", "Canadian dollar"}, {"AUD", "Australian dollar"},
        {"CHF", "Swiss franc"}, {"CNY", "Chinese yuan"}, {"BRL", "Brazilian real"}, {"INR", "Indian rupee"}, {"COP", "Colombian peso"}, {"ARS", "Argentine peso"}, {"CLP", "Chilean peso"},
        {"PEN", "Peruvian sol"}, {"KRW", "South Korean won"}, {"RUB", "Russian ruble"}, {"TRY", "Turkish lira"}, {"SEK", "Swedish krona"}, {"NOK", "Norwegian krone"}, {"DKK", "Danish krone"},
        {"PLN", "Polish zloty"}, {"NZD", "New Zealand dollar"}, {"ZAR", "South African rand"}, {"AED", "UAE dirham"}, {"SAR", "Saudi riyal"}, {"SGD", "Singapore dollar"}, {"HKD", "Hong Kong dollar"},
        {"THB", "Thai baht"}, {"VES", "Venezuelan bolívar"}, {"UYU", "Uruguayan peso"}, {"BOB", "Bolivian boliviano"}, {"PYG", "Paraguayan guaraní"}, {"DOP", "Dominican peso"},
        {"CRC", "Costa Rican colón"}, {"GTQ", "Guatemalan quetzal"}, {"ILS", "Israeli shekel"}, {"CZK", "Czech koruna"}, {"HUF", "Hungarian forint"}, {"UAH", "Ukrainian hryvnia"},
        {"IDR", "Indonesian rupiah"}, {"MYR", "Malaysian ringgit"}, {"PHP", "Philippine peso"}, {"VND", "Vietnamese dong"}, {"EGP", "Egyptian pound"}, {"NGN", "Nigerian naira"},
        {"RON", "Romanian leu"}, {"BGN", "Bulgarian lev"}, {"ISK", "Icelandic króna"}, {"TWD", "New Taiwan dollar"}, {"PKR", "Pakistani rupee"}, {"BTC", "Bitcoin"},
    };
    return n;
}


// Rates as open.er-api.com writes them ("rates":{"USD":1,"EUR":0.9,...}); out: per US dollar.
inline bool unParseRates(const std::string& j, std::map<std::string, double>& out, long long& at) {
    const size_t r = j.find("\"rates\"");
    if (r == std::string::npos) return false;
    out.clear();
    size_t i = j.find('{', r);
    const size_t end = i == std::string::npos ? i : j.find('}', i);
    while (i != std::string::npos && end != std::string::npos) {
        const size_t q1 = j.find('"', i + 1);
        if (q1 == std::string::npos || q1 > end) break;
        const size_t q2 = j.find('"', q1 + 1), colon = j.find(':', q2);
        if (q2 == std::string::npos || colon == std::string::npos) break;
        const double v = std::atof(j.c_str() + colon + 1);
        if (v > 0 && q2 - q1 - 1 == 3) out[j.substr(q1 + 1, 3)] = v;
        i = j.find(',', colon);
        if (i == std::string::npos || i > end) break;
    }
    const size_t t = j.find("\"time_last_update_unix\"");
    at = t == std::string::npos ? static_cast<long long>(std::time(nullptr)) : std::atoll(j.c_str() + j.find(':', t) + 1);
    return out.size() > 5;
}

// The whole currency category: popular ones first, then every other code the rates have.
inline std::vector<UnUnit> unCurrencyUnits(const std::map<std::string, double>& rates) {
    std::vector<UnUnit> units;
    std::vector<std::string> order;
    for (const char* code : kUnPopular) if (rates.count(code)) order.push_back(code);
    for (const auto& [code, v] : rates) if (std::find(order.begin(), order.end(), code) == order.end()) order.push_back(code);
    for (const std::string& code : order) {
        const auto n = unCurrencyNames().find(code);
        units.push_back({code, n == unCurrencyNames().end() ? code : n->second, code, 1.0 / rates.at(code)});
    }
    return units;
}

inline double unToKelvin(const std::string& id, double v) { return id == "c" ? v + 273.15 : id == "f" ? (v - 32) * 5 / 9 + 273.15 : v; }
inline double unFromKelvin(const std::string& id, double k) { return id == "c" ? k - 273.15 : id == "f" ? (k - 273.15) * 9 / 5 + 32 : k; }
inline double unConvert(bool temperature, double v, const UnUnit& a, const UnUnit& b) {
    return temperature ? unFromKelvin(b.id, unToKelvin(a.id, v)) : v * a.factor / b.factor;
}

inline std::string unFmt(double v) {
    if (!std::isfinite(v)) return "—";
    const double a = std::fabs(v);
    char buf[64];
    if (a != 0 && (a >= 1e15 || a < 1e-6)) { std::snprintf(buf, sizeof buf, "%.6g", v); return buf; }
    std::snprintf(buf, sizeof buf, "%.*f", a >= 1000 ? 2 : a >= 1 ? 4 : 6, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); }
    const size_t dot = s.find('.'), intEnd = dot == std::string::npos ? s.size() : dot, start = s[0] == '-' ? 1 : 0;
    std::string out = s.substr(dot == std::string::npos ? s.size() : dot);
    for (size_t i = intEnd; i > start;) {
        const size_t from = i >= start + 3 ? i - 3 : start;
        out = s.substr(from, i - from) + (i < intEnd ? "," : "") + out;
        i = from;
    }
    return (start ? "-" : "") + out;
}

// ---- reading a conversion from text -----------------------------------------------------------------------------------------------
// Lower case, no accents; with `keepSpaces` false also no spaces, dots, quotes or the degree sign (so "fl. oz" and "°C" match "floz" and "c").
inline std::string unNorm(const std::string& s, bool keepSpaces) {
    static const std::pair<const char*, const char*> kFold[] = {{"\xc3\xa1", "a"}, {"\xc3\xa0", "a"}, {"\xc3\xa4", "a"}, {"\xc3\xa9", "e"}, {"\xc3\xa8", "e"}, {"\xc3\xad", "i"}, {"\xc3\xb3", "o"}, {"\xc3\xb6", "o"},
                                                               {"\xc3\xba", "u"}, {"\xc3\xbc", "u"}, {"\xc3\xb1", "n"}, {"\xc3\x81", "a"}, {"\xc3\x89", "e"}, {"\xc3\x8d", "i"}, {"\xc3\x93", "o"},
                                                               {"\xc3\x9a", "u"}, {"\xc3\x91", "n"}, {"\xc2\xbf", ""}, {"\xc2\xa1", ""}, {"\xc2\xb2", "2"}, {"\xc2\xb3", "3"}, {"\xc2\xb0", ""}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool folded = false;
        for (const auto& [from, to] : kFold) {
            const size_t n = std::char_traits<char>::length(from);
            if (s.compare(i, n, from) == 0) { out += to; i += n; folded = true; break; }
        }
        if (folded) continue;
        const char c = s[i++];
        if (c == ' ' || c == '\t') { if (keepSpaces) out += ' '; continue; }
        if (c == '.' || c == '\'' || c == '_' || c == '-' ) { if (c == '-' && keepSpaces) out += c; continue; }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

struct UnRef { int cat; int unit; };

// Extra names people type for each unit (English and Spanish, no accents or spaces); the id, the symbol and the name always match too.
inline const std::vector<std::pair<std::string, std::string>>& unAliases() {
    static const std::vector<std::pair<std::string, std::string>> a = {
        {"m", "meters,metre,metres,metros,metro"}, {"km", "kilometers,kilometres,kilometros,kilometro,kms"}, {"cm", "centimeters,centimetres,centimetros,centimetro"}, {"mm", "millimeters,millimetres,milimetros,milimetro"},
        {"mi", "mile,miles,milla,millas"}, {"yd", "yard,yards,yarda,yardas"}, {"ft", "feet,foot,pies,pie,piece"}, {"in", "inch,inches,pulgada,pulgadas"}, {"nmi", "nauticalmile,nauticalmiles,millanautica,millasnauticas"},
        {"kg", "kilo,kilos,kgs,kilogram,kilograms,kilogramo,kilogramos"}, {"g", "gr,grams,gram,gramme,gramo,gramos"}, {"mg", "milligram,milligrams,miligramo,miligramos"}, {"t", "ton,tons,tonne,tonnes,tonelada,toneladas"},
        {"lb", "lbs,pound,pounds,libra,libras"}, {"oz", "ounce,ounces,onza,onzas"}, {"st", "stone,stones"},
        {"c", "celsius,centigrade,centigrados,gradoscelsius,gradocelsius,degc,degreescelsius"}, {"f", "fahrenheit,degf,gradosfahrenheit,gradofahrenheit,degreesfahrenheit"}, {"k", "kelvin,kelvins,gradoskelvin"},
        {"l", "liter,liters,litre,litres,lt,litro,litros"}, {"ml", "cc,milliliter,milliliters,millilitre,mililitro,mililitros"}, {"m3", "cubicmeter,cubicmeters,metrocubico,metroscubicos"},
        {"gal", "gallon,gallons,usgal,galon,galones"}, {"qt", "quart,quarts,cuarto,cuartosdegalon"}, {"pt", "pint,pints,pinta,pintas"}, {"cup", "cups,taza,tazas"}, {"floz", "fluidounce,fluidounces,usfloz,onzaliquida,onzasliquidas"},
        {"tbsp", "tablespoon,tablespoons,cucharada,cucharadas"}, {"tsp", "teaspoon,teaspoons,cucharadita,cucharaditas"}, {"igal", "impgal,imperialgallon,galonimperial"},
        {"kmh", "kph,km/h,kmph,kilometersperhour,kilometrosporhora,kmporhora"}, {"ms", "m/s,mps,meterspersecond,metrosporsegundo"}, {"mph", "mi/h,milesperhour,millasporhora"}, {"kn", "knot,knots,kt,kts,nudo,nudos"}, {"fts", "ft/s,fps"},
        {"m2", "sqm,squaremeter,squaremeters,metrocuadrado,metroscuadrados"}, {"km2", "sqkm,squarekilometer,kilometrocuadrado,kilometroscuadrados"}, {"ha", "hectare,hectares,hectarea,hectareas"}, {"acre", "acres,ac"},
        {"ft2", "sqft,squarefoot,squarefeet,piecuadrado,piescuadrados"}, {"yd2", "sqyd,squareyard"}, {"in2", "sqin,squareinch"}, {"cm2", "sqcm,squarecentimeter"}, {"mi2", "sqmi,squaremile"},
        {"s", "sec,secs,second,seconds,segundo,segundos,seg"}, {"ms", "msec,millisecond,milliseconds,milisegundo,milisegundos"}, {"min", "mins,minute,minutes,minuto,minutos"}, {"h", "hr,hrs,hour,hours,hora,horas"},
        {"d", "day,days,dia,dias"}, {"w", "wk,week,weeks,semana,semanas"}, {"mo", "month,months,mes,meses"}, {"y", "yr,yrs,year,years,ano,anos"},
        {"b", "byte,bytes"}, {"kb", "kilobyte,kilobytes"}, {"mb", "megabyte,megabytes,megas,mega"}, {"gb", "gigabyte,gigabytes,gigas,giga"}, {"tb", "terabyte,terabytes,teras,tera"}, {"pb", "petabyte,petabytes"},
        {"kib", "kibibyte,kibibytes"}, {"mib", "mebibyte,mebibytes"}, {"gib", "gibibyte,gibibytes"}, {"tib", "tebibyte,tebibytes"}, {"bit", "bits"},
    };
    return a;
}
inline const std::vector<std::pair<std::string, std::string>>& unCurrencyAliases() {
    static const std::vector<std::pair<std::string, std::string>> a = {
        {"USD", "$,dolar,dolares,dollar,dollars,bucks,dolarestadounidenses,dolaramericano,dolaresamericanos,usdollars,dolaresusd"}, {"EUR", "\xe2\x82\xac,euro,euros"},
        {"GBP", "\xc2\xa3,libra,libras,pound,pounds,libraesterlina,librasesterlinas,poundsterling,sterling"}, {"JPY", "\xc2\xa5,yen,yenes,japaneseyen"}, {"CNY", "yuan,yuanes,renminbi,rmb"},
        {"MXN", "pesomexicano,pesosmexicanos,mexicanpeso,mexicanpesos,pesomx,pesosmx"}, {"COP", "pesocolombiano,pesoscolombianos,colombianpeso,colombianpesos"}, {"ARS", "pesoargentino,pesosargentinos,argentinepeso,argentinepesos"},
        {"CLP", "pesochileno,peseoschilenos,pesoschilenos,chileanpeso,chileanpesos"}, {"UYU", "pesouruguayo,pesosuruguayos"}, {"DOP", "pesodominicano,pesosdominicanos"}, {"CAD", "dolarcanadiense,dolarescanadienses,canadiandollar,canadiandollars"},
        {"AUD", "dolaraustraliano,dolaresaustralianos,australiandollar,australiandollars"}, {"CHF", "franco,francos,francosuizo,francossuizos,swissfranc,swissfrancs"}, {"BRL", "real,reales,realbrasileno,realesbrasilenos,brazilianreal"},
        {"PEN", "sol,soles,nuevosol,nuevossoles,peruviansol"}, {"INR", "rupia,rupias,rupee,rupees,\xe2\x82\xb9"}, {"KRW", "won,wones,southkoreanwon"}, {"RUB", "rublo,rublos,ruble,rubles"}, {"TRY", "lira,liras,turkishlira"},
        {"BTC", "bitcoin,bitcoins"}, {"SEK", "coronasuecas,swedishkrona"}, {"NOK", "coronasnoruegas,norwegiankrone"}, {"DKK", "coronasdanesas,danishkrone"}, {"PLN", "zloty,zlotys"}, {"UAH", "grivna,grivnas,hryvnia"},
        {"VES", "bolivar,bolivares"}, {"BOB", "boliviano,bolivianos"}, {"PYG", "guarani,guaranies"}, {"CRC", "colon,colones"}, {"GTQ", "quetzal,quetzales"}, {"ILS", "shekel,shekels,nis"}, {"THB", "baht,bahts"},
        {"AED", "dirham,dirhams,dirhamesemiratis"}, {"SAR", "riyal,riyales"},
    };
    return a;
}

struct UnQuery {
    bool ok = false, needsRates = false;
    double amount = 1, value = 0;
    std::string fromSym, toSym, fromName, toName;
};

inline std::vector<std::string> unSplit(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) { if (c == ' ') { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
inline bool unStartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0 && (s.size() == p.size() || s[p.size()] == ' '); }

// One name -> every unit it can be (the same word may be a unit of two kinds: "pounds" are a weight and a currency; the other side of the sentence decides).
inline std::vector<UnRef> unCandidates(const std::string& rawName, const std::vector<UnCategory>& cats) {
    const std::string key = unNorm(rawName, false);
    std::vector<UnRef> out;
    if (key.empty()) return out;
    const auto add = [&](int c, int u) { for (const UnRef& r : out) if (r.cat == c && r.unit == u) return; out.push_back({c, u}); };
    for (size_t c = 0; c < cats.size(); c++) {
        const auto& units = cats[c].units;
        for (size_t u = 0; u < units.size(); u++) {
            const UnUnit& x = units[u];
            if (unNorm(x.id, false) == key || unNorm(x.sym, false) == key || unNorm(x.name, false) == key || unNorm(x.name, false) + "s" == key) add(static_cast<int>(c), static_cast<int>(u));
            if (c == 0) {
                for (const auto& [code, list] : unCurrencyAliases()) {
                    if (code != x.id) continue;
                    size_t from = 0;
                    while (from <= list.size()) {
                        size_t comma = list.find(',', from);
                        if (comma == std::string::npos) comma = list.size();
                        if (list.substr(from, comma - from) == key) add(0, static_cast<int>(u));
                        from = comma + 1;
                    }
                }
            } else if (c > 0) {
                for (const auto& [id, list] : unAliases()) {
                    if (id != x.id) continue;
                    size_t from = 0;
                    while (from <= list.size()) {
                        size_t comma = list.find(',', from);
                        if (comma == std::string::npos) comma = list.size();
                        if (list.substr(from, comma - from) == key) add(static_cast<int>(c), static_cast<int>(u));
                        from = comma + 1;
                    }
                }
            }
        }
    }
    return out;
}

inline bool unParseNumber(const std::string& s, double& v) {
    std::string t;
    for (char c : s) if (c != ' ') t += c;
    const size_t dot = t.find('.'), comma = t.find(',');
    if (comma != std::string::npos) {
        if (dot != std::string::npos || (t.size() - comma - 1 == 3 && t.find(',', comma + 1) == std::string::npos && comma > 0 && comma <= 3)) t.erase(std::remove(t.begin(), t.end(), ','), t.end());
        else std::replace(t.begin(), t.end(), ',', '.');
    }
    if (t.empty() || t == "-" || t == ".") return false;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    return end && *end == 0;
}

// "1usd to eur", "convert 5 km in miles", "cuanto es 100 dolares a pesos mexicanos". `cats` is the category list with the currencies filled in (empty when no rates are known).
inline UnQuery unParseQuery(const std::string& query, const std::vector<UnCategory>& cats, bool haveRates) {
    UnQuery res;
    std::string q = unNorm(query, true);
    while (!q.empty() && (q.back() == ' ' || q.back() == '?')) q.pop_back();
    static const char* const kLead[] = {"a cuanto equivale", "cuanto equivale", "cuanto es", "cuanto son", "cuantos son", "cuantas son", "cuanto cuesta", "cuanto vale", "cuanto seria", "how much is", "how much are", "how many",
                                        "what is", "what's", "whats", "convertir", "convierte", "convert", "conversion de", "conversion", "equivalencia de", "pasar", "cambiar", "cambia", "calcula", "calcular"};
    for (bool again = true; again;) {
        again = false;
        for (const char* p : kLead) if (unStartsWith(q, p)) { q = q.size() > std::strlen(p) ? q.substr(std::strlen(p) + 1) : ""; again = true; break; }
    }
    // the number (it may be glued to the unit: "1usd")
    size_t i = 0;
    while (i < q.size() && q[i] == ' ') i++;
    size_t j = i;
    if (j < q.size() && q[j] == '-') j++;
    while (j < q.size() && (std::isdigit(static_cast<unsigned char>(q[j])) || q[j] == '.' || q[j] == ',' || (q[j] == ' ' && j + 1 < q.size() && std::isdigit(static_cast<unsigned char>(q[j + 1])) && j > i && std::isdigit(static_cast<unsigned char>(q[j - 1]))))) j++;
    double amount = 1;
    bool hasNumber = false;
    if (j > i && unParseNumber(q.substr(i, j - i), amount)) hasNumber = true;
    else j = i;
    const std::string rest = q.substr(hasNumber ? j : i);
    const std::vector<std::string> tok = unSplit(rest);
    static const char* const kConnectors[] = {"to", "in", "into", "as", "a", "en", "->", "=", "\xe2\x86\x92", "equals", "es", "hacia", "al"};
    size_t cut = 0;
    for (size_t t = 1; t + 1 <= tok.size() && cut == 0; t++)
        for (const char* c : kConnectors) if (tok[t] == c && t + 1 < tok.size()) { cut = t; break; }
    if (cut == 0 || tok.empty()) return res;
    std::string from, to;
    for (size_t t = 0; t < cut; t++) from += (t ? " " : "") + tok[t];
    for (size_t t = cut + 1; t < tok.size(); t++) to += (t > cut + 1 ? " " : "") + tok[t];
    const std::vector<UnRef> a = unCandidates(from, cats), b = unCandidates(to, cats);
    for (const UnRef& x : a) {
        for (const UnRef& y : b) {
            if (x.cat != y.cat) continue;
            const UnUnit &ux = cats[static_cast<size_t>(x.cat)].units[static_cast<size_t>(x.unit)], &uy = cats[static_cast<size_t>(y.cat)].units[static_cast<size_t>(y.unit)];
            res.ok = true;
            res.amount = amount;
            res.value = unConvert(x.cat == kUnTemperature, amount, ux, uy);
            res.fromSym = ux.sym; res.toSym = uy.sym; res.fromName = ux.name; res.toName = uy.name;
            return res;
        }
    }
    // a currency was named but no rates are known yet: say so instead of staying silent
    if (!haveRates) {
        const auto looksCurrency = [&](const std::string& n) {
            const std::string k = unNorm(n, false);
            if (k.size() == 3 && unCurrencyNames().count([&] { std::string u = k; for (char& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return u; }())) return true;
            for (const auto& [code, list] : unCurrencyAliases()) {
                size_t from = 0;
                while (from <= list.size()) {
                    size_t comma = list.find(',', from);
                    if (comma == std::string::npos) comma = list.size();
                    if (list.substr(from, comma - from) == k) return true;
                    from = comma + 1;
                }
            }
            return false;
        };
        if (looksCurrency(from) && looksCurrency(to)) { res.needsRates = true; res.amount = amount; }
    }
    return res;
}

}  // namespace unitconv
