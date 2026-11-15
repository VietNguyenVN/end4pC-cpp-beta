
constexpr double kPopInSeconds = 0.34, kPopSwallowSeconds = 0.17, kPopOvershoot = 1.3, kPopSettleSeconds = 0.08;

bool popStep(int& phase, double& reveal, double from, std::chrono::steady_clock::time_point animStart,
             std::chrono::steady_clock::time_point& settleAt, std::chrono::steady_clock::time_point now, double overshoot = kPopOvershoot, double inSeconds = 0, double outSeconds = 0) {
    const double since = std::chrono::duration<double>(now - animStart).count();
    double speed = g_animSpeed / 100.0;
    if (!g_animEnabled) speed = 1e6;
    if (g_animStyle != "boing") overshoot = 0;
    inSeconds = (inSeconds > 0 ? inSeconds : kPopInSeconds) / speed;
    outSeconds = (outSeconds > 0 ? outSeconds : kPopSwallowSeconds) / speed;
    if (phase == kPopIn) {
        const double t = std::min(1.0, since / inSeconds), u = t - 1;
        reveal = from + (1 - from) * (u * u * ((overshoot + 1) * u + overshoot) + 1);
        if (t >= 1.0) { reveal = 1; phase = kPopOpen; }
    } else if (phase == kPopOut) {
        const double t = std::min(1.0, since / outSeconds);
        reveal = from * (1 - t * t * t);
        if (t >= 1.0) {
            reveal = 0;
            phase = kPopGone;
            settleAt = now + std::chrono::milliseconds(static_cast<long>(kPopSettleSeconds * 1000));
        }
    } else if (phase == kPopGone && now >= settleAt) {
        return true;
    }
    return false;
}

std::chrono::steady_clock::time_point popWake(int phase, std::chrono::steady_clock::time_point settleAt, std::chrono::steady_clock::time_point now) {
    if (phase == kPopIn || phase == kPopOut) return now + std::chrono::milliseconds(16);
    if (phase == kPopGone) return settleAt;
    return std::chrono::steady_clock::time_point::max();
}

void popGroupBegin(cairo_t* cr) { cairo_push_group(cr); }
constexpr double kPopSlidePx = 36, kPopZoomFrom = 0.88;
void popGroupEnd(cairo_t* cr, double rev, double ox, double oy, double alpha) {
    cairo_pop_group_to_source(cr);
    cairo_matrix_t m;
    if (g_animStyle == "boing") {
        cairo_matrix_init(&m, 1 / rev, 0, 0, 1 / rev, ox * (1 - 1 / rev), oy * (1 - 1 / rev));
    } else {
        const double r = std::clamp(rev, 0.0, 1.0);
        double x1, y1, x2, y2;
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        const double cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
        cairo_matrix_init_identity(&m);
        if (g_animStyle == "slide") {
            double dx = ox - cx, dy = oy - cy;
            double len = std::hypot(dx, dy);
            if (len < 1) { dx = 0; dy = -1; len = 1; }
            const double off = kPopSlidePx * (1 - r);
            cairo_matrix_init_translate(&m, -dx / len * off, -dy / len * off);
        } else if (g_animStyle == "zoom") {
            const double s = kPopZoomFrom + (1 - kPopZoomFrom) * r;
            cairo_matrix_init(&m, 1 / s, 0, 0, 1 / s, cx * (1 - 1 / s), cy * (1 - 1 / s));
        }
        alpha *= r;
    }
    cairo_pattern_set_matrix(cairo_get_source(cr), &m);
    cairo_paint_with_alpha(cr, alpha);
}

template <class Paint>
void popPaintGrown(cairo_t* cr, double rev, double ox, double oy, double alpha, Paint paint) {
    popGroupBegin(cr);
    paint();
    popGroupEnd(cr, rev, ox, oy, alpha);
}

double popReach(const App& app) {
    const double gap = app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0;
    const double widgetOffset = app.cfg.frame + gap + barThickness(app) / 2.0;
    const double cardOffset = sideBar(app) ? sideCardEdge(app) : app.cfg.frame + kBarHeight + 12 + gap;
    return std::max(8.0, cardOffset - widgetOffset);
}

void popGrowOrigin(const App& app, double cx, double cy, double cw, double ch, double along, double& ox, double& oy) {
    const double reach = popReach(app);
    if (sideBar(app)) {
        ox = app.cfg.barPosition == "right" ? cx + cw + reach : cx - reach;
        oy = along < 0 ? cy + ch / 2 : std::clamp(along, cy + 24, std::max(cy + 24, cy + ch - 24));
    } else {
        oy = app.cfg.barPosition == "bottom" ? cy + ch + reach : cy - reach;
        ox = along < 0 ? cx + cw / 2 : std::clamp(along, cx + 24, std::max(cx + 24, cx + cw - 24));
    }
}

void popStart(PopAnim& p) {
    p.phase = kPopIn;
    p.from = 0;
    p.reveal = 0;
}
bool popIsOut(const PopAnim& p) { return p.phase == kPopOut || p.phase == kPopGone; }
bool popRetract(PopAnim& p, std::chrono::steady_clock::time_point now) {
    if (popIsOut(p)) return false;
    p.phase = kPopOut;
    p.from = p.reveal;
    p.animStart = now;
    return true;
}
void popReverse(PopAnim& p, std::chrono::steady_clock::time_point now) {
    p.phase = kPopIn;
    p.from = p.reveal;
    p.animStart = now;
}
bool popTick(PopAnim& p, std::chrono::steady_clock::time_point now, bool& moving) {
    moving = p.phase == kPopIn || p.phase == kPopOut;
    return popStep(p.phase, p.reveal, p.from, p.animStart, p.settleAt, now, p.overshoot, p.inSeconds, p.outSeconds);
}

void popStartQuick(PopAnim& p, double inSeconds, double outSeconds) {
    p.overshoot = 0;
    p.inSeconds = inSeconds;
    p.outSeconds = outSeconds;
    popStart(p);
}
void popFadeEnd(cairo_t* cr, double rev, double ox, double oy, double scale0) {
    const double r = std::clamp(rev, 0.0, 1.0);
    if (g_animStyle == "boing") popGroupEnd(cr, scale0 + (1 - scale0) * r, ox, oy, r);
    else popGroupEnd(cr, r, ox, oy, 1.0);
}
void popSlideEnd(cairo_t* cr, double rev, double dx, double dy) {
    const double r = std::clamp(rev, 0.0, 1.0);
    cairo_pop_group_to_source(cr);
    cairo_matrix_t m;
    cairo_matrix_init_translate(&m, -dx * (1 - r), -dy * (1 - r));
    cairo_pattern_set_matrix(cairo_get_source(cr), &m);
    cairo_paint_with_alpha(cr, r);
}
