// Timers, a stopwatch and reminders (a time of the day, once or repeated). They live in the shell, so they keep running and ring with
// Tools closed; the state is written to timers.txt (in the state folder, absolute times in epoch ms) and the Tools page reads it to show them live.
struct TimerItem { int id = 0; std::string label; double total = 0, remaining = 0; bool running = false, done = false; long long endAt = 0; };
struct ReminderItem { int id = 0; std::string label; int hour = 8, minute = 0, repeat = 0, date = 0; bool enabled = true; long long lastFired = 0; };  // repeat: 0 once, 1 every day, 2 weekdays; date: yyyymmdd of a one-off day, 0 for none
struct TimersState {
    std::vector<TimerItem> timers;
    std::vector<ReminderItem> reminders;
    bool swRunning = false;
    double swAccum = 0;
    long long swStart = 0;
    std::vector<double> swLaps;
    int nextId = 1;
    bool loaded = false;
};
TimersState g_timers;

long long epochMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string timersPath() { return stateDir() + "/timers.txt"; }
std::string timersClean(std::string s) {
    for (char& c : s) if (c == '|' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s.substr(0, 240);
}

void timersSave() {
    std::filesystem::create_directories(stateDir());
    std::ofstream out(timersPath() + ".tmp", std::ios::trunc);
    const TimersState& T = g_timers;
    for (const TimerItem& t : T.timers) out << "T|" << t.id << '|' << t.label << '|' << t.total << '|' << t.remaining << '|' << t.running << '|' << t.done << '|' << t.endAt << '\n';
    for (const ReminderItem& r : T.reminders) out << "R|" << r.id << '|' << r.label << '|' << r.hour << '|' << r.minute << '|' << r.repeat << '|' << r.enabled << '|' << r.lastFired << '|' << r.date << '\n';
    out << "S|" << T.swRunning << '|' << T.swAccum << '|' << T.swStart << '|';
    for (size_t i = 0; i < T.swLaps.size(); i++) out << (i ? "," : "") << T.swLaps[i];
    out << '\n';
    out << "N|" << T.nextId << '\n';
    out.close();
    std::filesystem::rename(timersPath() + ".tmp", timersPath());
}

void timersLoad() {
    TimersState T;
    std::ifstream in(timersPath());
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '|');) f.push_back(part);
        if (f.empty()) continue;
        if (f[0] == "T" && f.size() >= 8) {
            TimerItem t;
            t.id = std::atoi(f[1].c_str()); t.label = f[2]; t.total = std::atof(f[3].c_str()); t.remaining = std::atof(f[4].c_str());
            t.running = f[5] == "1"; t.done = f[6] == "1"; t.endAt = std::atoll(f[7].c_str());
            T.timers.push_back(t);
        } else if (f[0] == "R" && f.size() >= 8) {
            ReminderItem r;
            r.id = std::atoi(f[1].c_str()); r.label = f[2]; r.hour = std::atoi(f[3].c_str()); r.minute = std::atoi(f[4].c_str());
            r.repeat = std::atoi(f[5].c_str()); r.enabled = f[6] == "1"; r.lastFired = std::atoll(f[7].c_str()); r.date = f.size() > 8 ? std::atoi(f[8].c_str()) : 0;
            T.reminders.push_back(r);
        } else if (f[0] == "S" && f.size() >= 4) {
            T.swRunning = f[1] == "1"; T.swAccum = std::atof(f[2].c_str()); T.swStart = std::atoll(f[3].c_str());
            if (f.size() > 4) { std::stringstream ls(f[4]); for (std::string lap; std::getline(ls, lap, ',');) if (!lap.empty()) T.swLaps.push_back(std::atof(lap.c_str())); }
        } else if (f[0] == "N" && f.size() >= 2) T.nextId = std::max(1, std::atoi(f[1].c_str()));
    }
    T.loaded = true;
    g_timers = std::move(T);
}

