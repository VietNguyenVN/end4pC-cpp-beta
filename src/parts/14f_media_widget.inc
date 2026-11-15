
constexpr int kMwPrev = 1, kMwPlay = 2, kMwNext = 3, kMwSeek = 4;

constexpr int kMwBlurN = 32;
cairo_surface_t* mediaWTinyBlur(cairo_surface_t* cover) {
    constexpr int n = kMwBlurN;
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, n, n);
    cairo_t* cr = mkcr(small);
    const double s = double(n) / cairo_image_surface_get_width(cover);
    cairo_scale(cr, s, s);
    cairo_set_source_surface(cr, cover, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(small);
    unsigned char* px = cairo_image_surface_get_data(small);
    const int stride = cairo_image_surface_get_stride(small);
    for (int pass = 0; pass < 4; pass++) {
        std::vector<unsigned char> copy(px, px + stride * n);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
                for (int ch = 0; ch < 4; ch++) {
                    int sum = 0, c = 0;
                    for (int dy = -2; dy <= 2; dy++)
                        for (int dx = -2; dx <= 2; dx++) {
                            sum += copy[std::clamp(y + dy, 0, n - 1) * stride + std::clamp(x + dx, 0, n - 1) * 4 + ch];
                            c++;
                        }
                    px[y * stride + x * 4 + ch] = static_cast<unsigned char>(sum / c);
                }
    }
    cairo_surface_mark_dirty(small);
    return small;
}

void mediaWRefreshCover(App& app) {
    MediaW& X = app.mediaW;
    if (app.media.coverKey != app.musicArtUrl) refreshMediaCover(app);
    if (X.blurKey != app.musicArtUrl) {
        if (X.blur) { cairo_surface_destroy(X.blur); X.blur = nullptr; }
        if (app.media.cover) X.blur = mediaWTinyBlur(app.media.cover);
        X.blurKey = app.musicArtUrl;
    }
}

void mediaWPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    MediaW& X = static_cast<MediaW&>(d);
    MediaPanel& M = app.media;
    mediaWRefreshCover(app);
    const bool have = !app.musicStatus.empty();
    const double pad = 22;

    cairo_save(cr);
    roundedRect(cr, 0, 0, W, H, 30);
    ClipGroup cut;
    beginClipGroup(cr, cut);
    setSource(cr, have && M.cover ? M.col.layer0 : app.color("surface_container"));
    cairo_paint(cr);
    if (have && X.blur) {
        const double sc = std::max(W, H) / static_cast<double>(kMwBlurN);
        cairo_save(cr);
        cairo_translate(cr, (W - kMwBlurN * sc) / 2, (H - kMwBlurN * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.blur, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint_with_alpha(cr, 0.9);
        cairo_restore(cr);
    }
    if (have) {
        cairo_pattern_t* veil = cairo_pattern_create_linear(0, 0, 0, H);
        cairo_pattern_add_color_stop_rgba(veil, 0, 0, 0, 0, 0.22);
        cairo_pattern_add_color_stop_rgba(veil, 0.55, 0, 0, 0, 0.32);
        cairo_pattern_add_color_stop_rgba(veil, 1, 0, 0, 0, 0.58);
        cairo_set_source(cr, veil);
        cairo_paint(cr);
        cairo_pattern_destroy(veil);
    }
    endClipGroup(cr, cut);
    cairo_restore(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    Color tint{1, 1, 1};
    auto text = [&](const std::string& s, double px, bool bold, double x, double y, double alpha, double maxW, bool fit = false, int align = 0) {
        PangoFontDescription* fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
        pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
        pango_layout_set_font_description(layout, fd);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        pango_layout_set_width(layout, -1);
        pango_layout_set_text(layout, s.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (fit && tw > maxW) {
            pango_font_description_set_absolute_size(fd, std::max(12.0, px * maxW / tw) * PANGO_SCALE);
            pango_layout_set_font_description(layout, fd);
            pango_layout_get_pixel_size(layout, &tw, &th);
        }
        pango_font_description_free(fd);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, static_cast<int>(maxW * PANGO_SCALE));
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgba(cr, tint.r, tint.g, tint.b, alpha);
        cairo_move_to(cr, align == 1 ? x + (maxW - tw) / 2 : align == 2 ? x + maxW - tw : x, y - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        return tw;
    };

    if (!have) {
        constexpr double kShapeBox = 112, kNote = 48, kGapShapeTitle = 24, kGapTitleHint = 8;
        const double cy = H / 2.0, sx = (W - kShapeBox) / 2.0, sy = cy - 56 - kShapeBox / 2.0;
        shapePath(cr, kM3IconShape, sx, sy, kShapeBox);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        drawIcon(cr, "music-notes", sx + (kShapeBox - kNote) / 2.0, sy + (kShapeBox - kNote) / 2.0, kNote, app.color("on_primary_container"), true);
        tint = app.color("on_surface");
        const double ty = sy + kShapeBox + kGapShapeTitle + 11;
        text("Nothing playing", 20, true, pad, ty, 1, W - 2 * pad, false, 1);
        tint = app.color("on_surface_variant");
        text("Play something and it shows up here", 12, false, pad, ty + 11 + kGapTitleHint + 8, 1, W - 2 * pad, false, 1);
        g_object_unref(layout);
        return;
    }

    const double art = 80;
    cairo_save(cr);
    roundedRect(cr, pad, pad, art, art, 22);
    if (M.cover) {
        cairo_clip(cr);
        cairo_translate(cr, pad, pad);
        cairo_scale(cr, art / cairo_image_surface_get_width(M.cover), art / cairo_image_surface_get_height(M.cover));
        cairo_set_source_surface(cr, M.cover, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
        cairo_fill(cr);
    }
    cairo_restore(cr);
    if (!M.cover) drawIcon(cr, "music-notes", pad + 22, pad + 22, 36, Color{1, 1, 1}, true);
    const double tx = pad + art + 16, tw2 = W - tx - pad;
    text(cleanMusicTitle(app.musicTitle), 18, true, tx, pad + 22, 1.0, tw2);
    text(app.musicArtist, 13, false, tx, pad + 46, 0.75, tw2);
    text(M.status == "Playing" ? "Playing" : "Paused", 10, true, tx, pad + 68, 0.5, tw2);

    const double lTop = pad + art + 14, lBottom = H - 112, lH = lBottom - lTop, center = lTop + lH / 2;
    if (lH > 50) {
        if (X.lyricsStatus == "ok" && !X.lines.empty()) {
            const double lw = W - 2 * pad - 10, gap = 18;
            PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
            pango_font_description_set_absolute_size(fd, 20 * PANGO_SCALE);
            pango_layout_set_font_description(layout, fd);
            pango_font_description_free(fd);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_width(layout, static_cast<int>(lw * PANGO_SCALE));
            auto lineText = [&](size_t i) { pango_layout_set_text(layout, X.lines[i].text.empty() ? "♪" : X.lines[i].text.c_str(), -1); };
            if (X.lineHW != W || X.lineH.size() != X.lines.size()) {
                X.lineH.assign(X.lines.size(), 0);
                X.lineTop.assign(X.lines.size(), 0);
                double y = 0;
                for (size_t i = 0; i < X.lines.size(); i++) {
                    lineText(i);
                    int tw = 0, th = 0;
                    pango_layout_get_pixel_size(layout, &tw, &th);
                    X.lineH[i] = th;
                    X.lineTop[i] = y;
                    y += th + gap;
                }
                X.lineHW = W;
            }
            const int n = static_cast<int>(X.lines.size());
            const int f0 = std::clamp(static_cast<int>(std::floor(X.anim)), 0, n - 1), f1 = std::min(f0 + 1, n - 1);
            const double fr = std::clamp(X.anim - f0, 0.0, 1.0);
            const double c0 = X.lineTop[f0] + X.lineH[f0] / 2, c1 = X.lineTop[f1] + X.lineH[f1] / 2, here = c0 + (c1 - c0) * fr;
            cairo_save(cr);
            cairo_rectangle(cr, 0, lTop, W, lH);
            cairo_clip(cr);
            for (int i = 0; i < n; i++) {
                const double top = center + X.lineTop[i] - here, bottom = top + X.lineH[i];
                if (bottom < lTop - 4) continue;
                if (top > lBottom + 4) break;
                const double mid = (top + bottom) / 2;
                const double emph = std::clamp(1.0 - std::fabs(i - X.anim), 0.0, 1.0);
                const double edge = std::clamp(std::min(mid - lTop, lBottom - mid) / 44.0, 0.0, 1.0);
                lineText(i);
                cairo_set_source_rgba(cr, 1, 1, 1, (0.30 + 0.70 * emph) * (0.25 + 0.75 * edge));
                cairo_move_to(cr, pad + 8 * emph, top);
                pango_cairo_show_layout(cr, layout);
            }
            cairo_restore(cr);
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
            pango_layout_set_width(layout, -1);
        } else {
            const std::string msg = X.lyricsStatus == "loading" ? "Looking for the lyrics…" : "No synced lyrics for this track";
            drawIcon(cr, "music-notes", pad, center - 34, 26, Color{1, 1, 1}, false);
            text(msg, 14, true, pad, center + 6, 0.8, W - 2 * pad);
        }
    }

    const double py = H - 90, pw = W - 2 * pad;
    const double len = M.length, pos = mediaPositionNow(M), frac = len > 0 ? std::clamp(pos / len, 0.0, 1.0) : 0;
    X.hits.push_back({kMwSeek, Rect{pad - 6, py - 12, pw + 12, 28}});
    const bool seekHot = X.hover == kMwSeek;
    const double inner = pw - 8, hx = pad + 4 + frac * inner, hw = seekHot ? 3 : 4, gap = 4;
    roundedRect(cr, pad, py - 3, std::max(0.0, hx - hw / 2 - gap - pad), 6, 3);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
    cairo_fill(cr);
    roundedRect(cr, hx + hw / 2 + gap, py - 3, std::max(0.0, pad + pw - hx - hw / 2 - gap), 6, 3);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
    cairo_fill(cr);
    roundedRect(cr, hx - hw / 2, py - (seekHot ? 14 : 12), hw, seekHot ? 28 : 24, hw / 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_fill(cr);
    text(friendlyTime(pos), 11, false, pad, py + 20, 0.7, 80);
    text(friendlyTime(len), 11, false, W - pad - 80, py + 20, 0.7, 80, false, 2);

    const double cy = H - 38, cx = W / 2.0;
    const bool playing = M.status == "Playing";
    auto circleBtn = [&](int id, double x, double r, const char* icon, int isz) {
        X.hits.push_back({id, Rect{x - r, cy - r, 2 * r, 2 * r}});
        if (X.hover == id) {
            cairo_new_path(cr);
            cairo_arc(cr, x, cy, r, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
            cairo_fill(cr);
        }
        drawIcon(cr, icon, x - isz / 2.0, cy - isz / 2.0, isz, Color{1, 1, 1}, true);
    };
    circleBtn(kMwPrev, cx - 78, 24, "skip_previous", 28);
    circleBtn(kMwNext, cx + 78, 24, "skip_next", 28);
    X.hits.push_back({kMwPlay, Rect{cx - 30, cy - 30, 60, 60}});
    roundedRect(cr, cx - 29, cy - 29, 58, 58, X.hover == kMwPlay ? 20 : (playing ? 22 : 29));
    cairo_set_source_rgba(cr, 1, 1, 1, X.hover == kMwPlay ? 1.0 : 0.94);
    cairo_fill(cr);
    drawIcon(cr, playing ? "pause" : "play_arrow", cx - 15, cy - 15, 30, Color{0.07, 0.07, 0.09}, true);
    g_object_unref(layout);
}

void mediaWClick(App& app, DeskWidget& d, int id) {
    MediaW& X = static_cast<MediaW&>(d);
    MediaPanel& M = app.media;
    const auto now = std::chrono::steady_clock::now();
    if (id == kMwPrev) mediaSend(app, {"previous"});
    else if (id == kMwNext) mediaSend(app, {"next"});
    else if (id == kMwPlay) {
        mediaSend(app, {"play-pause"});
        M.position = mediaPositionNow(M);
        M.polledAt = now;
        M.status = M.status == "Playing" ? "Paused" : "Playing";
    } else if (id == kMwSeek && M.length > 0) {
        const double pad = 22, frac = std::clamp((app.pointerX - kDeskRoom - pad - 4) / (X.w - 2 * pad - 8), 0.0, 1.0);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", frac * M.length);
        mediaSend(app, {"position", buf});
        M.position = frac * M.length;
        M.polledAt = now;
        X.activeIdx = -1;
    } else return;
    X.nextPoll = now + std::chrono::milliseconds(300);
    deskDraw(app, X);
}

int mediaWCursor(const DeskWidget&, int id) { return id >= 1 && id <= 4 ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : -1; }

void mediaWApply(App& app) {
    MediaW& X = app.mediaW;
    std::vector<LyricLine> lines;
    std::string status;
    uint64_t v = 0;
    lyricsSvcGet(lines, status, &v);
    if (v == X.lyricsSeen) return;
    X.lyricsSeen = v;
    if (!X.open()) return;
    X.lines = std::move(lines);
    X.lineH.clear();
    X.lyricsStatus = status;
    X.activeIdx = -1;
    X.anim = 0;
    deskDraw(app, X);
}

void mediaWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs) {
    MediaW& X = app.mediaW;
    if (!X.open() || !X.configured) return;
    auto want = [&](int ms) { timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms); };
    bool redraw = false;
    const std::string key = app.musicTitle + "\x1f" + app.musicArtist + "\x1f" + app.cfg.lyricsProvider;
    if (key != X.trackKey) {
        X.trackKey = key;
        X.lines.clear();
        X.lineH.clear();
        X.activeIdx = -1;
        X.anim = 0;
        redraw = true;
        if (!app.musicTitle.empty() && !app.musicArtist.empty()) {
            pollMedia(app);
            lyricsSvcRequest(app.musicTitle, app.musicArtist, app.media.length, app.cfg.lyricsProvider);
            X.lyricsStatus = "loading";
            X.nextPoll = now + std::chrono::seconds(1);
        } else X.lyricsStatus = "idle";
    }
    const bool playing = app.media.status == "Playing" && !app.musicStatus.empty();
    if (!app.musicStatus.empty() && now >= X.nextPoll) {
        pollMedia(app);
        X.nextPoll = now + std::chrono::seconds(1);
        redraw = true;
    }
    if (X.lyricsStatus == "ok" && !X.lines.empty()) {
        const double pos = mediaPositionNow(app.media) + 0.15;
        int lo = 0, hi = static_cast<int>(X.lines.size()) - 1, idx = -1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            if (X.lines[mid].time <= pos) { idx = mid; lo = mid + 1; } else hi = mid - 1;
        }
        X.activeIdx = idx;
    }
    const double dt = X.last == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(now - X.last).count());
    X.last = now;
    const double target = std::max(X.activeIdx, 0);
    if (std::fabs(target - X.anim) > 0.003) {
        X.anim += (target - X.anim) * (1 - std::exp(-dt * 9));
        redraw = true;
        want(16);
    } else X.anim = target;
    if (playing && now >= X.nextDraw) { X.nextDraw = now + std::chrono::milliseconds(250); redraw = true; }
    want(playing ? 100 : 500);
    if (redraw) deskDraw(app, X);
}

void mediaWToggled(App& app, bool open) {
    MediaW& X = app.mediaW;
    X.trackKey.clear();
    X.nextPoll = std::chrono::steady_clock::now();
    if (!open) {
        if (X.blur) { cairo_surface_destroy(X.blur); X.blur = nullptr; X.blurKey.clear(); }
        X.lines.clear();
    }
    (void)app;
}

void mediaWInit(MediaW& X) {
    X.name = "player";
    X.minW = 340; X.maxW = 640;
    X.minH = 340; X.maxH = 640;
    X.w = 400; X.h = 420;
    X.radius = 30;
    X.lightHandle = true;
    X.paint = mediaWPaint;
    X.click = mediaWClick;
    X.DeskWidget::cursor = mediaWCursor;
    X.onToggle = mediaWToggled;
}
