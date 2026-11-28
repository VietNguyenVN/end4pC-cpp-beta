// What Play3r draws. Included once by player.cpp, inside its anonymous namespace.
#pragma once

bool hot(const Rect& r, int id, int arg = 0) {
    g.hits.push_back({r, id, arg});
    return g.hoverId == id && g.hoverArg == arg;
}

// A hit area that only counts where it is visible: the rows of a list that scrolled under the search field must not catch the clicks aimed at the field.
bool hotIn(const Rect& r, const Rect& visible, int id, int arg = 0) {
    const double x0 = std::max(r.x, visible.x), y0 = std::max(r.y, visible.y);
    const double x1 = std::min(r.x + r.w, visible.x + visible.w), y1 = std::min(r.y + r.h, visible.y + visible.h);
    if (x1 <= x0 || y1 <= y0) return false;
    return hot({x0, y0, x1 - x0, y1 - y0}, id, arg);
}

// A round button of the controls: a tonal disc (or a filled one) with an icon, or just an icon.
void roundButton(cairo_t* cr, const Rect& r, int id, const char* name, bool filled, bool active, double iconSize) {
    const bool hov = hot(r, id);
    // On (shuffle, repeat): a tonal disc behind the icon, so it is clear that it is on.
    const Color bg = filled ? color("primary") : active ? color("secondary_container") : color("surface_container_highest");
    const Color fg = filled ? color("on_primary") : active ? color("on_secondary_container") : color("on_surface");
    if (filled || active || hov) {
        src(cr, hov ? mix(fg, bg, 0.12) : bg, filled || active ? 1.0 : 0.9);
        cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
        cairo_fill(cr);
    }
    icon(cr, name, r.x + (r.w - iconSize) / 2, r.y + (r.h - iconSize) / 2, static_cast<int>(iconSize), fg);
}

void pill(cairo_t* cr, const Rect& r, int id, const std::string& label, const char* ic, bool on) {
    const bool hov = hot(r, id);
    const Color bg = on ? color("secondary_container") : color("surface_container_high"), fg = on ? color("on_secondary_container") : color("on_surface");
    src(cr, hov ? mix(fg, bg, 0.12) : bg);
    rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
    cairo_fill(cr);
    const double tw = textWidth(label, 15, true), total = 22 + 8 + tw, x = r.x + (r.w - total) / 2;
    icon(cr, ic, x, r.y + (r.h - 22) / 2, 22, fg);
    text(cr, label, x + 30, r.y + (r.h - textHeight(label, 15, true)) / 2, 15, true, fg);
}

// The progress bar: the part already played is a wave that moves while the music plays, the rest a plain line, and a handle in between.
void waveBar(cairo_t* cr, const Rect& r, double frac, bool playing, bool hover) {
    frac = std::clamp(frac, 0.0, 1.0);
    const double cy = r.y + r.h / 2, fx = r.x + frac * r.w, amp = playing ? 3.2 : 1.2, len = 20;
    const double phase = std::chrono::duration<double>(Clock::now() - g.start).count() * 4.0;
    const Color primary = color("primary");
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 4);
    src(cr, primary);
    cairo_move_to(cr, r.x, cy + std::sin(phase) * amp * std::min(1.0, 0.0 + 0.0));
    for (double x = r.x; x <= fx - 6; x += 2) {
        const double fade = std::min(1.0, (x - r.x) / 24.0);  // starts flat so the left end is tidy
        cairo_line_to(cr, x, cy + std::sin((x - r.x) / len * 2 * M_PI - phase) * amp * fade);
    }
    cairo_stroke(cr);
    src(cr, color("outline_variant"), 0.9);
    cairo_move_to(cr, std::min(r.x + r.w, fx + 8), cy);
    cairo_line_to(cr, r.x + r.w, cy);
    cairo_stroke(cr);
    src(cr, primary);
    rrect(cr, fx - 2.5, cy - (hover || g.drag == Drag::Seek ? 13 : 11), 5, hover || g.drag == Drag::Seek ? 26 : 22, 2.5);
    cairo_fill(cr);
}

