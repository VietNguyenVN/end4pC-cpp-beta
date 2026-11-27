// The Picks and Record views of the Score page. Included INSIDE the body of struct ScorePage (src/tools/page_score.inc): it only holds the
// members that draw and load those two views, so the page file stays under the size limit. Nothing here is a separate class.

    // ---- the header: the mode pills, the refresh button, and the state of the data ----------------------------------------------
    void drawModePill(cairo_t* cr, double right, double y, int m, const char* label, const char* ic, double& left) {
        const std::string name = scT(label);
        double tw = 0, th = 0;
        measure(name, 15, true, 0, tw, th);
        const double w = 28 + 26 + tw;
        const Rect r{right - w, y, w, 44};
        const bool on = mode == m, hov = hot(r, ModeBtn, m);
        const Color bg = on ? g.color("secondary_container") : g.color("surface_container_high"), fg = on ? g.color("on_secondary_container") : g.color("on_surface");
        src(cr, hov ? mix(fg, bg, 0.14) : bg);
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        cairo_fill(cr);
        ::icon(cr, ic, r.x + 16, r.y + 13, 18, fg);
        text(cr, name, r.x + 42, r.y + (r.h - th) / 2, 15, true, fg);
        left = r.x;
    }
    double drawModePills(cairo_t* cr, double right) {
        double left = right;
        drawModePill(cr, right, 0, Record, "Record", "graph-up", left);
        drawModePill(cr, left - 8, 0, Picks, "Picks", "star", left);
        drawModePill(cr, left - 8, 0, Slip, "Slip", "bill-list", left);
        return left;
    }
    void setMode(int m) {
        mode = mode == m ? Matches : m;
        listPos = 0;
        band.reset();
        rPos = 0;
        rBand.reset();
        dayShown = modeShown = Clock::now();
        if (mode == Record) startRecord();
        if (mode == Slip) startSlipJob(1);
    }

    void startRecord() {
        if (rLoading) return;
        rLoading = rjob.start({"python3", "-I", scriptPath(), "--record"}, "", 180000);
    }
    void finishRecord() {
        rLoading = false;
        if (!rjob.ok) return;
        record = scParseRecord(rjob.out);
        recordLoaded = true;
    }

    // ---- Picks: for each match still to come, the most probable option and the one that pays best ---------------------------
    static std::string oddsText(double price) {
        char b[16];
        std::snprintf(b, sizeof b, "@%.2f", price);
        return b;
    }
    void pickChip(cairo_t* cr, double x, double y, double maxW, const std::string& caption, const std::string& label, double prob, double price, bool real, const ScLeg& leg, const char* bgRole, const char* fgRole, double alpha) {
        text(cr, caption, x + 4, y, 12, false, g.color("on_surface_variant"), 0, alpha);
        const std::string t = scT(label.c_str()) + "  ·  " + pct(prob) + (price > 0 ? "  ·  " + oddsText(price) + (real ? "" : "*") : "");
        double tw = 0, th = 0;
        measure(t, 14, true, 0, tw, th);
        const double w = std::min(maxW, tw + 32 + 30);
        const Rect chip{x, y + 20, w, 34};
        const bool hov = hotLeg(chip, leg), in = inSlip(leg);
        src(cr, in ? g.color("primary") : hov ? mix(g.color(fgRole), g.color(bgRole), 0.14) : g.color(bgRole), alpha);
        rrect(cr, chip.x, chip.y, chip.w, chip.h, 17);
        cairo_fill(cr);
        const Color fgc = g.color(in ? "on_primary" : fgRole);
        trLine(cr, t, x + 16, y + 20 + (34 - th) / 2, w - 32 - 28, 14, true, fgc, alpha);
        ::icon(cr, in ? "check-circle" : "add-circle", chip.x + chip.w - 28, chip.y + 7, 20, fgc, in);
    }
    void drawPicks(cairo_t* cr, Rect r) {
        const Rect card{0, 58, r.w, cardHeight(r)};
        src(cr, g.color("surface_container"));
        rrect(cr, card.x, card.y, card.w, card.h, 28);
        cairo_fill(cr);
        listArea = {card.x + 12, card.y + 12, card.w - 24, card.h - 24};
        std::vector<const ScMatch*> shown;
        for (const ScMatch* m : ordered()) if (!m->done() && (!m->pickLabel.empty() || !m->safeLabel.empty())) shown.push_back(m);
        std::stable_sort(shown.begin(), shown.end(), [](const ScMatch* a, const ScMatch* b) { return std::make_pair(stateRank(*a), a->kickoff) < std::make_pair(stateRank(*b), b->kickoff); });
        if (shown.empty()) {
            const std::string msg = scT("No picks for this day");
            double tw = 0, th = 0;
            measure(msg, 16, false, 0, tw, th);
            text(cr, msg, card.x + (card.w - tw) / 2, card.y + (card.h - th) / 2, 16, false, g.color("on_surface_variant"));
            scrollMax = 0;
            return;
        }
        constexpr double rowH = 110, gap = 8;  // (the chips sit 16 from the left and from the bottom of the row)
        const double contentH = shown.size() * (rowH + gap) - gap;
        scrollMax = std::max(0.0, contentH - listArea.h);
        listPos = std::clamp(listPos, 0.0, scrollMax);
        const double since = std::chrono::duration<double>(Clock::now() - dayShown).count();
        lastFade = since;
        cairo_save(cr);
        cairo_rectangle(cr, listArea.x, listArea.y, listArea.w, listArea.h);
        cairo_clip(cr);
        const double rowW = listArea.w - (scrollMax > 0 ? 10 : 0), fg = 0;
        (void)fg;
        for (size_t i = 0; i < shown.size(); i++) {
            const ScMatch& m = *shown[i];
            const double a = std::clamp((since - 0.03 * std::min<size_t>(i, 8)) / 0.2, 0.0, 1.0), e = 1 - (1 - a) * (1 - a) * (1 - a);
            const double y = listArea.y + i * (rowH + gap) - (listPos + band.over) + (1 - e) * 12;
            if (y + rowH < listArea.y || y > listArea.y + listArea.h) continue;
            const Rect row{listArea.x, y, rowW, rowH};
            hits.push_back({row, Row, static_cast<int>(shown[i] - matches.data())});
            const bool over = mx >= 0 && listArea.contains(mx, my) && row.contains(mx, my);
            const Color bg = g.color("surface_container_high"), on = g.color("on_surface"), dim = g.color("on_surface_variant");
            src(cr, over ? mix(on, bg, 0.06) : bg, e);
            rrect(cr, row.x, row.y, row.w, row.h, 20);
            cairo_fill(cr);
            char tb[48];
            struct tm lt{};
            localtime_r(&m.kickoff, &lt);
            std::strftime(tb, sizeof tb, "%H:%M", &lt);
            const std::string info = m.league + "  ·  " + (m.live() ? (m.detail.empty() ? scT("Live") : m.detail) : std::string(tb));
            double tw = 0, th = 0;
            measure(info, 13, false, 0, tw, th);
            text(cr, info, row.x + row.w - 18 - tw, row.y + 14, 13, false, m.live() ? g.color("error") : dim, 0, e);
            drawCrest(cr, m.home, row.x + 16, row.y + 10, 24, dim, e);
            trLine(cr, m.home.name + "  –  " + m.away.name, row.x + 48, row.y + 12, row.w - 48 - tw - 40, 16, true, on, e);
            const double half = (row.w - 32) / 2;
            if (!m.safeLabel.empty()) pickChip(cr, row.x + 16, row.y + 40, half - 12, scT("Most probable"), m.safeLabel, m.safeProb, m.safePrice, m.safeReal, legFor(m, m.safeKey, m.safeLabel, m.safeProb, m.safePrice, m.safeReal), "secondary_container", "on_secondary_container", e);
            if (!m.pickLabel.empty()) pickChip(cr, row.x + 16 + half, row.y + 40, half - 12, scT("Best value"), m.pickLabel, m.pickProb, m.pickPrice, m.pickReal, legFor(m, m.pickKey, m.pickLabel, m.pickProb, m.pickPrice, m.pickReal), "primary_container", "on_primary_container", e);
        }
        cairo_restore(cr);
        if (scrollMax > 0 && band.thumbAlpha() > 0) {
            const double th = std::max(36.0, listArea.h * listArea.h / contentH), ty = listArea.y + (listArea.h - th) * (listPos / scrollMax);
            src(cr, g.color("on_surface_variant"), 0.35 * band.thumbAlpha());
            rrect(cr, listArea.x + listArea.w - 4, ty, 4, th, 2);
            cairo_fill(cr);
        }
    }

    // ---- Record: how the saved picks did
    void drawRecordHeader(cairo_t* cr, double right) {
        const Color fg = g.color("on_surface"), dim = g.color("on_surface_variant");
        const Rect b{0, 0, 112, 44};
        const bool hov = hot(b, BackBtn);
        const Color bg = g.color("surface_container_high");
        src(cr, hov ? mix(fg, bg, 0.14) : bg);
        rrect(cr, b.x, b.y, b.w, b.h, 22);
        cairo_fill(cr);
        ::icon(cr, "alt-arrow-left", b.x + 16, b.y + 11, 22, fg);
        text(cr, scT("Back"), b.x + 46, b.y + 11, 15, true, fg);
        searchBox = {b.w + 12, 0, std::max(160.0, right - 12 - (b.w + 12)), 44};
        const bool hs = hot(searchBox, SearchBox);
        const Color sbg = g.color(qFocus ? "surface_container_highest" : "surface_container_high");
        src(cr, hs && !qFocus ? mix(fg, sbg, 0.08) : sbg);
        rrect(cr, searchBox.x, searchBox.y, searchBox.w, searchBox.h, 22);
        cairo_fill(cr);
        if (qFocus) {
            src(cr, g.color("primary"));
            cairo_set_line_width(cr, 2);
            rrect(cr, searchBox.x + 1, searchBox.y + 1, searchBox.w - 2, searchBox.h - 2, 21);
            cairo_stroke(cr);
        }
        ::icon(cr, "magnifer", searchBox.x + 16, searchBox.y + 11, 22, dim);
        const double tx = searchBox.x + 46, tw = searchBox.w - 60;
        if (query.empty()) trLine(cr, scT("Search team, league or pick"), tx, searchBox.y + 12, tw, 15, false, dim, 0.7);
        else trLine(cr, query, tx, searchBox.y + 12, tw, 15, false, fg);
        if (qFocus && lastCaret) {
            double cw = 0, ch = 0;
            measure(query.substr(0, std::min(qCur, query.size())), 15, false, 0, cw, ch);
            src(cr, fg);
            cairo_rectangle(cr, tx + std::min(cw, tw), searchBox.y + 11, 2, 22);
            cairo_fill(cr);
        }
    }
    static bool lowerContains(std::string hay, std::string needle) {
        for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return hay.find(needle) != std::string::npos;
    }
    // The saved picks that pass the search and the two filters.
    std::vector<const ScEntry*> filteredEntries() const {
        std::vector<const ScEntry*> v;
        const time_t now = time(nullptr), from = fPeriod == 1 ? now - 7 * 86400 : fPeriod == 2 ? now - 30 * 86400 : 0;
        for (const ScEntry& e : record.entries) {
            if (fResult == 1 && e.hit != 1) continue;
            if (fResult == 2 && e.hit != 0) continue;
            if (fResult == 3 && e.hit >= 0) continue;
            if (from > 0 && scParseIso(e.date) < from) continue;
            if (!query.empty() && !lowerContains(e.home + " " + e.away + " " + e.league + " " + e.label, query)) continue;
            v.push_back(&e);
        }
        return v;
    }
    // A row of chips; returns the width used.
    double filterChips(cairo_t* cr, double x, double y, int kind, const char* const* labels, int count, int chosen, double alpha) {
        double used = 0;
        for (int i = 0; i < count; i++) {
            const std::string name = scT(labels[i]);
            double tw = 0, th = 0;
            measure(name, 14, true, 0, tw, th);
            const Rect c{x + used, y, tw + 28, 36};
            const bool on = i == chosen, hov = hot(c, kind, i);
            const Color bg = on ? g.color("secondary_container") : g.color("surface_container_high"), fg = on ? g.color("on_secondary_container") : g.color("on_surface");
            src(cr, hov ? mix(fg, bg, 0.14) : bg, alpha);
            rrect(cr, c.x, c.y, c.w, c.h, 18);
            cairo_fill(cr);
            text(cr, name, c.x + 14, c.y + (c.h - th) / 2, 14, true, fg, 0, alpha);
            used += c.w + 6;
        }
        return used;
    }
    static std::string ratio(int hits, int n) { return n > 0 ? pct(static_cast<double>(hits) / n) : "–"; }
    void drawRecord(cairo_t* cr, Rect r) {
        const Rect card{0, 58, r.w, cardHeight(r)};
        src(cr, g.color("surface_container"));
        rrect(cr, card.x, card.y, card.w, card.h, 28);
        cairo_fill(cr);
        listArea = {card.x + 12, card.y + 12, card.w - 24, card.h - 24};
        const Color on = g.color("on_surface"), dim = g.color("on_surface_variant");
        if (!recordLoaded) {
            const std::string msg = rLoading ? scT("Loading…") : scT("No picks saved yet");
            double tw = 0, th = 0;
            measure(msg, 16, false, 0, tw, th);
            text(cr, msg, card.x + (card.w - tw) / 2, card.y + (card.h - th) / 2, 16, false, dim);
            return;
        }
        const double since = std::chrono::duration<double>(Clock::now() - modeShown).count();
        lastFade = since;
        rPos = std::clamp(rPos, 0.0, rMax);
        cairo_save(cr);
        cairo_rectangle(cr, listArea.x, listArea.y, listArea.w, listArea.h);
        cairo_clip(cr);
        const double W = listArea.w - 10, X = listArea.x;
        double y = listArea.y - (rPos + rBand.over);
        const double top = y;
        // The totals.
        struct Stat { std::string value, caption, detail; };
        const Stat stats[4] = {
            {ratio(record.hits, record.n), scT("Best value picks"), std::to_string(record.hits) + " / " + std::to_string(record.n)},
            {ratio(record.hitsRecent, record.nRecent), scT("Last 30"), std::to_string(record.hitsRecent) + " / " + std::to_string(record.nRecent)},
            {ratio(record.safeHits, record.safeN), scT("Most probable picks"), std::to_string(record.safeHits) + " / " + std::to_string(record.safeN)},
            {ratio(record.hits1x2, record.n1x2), scT("Result of the match"), std::to_string(record.hits1x2) + " / " + std::to_string(record.n1x2)}};
        const double cw = (W - 3 * 10) / 4;
        for (int i = 0; i < 4; i++) {
            const double a = std::clamp((since - 0.04 * i) / 0.2, 0.0, 1.0), e = 1 - (1 - a) * (1 - a) * (1 - a);
            const Rect c{X + i * (cw + 10), y + (1 - e) * 10, cw, 96};
            src(cr, g.color(i == 0 ? "primary_container" : "surface_container_high"), e);
            rrect(cr, c.x, c.y, c.w, c.h, 22);
            cairo_fill(cr);
            const Color fg = g.color(i == 0 ? "on_primary_container" : "on_surface");
            static const char* shapes[4] = {"Cookie9Sided", "Flower", "Clover4Leaf", "SoftBurst"};
            static const char* icons[4] = {"check-circle", "calendar-date", "star", "football"};
            shapePath(cr, shapes[i], c.x + c.w - 70, c.y + 14, 54);
            src(cr, fg, 0.16 * e);
            cairo_fill(cr);
            ::icon(cr, icons[i], c.x + c.w - 70 + 15, c.y + 14 + 15, 24, fg);
            text(cr, stats[i].value, c.x + 18, c.y + 12, 30, true, fg, 0, e);
            trLine(cr, stats[i].caption, c.x + 18, c.y + 52, c.w - 36, 13, true, fg, e);
            trLine(cr, stats[i].detail, c.x + 18, c.y + 71, c.w - 36, 12, false, fg, 0.7 * e);
        }
        y += 96 + 14;
        // The filters: the result, the period, and how many picks are left.
        {
            static const char* resultNames[4] = {"All", "Hits", "Misses", "Pending"};
            static const char* periodNames[3] = {"Any date", "7 days", "30 days"};
            const double used = filterChips(cr, X, y, FilterResult, resultNames, 4, fResult, 1);
            filterChips(cr, X + used + 18, y, FilterPeriod, periodNames, 3, fPeriod, 1);
            const std::string count = std::to_string(filteredEntries().size()) + " " + scT("picks");
            double tw = 0, th = 0;
            measure(count, 14, false, 0, tw, th);
            text(cr, count, X + W - tw - 6, y + (36 - th) / 2, 14, false, dim);
            y += 36 + 10;
        }
        const auto shownEntries = filteredEntries();
        if (shownEntries.empty()) {
            const std::string msg = scT("No picks match");
            double tw = 0, th = 0;
            measure(msg, 15, false, 0, tw, th);
            text(cr, msg, X + (W - tw) / 2, y + 40, 15, false, dim, 0, 0.8);
            y += 90;
        }
        // The picks, newest first.
        constexpr double rowH = 56, gap = 6;
        const double colDate = 16, colMatch = 120, colRight = 16;
        for (size_t i = 0; i < shownEntries.size(); i++) {
            const ScEntry& e = *shownEntries[i];
            const double ry = y + i * (rowH + gap);
            if (ry + rowH < listArea.y || ry > listArea.y + listArea.h) continue;
            const double a = std::clamp((since - 0.15 - 0.02 * std::min<size_t>(i, 8)) / 0.2, 0.0, 1.0), al = 1 - (1 - a) * (1 - a) * (1 - a);
            const Rect row{X, ry + (1 - al) * 10, W, rowH};
            const bool over = mx >= 0 && listArea.contains(mx, my) && row.contains(mx, my);
            const Color bg = g.color("surface_container_high");
            src(cr, over ? mix(on, bg, 0.06) : bg, al);
            rrect(cr, row.x, row.y, row.w, row.h, 18);
            cairo_fill(cr);
            struct tm lt{};
            const time_t t = scParseIso(e.date);
            localtime_r(&t, &lt);
            char db[24];
            std::strftime(db, sizeof db, "%d %b", &lt);
            const double mid = row.y + (rowH - 16) / 2;
            text(cr, db, row.x + colDate, mid, 13, false, dim, 0, al);
            const double rightEdge = row.x + row.w - colRight - 34;
            const double scoreW = 52, oddsW = 56, probW = 48, pickW = 210;
            const double scoreX = rightEdge - scoreW, oddsX = scoreX - oddsW, probX = oddsX - probW, pickX = probX - pickW;
            trLine(cr, e.home + "  –  " + e.away, row.x + colMatch, mid, std::max(60.0, pickX - row.x - colMatch - 16), 14, true, on, al);
            trLine(cr, scT(e.label.c_str()), pickX, mid, pickW - 8, 13, false, on, al);
            text(cr, pct(e.prob), probX, mid, 13, false, dim, 0, al);
            text(cr, e.price > 0 ? oddsText(e.price) + (e.real ? "" : "*") : "–", oddsX, mid, 13, false, dim, 0, al);
            const std::string sc = e.homeGoals >= 0 ? std::to_string(e.homeGoals) + "-" + std::to_string(e.awayGoals) : "…";
            text(cr, sc, scoreX, mid, 13, true, on, 0, al);
            const char* ic = e.hit < 0 ? "clock-circle" : e.hit ? "check-circle" : "close-circle";
            ::icon(cr, ic, row.x + row.w - colRight - 22, row.y + (rowH - 22) / 2, 22, g.color(e.hit < 0 ? "outline" : e.hit ? "primary" : "error"), e.hit >= 0);
        }
        y += shownEntries.size() * (rowH + gap);
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

