// The slip of the Score page: the picks the user collects (from the Picks view or the detail of a match), the chance and odds of all of them
// together, a parlay suggested from the model, and a booking code reserved on the betting site (nothing is bet). It talks to
// scripts/score/altenar.py. Included INSIDE the body of struct ScorePage, like page_score_views.inc.

    static std::string jsonStr(const std::string& s) {
        std::string o = "\"";
        for (const unsigned char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += static_cast<char>(c);
        }
        return o + "\"";
    }
    static std::string configText(const char* key) {
        std::string v;
        return jsonValue(readFile(configPath()), key, v) ? v : "";
    }
    // The model's names of the markets -> the names the site's script knows.
    static std::string siteKey(const std::string& k) {
        if (k == "btts") return "btts_y";
        if (k == "dc1x") return "1x";
        if (k == "dcx2") return "x2";
        if (k == "dc12") return "12";
        return k;
    }
    static std::string isoUtc(time_t t) {
        struct tm u{};
        gmtime_r(&t, &u);
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &u);
        return b;
    }
    ScLeg legFor(const ScMatch& m, const std::string& key, const std::string& label, double prob, double price, bool real) const {
        const bool model = price <= 0;  // no site odds: the model's fair price stands in (marked with *)
        return {m.id, siteKey(key), label, m.home.name, m.away.name, m.league, isoUtc(m.kickoff), m.kickoff, prob, model ? 1 / std::max(0.05, prob) : price, model ? false : real};
    }
    // A button that adds (or takes off) a leg; the leg is kept for the click.
    bool hotLeg(const Rect& r, const ScLeg& leg) {
        legTargets.push_back(leg);
        return hot(r, LegToggle, static_cast<int>(legTargets.size()) - 1);
    }
    int legIndex(const std::string& matchId, const std::string& key) const {
        for (size_t i = 0; i < slip.size(); i++) if (slip[i].matchId == matchId && (key.empty() || slip[i].key == key)) return static_cast<int>(i);
        return -1;
    }
    bool inSlip(const ScLeg& leg) const { return legIndex(leg.matchId, leg.key) >= 0; }
    void toggleLeg(const ScLeg& leg) {
        const int same = legIndex(leg.matchId, leg.key);
        if (same >= 0) { slip.erase(slip.begin() + same); }
        else {
            const int other = legIndex(leg.matchId, "");  // one pick per match: it replaces the earlier one
            if (other >= 0) slip.erase(slip.begin() + other);
            slip.push_back(leg);
        }
        slipCode.clear();
        slipError.clear();
    }
    double slipChance() const { double p = 1; for (const ScLeg& l : slip) p *= std::max(0.0001, l.prob); return slip.empty() ? 0 : p; }
    double slipOdds(bool& modelOnly) const {
        double o = 1;
        modelOnly = false;
        for (const ScLeg& l : slip) {
            if (l.price > 0) { o *= l.price; if (!l.real) modelOnly = true; }
            else { o *= 1 / std::max(0.01, l.prob); modelOnly = true; }
        }
        return slip.empty() ? 0 : o;
    }

    // ---- the jobs: 1 = refresh the odds, 2 = suggest a parlay, 3 = reserve a code --------------------------------------------------
    std::string legsJson(bool withStake) const {
        std::string j = withStake ? "{\"stake\":1,\"legs\":[" : "{\"legs\":[";
        for (size_t i = 0; i < slip.size(); i++) {
            const ScLeg& l = slip[i];
            j += (i ? "," : "") + std::string("{\"matchId\":") + jsonStr(l.matchId) + ",\"key\":" + jsonStr(l.key) + ",\"kickoff\":" + jsonStr(l.kickoff) + ",\"home\":" + jsonStr(l.home) + ",\"away\":" + jsonStr(l.away) + "}";
        }
        return j + "]}";
    }
    void startSlipJob(int which) {
        if (sStage != 0) return;
        const std::string provider = configText("scoreOdds");
        std::vector<std::string> argv{"python3", "-I", projectDir() + "/scripts/score/altenar.py"};
        if (which == 2) {
            std::string ids = "[";
            bool first = true;
            for (const ScMatch& m : matches) if (!m.done() && !m.live() && m.pHome >= 0 && (slipDay == 0 || m.dayKey == slipDay)) { ids += (first ? "" : ",") + jsonStr(m.id); first = false; }
            argv.insert(argv.end(), {"parlay", "{\"matchIds\":" + ids + "],\"mode\":\"legs\",\"value\":" + std::to_string(slipLegs) + ",\"kinds\":[\"result\",\"goals\",\"corners\"]}"});
        } else {
            if (slip.empty()) return;
            if (provider.empty()) {  // (the odds come from a site: say where to set it instead of doing nothing)
                if (which == 3) slipError = scT("A code is reserved on a betting site. Set the Odds provider in Settings, Services, Score first.");
                return;
            }
            argv.insert(argv.end(), {which == 1 ? "prices" : "reserve", legsJson(which == 3)});
        }
        argv.push_back("--tsv");
        setenv("SCORE_ODDS_INTEGRATION", provider.c_str(), 1);
        setenv("SCORE_ODDS_SITE", configText("scoreSite").c_str(), 1);
        slipError.clear();
        if (which == 3) slipCode.clear();
        sStage = sjob.start(argv, "", which == 3 ? 60000 : 120000) ? which : 0;
        if (sStage == 0) slipError = scT("Could not start the script");
        spinStart = Clock::now();
    }
    void finishSlipJob() {
        const int which = sStage;
        sStage = 0;
        std::istringstream in(sjob.out);
        std::string line;
        std::vector<ScLeg> suggested;
        bool any = false;
        while (std::getline(in, line)) {
            const auto f = scSplitTabs(line);
            if (f.empty()) continue;
            if (f[0] == "E" && f.size() > 1) slipError = f[1];
            else if (f[0] == "P" && f.size() >= 3) { const int i = legIndex(f[1], ""); if (i >= 0) { slip[static_cast<size_t>(i)].price = std::atof(f[2].c_str()); slip[static_cast<size_t>(i)].real = true; } }
            else if (f[0] == "C" && f.size() >= 4) { slipCode = f[1]; slipMissing = std::atoi(f[3].c_str()); }
            else if (f[0] == "L" && f.size() >= 11) {
                ScLeg l;
                l.matchId = f[1]; l.key = f[2]; l.prob = std::atof(f[4].c_str()); l.price = f[5].empty() ? 0 : std::atof(f[5].c_str()); l.real = !f[5].empty();
                l.league = f[6]; l.home = f[7]; l.away = f[8]; l.kickoff = f[9]; l.label = f[10]; l.time = scParseIso(f[9]);
                if (l.price <= 0) l.price = 1 / std::max(0.05, l.prob);  // no site odds: the model's fair price, marked with *
                suggested.push_back(std::move(l));
            }
            any = true;
        }
        if (which == 2 && !suggested.empty()) { slip = std::move(suggested); slipCode.clear(); }
        else if (which == 2 && slipError.empty()) slipError = scT("No parlay could be built");
        else if (!any && slipError.empty() && !sjob.ok) slipError = scT("The script failed");
    }

    // ---- the view ---------------------------------------------------------------------------------------------------------------------
    // style: 0 tonal, 1 filled, 2 outline, 3 the design of the Back button.
    bool slipButton(cairo_t* cr, const Rect& r, int id, int arg, const std::string& label, const char* ic, int style, bool enabled, double alpha) {
        const bool filled = style == 1, outline = style == 2;
        const Color bg = g.color(filled ? "primary" : style == 3 ? "surface_container_high" : "surface_container_highest"), fg = g.color(filled ? "on_primary" : "on_surface");
        const bool hov = enabled && hot(r, id, arg);
        if (outline) {
            if (hov) { src(cr, fg, 0.08 * alpha); rrect(cr, r.x, r.y, r.w, r.h, r.h / 2); cairo_fill(cr); }
            src(cr, g.color("outline"), (enabled ? 0.9 : 0.35) * alpha);
            cairo_set_line_width(cr, 1.5);
            rrect(cr, r.x + 0.75, r.y + 0.75, r.w - 1.5, r.h - 1.5, (r.h - 1.5) / 2);
            cairo_stroke(cr);
        } else {
            src(cr, hov ? mix(fg, bg, 0.14) : bg, (enabled ? 1.0 : 0.4) * alpha);
            rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
            cairo_fill(cr);
        }
        double tw = 0, th = 0;
        measure(label, 14, true, 0, tw, th);
        const bool iconOnly = ic && label.empty();
        double x = r.x + (r.w - (iconOnly ? 20 : tw + (ic ? 26 : 0))) / 2;
        if (ic) { ::icon(cr, ic, x, r.y + (r.h - 20) / 2, 20, fg); x += 26; }
        text(cr, label, x, r.y + (r.h - th) / 2, 14, true, fg, 0, (enabled ? 1.0 : 0.5) * alpha);
        return hov;
    }
    static double buttonWidth(const std::string& label, bool withIcon) {
        double tw = 0, th = 0;
        measure(label, 14, true, 0, tw, th);
        return tw + 36 + (withIcon ? 26 : 0);
    }
    void drawSlipHeader(cairo_t* cr, double right) {
        const Color fg = g.color("on_surface");
        const Rect b{0, 0, 112, 44};
        const bool hov = hot(b, BackBtn);
        const Color bg = g.color("surface_container_high");
        src(cr, hov ? mix(fg, bg, 0.14) : bg);
        rrect(cr, b.x, b.y, b.w, b.h, 22);
        cairo_fill(cr);
        ::icon(cr, "alt-arrow-left", b.x + 16, b.y + 11, 22, fg);
        text(cr, scT("Back"), b.x + 46, b.y + 11, 15, true, fg);
        const std::string title = scT("Slip") + "  ·  " + std::to_string(slip.size()) + " " + scT(slip.size() == 1 ? "pick" : "picks");
        fillPill(cr, b.w + 12, title, std::max(100.0, right - b.w - 24), 1);
    }
    void drawSlip(cairo_t* cr, Rect r) {
        const Rect card{0, 58, r.w, cardHeight(r)};
        src(cr, g.color("surface_container"));
        rrect(cr, card.x, card.y, card.w, card.h, 28);
        cairo_fill(cr);
        listArea = {card.x + 12, card.y + 12, card.w - 24, card.h - 24};
        const Color on = g.color("on_surface"), dim = g.color("on_surface_variant");
        const double since = std::chrono::duration<double>(Clock::now() - modeShown).count();
        lastFade = since;
        rPos = std::clamp(rPos, 0.0, rMax);
        cairo_save(cr);
        cairo_rectangle(cr, listArea.x, listArea.y, listArea.w, listArea.h);
        cairo_clip(cr);
        const double W = listArea.w - 10, X = listArea.x;
        double y = listArea.y - (rPos + rBand.over);
        const double top = y;
        const bool busy = sStage != 0, haveProvider = !configText("scoreOdds").empty();
        // The totals.
        {
            bool modelOnly = false;
            const double odds = slipOdds(modelOnly), chance = slipChance();
            src(cr, g.color("primary_container"));
            rrect(cr, X, y, W, 104, 24);
            cairo_fill(cr);
            const Color fg = g.color("on_primary_container");
            const std::string cells[3][2] = {{std::to_string(slip.size()), scT("Picks")}, {slip.empty() ? "–" : pct(chance), scT("Chance of all")}, {slip.empty() ? "–" : oddsText(odds).substr(1) + (modelOnly ? "*" : ""), scT("Total odds")}};
            shapePath(cr, "Cookie9Sided", X + W - 24 - 84, y + 10, 84);  // a Material shape with the slip icon fills the right of the card
            src(cr, fg, 0.16);
            cairo_fill(cr);
            ::icon(cr, "bill-list", X + W - 24 - 84 + 24, y + 10 + 24, 36, fg);
            for (int i = 0; i < 3; i++) {
                const double cx = X + 24 + i * (W - 48 - 130) / 3;
                text(cr, cells[i][0], cx, y + 18, 34, true, fg);
                text(cr, cells[i][1], cx, y + 66, 14, false, fg, 0, 0.75);
            }
            y += 104 + 12;
        }
        // The controls.
        {
            const Rect minus{X, y, 48, 48}, plus{X + 48 + 44, y, 48, 48};  // outlined, as tall as the buttons beside them
            slipButton(cr, minus, SlipMinus, 0, "", "minus-circle", 3, !busy && slipLegs > 2, 1);
            slipButton(cr, plus, SlipPlus, 0, "", "add-circle", 3, !busy && slipLegs < 10, 1);
            const std::string n = std::to_string(slipLegs);
            double tw = 0, th = 0;
            measure(n, 20, true, 0, tw, th);
            text(cr, n, X + 48 + 22 - tw / 2, y + 24 - th / 2, 20, true, on);
            double x = X + 48 + 44 + 48 + 14;
            const std::string suggest = scT("Suggest parlay"), code = scT("Create code"), clear = scT("Clear");
            const Rect s{x, y, buttonWidth(suggest, true), 48};
            slipButton(cr, s, SlipSuggest, 0, suggest, "magic-stick", 1, !busy, 1);
            x += s.w + 8;
            const Rect c{x, y, buttonWidth(code, true), 48};
            slipButton(cr, c, SlipCode, 0, code, "bill-check", 2, !busy && !slip.empty(), 1);
            x += c.w + 8;
            const Rect k{x, y, buttonWidth(clear, true), 48};
            slipButton(cr, k, SlipClear, 0, clear, "close-circle", 2, !busy && !slip.empty(), 1);
            const std::string hint = busy ? scT("Working…") : !haveProvider ? scT("Set the odds provider in Settings to get a code") : "";
            if (!hint.empty()) {
                measure(hint, 13, false, 0, tw, th);
                text(cr, hint, X + W - tw - 6, y + 24 - th / 2, 13, false, dim, 0, 0.9);
            }
            y += 48 + 12;
        }
        // The day the suggestion is built from.
        if (!days.empty()) {
            const double chipH = 36;
            double x = X;
            text(cr, scT("Parlay from"), x + 4, y + (chipH - 15) / 2, 13, false, dim);
            double lw = 0, lh = 0;
            measure(scT("Parlay from"), 13, false, 0, lw, lh);
            x += lw + 16;
            std::vector<int> options{0};
            options.insert(options.end(), days.begin(), days.end());
            for (const int d : options) {
                const std::string name = d == 0 ? scT("Any day") : dayLabel(d);
                double tw = 0, th = 0;
                measure(name, 14, true, 0, tw, th);
                const Rect c{x, y, tw + 28, chipH};
                if (c.x + c.w > X + W) break;
                const bool on = slipDay == d, hov = hot(c, SlipDay, d);
                const Color bg = on ? g.color("secondary_container") : g.color("surface_container_high"), fg = on ? g.color("on_secondary_container") : g.color("on_surface");
                src(cr, hov ? mix(fg, bg, 0.14) : bg);
                rrect(cr, c.x, c.y, c.w, c.h, 18);
                cairo_fill(cr);
                text(cr, name, c.x + 14, c.y + (c.h - th) / 2, 14, true, fg);
                x += c.w + 6;
            }
            y += chipH + 12;
        }
        // The booking code, or what went wrong.
        if (!slipCode.empty()) {
            src(cr, g.color("secondary_container"));
            rrect(cr, X, y, W, 112, 24);
            cairo_fill(cr);
            const Color fg = g.color("on_secondary_container");
            text(cr, scT("Booking code"), X + 24, y + 20, 13, false, fg, 0, 0.8);
            text(cr, slipCode, X + 24, y + 42, 30, true, fg);
            const bool copied = copiedUntil > Clock::now();
            const std::string cp = scT(copied ? "Copied" : "Copy");
            const double bw = std::max(buttonWidth(scT("Copy"), true), buttonWidth(scT("Copied"), true));
            const Rect b{X + W - 20 - bw, y + 20, bw, 48};  // the same 20 px from the top and from the right of the card
            slipButton(cr, b, SlipCopy, 0, cp, copied ? "check-circle" : "copy", 1, true, 1);
            const std::string note = scT("Reserved on the site. Nothing is bet.") + (slipMissing > 0 ? "  " + std::to_string(slipMissing) + " " + scT("picks are not on the site") : "");
            double tw = 0, th = 0;
            measure(note, 12, false, 0, tw, th);
            text(cr, note, X + W - 20 - std::min(tw, W - 300), y + 82, 12, false, fg, 0, 0.7);
            y += 112 + 12;
        } else if (!slipError.empty()) {
            src(cr, g.color("error_container"));
            rrect(cr, X, y, W, 56, 20);
            cairo_fill(cr);
            trLine(cr, slipError, X + 20, y + 18, W - 40, 14, true, g.color("on_error_container"));
            y += 56 + 12;
        }
        // The picks.
        if (slip.empty()) {
            const std::string msg = scT("The slip is empty. Add picks with the + buttons, or suggest a parlay.");
            double tw = 0, th = 0;
            measure(msg, 15, false, 0, tw, th);
            text(cr, msg, X + (W - tw) / 2, y + 40, 15, false, dim, 0, 0.85);
            y += 100;
        }
        constexpr double rowH = 72, gap = 6;
        for (size_t i = 0; i < slip.size(); i++) {
            const ScLeg& l = slip[i];
            const double ry = y + i * (rowH + gap);
            if (ry + rowH < listArea.y || ry > listArea.y + listArea.h) continue;
            const double a = std::clamp((since - 0.04 * std::min<size_t>(i, 8)) / 0.2, 0.0, 1.0), al = 1 - (1 - a) * (1 - a) * (1 - a);
            const Rect row{X, ry + (1 - al) * 10, W, rowH};
            src(cr, g.color("surface_container_high"), al);
            rrect(cr, row.x, row.y, row.w, row.h, 20);
            cairo_fill(cr);
            struct tm lt{};
            localtime_r(&l.time, &lt);
            char tb[32];
            std::strftime(tb, sizeof tb, "%a %d · %H:%M", &lt);
            text(cr, l.league + "  ·  " + tb, row.x + 20, row.y + 12, 12, false, dim, 0, al);
            trLine(cr, l.home + "  –  " + l.away, row.x + 20, row.y + 30, row.w - 330, 16, true, on, al);
            const std::string pick = scT(l.label.c_str());
            double tw = 0, th = 0;
            measure(pick, 13, true, 0, tw, th);
            const double right = row.x + row.w - 20;
            const Rect rm{right - 36, row.y + (rowH - 36) / 2, 36, 36};
            const bool hov = hot(rm, SlipRemove, static_cast<int>(i));
            src(cr, hov ? g.color("error_container") : g.color("surface_container_highest"), al);
            rrect(cr, rm.x, rm.y, rm.w, rm.h, 18);
            cairo_fill(cr);
            ::icon(cr, "close-circle", rm.x + 8, rm.y + 8, 20, g.color(hov ? "on_error_container" : "on_surface_variant"), false);
            const std::string odds = l.price > 0 ? oddsText(l.price) + (l.real ? "" : "*") : "–";
            measure(odds, 16, true, 0, tw, th);
            text(cr, odds, rm.x - 18 - tw, row.y + 14, 16, true, on, 0, al);
            text(cr, pct(l.prob), rm.x - 18 - tw, row.y + 40, 13, false, dim, 0, al);
            double pw = 0;
            measure(pick, 13, true, 0, pw, th);
            const double chipW = std::min(pw + 28, 260.0), chipX = rm.x - 18 - tw - 24 - chipW;
            src(cr, g.color("secondary_container"), al);
            rrect(cr, chipX, row.y + (rowH - 32) / 2, chipW, 32, 16);
            cairo_fill(cr);
            trLine(cr, pick, chipX + 14, row.y + (rowH - th) / 2, chipW - 28, 13, true, g.color("on_secondary_container"), al);
        }
        y += slip.size() * (rowH + gap);
        const double contentH = y - gap - top;
        rMax = std::max(0.0, contentH - listArea.h);
        cairo_restore(cr);
        if (rMax > 0 && rBand.thumbAlpha() > 0) {
            const double th = std::max(36.0, listArea.h * listArea.h / contentH), ty = listArea.y + (listArea.h - th) * (rPos / rMax);
            src(cr, g.color("on_surface_variant"), 0.35 * rBand.thumbAlpha());
            rrect(cr, listArea.x + listArea.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    // What a click on the slip view does.
    bool slipAct(int id, int arg) {
        switch (id) {
            case SlipMinus: slipLegs = std::max(2, slipLegs - 1); return true;
            case SlipPlus: slipLegs = std::min(10, slipLegs + 1); return true;
            case SlipDay: slipDay = arg; return true;
            case SlipSuggest: startSlipJob(2); return true;
            case SlipCode: startSlipJob(3); return true;
            case SlipClear: slip.clear(); slipCode.clear(); slipError.clear(); return true;
            case SlipRemove: if (arg >= 0 && arg < static_cast<int>(slip.size())) { slip.erase(slip.begin() + arg); slipCode.clear(); } return true;
            case SlipCopy:
                phSpawnDetached({"wl-copy", "--", slipCode});  // (detached: wl-copy stays alive to serve the clipboard)
                copiedUntil = Clock::now() + std::chrono::milliseconds(1600);
                return true;
            default: return false;
        }
    }