void slider(cairo_t* cr, const Rect& r, double frac, bool hover) {
    frac = std::clamp(frac, 0.0, 1.0);
    const double cy = r.y + r.h / 2, fx = r.x + 4 + frac * (r.w - 8), hw = hover || g.drag == Drag::Volume ? 3 : 4;
    src(cr, color("primary"));
    rrect(cr, r.x, cy - 3, std::max(0.0, fx - hw - 4 - r.x), 6, 3);
    cairo_fill(cr);
    src(cr, color("secondary_container"));
    rrect(cr, fx + hw + 4, cy - 3, std::max(0.0, r.x + r.w - fx - hw - 4), 6, 3);
    cairo_fill(cr);
    src(cr, color("primary"));
    rrect(cr, fx - hw / 2, cy - (hover || g.drag == Drag::Volume ? 14 : 12), hw, hover || g.drag == Drag::Volume ? 28 : 24, hw / 2);
    cairo_fill(cr);
}

// The wave, and the two times under it: what changes while the music plays.
void drawProgress(cairo_t* cr) {
    const double dur = g.engine->duration(), pos = g.drag == Drag::Seek ? g.dragFrac * dur : g.shownPosition;
    waveBar(cr, g.bar, dur > 0 ? pos / dur : 0, g.engine->state() == player::State::Playing, g.barHot);
}