// "90" (seconds), "5m", "1h30m", "45s", "25:00" (minutes:seconds), "1:30:00"
bool parseDuration(const std::string& raw, double& out) {
    std::string s = raw;
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    if (s.empty()) return false;
    double total = 0;
    if (s.find(':') != std::string::npos) {
        std::vector<double> parts;
        std::stringstream ss(s);
        for (std::string p; std::getline(ss, p, ':');) {
            if (p.empty() || p.find_first_not_of("0123456789") != std::string::npos) return false;
            parts.push_back(std::atof(p.c_str()));
        }
        if (parts.size() == 2) total = parts[0] * 60 + parts[1];
        else if (parts.size() == 3) total = parts[0] * 3600 + parts[1] * 60 + parts[2];
        else return false;
    } else {
        double num = 0;
        bool have = false, any = false;
        for (const char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) { num = num * 10 + (c - '0'); have = true; continue; }
            if (!have) return false;
            const char u = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (u == 'h') total += num * 3600; else if (u == 'm') total += num * 60; else if (u == 's') total += num; else return false;
            num = 0; have = false; any = true;
        }
        if (have) total += num;  // a bare number is seconds
    }
    if (total < 1 || total > 99 * 3600) return false;
    out = total;
    return true;
}

double timerLeft(const TimerItem& t) { return t.running ? std::max(0.0, (t.endAt - epochMs()) / 1000.0) : t.remaining; }
double swElapsed(const TimersState& T) { return T.swAccum + (T.swRunning ? (epochMs() - T.swStart) / 1000.0 : 0); }

std::string clockText(double secs) {
    const long s = static_cast<long>(std::floor(secs + 0.0001));
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(b, sizeof b, "%02ld:%02ld", s / 60, s % 60);
    return b;
}

// The bell of a timer or a reminder: a notification that stays until you answer it (Snooze 5 min or Dismiss, or the cross), and the sound,
// which repeats for about 20 seconds or until you answer. The notification is the island's when the island is on.
void alarmStop() { runDetached({"pkill", "-f", "end4pC-alarm-loop"}); }
std::string timersCommand(App& app, const std::string& what, const std::string& arg);

