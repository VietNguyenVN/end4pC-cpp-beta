
std::mutex g_wsMx;
WeatherData g_wsData;
std::string g_wsCity, g_wsUnit = "celsius";
int g_wsInterval = 10;
bool g_wsStarted = false;
std::atomic<bool> g_wsWanted{false}, g_wsRefresh{false}, g_wsForce{false};
std::atomic<uint64_t> g_wsVersion{0};
int g_weatherFd = -1, g_weatherWrite = -1;

std::string wsCachePath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/.cache/end4pC/weather-service.txt";
}

void wsSaveCache(const WeatherData& w) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(wsCachePath()).parent_path(), ec);
    {
        const std::string lockPath = std::filesystem::path(wsCachePath()).parent_path().string() + "/weather.json";
        char b[512];
        std::snprintf(b, sizeof b, "{\"city\": \"%s\", \"unit\": \"%s\", \"temp\": %.1f, \"code\": %d, \"day\": %d}\n", w.city.c_str(), w.unit.c_str(), w.temp, w.code, w.day ? 1 : 0);
        std::ofstream(lockPath + ".tmp", std::ios::trunc) << b;
        std::filesystem::rename(lockPath + ".tmp", lockPath, ec);
    }
    const std::string tmpPath = wsCachePath() + ".tmp";
    std::ofstream out(tmpPath, std::ios::trunc);
    if (!out) return;
    out << w.city << "\n" << w.unit << "\n" << w.temp << " " << w.feels << " " << w.hi << " " << w.lo << " " << w.code << " " << (w.day ? 1 : 0) << " " << static_cast<long long>(w.fetched) << "\n";
    for (const auto& h : w.hours) out << h.hour << " " << h.temp << " " << h.code << " " << (h.day ? 1 : 0) << "\n";
    for (const auto& h : w.hourly) out << h.hour << " " << h.temp << " " << h.code << " " << (h.day ? 1 : 0) << "\n";
    out << w.humidity << " " << w.wind << " " << w.sunrise << " " << w.sunset << " " << w.nDays << "\n";
    for (int i = 0; i < w.nDays; i++) out << w.days[i].year << " " << w.days[i].mon << " " << w.days[i].mday << " " << w.days[i].hi << " " << w.days[i].lo << " " << w.days[i].code << "\n";
    out.close();
    std::filesystem::rename(tmpPath, wsCachePath(), ec);
}

bool wsLoadCache(WeatherData& w) {
    std::ifstream in(wsCachePath());
    if (!in) return false;
    std::getline(in, w.city);
    std::getline(in, w.unit);
    int day = 1;
    long long fetched = 0;
    if (!(in >> w.temp >> w.feels >> w.hi >> w.lo >> w.code >> day >> fetched)) return false;
    w.day = day != 0;
    w.fetched = static_cast<time_t>(fetched);
    for (auto& h : w.hours) {
        int d = 1;
        if (!(in >> h.hour >> h.temp >> h.code >> d)) return false;
        h.day = d != 0;
    }
    for (auto& h : w.hourly) {
        int d = 1;
        if (!(in >> h.hour >> h.temp >> h.code >> d)) return false;
        h.day = d != 0;
    }
    if (!(in >> w.humidity >> w.wind >> w.sunrise >> w.sunset >> w.nDays) || w.nDays < 0 || w.nDays > 7) return false;
    for (int i = 0; i < w.nDays; i++)
        if (!(in >> w.days[i].year >> w.days[i].mon >> w.days[i].mday >> w.days[i].hi >> w.days[i].lo >> w.days[i].code)) return false;
    w.ok = true;
    return true;
}