void drawNowPlaying(cairo_t* cr, const Rect& card) {
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, 28);
    cairo_fill(cr);
    const bool hasTrack = g.current >= 0;
    const player::TrackInfo now = hasTrack ? trackInfo(g.current) : player::TrackInfo{};
    const player::TrackInfo* info = hasTrack ? &now : nullptr;
    const double inner = card.w - 2 * kPad;

    // The extras at the very bottom (what the file is made of, and the song that comes next); above them the volume, the controls and the progress;
    // what is left is for the cover and the names. When the window is short the extras give way first.
    const player::StreamInfo& si = g.engine->streamInfo();
    const bool chips = info && g.engine->state() != player::State::Stopped && !si.codec.empty();
    const bool next = info && g.nextIndex >= 0 && g.nextIndex < static_cast<int>(g.queue.size());
    constexpr double chipH = 32, cardH = 76, extrasGap = 14;
    double extrasH = (chips ? chipH : 0) + (chips && next ? extrasGap : 0) + (next ? cardH : 0);
    auto layoutFor = [&](double extras, double& volY, double& ctrlY, double& barY, double& coverSize) {
        const double bottom = card.y + card.h - kPad - (extras > 0 ? extras + 20 : 0);
        volY = bottom - 28;
        ctrlY = volY - 22 - 72;
        barY = ctrlY - 22 - 22;
        coverSize = std::clamp(std::min(inner, barY - 36 - 56 - (card.y + kPad) - 22), 120.0, 320.0);
    };
    double volY, ctrlY, barY, coverSize;
    layoutFor(extrasH, volY, ctrlY, barY, coverSize);
    if (extrasH > 0 && barY - 36 - 56 - (card.y + kPad) - 22 < 190) { extrasH = 0; layoutFor(0, volY, ctrlY, barY, coverSize); }
    const double cx = card.x + card.w / 2, coverX = cx - coverSize / 2, coverY = card.y + kPad;

    // The cover on a Material shape. The shaped picture is made once per size (scaling a big JPEG on every frame was most of the work of the program).
    {
        const int px = hidpi::bufferExtent(static_cast<int>(coverSize), g_scale);
        if (!g.coverShaped || g.coverShapedPx != px || g.coverShapedFor != g.cover) {
            if (g.coverShaped) { cairo_surface_destroy(g.coverShaped); g.coverShaped = nullptr; }
            g.coverShaped = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
            cairo_t* c2 = mkcr(g.coverShaped);
            shapePath(c2, "Cookie12Sided", 0, 0, px);
            if (g.cover) {
                cairo_clip(c2);
                const double iw = cairo_image_surface_get_width(g.cover), ih = cairo_image_surface_get_height(g.cover), sc = std::max(px / iw, px / ih);
                cairo_translate(c2, (px - iw * sc) / 2, (px - ih * sc) / 2);
                cairo_scale(c2, sc, sc);
                cairo_set_source_surface(c2, g.cover, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(c2), CAIRO_FILTER_BEST);
                cairo_paint(c2);
            } else {
                src(c2, color("primary_container"));
                cairo_fill(c2);
            }
            cairo_destroy(c2);
            g.coverShapedPx = px;
            g.coverShapedFor = g.cover;
        }
        cairo_save(cr);
        cairo_translate(cr, coverX, coverY);
        cairo_scale(cr, coverSize / px, coverSize / px);
        cairo_set_source_surface(cr, g.coverShaped, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    if (!g.cover) icon(cr, "music-notes", coverX + coverSize / 2 - coverSize * 0.18, coverY + coverSize / 2 - coverSize * 0.18, static_cast<int>(coverSize * 0.36), color("on_primary_container"), 0.8);

    // The names.
    const double ty = coverY + coverSize + 18;
    const std::string title = info ? info->title : i18n::tr("Nothing playing");
    text(cr, title, card.x + kPad, ty, 22, true, color("on_surface"), 1, inner, 1);
    if (info) {
        text(cr, info->artist, card.x + kPad, ty + 32, 15, false, color("on_surface_variant"), 1, inner, 1);
        text(cr, info->album, card.x + kPad, ty + 54, 13, false, color("on_surface_variant"), 0.7, inner, 1);
    }

    // The extras, at the bottom of the card.
    if (extrasH > 0) {
        double y = card.y + card.h - kPad - extrasH;
        if (chips) {
            std::string codec = si.codec;
            for (char& c : codec) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (codec.rfind("PCM", 0) == 0) codec = "WAV";
            std::vector<std::string> labels{codec};
            if (si.bitrateKbps > 0) labels.push_back(std::to_string(si.bitrateKbps) + " kbps");
            if (si.sampleRate > 0) { char b[24]; std::snprintf(b, sizeof b, "%g kHz", si.sampleRate / 1000.0); labels.push_back(b); }
            if (si.channels > 0) labels.push_back(si.channels == 1 ? "Mono" : si.channels == 2 ? "Stereo" : std::to_string(si.channels) + " ch");
            double total = -8;
            for (const std::string& l : labels) total += textWidth(l, 13, true) + 28 + 8;
            double x = cx - total / 2;
            for (const std::string& l : labels) {
                const double w = textWidth(l, 13, true) + 28;
                src(cr, color("surface_container_highest"));
                rrect(cr, x, y, w, chipH, chipH / 2);
                cairo_fill(cr);
                text(cr, l, x + 14, y + (chipH - 17) / 2, 13, true, color("on_surface_variant"));
                x += w + 8;
            }
            y += chipH + extrasGap;
        }
        if (next) {
            const Rect c{card.x + kPad, y, inner, cardH};
            const bool hov = hot(c, UpNext);
            src(cr, hov ? mix(color("on_surface"), color("surface_container_high"), 0.08) : color("surface_container_high"));
            rrect(cr, c.x, c.y, c.w, c.h, 24);
            cairo_fill(cr);
            src(cr, color("secondary_container"));
            shapePath(cr, "Cookie9Sided", c.x + 14, c.y + 14, 48);
            cairo_fill(cr);
            icon(cr, "skip-next", c.x + 14 + 12, c.y + 14 + 12, 24, color("on_secondary_container"));
            const player::TrackInfo nt = trackInfo(g.nextIndex);
            text(cr, i18n::tr("Up next"), c.x + 78, c.y + 12, 12, false, color("on_surface_variant"), 0.8, c.w - 78 - 16);
            text(cr, nt.title, c.x + 78, c.y + 29, 16, true, color("on_surface"), 1, c.w - 78 - 16);
            text(cr, nt.artist, c.x + 78, c.y + 51, 13, false, color("on_surface_variant"), 0.8, c.w - 78 - 16);
        }
    }

    // The progress is drawn apart (drawProgress): it moves every frame, the rest of the window does not.
    g.bar = {card.x + kPad, barY - 12, inner, 24};
    g.barHot = hot({g.bar.x, g.bar.y - 8, g.bar.w, g.bar.h + 16}, Progress);
    {   // the two times change once a second: they are part of the full draw
        const double d = g.engine->duration(), p = g.drag == Drag::Seek ? g.dragFrac * d : g.shownPosition;
        g.timeDrawn = static_cast<int>(p);
        text(cr, fmtTime(p), g.bar.x, g.bar.y + 26, 12, false, color("on_surface_variant"));
        text(cr, fmtTime(d), g.bar.x, g.bar.y + 26, 12, false, color("on_surface_variant"), 1, g.bar.w, 2);
    }

    // The controls.
    const double mid = card.x + card.w / 2, cyc = ctrlY + 36;
    roundButton(cr, {mid - 36 - 12 - 52 - 12 - 44, cyc - 22, 44, 44}, BtnShuffle, "shuffle", false, g.saved.shuffle, 22);
    roundButton(cr, {mid - 36 - 12 - 52, cyc - 26, 52, 52}, BtnPrev, "skip-previous", false, false, 26);
    {
        const Rect r{mid - 36, ctrlY, 72, 72};
        const bool playing = g.engine->state() == player::State::Playing;
        const bool hov = hot(r, BtnPlay);
        src(cr, hov ? mix(color("on_primary"), color("primary"), 0.12) : color("primary"));
        shapePath(cr, playing ? "Cookie9Sided" : "Circle", r.x, r.y, r.w);
        cairo_fill(cr);
        icon(cr, playing ? "pause" : "play", r.x + 20, r.y + 20, 32, color("on_primary"));
    }
    roundButton(cr, {mid + 36 + 12, cyc - 26, 52, 52}, BtnNext, "skip-next", false, false, 26);
    roundButton(cr, {mid + 36 + 12 + 52 + 12, cyc - 22, 44, 44}, BtnRepeat, g.saved.repeat == 2 ? "repeat-one" : "repeat", false, g.saved.repeat != 0, 22);

    // The volume.
    icon(cr, g.engine->volume() <= 0.001f ? "volume-cross" : g.engine->volume() < 0.5f ? "volume-small" : "volume-loud", card.x + kPad, volY + 2, 24, color("on_surface_variant"));
    const Rect vr{card.x + kPad + 36, volY, inner - 36 - 44, 28};
    slider(cr, vr, g.engine->volume(), hot({vr.x - 6, vr.y - 6, vr.w + 12, vr.h + 12}, VolumeBar));
    char pct[8];
    std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(g.engine->volume() * 100)));
    text(cr, pct, card.x + card.w - kPad - 40, volY + 5, 13, false, color("on_surface_variant"), 1, 40, 2);
}