void alarmAction(App& app, const Notif& n, const std::string& key) {
    alarmStop();
    if (key != "snooze") return;
    const std::string label = n.summary == "Timer" || n.summary == "Reminder" ? "" : n.summary;
    if (n.body.compare(0, 8, "Reminder") == 0) {
        std::time_t later = std::time(nullptr) + 300;
        std::tm tm{};
        localtime_r(&later, &tm);
        char when[32];
        std::snprintf(when, sizeof when, "%02d:%02d %04d-%02d-%02d ", tm.tm_hour, tm.tm_min, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        timersCommand(app, "reminder", std::string("add ") + when + label);
    } else {
        timersCommand(app, "timer", "add 5m " + label);
    }
}

void timersRing(App& app, const std::string& title, const std::string& body) {
    alarmStop();
    const std::string snooze = i18n::tr("Snooze 5 min"), dismiss = i18n::tr("Dismiss");
    const NotifyAction actions[2] = {{"snooze", snooze.c_str()}, {"dismiss", dismiss.c_str()}};
    NotifyRequest r{};
    r.app = "end4pC";
    r.desktopEntry = "end4pC-alarm";  // (what marks the notification as the alarm of a timer or a reminder)
    r.icon = "alarm-symbolic";
    r.summary = title.c_str();
    r.body = body.c_str();
    r.nactions = 2;
    r.actions = actions;
    r.urgency = 2;
    onNotifyRequest(nullptr, &r);
    runDetached({"bash", "-c", "# end4pC-alarm-loop\nf=/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga; for i in $(seq 1 10); do paplay \"$f\" 2>/dev/null || canberra-gtk-play -i alarm-clock-elapsed 2>/dev/null || pw-play \"$f\" 2>/dev/null; sleep 1; done"});
}

void timersCheck(App& app) {
    TimersState& T = g_timers;
    const long long now = epochMs();
    bool changed = false;
    for (TimerItem& t : T.timers) {
        if (!t.running || now < t.endAt) continue;
        t.running = false; t.done = true; t.remaining = 0; changed = true;
        timersRing(app, t.label.empty() ? "Timer" : t.label, "Time is up (" + clockText(t.total) + ")");
    }
    std::time_t tt = static_cast<std::time_t>(now / 1000);
    std::tm tm{};
    localtime_r(&tt, &tm);
    const long long stamp = (tm.tm_year + 1900) * 100000000LL + (tm.tm_mon + 1) * 1000000LL + tm.tm_mday * 10000LL + tm.tm_hour * 100 + tm.tm_min;
    for (ReminderItem& r : T.reminders) {
        if (!r.enabled || r.hour != tm.tm_hour || r.minute != tm.tm_min || r.lastFired == stamp) continue;
        if (r.repeat == 2 && (tm.tm_wday == 0 || tm.tm_wday == 6)) continue;
        if (r.date && r.date != (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday) continue;
        r.lastFired = stamp;
        if (r.repeat == 0) r.enabled = false;
        changed = true;
        char at[8];
        std::snprintf(at, sizeof at, "%02d:%02d", r.hour, r.minute);
        timersRing(app, r.label.empty() ? "Reminder" : r.label, std::string("Reminder \xC2\xB7 ") + at);
    }
    if (changed) timersSave();
}

// When something is next due (a timer's end, the pomodoro's, the next minute while a reminder is on).
std::chrono::steady_clock::time_point timersWake(const App&) {
    const TimersState& T = g_timers;
    long long due = std::numeric_limits<long long>::max();
    for (const TimerItem& t : T.timers) if (t.running) due = std::min(due, t.endAt);
    const long long now = epochMs();
    for (const ReminderItem& r : T.reminders) if (r.enabled) { due = std::min(due, (now / 60000 + 1) * 60000); break; }
    if (due == std::numeric_limits<long long>::max()) return std::chrono::steady_clock::time_point::max();
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0LL, due - now) + 5);
}