bool wsScalar(const std::string& t, const std::string& key, std::string& out) {
    const size_t k = t.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = t.find(':', k);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\n')) i++;
    if (i < t.size() && t[i] == '"') {
        const size_t e = t.find('"', i + 1);
        if (e == std::string::npos) return false;
        out = t.substr(i + 1, e - i - 1);
        return true;
    }
    size_t e = i;
    while (e < t.size() && t[e] != ',' && t[e] != '}' && t[e] != ']' && t[e] != '\n') e++;
    out = t.substr(i, e - i);
    return !out.empty();
}
std::string wsObject(const std::string& t, const std::string& key) {
    const size_t k = t.find("\"" + key + "\":{");
    if (k == std::string::npos) return "";
    const size_t s = t.find('{', k), e = t.find('}', s);
    return e == std::string::npos ? "" : t.substr(s + 1, e - s - 1);
}
std::vector<std::string> wsArray(const std::string& t, const std::string& key) {
    std::vector<std::string> out;
    const size_t k = t.find("\"" + key + "\":[");
    if (k == std::string::npos) return out;
    const size_t s = t.find('[', k), e = t.find(']', s);
    if (e == std::string::npos) return out;
    std::string item;
    for (size_t i = s + 1; i <= e; i++) {
        if (i == e || t[i] == ',') { if (!item.empty()) out.push_back(item); item.clear(); }
        else if (t[i] != '"' && t[i] != ' ') item += t[i];
    }
    return out;
}