// ---- the lyrics ------------------------------------------------------------------------------------------------------------------------
int currentLyric(double pos) {
    int cur = -1;
    for (size_t i = 0; i < g.lyrics.size(); i++) { if (g.lyrics[i].time <= pos + 0.15) cur = static_cast<int>(i); else break; }
    return cur;
}
void drawLyrics(cairo_t* cr, const Rect& area) {
    if (g.lyrics.empty()) {
        const std::string msg = g.lyricsStatus == "loading" ? i18n::tr("Searching the lyrics…") : g.current < 0 ? i18n::tr("Play something to see its lyrics") : i18n::tr("No synced lyrics for this song");
        const double tw = textWidth(msg, 16, false);
        text(cr, msg, area.x + (area.w - tw) / 2, area.y + area.h / 2 - 10, 16, false, color("on_surface_variant"), 0.8);
        return;
    }
    const double wrap = area.w - 48;
    if (g.lyHeights.size() != g.lyrics.size() || std::abs(g.lyHeightsWidth - wrap) > 0.5) {
        g.lyHeights.assign(g.lyrics.size(), 0);
        for (size_t i = 0; i < g.lyrics.size(); i++) g.lyHeights[i] = textHeight(g.lyrics[i].text.empty() ? "♪" : g.lyrics[i].text, 26, true, wrap) + 22;
        g.lyHeightsWidth = wrap;
    }
    const int cur = currentLyric(g.shownPosition);
    double offsetOfCur = 0;
    for (int i = 0; i < cur; i++) offsetOfCur += g.lyHeights[static_cast<size_t>(i)];
    const double curH = cur >= 0 ? g.lyHeights[static_cast<size_t>(cur)] : 0;
    const double target = offsetOfCur + curH / 2 - area.h * 0.42;
    g.lyBusy = std::abs(target - g.lyScroll) > 0.4;
    g.lyScroll += (target - g.lyScroll) * 0.14;  // eased: the lines glide
    g.lyCurrentDrawn = cur;
    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);
    double y = area.y - g.lyScroll;
    for (size_t i = 0; i < g.lyrics.size(); i++) {
        const double h = g.lyHeights[i];
        if (y + h >= area.y - 4 && y <= area.y + area.h + 4) {
            const double dist = std::abs(static_cast<int>(i) - cur);
            const bool now = static_cast<int>(i) == cur;
            const Rect row{area.x + 8, y, area.w - 16, h};
            const bool hov = hotIn(row, area, LyricRow, static_cast<int>(i));
            if (hov && !now) { src(cr, color("on_surface"), 0.06); rrect(cr, row.x, row.y + 2, row.w, row.h - 4, 16); cairo_fill(cr); }
            const double alpha = now ? 1.0 : std::max(0.28, 0.62 - dist * 0.1);
            const std::string line = g.lyrics[i].text.empty() ? "♪" : g.lyrics[i].text;
            text(cr, line, area.x + 24, y + 11, 26, true, now ? color("primary") : color("on_surface"), alpha, wrap, 0, true);
        }
        y += h;
    }
    cairo_restore(cr);
}