std::string timersCommand(App& app, const std::string& what, const std::string& arg) {
    if (!g_timers.loaded) timersLoad();
    TimersState& T = g_timers;
    std::istringstream in(arg);
    std::string verb;
    in >> verb;
    std::string rest;
    std::getline(in, rest);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    auto byId = [&](auto& list, const std::string& idText) {
        const int id = std::atoi(idText.c_str());
        for (auto it = list.begin(); it != list.end(); ++it) if (it->id == id) return it;
        return list.end();
    };
    std::string reply = "ok";
    if (what == "timer") {
        if (verb == "add") {
            std::istringstream r(rest);
            std::string dur;
            r >> dur;
            double secs = 0;
            if (!parseDuration(dur, secs)) return "usage: timer add <5m|1h30m|90|25:00> [label]";
            std::string label;
            std::getline(r, label);
            while (!label.empty() && label.front() == ' ') label.erase(label.begin());
            TimerItem t;
            t.id = T.nextId++; t.label = timersClean(label); t.total = t.remaining = secs; t.running = true; t.endAt = epochMs() + static_cast<long long>(secs * 1000);
            T.timers.push_back(t);
            reply = std::to_string(t.id);
        } else if (verb == "pause" || verb == "resume" || verb == "reset" || verb == "remove") {
            const auto it = byId(T.timers, rest);
            if (it == T.timers.end()) return "no such timer";
            if (verb == "pause" && it->running) { it->remaining = timerLeft(*it); it->running = false; }
            else if (verb == "resume" && !it->running && !it->done) { it->running = true; it->endAt = epochMs() + static_cast<long long>(it->remaining * 1000); }
            else if (verb == "reset") { it->done = false; it->running = true; it->remaining = it->total; it->endAt = epochMs() + static_cast<long long>(it->total * 1000); }
            else if (verb == "remove") T.timers.erase(it);
        } else if (verb == "clear") {
            T.timers.erase(std::remove_if(T.timers.begin(), T.timers.end(), [](const TimerItem& t) { return t.done; }), T.timers.end());
        } else if (verb == "list" || verb.empty()) {
            reply.clear();
            for (const TimerItem& t : T.timers) reply += std::to_string(t.id) + " " + (t.done ? "done" : t.running ? "running" : "paused") + " " + clockText(timerLeft(t)) + " " + t.label + "\n";
            if (reply.empty()) reply = "no timers";
            return reply;
        } else return "usage: timer add|pause|resume|reset|remove|clear|list";
    } else if (what == "stopwatch") {
        if (verb == "start" && !T.swRunning) { T.swRunning = true; T.swStart = epochMs(); }
        else if (verb == "pause" && T.swRunning) { T.swAccum = swElapsed(T); T.swRunning = false; }
        else if (verb == "reset") { T.swRunning = false; T.swAccum = 0; T.swLaps.clear(); }
        else if (verb == "lap" && (T.swRunning || T.swAccum > 0)) T.swLaps.push_back(swElapsed(T));
        else if (verb == "get" || verb.empty()) return clockText(swElapsed(T)) + (T.swRunning ? " running" : " stopped");
        else if (verb != "start" && verb != "pause") return "usage: stopwatch start|pause|reset|lap|get";
    } else if (what == "reminder") {
        if (verb == "add") {
            std::istringstream r(rest);
            std::string hm, rep;
            r >> hm;
            int h = 0, m = 0;
            if (std::sscanf(hm.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return "usage: reminder add HH:MM [once|daily|weekdays|YYYY-MM-DD] [label]";
            const std::streampos before = r.tellg();
            r >> rep;
            int repeat = 0, date = 0, yy = 0, mo = 0, dd = 0;
            if (rep == "daily") repeat = 1; else if (rep == "weekdays") repeat = 2;
            else if (std::sscanf(rep.c_str(), "%d-%d-%d", &yy, &mo, &dd) == 3 && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31) date = yy * 10000 + mo * 100 + dd;  // a day: YYYY-MM-DD
            else if (rep != "once") r.seekg(before);
            std::string label;
            std::getline(r, label);
            while (!label.empty() && label.front() == ' ') label.erase(label.begin());
            ReminderItem x;
            x.id = T.nextId++; x.label = timersClean(label); x.hour = h; x.minute = m; x.repeat = repeat; x.date = date; x.enabled = true;
            {  // set for the minute it already is: it is meant for the next time the clock says it, not for now
                const std::time_t nowT = std::time(nullptr);
                std::tm tn{};
                localtime_r(&nowT, &tn);
                if (!x.date && x.hour == tn.tm_hour && x.minute == tn.tm_min)
                    x.lastFired = (tn.tm_year + 1900) * 100000000LL + (tn.tm_mon + 1) * 1000000LL + tn.tm_mday * 10000LL + tn.tm_hour * 100 + tn.tm_min;
            }
            T.reminders.push_back(x);
            reply = std::to_string(x.id);
        } else if (verb == "remove" || verb == "toggle") {
            const auto it = byId(T.reminders, rest);
            if (it == T.reminders.end()) return "no such reminder";
            if (verb == "remove") T.reminders.erase(it); else { it->enabled = !it->enabled; it->lastFired = 0; }
        } else if (verb == "list" || verb.empty()) {
            reply.clear();
            for (const ReminderItem& r : T.reminders) {
                char b[16];
                std::snprintf(b, sizeof b, "%02d:%02d", r.hour, r.minute);
                reply += std::to_string(r.id) + " " + b + " " + (r.date ? std::to_string(r.date) : r.repeat == 1 ? "daily" : r.repeat == 2 ? "weekdays" : "once") + (r.enabled ? " on " : " off ") + r.label + "\n";
            }
            return reply.empty() ? "no reminders" : reply;
        } else return "usage: reminder add|remove|toggle|list";
    }
    timersSave();
    return reply;
}