std::string wsCurl(const std::string& url) {
    const std::string cmd = "curl -s --max-time 8 '" + url + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    pclose(f);
    return out;
}
std::string wsEncode(const std::string& s) {
    std::string o;
    char b[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += static_cast<char>(c);
        else { std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

bool wsFetch(const std::string& cityCfg, const std::string& unit) {
    std::string lat, lon, city;
    if (!cityCfg.empty()) {
        const std::string j = wsCurl("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + wsEncode(cityCfg));
        if (!wsScalar(j, "latitude", lat) || !wsScalar(j, "longitude", lon)) return false;
        wsScalar(j, "name", city);
    } else {
        const std::string j = wsCurl("https://ipwho.is/");
        if (j.find("\"success\":true") == std::string::npos && j.find("\"success\": true") == std::string::npos) return false;
        if (!wsScalar(j, "latitude", lat) || !wsScalar(j, "longitude", lon)) return false;
        wsScalar(j, "city", city);
    }
    const std::string j = wsCurl("https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon +
        "&current=temperature_2m,apparent_temperature,weather_code,is_day,relative_humidity_2m,wind_speed_10m&hourly=temperature_2m,weather_code,is_day" +
        "&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&forecast_days=7&timezone=auto" + std::string(unit == "fahrenheit" ? "&temperature_unit=fahrenheit" : ""));
    const std::string cur = wsObject(j, "current"), hourly = wsObject(j, "hourly"), daily = wsObject(j, "daily");
    std::string time, t, ft, code, day;
    if (cur.empty() || !wsScalar(cur, "time", time) || !wsScalar(cur, "temperature_2m", t) || !wsScalar(cur, "weather_code", code) || !wsScalar(cur, "is_day", day)) return false;
    WeatherData w;
    w.ok = true;
    w.city = city;
    w.unit = unit;
    w.temp = std::atof(t.c_str());
    w.feels = wsScalar(cur, "apparent_temperature", ft) ? std::atof(ft.c_str()) : w.temp;
    w.code = std::atoi(code.c_str());
    w.day = std::atoi(day.c_str()) != 0;
    const auto mx = wsArray(daily, "temperature_2m_max"), mn = wsArray(daily, "temperature_2m_min");
    w.hi = mx.empty() ? w.temp : std::atof(mx[0].c_str());
    w.lo = mn.empty() ? w.temp : std::atof(mn[0].c_str());
    w.hours[0] = {-1, w.temp, w.code, w.day};
    const auto ht = wsArray(hourly, "time"), hv = wsArray(hourly, "temperature_2m"), hc = wsArray(hourly, "weather_code"), hd = wsArray(hourly, "is_day");
    size_t idx = 0;
    for (size_t i = 0; i < ht.size(); i++) if (ht[i].compare(0, 13, time, 0, 13) == 0) { idx = i; break; }
    for (int k = 1; k <= 2; k++) {
        const size_t i = idx + k;
        if (i >= ht.size() || i >= hv.size() || i >= hc.size() || i >= hd.size()) { w.hours[k] = {-1, w.temp, w.code, w.day}; continue; }
        w.hours[k] = {std::atoi(ht[i].c_str() + 11), std::atof(hv[i].c_str()), std::atoi(hc[i].c_str()), std::atoi(hd[i].c_str()) != 0};
    }
    w.hourly[0] = {-1, w.temp, w.code, w.day};
    for (int k = 1; k < 5; k++) {
        const size_t i = idx + k;
        if (i >= ht.size() || i >= hv.size() || i >= hc.size() || i >= hd.size()) { w.hourly[k] = w.hourly[k - 1]; continue; }
        w.hourly[k] = {std::atoi(ht[i].c_str() + 11), std::atof(hv[i].c_str()), std::atoi(hc[i].c_str()), std::atoi(hd[i].c_str()) != 0};
    }
    std::string hum, wnd;
    w.humidity = wsScalar(cur, "relative_humidity_2m", hum) ? std::atoi(hum.c_str()) : -1;
    w.wind = wsScalar(cur, "wind_speed_10m", wnd) ? std::atof(wnd.c_str()) : 0;
    const auto minutesOf = [](const std::vector<std::string>& v) { return v.empty() || v[0].size() < 16 ? -1 : std::atoi(v[0].c_str() + 11) * 60 + std::atoi(v[0].c_str() + 14); };
    w.sunrise = minutesOf(wsArray(daily, "sunrise"));
    w.sunset = minutesOf(wsArray(daily, "sunset"));
    const auto dt = wsArray(daily, "time"), dc = wsArray(daily, "weather_code");
    w.nDays = 0;
    for (size_t i = 0; i < dt.size() && i < mx.size() && i < mn.size() && i < dc.size() && w.nDays < 7; i++) {
        if (dt[i].size() < 10) continue;
        w.days[w.nDays++] = {std::atoi(dt[i].c_str()), std::atoi(dt[i].c_str() + 5) - 1, std::atoi(dt[i].c_str() + 8), std::atof(mx[i].c_str()), std::atof(mn[i].c_str()), std::atoi(dc[i].c_str())};
    }
    w.fetched = std::time(nullptr);
    wsSaveCache(w);
    {
        std::lock_guard<std::mutex> lk(g_wsMx);
        g_wsData = w;
    }
    g_wsVersion++;
    if (g_weatherWrite >= 0) { const char b = 'w'; (void)!write(g_weatherWrite, &b, 1); }
    return true;
}

void wsLoop() {
    int failures = 0;
    for (;;) {
        if (g_wsWanted.load()) {
            std::string city, unit;
            bool need;
            int interval;
            {
                std::lock_guard<std::mutex> lk(g_wsMx);
                city = g_wsCity;
                unit = g_wsUnit;
                interval = g_wsInterval;
                need = !g_wsData.ok || g_wsData.unit != unit || std::time(nullptr) - g_wsData.fetched >= interval * 60;
            }
            if (g_wsRefresh.exchange(false) || g_wsForce.exchange(false)) need = true;
            if (need) {
                if (wsFetch(city, unit)) failures = 0;
                else failures++;
            }
        }
        const int wait = failures ? std::min(60, 5 * failures) : 20;
        for (int i = 0; i < wait; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_wsRefresh.load() || g_wsForce.load()) break;
        }
    }
}

void weatherSvcConfigure(const App& app) {
    std::lock_guard<std::mutex> lk(g_wsMx);
    const bool changed = g_wsCity != app.cfg.weatherCity || g_wsUnit != app.cfg.weatherUnit;
    g_wsCity = app.cfg.weatherCity;
    g_wsUnit = app.cfg.weatherUnit;
    g_wsInterval = std::clamp(app.cfg.weatherInterval, 5, 120);
    if (changed) g_wsRefresh = true;
}

void weatherSvcWant(App& app, bool want) {
    want = want || (app.clockP.open() && app.clockP.kind == 1);
    if (want && !g_wsStarted) {
        g_wsStarted = true;
        weatherSvcConfigure(app);
        {
            std::lock_guard<std::mutex> lk(g_wsMx);
            WeatherData cached;
            if (wsLoadCache(cached) && cached.unit == g_wsUnit) g_wsData = cached;
        }
        g_wsVersion++;
        g_wsRefresh = false;
        std::thread(wsLoop).detach();
    }
    g_wsWanted = want;
}

void weatherSvcRefreshNow() { g_wsForce = true; }

bool weatherSvcGet(WeatherData& out, uint64_t* version) {
    std::lock_guard<std::mutex> lk(g_wsMx);
    if (version) *version = g_wsVersion.load();
    out = g_wsData;
    return g_wsData.ok;
}