// ---- a text field (the search, the name of a playlist) -----------------------------------------------------------------------------------------
void drawField(cairo_t* cr, const Rect& r, int id, const Field& f, int which, const std::string& placeholder, bool clearButton) {
    const bool focused = g.focus == which, hov = hot(r, id);
    const Color bg = color(focused ? "surface_container_highest" : "surface_container_high");
    src(cr, hov && !focused ? mix(color("on_surface"), bg, 0.06) : bg);
    rrect(cr, r.x, r.y, r.w, r.h, r.h / 2);
    cairo_fill(cr);
    if (focused) {
        src(cr, color("primary"));
        cairo_set_line_width(cr, 2);
        rrect(cr, r.x + 1, r.y + 1, r.w - 2, r.h - 2, r.h / 2 - 1);
        cairo_stroke(cr);
    }
    icon(cr, "magnifer", r.x + 16, r.y + (r.h - 22) / 2, 22, color("on_surface_variant"));
    const double tx = r.x + 46, tw = r.w - 46 - (clearButton && !f.s.empty() ? 46 : 18);
    if (f.s.empty()) text(cr, placeholder, tx, r.y + (r.h - 18) / 2, 15, false, color("on_surface_variant"), 0.7, tw);
    else text(cr, f.s, tx, r.y + (r.h - 18) / 2, 15, false, color("on_surface"), 1, tw);
    if (focused && g.caretOn) {
        const double cw = textWidth(f.s.substr(0, std::min(f.cur, f.s.size())), 15, false);
        src(cr, color("on_surface"));
        cairo_rectangle(cr, tx + std::min(cw, tw), r.y + (r.h - 22) / 2, 2, 22);
        cairo_fill(cr);
    }
    if (clearButton && !f.s.empty()) {
        const Rect x{r.x + r.w - 38, r.y + (r.h - 28) / 2, 28, 28};
        const bool xh = hot(x, SearchClear);
        if (xh) { src(cr, color("on_surface"), 0.1); cairo_arc(cr, x.x + 14, x.y + 14, 14, 0, 2 * M_PI); cairo_fill(cr); }
        icon(cr, "close-circle", x.x + 4, x.y + 4, 20, color("on_surface_variant"));
    }
}

// ---- the queue -------------------------------------------------------------------------------------------------------------------------------
void drawQueue(cairo_t* cr, const Rect& whole) {
    const Rect searchRect{whole.x + 4, whole.y, whole.w - 8, 44};
    drawField(cr, searchRect, SearchField, g.search, 1, i18n::tr("Search songs"), true);
    if (!g.search.s.empty() && !g.queue.empty()) {
        const std::string n = std::to_string(g.view.size()) + " / " + std::to_string(g.queue.size());
        text(cr, n, searchRect.x + searchRect.w - 46 - 90, searchRect.y + 14, 13, false, color("on_surface_variant"), 0.8, 90, 2);
    }
    const Rect area{whole.x, whole.y + 56, whole.w, whole.h - 56};
    if (g.queue.empty() || g.view.empty()) {
        const std::string msg = g.queue.empty() ? i18n::tr("The queue is empty. Open a folder with the button above.") : i18n::tr("No song matches your search");
        const double tw = textWidth(msg, 16, false);
        text(cr, msg, area.x + std::max(0.0, (area.w - tw) / 2), area.y + area.h / 2 - 10, 16, false, color("on_surface_variant"), 0.8, area.w, tw < area.w ? 0 : 1);
        return;
    }
    constexpr double rowH = 56, gap = 4;
    const int rows = static_cast<int>(g.view.size());
    const double content = rows * (rowH + gap) - gap;
    g.queueMax = std::max(0.0, content - area.h);
    if (g.queueToCurrent && g.current >= 0) {
        const auto it = std::find(g.view.begin(), g.view.end(), g.current);
        if (it != g.view.end()) {
            const double top = (it - g.view.begin()) * (rowH + gap), bottom = top + rowH;
            if (top < g.queuePos) g.queuePos = std::max(0.0, top - 8);
            else if (bottom > g.queuePos + area.h) g.queuePos = std::min(g.queueMax, bottom - area.h + 8);
        }
        g.queueToCurrent = false;
    }
    g.queuePos = std::clamp(g.queuePos, 0.0, g.queueMax);
    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);
    const double w = area.w - (g.queueMax > 0 ? 10 : 0);
    const double off = g.queuePos + g.queueBand.over;
    const int first = std::max(0, static_cast<int>(off / (rowH + gap)));
    g_scanner.focus = g.view[static_cast<size_t>(std::min(first, rows - 1))];  // the scanner reads the tags from the rows on screen onwards
    const int last = std::min(rows - 1, static_cast<int>((off + area.h) / (rowH + gap)));
    for (int k = first; k <= last; k++) {
        const int i = g.view[static_cast<size_t>(k)];
        const double y = area.y + k * (rowH + gap) - off;
        const Rect row{area.x, y, w, rowH};
        const bool now = i == g.current, hov = hotIn(row, area, QueueRow, i), marked = g.focus == 1 && k == g.selPos && !g.search.s.empty();
        const player::TrackInfo ti = trackInfo(i);
        const Color bg = now ? color("secondary_container") : color("surface_container_high"), fg = now ? color("on_secondary_container") : color("on_surface");
        src(cr, hov && !now ? mix(fg, bg, 0.08) : bg);
        rrect(cr, row.x, row.y, row.w, row.h, 18);
        cairo_fill(cr);
        if (marked) {  // the result that Enter plays
            src(cr, color("primary"));
            cairo_set_line_width(cr, 2);
            rrect(cr, row.x + 1, row.y + 1, row.w - 2, row.h - 2, 17);
            cairo_stroke(cr);
        }
        if (now) icon(cr, g.engine->state() == player::State::Playing ? "play" : "pause", row.x + 16, row.y + (rowH - 22) / 2, 22, fg);
        else text(cr, std::to_string(i + 1), row.x + 8, row.y + (rowH - 15) / 2, 13, false, color("on_surface_variant"), 0.8, 36, 1);
        text(cr, ti.title, row.x + 52, row.y + 9, 15, true, fg, 1, row.w - 52 - 70);
        text(cr, ti.artist, row.x + 52, row.y + 31, 12, false, fg, 0.7, row.w - 52 - 70);
        if (ti.duration > 0) text(cr, fmtTime(ti.duration), row.x + row.w - 16 - 50, row.y + (rowH - 14) / 2, 13, false, fg, 0.7, 50, 2);
    }
    cairo_restore(cr);
    if (g.queueMax > 0 && g.queueBand.thumbAlpha() > 0) {  // the scroll mark: shown while the wheel moves, then it fades
        const double th = std::max(36.0, area.h * area.h / content), ty = area.y + (area.h - th) * (g.queuePos / g.queueMax);
        src(cr, color("on_surface_variant"), 0.35 * g.queueBand.thumbAlpha());
        rrect(cr, area.x + area.w - 4, ty, 4, th, 2);
        cairo_fill(cr);
    }
}

// ---- the playlists ----------------------------------------------------------------------------------------------------------------------------
void drawPlaylists(cairo_t* cr, const Rect& whole) {
    if (g.playlistsDirty) refreshPlaylists();
    const bool canSave = !g.queue.empty() && !cleanPlaylistName(g.plName.s).empty();
    const std::string saveLabel = i18n::tr("Save queue");
    const double saveW = textWidth(saveLabel, 15, true) + 70;
    drawField(cr, {whole.x + 4, whole.y, whole.w - 8 - saveW - 8, 44}, NameField, g.plName, 2, i18n::tr("Name of a new playlist"), false);
    {
        const Rect b{whole.x + whole.w - 4 - saveW, whole.y, saveW, 44};
        const bool hov = canSave && hot(b, SaveList);
        const Color bg = color("primary"), fg = color("on_primary");
        src(cr, hov ? mix(fg, bg, 0.12) : bg, canSave ? 1.0 : 0.35);
        rrect(cr, b.x, b.y, b.w, b.h, 22);
        cairo_fill(cr);
        icon(cr, "playlist", b.x + 18, b.y + 11, 22, fg, canSave ? 1.0 : 0.6);
        text(cr, saveLabel, b.x + 48, b.y + 13, 15, true, fg, canSave ? 1.0 : 0.6);
    }
    if (!g.plNote.empty()) text(cr, g.plNote, whole.x + 8, whole.y + 52, 12, false, color("primary"), 0.9, whole.w - 16);
    const Rect area{whole.x, whole.y + 72, whole.w, whole.h - 72};
    if (g.playlists.empty()) {
        const std::string msg = i18n::tr("No playlists yet. Type a name above to save the queue.");
        const double tw = textWidth(msg, 16, false);
        text(cr, msg, area.x + std::max(0.0, (area.w - tw) / 2), area.y + area.h / 2 - 30, 16, false, color("on_surface_variant"), 0.8, area.w, tw < area.w ? 0 : 1);
        return;
    }
    constexpr double rowH = 64, gap = 4;
    const double content = g.playlists.size() * (rowH + gap) - gap;
    g.plMax = std::max(0.0, content - area.h);
    g.plPos = std::clamp(g.plPos, 0.0, g.plMax);
    cairo_save(cr);
    cairo_rectangle(cr, area.x, area.y, area.w, area.h);
    cairo_clip(cr);
    const double w = area.w - (g.plMax > 0 ? 10 : 0), off = g.plPos + g.plBand.over;
    for (size_t i = 0; i < g.playlists.size(); i++) {
        const double y = area.y + i * (rowH + gap) - off;
        if (y + rowH < area.y || y > area.y + area.h) continue;
        const PlaylistInfo& pl = g.playlists[i];
        const Rect row{area.x, y, w, rowH}, del{row.x + row.w - 16 - 40, row.y + (rowH - 40) / 2, 40, 40};
        const bool hov = hotIn(row, area, PlaylistRow, static_cast<int>(i)), delHot = hotIn(del, area, PlaylistDelete, static_cast<int>(i));
        const Color bg = color("surface_container_high"), fg = color("on_surface");
        src(cr, hov ? mix(fg, bg, 0.08) : bg);
        rrect(cr, row.x, row.y, row.w, row.h, 20);
        cairo_fill(cr);
        src(cr, color("secondary_container"));
        shapePath(cr, "Cookie9Sided", row.x + 12, row.y + 10, 44);
        cairo_fill(cr);
        icon(cr, "playlist", row.x + 12 + 11, row.y + 10 + 11, 22, color("on_secondary_container"));
        text(cr, pl.name, row.x + 70, row.y + 12, 16, true, fg, 1, row.w - 70 - 72);
        text(cr, std::to_string(pl.count) + " " + i18n::tr("songs"), row.x + 70, row.y + 36, 13, false, fg, 0.7, row.w - 70 - 72);
        src(cr, delHot ? color("error_container") : color("surface_container_highest"), hov || delHot ? 1.0 : 0.0);
        cairo_arc(cr, del.x + 20, del.y + 20, 20, 0, 2 * M_PI);
        cairo_fill(cr);
        if (hov || delHot) icon(cr, "close-circle", del.x + 9, del.y + 9, 22, color(delHot ? "on_error_container" : "on_surface_variant"));
    }
    cairo_restore(cr);
    if (g.plMax > 0 && g.plBand.thumbAlpha() > 0) {
        const double th = std::max(36.0, area.h * area.h / content), ty = area.y + (area.h - th) * (g.plPos / g.plMax);
        src(cr, color("on_surface_variant"), 0.35 * g.plBand.thumbAlpha());
        rrect(cr, area.x + area.w - 4, ty, 4, th, 2);
        cairo_fill(cr);
    }
}

void drawRight(cairo_t* cr, const Rect& card) {
    src(cr, color("surface_container"));
    rrect(cr, card.x, card.y, card.w, card.h, 28);
    cairo_fill(cr);
    const double tabY = card.y + 16, tabH = 44;
    // The tabs, in this order: the queue, the lyrics, the playlists (the numbers of g.tab: 0 lyrics, 1 queue, 2 playlists).
    const std::string names[3] = {i18n::tr("Lyrics"), i18n::tr("Queue"), i18n::tr("Playlists")};
    const char* icons[3] = {"music-notes", "playlist", "checklist-minimalistic"};
    const int ids[3] = {TabLyrics, TabQueue, TabPlaylists};
    double x = card.x + 16;
    for (const int i : {1, 0, 2}) {
        const double w = textWidth(names[i], 15, true) + 70;
        pill(cr, {x, tabY, w, tabH}, ids[i], names[i], icons[i], g.tab == i);
        x += w + 8;
    }
    if (!g.queue.empty() && card.x + card.w - 24 - x > 100) {
        const std::string n = std::to_string(g.queue.size()) + " " + i18n::tr("songs");
        text(cr, n, card.x + card.w - 24 - 100, tabY + 14, 13, false, color("on_surface_variant"), 0.9, 100, 2);
    }
    const Rect area{card.x + 12, tabY + tabH + 12, card.w - 24, card.h - (tabH + 16 + 12 + 12)};
    if (g.tab == 0) drawLyrics(cr, area);
    else if (g.tab == 1) drawQueue(cr, area);
    else drawPlaylists(cr, area);
}

void drawWindow(cairo_t* cr, int W, int H) {
    g.hits.clear();
    cairo_rectangle(cr, 0, 0, W, H);  // (square: the compositor rounds the corners of its windows, Hyprland does it with its own radius)
    src(cr, color("background"));
    cairo_fill(cr);
    // The logo: a Material shape with a music icon, and the name.
    shapePath(cr, "Cookie12Sided", kPad, 20, 48);
    src(cr, color("primary_container"));
    cairo_fill(cr);
    icon(cr, "music-notes", kPad + 12, 20 + 12, 24, color("on_primary_container"));
    text(cr, "Play3r", kPad + 48 + 14, 28, 26, true, color("on_surface"));
    // The buttons of the corner.
    const Rect close{W - kPad - 44, 22, 44, 44}, open{W - kPad - 44 - 8 - 44, 22, 44, 44};
    for (auto [r, id, ic] : {std::tuple<Rect, int, const char*>{open, BtnOpen, "folder-open"}, {close, BtnClose, "close-circle"}}) {
        const bool hov = hot(r, id);
        src(cr, id == BtnClose && hov ? color("error_container") : hov ? color("surface_container_highest") : color("surface_container_high"));
        cairo_arc(cr, r.x + 22, r.y + 22, 22, 0, 2 * M_PI);
        cairo_fill(cr);
        icon(cr, ic, r.x + 11, r.y + 11, 22, color(id == BtnClose && hov ? "on_error_container" : "on_surface"));
    }
    const double bottom = H - kPad;
    drawNowPlaying(cr, {kPad, kTop, kLeftW, bottom - kTop});
    drawRight(cr, {kPad + kLeftW + 16, kTop, W - 2 * kPad - kLeftW - 16, bottom - kTop});
}
