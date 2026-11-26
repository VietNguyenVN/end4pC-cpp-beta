
bool anyPopupOpen(const App& app);
void islandPowerActivate(App& app, int i);
void islandToggleView(App& app);
void islandRegion(App& app);
void retractIslandCard(App& app);

constexpr double kIcW = 380, kIcH = 180, kIcPad = 22, kIcMPad = 16, kIcRoom = 20, kIcOver = 56,
               kIcRadius = 28;
constexpr double kIcImg = kIcH - 2 * kIcMPad, kIcImgRadius = 22;
constexpr double kIcPlay = 52, kIcSide = 44, kIcGap = 6, kIcBarH = 4;
constexpr int kIcLeaveMs = 250;

struct IcItem { int id; Rect r; };
std::vector<IcItem> icItems() {
    const double y = kIcH - kIcMPad - kIcPlay, x = kIcMPad, sy = y + (kIcPlay - kIcSide) / 2;
    return {{1, {x, y, kIcPlay, kIcPlay}}, {0, {x + kIcPlay + kIcGap, sy, kIcSide, kIcSide}}, {2, {x + kIcPlay + kIcGap + kIcSide + 2, sy, kIcSide, kIcSide}}};
}
constexpr int kIcNClose = 20, kIcNCard = 21, kIcNAct = 30;
constexpr double kIcNIcon = 52, kIcNClosePx = 26, kIcNBtnH = 32;
constexpr double kIcNAppCy = kIcPad + 7, kIcNTitleCy = kIcNAppCy + 7 + 4 + 10, kIcNBodyTop = kIcNTitleCy + 10 + 4;
uint32_t g_icNotif = 0;
std::chrono::steady_clock::time_point g_icNotifUntil{};
std::vector<IcItem> g_icNotifItems;
std::vector<std::string> g_icNotifKeys;
double g_icNotifH = 120;
const Notif* icNotifOf(const App& app) {
    if (!g_icNotif) return nullptr;
    for (const Notif& n : app.notifs) if (n.id == g_icNotif) return &n;
    return nullptr;
}
bool islandNotifShown(const App& app) { return icNotifOf(app) != nullptr; }
void icNotifBuild(const Notif& n) {
    g_icNotifItems.clear();
    g_icNotifKeys.clear();
    std::vector<std::pair<std::string, std::string>> acts;
    for (const auto& a : n.actions) if (!a.second.empty() && acts.size() < 3) acts.push_back(a);
    g_icNotifItems.push_back({kIcNClose, {kIcW - kIcPad - kIcNClosePx, 16, kIcNClosePx, kIcNClosePx}});
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* L = pango_cairo_create_layout(cr);
    double bodyH = 0;
    if (!n.body.empty()) {
        PangoFontDescription* fd = pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
        pango_layout_set_font_description(L, fd);
        pango_font_description_free(fd);
        pango_layout_set_width(L, static_cast<int>((kIcW - (kIcPad + kIcNIcon + 14) - kIcPad) * PANGO_SCALE));
        pango_layout_set_wrap(L, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_END);
        pango_layout_set_height(L, -2);
        setNotifBody(L, n.body);
        int bw = 0, bh = 0;
        pango_layout_get_pixel_size(L, &bw, &bh);
        bodyH = bh;
        pango_layout_set_attributes(L, nullptr);
        pango_layout_set_height(L, -1);
        pango_layout_set_width(L, -1);
        pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_NONE);
    }
    const double textBottom = kIcNBodyTop + (bodyH > 0 ? bodyH : -4);
    g_icNotifH = std::max<double>(kIcPad + kIcNIcon, textBottom) + kIcPad + (acts.empty() ? 0 : 10 + kIcNBtnH);
    std::vector<double> widths;
    double row = 0;
    for (size_t i = 0; i < acts.size(); i++) {
        widths.push_back(clkTextWidth(L, acts[i].second, 13, false) + 28.0);
        row += widths.back() + (i ? 6 : 0);
    }
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    double bx = std::max<double>(kIcPad + kIcNIcon + 14, kIcW - kIcPad - row);
    for (size_t i = 0; i < acts.size(); i++) {
        if (bx + widths[i] > kIcW - kIcPad + 0.5) break;
        g_icNotifItems.push_back({kIcNAct + static_cast<int>(i), {bx, g_icNotifH - kIcPad - kIcNBtnH, widths[i], kIcNBtnH}});
        g_icNotifKeys.push_back(acts[i].first);
        bx += widths[i] + 6;
    }
    g_icNotifItems.push_back({kIcNCard, {0, 0, kIcW, g_icNotifH}});
}

struct IcLive { bool active = false, dot = false; std::string key, icon, label, value, iconRole = "on_surface", valueRole = "on_surface"; bool track = false; };
IcLive g_icLive;
std::chrono::steady_clock::time_point g_icLiveUntil{};
double g_icLiveW = 200;
constexpr double kIcLiveMinW = 200, kIcLiveMaxW = 380, kIcLiveIcon = 22, kIcLivePadL = 9, kIcLivePadR = 14, kIcLiveGap = 8;

constexpr int kIcRecStop = 50;
constexpr double kIcRecH = 64, kIcRecBtn = 44, kIcRecPad = 10;
std::vector<IcItem> icRecItems() { return {{kIcRecStop, {kIcW - kIcRecPad - kIcRecBtn, (kIcRecH - kIcRecBtn) / 2, kIcRecBtn, kIcRecBtn}}}; }

constexpr double kIcPMax = 540, kIcPPad = 12, kIcPAir = 11, kIcPLabelPx = 10, kIcPSlotH = 76, kIcPH = kIcPSlotH + 2 * kIcPPad;
constexpr double kIcPIconCy = 29, kIcPLabelCy = 59;
constexpr int kIcPSlot = 40;
bool g_icPowerMode = false;
int g_icPSel = -1, g_icPArmed = -1;
double g_icPPos = 0, g_icPVel = 0, g_icPLit = 0;
bool g_icPMoving = false;
std::chrono::steady_clock::time_point g_icPLast{};
struct IcPowerGeom { double w = 0; std::vector<double> left, width; std::string key; };
const IcPowerGeom& icPowerGeom() {
    static IcPowerGeom g;
    std::string key;
    for (const PowerAction& a : kPowerActions) key += std::string(i18n::tr(a.label)) + "|";
    key += i18n::tr("Confirm");
    if (g.key == key) return g;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* L = pango_cairo_create_layout(cr);
    const double confirmW = clkTextWidth(L, i18n::tr("Confirm"), kIcPLabelPx, false);
    std::vector<double> nat;
    double sum = 0;
    for (const PowerAction& a : kPowerActions) {
        nat.push_back(std::max({static_cast<double>(clkTextWidth(L, i18n::tr(a.label), kIcPLabelPx, false)), confirmW, 40.0}) + 2 * kIcPAir);
        sum += nat.back();
    }
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    g.w = std::clamp(sum + 2 * kIcPPad, 400.0, kIcPMax);
    const double extra = (g.w - 2 * kIcPPad - sum) / kPowerCount;
    g.left.clear();
    g.width.clear();
    double x = kIcPPad;
    for (double n : nat) { g.left.push_back(x); g.width.push_back(n + extra); x += n + extra; }
    g.key = key;
    return g;
}
double islandCardW(const App&) { return g_icPowerMode ? icPowerGeom().w : g_icLive.active ? g_icLiveW : kIcW; }
double islandCardH(const App& app) { return g_icPowerMode ? kIcPH : islandNotifShown(app) ? g_icNotifH : g_icLive.active ? kBarHeight : islandShowsRec(app) ? kIcRecH : kIcH; }
std::vector<IcItem> icPowerItems() {
    const IcPowerGeom& g = icPowerGeom();
    std::vector<IcItem> v;
    for (int i = 0; i < kPowerCount; i++) v.push_back({kIcPSlot + i, {g.left[static_cast<size_t>(i)], kIcPPad, g.width[static_cast<size_t>(i)], kIcPSlotH}});
    return v;
}

constexpr int kIcImage = 10, kIcLock = 11, kIcSettings = 12, kIcPower = 13, kIcPick = 14, kIcReset = 15;
constexpr double kIcPowerW = 50, kIcSettingsW = 30;
constexpr double kIcCorner = 32, kIcCornerPad = 12;
constexpr double kIcBanner = 112, kIcAv = 44, kIcEdge = 14, kIcRound = 36, kIcRoundGap = 6;
bool islandIdle(const App& app) { return !islandShowsMusic(app); }
int islandBaseMode(const App& app) { return islandShowsRec(app) ? 2 : islandShowsMusic(app) ? 1 : 0; }
std::vector<IcItem> icIdleItems(const App& app) {
    const double y = kIcH - kIcEdge - kIcRound, xPower = kIcW - kIcEdge - kIcPowerW, xLock = xPower - kIcRoundGap - kIcRound, xSet = xLock - kIcRoundGap - kIcSettingsW;
    std::vector<IcItem> v;
    v.push_back({kIcPick, {kIcW - kIcCornerPad - kIcCorner, kIcCornerPad, kIcCorner, kIcCorner}});
    if (!app.userW.customImage.empty()) v.push_back({kIcReset, {kIcW - kIcCornerPad - 2 * kIcCorner - 6, kIcCornerPad, kIcCorner, kIcCorner}});
    v.push_back({kIcImage, {0, 0, kIcW, kIcBanner}});
    v.push_back({kIcPower, {xPower, y, kIcPowerW, kIcRound}});
    v.push_back({kIcLock, {xLock, y, kIcRound, kIcRound}});
    v.push_back({kIcSettings, {xSet, y, kIcSettingsW, kIcRound}});
    return v;
}
std::vector<IcItem> icCurrentItems(const App& app) { return g_icPowerMode ? icPowerItems() : islandNotifShown(app) ? g_icNotifItems : g_icLive.active ? std::vector<IcItem>{} : islandShowsRec(app) ? icRecItems() : islandIdle(app) ? icIdleItems(app) : icItems(); }
int icItemAt(const std::vector<IcItem>& items, double x, double y) {
    for (const IcItem& it : items) if (it.r.contains(x, y)) return it.id;
    return -1;
}

void islandBodyPath(cairo_t* cr, const IslandCard& C, double r) {
    const double t = std::clamp(r, 0.0, 1.0);
    const double x0 = C.col.x + (C.exp.x - C.col.x) * r, x1 = C.col.x + C.col.w + (C.exp.x + C.exp.w - C.col.x - C.col.w) * r;
    const double y0 = C.col.y + (C.exp.y - C.col.y) * r, y1 = C.col.y + C.col.h + (C.exp.y + C.exp.h - C.col.y - C.col.h) * r;
    const double h = y1 - y0;
    if (!C.notch) {
        const double rad = std::min(C.col.h / 2.0 + (kIcRadius - C.col.h / 2.0) * t, h / 2.0);
        roundedRect(cr, x0, y0, x1 - x0, h, rad);
        return;
    }
    const double e = kNotchEar, r0 = std::min(kFloatRadius + 2, C.col.h / 2.0 - 1), rad = std::min(r0 + (kIcRadius - r0) * t, h / 2.0);
    cairo_save(cr);
    if (C.flip) { cairo_translate(cr, 0, y0 + y1); cairo_scale(cr, 1, -1); }
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x0 - e, y0);
    cairo_arc(cr, x0 - e, y0 + e, e, -M_PI / 2, 0);
    cairo_line_to(cr, x0, y1 - rad);
    cairo_arc_negative(cr, x0 + rad, y1 - rad, rad, M_PI, M_PI / 2);
    cairo_line_to(cr, x1 - rad, y1);
    cairo_arc_negative(cr, x1 - rad, y1 - rad, rad, M_PI / 2, 0);
    cairo_line_to(cr, x1, y0 + e);
    cairo_arc(cr, x1 + e, y0 + e, e, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
    cairo_restore(cr);
}

bool g_icTicking = false;
std::chrono::steady_clock::time_point g_icTickSince{};
std::string g_icTickKey;
void icText(cairo_t* cr, PangoLayout* L, const std::string& s, double px, bool bold, double x, double cy, const Color& c, double alpha, double maxW) {
    pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_width(L, -1);
    const int fw = clkTextWidth(L, s, px, bold);
    if (fw <= maxW) { clkText(cr, L, s, px, bold, x, cy, c, alpha); return; }
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(L, &tw, &th);
    constexpr double kGap = 36, kWait = 0.8, kSpeed = 42;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_icTickSince).count() - kWait;
    const double off = secs > 0 ? std::fmod(secs * kSpeed, fw + kGap) : 0;
    cairo_save(cr);
    cairo_rectangle(cr, x, cy - th / 2.0 - 1, maxW, th + 2);
    cairo_clip(cr);
    clkText(cr, L, s, px, bold, x - off, cy, c, alpha);
    clkText(cr, L, s, px, bold, x - off + fw + kGap, cy, c, alpha);
    cairo_restore(cr);
    g_icTicking = true;
}

// The progress bar of the track card: where it is (also what a click or a drag on it seeks).
struct IcSeekGeom { double sx, sw, pad, mid; };
IcSeekGeom icSeekGeom() {
    const double imgX = kIcW - kIcMPad - kIcImg, tw = imgX - kIcMPad - 12;
    const double artistCy = kIcMPad + 14 + 14 + 6 + 9;
    return {kIcMPad, tw - 8, 4, (artistCy + 9 + (kIcH - kIcMPad - kIcPlay)) / 2.0};
}

void islandContentPaint(App& app, cairo_t* cr) {
    PangoLayout* L = pango_cairo_create_layout(cr);
    const IslandCard& C = app.ic;
    const MediaColors& K = app.media.col;
    const bool playing = app.musicStatus == "Playing";
    const double imgX = kIcW - kIcMPad - kIcImg, imgY = kIcH - kIcMPad - kIcImg;
    cairo_surface_t* art = C.art ? C.art : app.musicArt;
    if (art) {
        cairo_save(cr);
        roundedRect(cr, imgX, imgY, kIcImg, kIcImg, kIcImgRadius);
        cairo_clip(cr);
        cairo_translate(cr, imgX, imgY);
        const double sc = kIcImg / cairo_image_surface_get_width(art);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, art, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        roundedRect(cr, imgX, imgY, kIcImg, kIcImg, kIcImgRadius);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        drawIcon(cr, "music_note", imgX + (kIcImg - 48) / 2.0, imgY + (kIcImg - 48) / 2.0, 48, app.color("on_surface_variant"));
    }
    const double tw = imgX - kIcMPad - 12;
    pango_layout_set_width(L, static_cast<int>(tw * PANGO_SCALE));
    pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_END);
    const double titleCy = kIcMPad + 14, artistCy = titleCy + 14 + 6 + 9;
    icText(cr, L, musicTitleShown(app), 22, true, kIcMPad, titleCy, app.color("on_surface"), 1, tw);
    icText(cr, L, app.musicArtist, 14, false, kIcMPad, artistCy, app.color("on_surface_variant"), 1, tw);
    pango_layout_set_width(L, -1);
    pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_NONE);
    const double pos = mediaPositionNow(app.media), len = app.media.length;
    const IcSeekGeom SG = icSeekGeom();
    const double sx = SG.sx, sw = SG.sw, pad = SG.pad, eff = sw - 2 * pad, mid = SG.mid;
    const double v = len > 0 ? std::clamp(pos / len, 0.0, 1.0) : 0, hx = sx + pad + v * eff, handleW = 3;
    const double phase = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() * 1000.0 / 400.0;
    setSource(cr, K.primary);
    if (hx - handleW / 2 - 4 > sx + pad) wavyLine(cr, sx + pad, hx - handleW / 2 - 4, mid, 4, playing ? 0.5 : 0, 5, sw, phase);
    roundedRect(cr, hx - handleW / 2, mid - 12, handleW, 24, handleW / 2);
    cairo_fill(cr);
    const double tx0 = hx + handleW / 2 + 4, tx1 = sx + sw - pad;
    if (tx1 > tx0) {
        roundedRect(cr, tx0, mid - 2, tx1 - tx0, 4, 2);
        setSource(cr, K.secondaryContainer);
        cairo_fill(cr);
        cairo_new_path(cr);
        cairo_arc(cr, tx1 - 1.5, mid, 1.5, 0, 2 * M_PI);
        setSource(cr, K.primary);
        cairo_fill(cr);
    }
    for (const IcItem& it : icItems()) {
        const bool hot = C.hover == it.id, down = C.pressed == it.id;
        const Rect r = it.r;
        cairo_new_path(cr);
        cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 2 * M_PI);
        if (it.id == 1) {
            setSource(cr, down ? K.primaryActive : hot ? K.primaryHover : K.primary);
            cairo_fill(cr);
            drawIcon(cr, playing ? "pause" : "play_arrow", r.x + (r.w - 26) / 2, r.y + (r.h - 26) / 2, 26, K.onPrimary, true);
        } else {
            if (hot || down) {
                setSource(cr, down ? K.secondaryContainerActive : K.secondaryContainer);
                cairo_fill(cr);
            }
            drawIcon(cr, it.id == 0 ? "skip_previous" : "skip_next", r.x + (r.w - 24) / 2, r.y + (r.h - 24) / 2, 24, hot || down ? K.onSecondaryContainer : K.primary, true);
        }
    }
    g_object_unref(L);
}

void islandNotifPaint(App& app, cairo_t* cr) {
    const Notif* np = icNotifOf(app);
    if (!np) return;
    const Notif& n = *np;
    const IslandCard& C = app.ic;
    const double x0 = kIcPad + kIcNIcon + 14, tw = kIcW - x0 - kIcPad;
    const bool crit = n.urgency >= 2;
    const std::string shape = crit ? "SoftBurst" : app.cfg.notificationShape;
    shapePath(cr, shape, kIcPad, kIcPad, kIcNIcon);
    setSource(cr, app.color(crit ? "primary_container" : "secondary_container"));
    cairo_fill(cr);
    if (cairo_surface_t* pic = notifPicture(app, n)) {
        cairo_save(cr);
        shapePath(cr, shape, kIcPad, kIcPad, kIcNIcon);
        cairo_clip(cr);
        cairo_translate(cr, kIcPad, kIcPad);
        cairo_scale(cr, kIcNIcon / static_cast<double>(cairo_image_surface_get_width(pic)), kIcNIcon / static_cast<double>(cairo_image_surface_get_height(pic)));
        cairo_set_source_surface(cr, pic, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else if (cairo_surface_t* appIcon = app.cfg.notificationAppIcons ? notifAppIcon(app, n) : nullptr) {
        paintNotifAppIcon(cr, appIcon, shape, kIcPad, kIcPad, kIcNIcon, 32);
    } else {
        drawIcon(cr, notifSolarIcon(n), kIcPad + (kIcNIcon - 26) / 2.0, kIcPad + (kIcNIcon - 26) / 2.0, 26, app.color(crit ? "on_primary_container" : "on_secondary_container"), true);
    }
    PangoLayout* L = pango_cairo_create_layout(cr);
    const double appCy = kIcNAppCy, titleCy = kIcNTitleCy, bodyTop = kIcNBodyTop;
    icText(cr, L, notifAppLabel(n), 11, false, x0, appCy, app.color("on_surface_variant"), 1, tw - kIcNClosePx - 8);
    icText(cr, L, n.summary, 16, true, x0, titleCy, app.color("on_surface"), 1, tw);
    if (!n.body.empty()) {
        PangoFontDescription* fd = pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
        pango_layout_set_font_description(L, fd);
        pango_font_description_free(fd);
        pango_layout_set_width(L, static_cast<int>(tw * PANGO_SCALE));
        pango_layout_set_wrap(L, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_END);
        pango_layout_set_height(L, -2);
        setNotifBody(L, n.body);
        setSource(cr, app.color("on_surface_variant"));
        cairo_move_to(cr, x0, bodyTop);
        pango_cairo_show_layout(cr, L);
        pango_layout_set_attributes(L, nullptr);
        pango_layout_set_height(L, -1);
        pango_layout_set_width(L, -1);
        pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_NONE);
    }
    for (const IcItem& it : g_icNotifItems) {
        const bool hot = C.hover == it.id, down = C.pressed == it.id;
        if (it.id == kIcNClose) {
            cairo_new_path(cr);
            cairo_arc(cr, it.r.x + it.r.w / 2, it.r.y + it.r.h / 2, it.r.w / 2, 0, 2 * M_PI);
            setSource(cr, app.color("on_surface"), hot || down ? 0.16 : 0.08);
            cairo_fill(cr);
            drawIcon(cr, "close", it.r.x + (it.r.w - 16) / 2, it.r.y + (it.r.h - 16) / 2, 16, app.color("on_surface_variant"), hot);
        } else if (it.id >= kIcNAct) {
            roundedRect(cr, it.r.x, it.r.y, it.r.w, it.r.h, it.r.h / 2);
            setSource(cr, app.color(crit ? "primary" : "secondary_container"));
            cairo_fill(cr);
            if (hot || down) {
                roundedRect(cr, it.r.x, it.r.y, it.r.w, it.r.h, it.r.h / 2);
                setSource(cr, app.color("on_surface"), down ? 0.16 : 0.1);
                cairo_fill(cr);
            }
            std::string label;
            for (const auto& a : n.actions) if (a.first == g_icNotifKeys[static_cast<size_t>(it.id - kIcNAct)]) { label = a.second; break; }
            clkText(cr, L, label, 13, false, it.r.x, it.r.y + it.r.h / 2, app.color(crit ? "on_primary" : "on_secondary_container"), 1, it.r.w, 1);
        }
    }
    g_object_unref(L);
}

// The pill's spring: boing is underdamped (it passes the entry and settles), the other animation styles are critically damped. The icon sizes
// follow the pill, so they swell and shrink with it.
int icPowerTarget(const App&, const IslandCard& C) { return C.hover >= kIcPSlot ? C.hover - kIcPSlot : g_icPSel; }
void icPowerStep(App& app, std::chrono::steady_clock::time_point now) {
    const IslandCard& C = app.ic;
    const int target = icPowerTarget(app, C);
    const double dt = g_icPLast.time_since_epoch().count() == 0 ? 0.016 : std::min(0.05, std::chrono::duration<double>(now - g_icPLast).count());
    g_icPLast = now;
    const double litTarget = target >= 0 ? 1.0 : 0.0;
    if (target >= 0 && g_icPLit < 0.02) { g_icPPos = target; g_icPVel = 0; }
    if (!g_animEnabled) { g_icPLit = litTarget; if (target >= 0) { g_icPPos = target; g_icPVel = 0; } }
    else {
        const double speed = g_animSpeed / 100.0, k = 380 * speed * speed, zeta = g_animStyle == "boing" ? 0.42 : 1.0, c = 2 * zeta * std::sqrt(k);
        if (target >= 0)
            for (int step = 0; step < 4; step++) {
                const double h = dt / 4;
                g_icPVel += (k * (target - g_icPPos) - c * g_icPVel) * h;
                g_icPPos += g_icPVel * h;
            }
        g_icPLit += std::clamp(litTarget - g_icPLit, -1.0, 1.0) * std::min(1.0, dt * 14 * speed);
        if (std::fabs(litTarget - g_icPLit) < 0.01) g_icPLit = litTarget;
    }
    g_icPMoving = std::fabs(litTarget - g_icPLit) > 0.001 || (target >= 0 && (std::fabs(target - g_icPPos) > 0.002 || std::fabs(g_icPVel) > 0.01));
}

void islandPowerPaint(App& app, cairo_t* cr) {
    const IcPowerGeom& g = icPowerGeom();
    if (g_icPLit > 0.004) {
        const int i0 = std::clamp(static_cast<int>(std::floor(g_icPPos)), 0, kPowerCount - 2);
        const double f = g_icPPos - i0;
        const size_t a = static_cast<size_t>(i0), b = a + 1;
        const double left = g.left[a] + (g.left[b] - g.left[a]) * f, width = g.width[a] + (g.width[b] - g.width[a]) * f;
        const double danger = std::clamp(g_icPPos - (kPowerCount - 2), 0.0, 1.0);
        roundedRect(cr, left + 2, kIcPPad, width - 4, kIcPSlotH, 26);
        setSource(cr, mixColor(app.color("error_container"), app.color("primary_container"), danger), g_icPLit);
        cairo_fill(cr);
    }
    PangoLayout* L = pango_cairo_create_layout(cr);
    for (int i = 0; i < kPowerCount; i++) {
        const bool dangerSlot = i == kPowerCount - 1, armed = g_icPArmed == i;
        const double prox = std::clamp(1 - std::fabs(i - g_icPPos), 0.0, 1.0) * g_icPLit;
        const Color rest = app.color(dangerSlot ? "error" : "on_surface"), lit = app.color(dangerSlot ? "on_error_container" : "on_primary_container");
        const Color ink = mixColor(lit, rest, prox);
        const double isz = 26 + 12 * prox, cx = g.left[static_cast<size_t>(i)] + g.width[static_cast<size_t>(i)] / 2;
        drawIcon(cr, armed ? "check-circle" : kPowerActions[i].icon, cx - isz / 2, kIcPPad + kIcPIconCy - isz / 2, isz, ink, prox > 0.5);
        clkText(cr, L, armed ? i18n::tr("Confirm") : i18n::tr(kPowerActions[i].label), kIcPLabelPx, false, g.left[static_cast<size_t>(i)], kIcPPad + kIcPLabelCy, ink, 1, g.width[static_cast<size_t>(i)], 1);
    }
    g_object_unref(L);
}

constexpr double kIcTrackCover = 28, kIcTrackPad = 6, kIcTrackGap = 10;
void islandTrackPaint(App& app, cairo_t* cr) {
    const double W = app.ic.exp.w, cy = app.ic.exp.h / 2.0, tx = kIcTrackPad + kIcTrackCover + kIcTrackGap;
    const double tw = W - tx - kIcTrackGap - kIcTrackCover - kIcTrackPad;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, kIcTrackPad + kIcTrackCover / 2, cy, kIcTrackCover / 2, 0, 2 * M_PI);
    if (app.musicArt) {
        cairo_clip(cr);
        cairo_translate(cr, kIcTrackPad, cy - kIcTrackCover / 2);
        const double sc = kIcTrackCover / cairo_image_surface_get_width(app.musicArt);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.musicArt, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
    } else {
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
    }
    cairo_restore(cr);
    if (!app.musicArt) drawIcon(cr, "music_note", kIcTrackPad + (kIcTrackCover - 16) / 2.0, cy - 8, 16, app.color("on_surface_variant"));
    PangoLayout* L = pango_cairo_create_layout(cr);
    icText(cr, L, g_icLive.label, 13, true, tx, cy - 5, app.color("on_surface"), 1, tw);
    icText(cr, L, g_icLive.value, 11, false, tx, cy + 6, app.color("on_surface_variant"), 1, tw);
    g_object_unref(L);
    drawIcon(cr, "music_note", W - kIcTrackPad - kIcTrackCover + (kIcTrackCover - 18) / 2.0, cy - 9, 18, app.color("primary"));
}

void islandLivePaint(App& app, cairo_t* cr) {
    if (g_icLive.track) { islandTrackPaint(app, cr); return; }
    const double W = app.ic.exp.w, cy = app.ic.exp.h / 2.0;
    PangoLayout* L = pango_cairo_create_layout(cr);
    if (g_icLive.dot) {
        cairo_new_path(cr);
        cairo_arc(cr, kIcLivePadL + kIcLiveIcon / 2, cy, 6, 0, 2 * M_PI);
        setSource(cr, app.color("error"));
        cairo_fill(cr);
    } else {
        drawIcon(cr, g_icLive.icon.c_str(), kIcLivePadL, cy - kIcLiveIcon / 2, kIcLiveIcon, app.color(g_icLive.iconRole.c_str()), true);
    }
    if (!g_icLive.label.empty()) clkText(cr, L, g_icLive.label, 13, false, kIcLivePadL + kIcLiveIcon + kIcLiveGap, cy, app.color("on_surface"));
    if (!g_icLive.value.empty()) clkText(cr, L, g_icLive.value, 14, true, 0, cy, app.color(g_icLive.valueRole.c_str()), 1, W - kIcLivePadR, 2);
    g_object_unref(L);
}

void islandRecPaint(App& app, cairo_t* cr) {
    const IslandCard& C = app.ic;
    const double cy = kIcRecH / 2;
    const long sec = static_cast<long>(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_recStartAt).count());
    cairo_new_path(cr);
    cairo_arc(cr, 30, cy, 7, 0, 2 * M_PI);
    setSource(cr, app.color("error"), sec % 2 == 0 ? 1.0 : 0.55);
    cairo_fill(cr);
    PangoLayout* L = pango_cairo_create_layout(cr);
    clkText(cr, L, recElapsed(), 24, true, 48, cy, app.color("on_surface"));
    const IcItem b = icRecItems().front();
    const bool hot = C.hover == kIcRecStop, down = C.pressed == kIcRecStop;
    cairo_new_path(cr);
    cairo_arc(cr, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2, b.r.w / 2, 0, 2 * M_PI);
    setSource(cr, app.color("error"), down ? 1.0 : hot ? 0.9 : 0.75);
    cairo_fill(cr);
    roundedRect(cr, b.r.x + b.r.w / 2 - 8, b.r.y + b.r.h / 2 - 8, 16, 16, 4);
    setSource(cr, app.color("on_error"));
    cairo_fill(cr);
    g_object_unref(L);
}

std::string islandUptime() {
    std::ifstream f("/proc/uptime");
    double secs = 0;
    f >> secs;
    const long m = static_cast<long>(secs) / 60, d = m / 1440, h = (m % 1440) / 60;
    return std::string(i18n::tr("Uptime")) + " \u2022 " + (d > 0 ? std::to_string(d) + "d " : "") + (d > 0 || h > 0 ? std::to_string(h) + "h " : "") + std::to_string(m % 60) + "m";
}
void islandIdlePaint(App& app, cairo_t* cr) {
    UserW& X = app.userW;
    const IslandCard& C = app.ic;
    const bool overPic = C.hover == kIcImage || C.hover == kIcPick || C.hover == kIcReset;
    const double hx0 = C.notch ? -kNotchEar : 0, hw = kIcW + (C.notch ? 2 * kNotchEar : 0);
    cairo_save(cr);
    cairo_push_group(cr);
    if (X.hero) {
        cairo_save(cr);
        const double pw = cairo_image_surface_get_width(X.hero), ph = cairo_image_surface_get_height(X.hero), sc = std::max(hw / pw, kIcBanner / ph);
        cairo_translate(cr, hx0 + (hw - pw * sc) / 2, (kIcBanner - ph * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.hero, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_rectangle(cr, hx0, 0, hw, kIcBanner);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_pattern_t* fade = cairo_pattern_create_linear(0, 0, 0, kIcBanner);
    cairo_pattern_add_color_stop_rgba(fade, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(fade, 0.45, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(fade, 1, 0, 0, 0, 0);
    cairo_mask(cr, fade);
    cairo_pattern_destroy(fade);
    cairo_restore(cr);
    if (overPic) {
        for (const IcItem& it : icIdleItems(app)) {
            if (it.id != kIcPick && it.id != kIcReset) continue;
            const bool hot = C.hover == it.id, down = C.pressed == it.id;
            cairo_new_path(cr);
            cairo_arc(cr, it.r.x + it.r.w / 2, it.r.y + it.r.h / 2, it.r.w / 2, 0, 2 * M_PI);
            setSource(cr, app.color(hot || down ? "surface_container_highest" : "surface_container"), hot || down ? 0.95 : 0.78);
            cairo_fill(cr);
            drawIcon(cr, it.id == kIcPick ? "gallery" : "restart", it.r.x + (it.r.w - 18) / 2, it.r.y + (it.r.h - 18) / 2, 18, app.color("on_surface"), hot);
        }
    }
    const double upCy = kIcH - kIcEdge - 7, nameCy = upCy - 7 - 1 - 8, ay = nameCy - 8 - 2 - kIcAv;
    cairo_new_path(cr);
    cairo_arc(cr, kIcEdge + kIcAv / 2, ay + kIcAv / 2, kIcAv / 2 + 3, 0, 2 * M_PI);
    setSource(cr, app.color("surface"));
    cairo_fill(cr);
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, kIcEdge + kIcAv / 2, ay + kIcAv / 2, kIcAv / 2, 0, 2 * M_PI);
    cairo_clip(cr);
    if (X.face) {
        const double pw = cairo_image_surface_get_width(X.face), ph = cairo_image_surface_get_height(X.face), sc = std::max(kIcAv / pw, kIcAv / ph);
        cairo_translate(cr, kIcEdge + (kIcAv - pw * sc) / 2, ay + (kIcAv - ph * sc) / 2);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, X.face, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
    } else {
        setSource(cr, app.color("primary_container"));
        cairo_paint(cr);
    }
    cairo_restore(cr);
    PangoLayout* L = pango_cairo_create_layout(cr);
    const double stepX = 0, tw = kIcW - kIcEdge - (kIcSettingsW + kIcRound + kIcPowerW + 2 * kIcRoundGap) - kIcEdge - 10;
    pango_layout_set_width(L, static_cast<int>(tw * PANGO_SCALE));
    pango_layout_set_ellipsize(L, PANGO_ELLIPSIZE_END);
    (void)stepX;
    icText(cr, L, X.name + "@" + X.host, 13, true, kIcEdge, nameCy, app.color("on_surface"), 1, tw);
    clkText(cr, L, islandUptime(), 11, false, kIcEdge, upCy, app.color("on_surface_variant"), 0.8, tw, 0);
    g_object_unref(L);
    for (const IcItem& it : icIdleItems(app)) {
        if (it.id != kIcSettings && it.id != kIcLock && it.id != kIcPower) continue;
        const bool hot = C.hover == it.id, down = C.pressed == it.id;
        const Rect r = it.r;
        roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2);
        if (it.id == kIcSettings) {
            if (hot || down) { setSource(cr, app.color("on_surface"), down ? 0.14 : 0.08); cairo_fill_preserve(cr); }
            setSource(cr, app.color("outline"));
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
            drawIcon(cr, "settings", r.x + (r.w - 18) / 2, r.y + (r.h - 18) / 2, 18, app.color("on_surface"), hot);
        } else if (it.id == kIcLock) {
            setSource(cr, app.color("secondary_container"));
            cairo_fill_preserve(cr);
            if (hot || down) { setSource(cr, app.color("on_secondary_container"), down ? 0.16 : 0.08); cairo_fill_preserve(cr); }
            cairo_new_path(cr);
            drawIcon(cr, "lock", r.x + (r.w - 18) / 2, r.y + (r.h - 18) / 2, 18, app.color("on_secondary_container"), hot);
        } else {
            setSource(cr, app.color("primary"));
            cairo_fill_preserve(cr);
            if (hot || down) { setSource(cr, app.color("on_primary"), down ? 0.16 : 0.1); cairo_fill_preserve(cr); }
            cairo_new_path(cr);
            drawIcon(cr, "power", r.x + (r.w - 18) / 2, r.y + (r.h - 18) / 2, 18, app.color("on_primary"), hot);
        }
        cairo_new_path(cr);
    }
}

std::string g_icArtKey;
bool g_icSizeMoving = false;
std::chrono::steady_clock::time_point g_icSizeLast{};
bool g_icSwitching = false;
int g_icPrevMode = 0;
std::chrono::steady_clock::time_point g_icSwitchAt{};
int g_icModeAtOpen = 0;
void islandSyncCover(App& app) {
    IslandCard& C = app.ic;
    if (g_icPowerMode || islandNotifShown(app) || g_icLive.active || islandShowsRec(app)) return;
    if (islandIdle(app)) {
        UserW& X = app.userW;
        if (X.name.empty() || X.name != shownName(app) || X.heroPath != userHeroSource(app, X)) userRefresh(app, X);
        return;
    }
    const std::string path = mediaCoverPath(app);
    if (g_icArtKey != app.musicArtUrl || (!C.art && !path.empty())) {
        if (C.art) { cairo_surface_destroy(C.art); C.art = nullptr; }
        if (!path.empty()) C.art = loadSquare(path, static_cast<int>(kIcImg * 2));
        g_icArtKey = app.musicArtUrl;
    }
    refreshMediaCoverIfNeeded(app);
}

void islandRegion(App& app) {
    IslandCard& C = app.ic;
    if (!C.open()) return;
    const double ear = C.notch ? kNotchEar : 0;
    wl_region* region = wl_compositor_create_region(app.compositor);
    wl_region_add(region, static_cast<int>(C.exp.x - ear), static_cast<int>(C.exp.y), static_cast<int>(C.exp.w + 2 * ear), static_cast<int>(C.exp.h));
    wl_surface_set_input_region(C.surface, region);
    wl_region_destroy(region);
}

void icPaintBase(App& app, cairo_t* cr, int mode) {
    if (mode == 2) islandRecPaint(app, cr);
    else if (mode == 1) islandContentPaint(app, cr);
    else islandIdlePaint(app, cr);
}

void drawIslandCard(App& app) {
    IslandCard& C = app.ic;
    if (!C.open() || !C.configured || !C.bar) return;
    {
        const double th = islandCardH(app), tw = islandCardW(app);
        const auto nowT = std::chrono::steady_clock::now();
        const double dt = g_icSizeLast.time_since_epoch().count() == 0 ? 0.016 : std::min(0.05, std::chrono::duration<double>(nowT - g_icSizeLast).count());
        g_icSizeLast = nowT;
        const double k = g_animEnabled ? 1 - std::exp(-dt * 17 * g_animSpeed / 100.0) : 1.0;
        double nw = C.exp.w + (tw - C.exp.w) * k, nh = C.exp.h + (th - C.exp.h) * k;
        if (std::fabs(tw - nw) < 0.4) nw = tw;
        if (std::fabs(th - nh) < 0.4) nh = th;
        bool changed = false;
        if (nw != C.exp.w) { C.exp.x += (C.exp.w - nw) / 2; C.exp.w = nw; changed = true; }
        if (nh != C.exp.h) { if (C.flip) C.exp.y += C.exp.h - nh; C.exp.h = nh; changed = true; }
        g_icSizeMoving = C.exp.w != tw || C.exp.h != th;
        if (changed) islandRegion(app);
        if (!g_icSizeMoving) g_icSizeLast = {};
    }
    if (g_icPowerMode) icPowerStep(app, std::chrono::steady_clock::now());
    islandSyncCover(app);
    if (const std::string key = app.musicTitle + "\n" + app.musicArtist; key != g_icTickKey) { g_icTickKey = key; g_icTickSince = std::chrono::steady_clock::now(); }
    g_icTicking = false;
    const int W = C.surfW, H = C.surfH;
    hidpi::Frame hf;
    if (!hidpi::begin(hf, W, H, C.scale.factor(), "end4pC-island")) return;
    cairo_surface_t* surf = hf.surface;
    cairo_t* cr = mkcr(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double r = std::max(0.0, C.anim.reveal), t = std::min(1.0, r);
    const Bar& bar = *C.bar;
    // Everything is drawn into one group and the body's curve cuts it ONCE at the end: a body filled first and a picture clipped to the same curve
    // after it would add their antialiased edges, and a dark fringe (dots on the notch's ears) would show round the picture.
    cairo_push_group(cr);
    if (bar.app->cfg.barFollowsFrame) setSource(cr, frameColorOf(*bar.app));
    else setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_paint(cr);
    const double outA = 1 - std::clamp(t / 0.35, 0.0, 1.0);
    if (outA > 0.01) {
        PangoLayout* L = pango_cairo_create_layout(cr);
        cairo_save(cr);
        cairo_translate(cr, C.barOx, C.barOy);
        cairo_push_group(cr);
        islandPaint(cr, app, L, bar);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, outA);
        cairo_restore(cr);
        g_object_unref(L);
    }
    const double lin = std::clamp((t - 0.4) / 0.6, 0.0, 1.0), soft = lin * lin * (3 - 2 * lin);
    const bool banner = !g_icPowerMode && !islandNotifShown(app) && !g_icLive.active && !g_icSwitching && islandBaseMode(app) == 0;
    const double inA = g_icLive.active && g_icLive.track && !g_icPowerMode && !islandNotifShown(app) ? 1.0 : banner ? soft * soft : soft;
    if (inA > 0.01) {
        cairo_save(cr);
        cairo_translate(cr, C.exp.x, C.exp.y);
        if (g_animStyle == "slide") cairo_translate(cr, 0, (C.flip ? -1 : 1) * -14 * (1 - inA));
        else if (g_animStyle == "zoom") {
            const double s = 0.9 + 0.1 * inA;
            cairo_translate(cr, C.exp.w / 2 * (1 - s), C.exp.h / 2 * (1 - s));
            cairo_scale(cr, s, s);
        }
        cairo_push_group(cr);
        if (g_icPowerMode) islandPowerPaint(app, cr);
        else if (islandNotifShown(app)) islandNotifPaint(app, cr);
        else if (g_icLive.active) islandLivePaint(app, cr);
        else if (g_icSwitching) {
            const double e = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_icSwitchAt).count() / 0.34;
            const double sw = g_animEnabled ? std::clamp(e, 0.0, 1.0) : 1.0, oldA = 1 - islandSmooth(0.0, 0.55, sw), newA = islandSmooth(0.45, 1.0, sw);
            if (oldA > 0.003) {
                cairo_save(cr);
                cairo_translate(cr, -12 * (1 - oldA), 0);
                cairo_push_group(cr);
                icPaintBase(app, cr, g_icPrevMode);
                cairo_pop_group_to_source(cr);
                cairo_paint_with_alpha(cr, oldA);
                cairo_restore(cr);
            }
            cairo_save(cr);
            cairo_translate(cr, 12 * (1 - newA), 0);
            cairo_push_group(cr);
            icPaintBase(app, cr, islandBaseMode(app));
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, newA);
            cairo_restore(cr);
            if (sw >= 1.0) g_icSwitching = false;
        } else icPaintBase(app, cr, islandBaseMode(app));
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, inA);
        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_new_path(cr);
    islandBodyPath(cr, C, r);
    cairo_fill(cr);
    cairo_destroy(cr);
    wl_buffer* buffer = hidpi::buffer(hf, app.shm);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    updateBlurRegion(app, C.surface, C.blur, W, H, [&](cairo_t* m) { islandBodyPath(m, C, r); cairo_fill(m); });
    hidpi::present(C.surface, buffer, hf, C.scale);
}

void closeIslandCard(App& app) {
    IslandCard& C = app.ic;
    if (!C.open()) return;
    if (C.layer) zwlr_layer_surface_v1_destroy(C.layer);
    releaseBlurSurface(C.blur);
    hidpi::detach(C.scale);
    wl_surface_destroy(C.surface);
    C.surface = nullptr;
    C.layer = nullptr;
    C.configured = false;
    C.bar = nullptr;
    C.hover = C.pressed = -1;
    C.leaveAt = {};
    g_icSwitching = false;
    g_icSizeMoving = false;
    g_icSizeLast = {};
    g_icNotif = 0;
    g_icLive.active = false;
    g_icPowerMode = false;
    g_icPSel = g_icPArmed = -1;
    g_icPLit = 0;
    g_icPLast = {};
    if (C.art) { cairo_surface_destroy(C.art); C.art = nullptr; }
    if (!app.userW.open() && app.userW.hero) { cairo_surface_destroy(app.userW.hero); app.userW.hero = nullptr; app.userW.heroPath.clear(); }
    C.anim = PopAnim{};
    app.onIsland = false;
    drawAll(app);
    updateCursor(app);
    malloc_trim(0);
}

void onIcConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    const bool first = !app->ic.configured;
    if (first) app->ic.anim.animStart = std::chrono::steady_clock::now();
    app->ic.configured = true;
    drawIslandCard(*app);
    if (first) drawAll(*app);
}
void onIcClosed(void* d, zwlr_layer_surface_v1*) { closeIslandCard(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kIcListener = {onIcConfigure, onIcClosed};

bool islandBarBody(const Bar& bar, double& x0, double& x1) {
    const App& app = *bar.app;
    const double ix = slotX(bar, "island"), iw = islandW(app);
    if (app.cfg.barStyle == "split") {
        for (const auto& [a, b] : bar.segs) if (a <= ix + 0.5 && ix + iw <= b + 0.5) { x0 = a; x1 = b; return true; }
        return false;
    }
    for (const Rect& r : islandRects(bar)) if (r.x <= ix + 0.5 && ix + iw <= r.x + r.w + 0.5) { x0 = r.x; x1 = r.x + r.w; return true; }
    return false;
}

void openIslandCard(App& app, Bar* bar) {
    if (!bar || !hasSlot(*bar, "island") || !app.layerShell || !app.compositor || !app.shm) return;
    IslandCard& C = app.ic;
    if (C.open()) return;
    double bx0 = 0, bx1 = 0;
    if (!islandBarBody(*bar, bx0, bx1)) return;
    const bool split = app.cfg.barStyle == "split", atBottom = app.cfg.barPosition == "bottom";
    const double sideMargin = app.cfg.frame + (split ? 0 : kBarGap), edgeGap = app.cfg.frame + (split ? 0 : kBarGap);
    const double barH = bar->height, screenW = bar->width + 2.0 * sideMargin;
    const double cx = sideMargin + (bx0 + bx1) / 2.0;
    const double ear = split ? kNotchEar : 0;
    const double expW = islandCardW(app), expLeft = std::clamp(cx - expW / 2.0, app.cfg.frame + 12.0 + ear, std::max(app.cfg.frame + 12.0 + ear, screenW - app.cfg.frame - 12.0 - ear - expW));
    const double surfLeft = expLeft - kIcOver - ear - kIcRoom;
    C.surfW = static_cast<int>(std::max(kIcW, kIcPMax) + 2 * (kIcOver + ear + kIcRoom));
    C.surfH = static_cast<int>(kIcH + kIcOver + 2 * kIcRoom);
    C.notch = split;
    C.flip = atBottom;
    const double cardH = islandCardH(app);
    C.exp = {expLeft - surfLeft, 0, expW, cardH};
    C.col = {sideMargin + bx0 - surfLeft, 0, bx1 - bx0, barH};
    if (!atBottom) { C.col.y = kIcRoom; C.exp.y = kIcRoom; }
    else { C.col.y = C.surfH - kIcRoom - barH; C.exp.y = C.surfH - kIcRoom - cardH; }
    C.barOx = C.col.x - bx0;
    C.barOy = C.col.y;
    C.bar = bar;
    C.hover = C.pressed = -1;
    C.configured = false;
    C.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, C.scale, C.surface, [&app] { drawIslandCard(app); });
    C.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, C.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-island-card");
    constexpr uint32_t kB = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM, kT = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP, kL = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    zwlr_layer_surface_v1_set_anchor(C.layer, (atBottom ? kB : kT) | kL);
    const int edgeM = static_cast<int>(edgeGap - kIcRoom);
    zwlr_layer_surface_v1_set_margin(C.layer, atBottom ? 0 : edgeM, 0, atBottom ? edgeM : 0, static_cast<int>(surfLeft));
    zwlr_layer_surface_v1_set_size(C.layer, C.surfW, C.surfH);
    zwlr_layer_surface_v1_set_exclusive_zone(C.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(C.layer, g_icPowerMode ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    islandRegion(app);
    popStart(C.anim);
    zwlr_layer_surface_v1_add_listener(C.layer, &kIcListener, &app);
    wl_surface_commit(C.surface);
    if (C.art) { cairo_surface_destroy(C.art); C.art = nullptr; }
    g_icArtKey.clear();
    g_icTickKey.clear();
    g_icModeAtOpen = islandBaseMode(app);
    if (!islandNotifShown(app) && !g_icLive.active && !g_icPowerMode) pollMedia(app);
}

void retractIslandCard(App& app) {
    IslandCard& C = app.ic;
    if (!C.open() || popIsOut(C.anim)) return;
    if (!C.configured) { closeIslandCard(app); return; }
    popRetract(C.anim, std::chrono::steady_clock::now());
    C.hover = C.pressed = -1;
    drawIslandCard(app);
}

bool pointerOnIsland(const App& app) {
    return app.onIsland || (app.hoverBar && hitTest(*app.hoverBar, app.pointerX, app.pointerY) == HitIsland);
}

void islandHoverCheck(App& app) {
    if (!app.hoverBar || app.ic.open() || !islandActive(app) || app.cfg.islandClick) return;
    if (hitTest(*app.hoverBar, app.pointerX, app.pointerY) != HitIsland) return;
    if (anyPopupOpen(app)) return;
    openIslandCard(app, app.hoverBar);
}

void tickIslandCard(App& app, std::chrono::steady_clock::time_point now) {
    IslandCard& C = app.ic;
    if (!C.open() || !C.configured) return;
    bool moving = false;
    if (popTick(C.anim, now, moving)) { closeIslandCard(app); return; }
    if (!popIsOut(C.anim)) {
        if (!islandActive(app)) { g_icNotif = 0; g_icLive.active = false; retractIslandCard(app); return; }
        if (g_icPowerMode) C.leaveAt = {};
        else if (islandNotifShown(app)) {
            const Notif* n = icNotifOf(app);
            const bool held = pointerOnIsland(app);
            if (held && !n->sticky) g_icNotifUntil = now + std::chrono::milliseconds(app.cfg.notificationTimeout);
            C.leaveAt = {};
            if (!held && now >= g_icNotifUntil) { retractIslandCard(app); return; }
        } else if (g_icLive.active) {
            C.leaveAt = {};
            if (now >= g_icLiveUntil) { retractIslandCard(app); return; }
        } else {
            if (pointerOnIsland(app)) C.leaveAt = {};
            else if (C.leaveAt == std::chrono::steady_clock::time_point{}) C.leaveAt = now + std::chrono::milliseconds(kIcLeaveMs);
            else if (now >= C.leaveAt) { retractIslandCard(app); return; }
            if (islandBaseMode(app) != g_icModeAtOpen) { retractIslandCard(app); return; }
        }
    }
    static long recSec = -1;
    const long nowSec = static_cast<long>(std::chrono::duration<double>(now - g_recStartAt).count());
    if (islandShowsRec(app) && !g_icPowerMode && !g_icLive.active && !islandNotifShown(app) && nowSec != recSec) { recSec = nowSec; drawIslandCard(app); }
    if (moving || (g_icPowerMode && g_icPMoving) || g_icSizeMoving || g_icSwitching) drawIslandCard(app);
    else if (!g_icPowerMode && (app.musicStatus == "Playing" || g_icTicking) && now >= C.nextFrame) { C.nextFrame = now + std::chrono::milliseconds(33); drawIslandCard(app); }
}
std::chrono::steady_clock::time_point islandCardWake(const App& app) {
    const IslandCard& C = app.ic;
    if (!C.open()) return std::chrono::steady_clock::time_point::max();
    auto when = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_icSizeMoving || g_icSwitching ? 16 : g_icPowerMode ? (g_icPMoving ? 16 : 100) : app.musicStatus == "Playing" || g_icTicking ? 33 : 100);
    if (C.configured) when = std::min(when, popWake(C.anim.phase, C.anim.settleAt, std::chrono::steady_clock::now()));
    return when;
}

// A click or a drag on the progress bar of the track card seeks the player (the same message the Player panel sends).
bool g_icSeeking = false;
std::chrono::steady_clock::time_point g_icSeekSent{};
bool icSeekable(const App& app) {
    return app.ic.open() && !g_icPowerMode && !islandNotifShown(app) && !g_icLive.active && !islandShowsRec(app) && islandShowsMusic(app) && app.media.length > 0;
}
bool icOnSeekBar(const App& app) {
    const IcSeekGeom G = icSeekGeom();
    const double x = app.pointerX - app.ic.exp.x, y = app.pointerY - app.ic.exp.y;
    return icSeekable(app) && x >= G.sx - 6 && x <= G.sx + G.sw + 6 && y >= G.mid - 14 && y <= G.mid + 14;
}
void icSeekTo(App& app, bool final) {
    MediaPanel& M = app.media;
    const IcSeekGeom G = icSeekGeom();
    const double eff = G.sw - 2 * G.pad, x = app.pointerX - app.ic.exp.x;
    M.position = (eff > 0 ? std::clamp((x - G.sx - G.pad) / eff, 0.0, 1.0) : 0.0) * M.length;
    const auto now = std::chrono::steady_clock::now();
    M.polledAt = now;
    if (final || now - g_icSeekSent > std::chrono::milliseconds(150)) {
        g_icSeekSent = now;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", M.position);
        mediaSend(app, {"position", buf});
    }
    drawIslandCard(app);
}

int islandItemUnder(const App& app) { return icItemAt(icCurrentItems(app), app.pointerX - app.ic.exp.x, app.pointerY - app.ic.exp.y); }

void islandCardMotion(App& app) {
    IslandCard& C = app.ic;
    if (!C.open() || popIsOut(C.anim) || C.anim.reveal < 0.9) return;
    if (g_icSeeking) { icSeekTo(app, false); return; }
    const int hover = islandItemUnder(app);
    if (hover == C.hover) return;
    C.hover = hover;
    if (g_icPowerMode && g_icPArmed != hover - kIcPSlot) g_icPArmed = -1;
    drawIslandCard(app);
    updateCursor(app);
}

void islandCardButton(App& app, uint32_t button, bool pressed) {
    IslandCard& C = app.ic;
    if (popIsOut(C.anim) || C.anim.reveal < 0.9) return;
    if (button == 0x111) {
        if (pressed && islandIdle(app) && islandItemUnder(app) == kIcImage) userSetImage(app, "");
        return;
    }
    if (button != 0x110) return;
    if (g_icSeeking && !pressed) { g_icSeeking = false; icSeekTo(app, true); return; }
    const int k = islandItemUnder(app);
    if (pressed && k < 0 && icOnSeekBar(app)) { g_icSeeking = true; icSeekTo(app, false); return; }
    if (pressed) { C.pressed = k; drawIslandCard(app); return; }
    const int held = C.pressed;
    C.pressed = -1;
    if (held >= 0 && held == k && held == kIcRecStop && islandShowsRec(app) && !g_icPowerMode && !islandNotifShown(app) && !g_icLive.active) { stopRecording(app); retractIslandCard(app); return; }
    if (held >= 0 && held == k && g_icPowerMode) { islandPowerActivate(app, held - kIcPSlot); return; }
    if (held >= 0 && held == k && islandNotifShown(app)) {
        const uint32_t id = g_icNotif;
        if (held >= kIcNAct) notifInvoke(app, id, g_icNotifKeys[static_cast<size_t>(held - kIcNAct)]);
        else if (held == kIcNCard)
            for (const Notif& n : app.notifs)
                if (n.id == id) for (const auto& a : n.actions) if (a.first == "default") notifInvoke(app, id, "default");
        if (Notif* n = findNotif(app, id); n && app.bus && !n->closedSent) { notifyd_emit_closed(app.bus, id, 2); n->closedSent = true; }
        retractIslandCard(app);
        return;
    }
    if (held == kIcReset && held == k && islandIdle(app)) { userSetImage(app, ""); drawIslandCard(app); return; }
    if (held >= 0 && held == k && islandIdle(app)) {
        retractIslandCard(app);
        if (held == kIcPick) userPickImage();
        else if (held == kIcLock) startLock();
        else if (held == kIcSettings) toggleSettings();
        else if (held == kIcPower) openPowerMenu(app);
        return;
    }
    if (held >= 0 && held == k) {
        const char* verb = held == 0 ? "previous" : held == 1 ? "play-pause" : "next";
        mediaSend(app, {verb});
    }
    drawIslandCard(app);
}

void islandNotify(App& app, uint32_t id) {
    const Notif* n = nullptr;
    for (const Notif& x : app.notifs) if (x.id == id) { n = &x; break; }
    if (!n) return;
    const auto now = std::chrono::steady_clock::now();
    g_icLive.active = false;
    g_icNotif = id;
    icNotifBuild(*n);
    g_icNotifUntil = n->sticky ? std::chrono::steady_clock::time_point::max() : now + std::chrono::milliseconds(app.cfg.notificationTimeout);
    IslandCard& C = app.ic;
    if (!C.open()) {
        openIslandCard(app, mainBar(app));
        if (!C.open()) g_icNotif = 0;
        return;
    }
    if (popIsOut(C.anim)) popReverse(C.anim, now);
    C.hover = C.pressed = -1;
    drawIslandCard(app);
}

void islandPowerActivate(App& app, int i) {
    if (i < 0 || i >= kPowerCount) return;
    if (i != 0 && g_icPArmed != i) {
        g_icPArmed = i;
        g_icPSel = i;
        drawIslandCard(app);
        return;
    }
    retractIslandCard(app);
    powerRunAction(i);
}

void islandPowerKey(App& app, uint32_t key) {
    IslandCard& C = app.ic;
    if (!app.xkbState || popIsOut(C.anim)) return;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, key + 8);
    const int lit = C.hover >= kIcPSlot ? C.hover - kIcPSlot : g_icPSel;
    switch (sym) {
        case XKB_KEY_Escape: retractIslandCard(app); return;
        case XKB_KEY_Left: case XKB_KEY_Up: g_icPSel = lit < 0 ? kPowerCount - 1 : (lit + kPowerCount - 1) % kPowerCount; break;
        case XKB_KEY_Right: case XKB_KEY_Down: case XKB_KEY_Tab: g_icPSel = lit < 0 ? 0 : (lit + 1) % kPowerCount; break;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: if (lit >= 0) islandPowerActivate(app, lit); return;
        default: return;
    }
    C.hover = -1;
    g_icPArmed = -1;
    drawIslandCard(app);
}

bool islandOpenPower(App& app) {
    if (!islandActive(app)) return false;
    Bar* bar = mainBar(app);
    if (!bar || !hasSlot(*bar, "island")) return false;
    closeSelector(app);
    closeLauncher(app);
    closeNotifPanel(app);
    closeMediaPanel(app);
    closeClockPanel(app);
    IslandCard& C = app.ic;
    g_icNotif = 0;
    g_icLive.active = false;
    g_icPowerMode = true;
    g_icPSel = g_icPArmed = -1;
    g_icPLit = 0;
    g_icPLast = {};
    if (!C.open()) {
        openIslandCard(app, bar);
        if (!C.open()) g_icPowerMode = false;
        return C.open();
    }
    if (popIsOut(C.anim)) popReverse(C.anim, std::chrono::steady_clock::now());
    C.hover = C.pressed = -1;
    zwlr_layer_surface_v1_set_keyboard_interactivity(C.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    drawIslandCard(app);
    return true;
}

bool islandTogglePower(App& app) {
    if (!islandActive(app)) return false;
    IslandCard& C = app.ic;
    if (C.open() && g_icPowerMode) {
        if (popIsOut(C.anim)) { popReverse(C.anim, std::chrono::steady_clock::now()); drawIslandCard(app); }
        else retractIslandCard(app);
        return true;
    }
    return islandOpenPower(app);
}

void islandToggleView(App& app) {
    if (g_icPowerMode || islandNotifShown(app) || g_icLive.active || !islandActive(app)) return;
    std::vector<std::string> views;
    if (app.recording) views.push_back("rec");
    if (!app.musicStatus.empty()) views.push_back("media");
    views.push_back("clock");
    if (views.size() < 2) return;
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(300)) return;
    last = now;
    const std::string cur = islandShowsRec(app) ? "rec" : islandShowsMusic(app) ? "media" : "clock";
    size_t at = 0;
    for (size_t i = 0; i < views.size(); i++) if (views[i] == cur) at = i;
    const std::string next = views[(at + 1) % views.size()];
    const int oldMode = islandBaseMode(app);
    g_icRecShown = next == "rec";
    if (next != "rec" && setConfig(app, "islandView", next).empty()) applyConfig(app);
    if (app.ic.open()) {
        g_icModeAtOpen = islandBaseMode(app);
        g_icPrevMode = oldMode;
        g_icSwitchAt = std::chrono::steady_clock::now();
        g_icSwitching = true;
    }
    syncCava(app);
    drawAll(app);
    if (app.ic.open()) drawIslandCard(app);
}

void islandLive(App& app, const std::string& key, const std::string& icon, const std::string& label, const std::string& value,
                const char* iconRole, const char* valueRole, double seconds, bool dot = false) {
    if (!islandActive(app) || g_icPowerMode || islandNotifShown(app)) return;
    Bar* bar = mainBar(app);
    if (!bar || !hasSlot(*bar, "island")) return;
    g_icLive = IcLive{true, dot, key, icon, label, value, iconRole, valueRole};
    g_icLiveUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long>(seconds * 1000));
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* L = pango_cairo_create_layout(cr);
    const double left = kIcLivePadL + kIcLiveIcon + (label.empty() ? 0 : kIcLiveGap + clkTextWidth(L, label, 13, false));
    const double right = value.empty() ? 0 : clkTextWidth(L, value, 14, true) + kIcLivePadR;
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    g_icLiveW = std::clamp(left + 24 + right, kIcLiveMinW, kIcLiveMaxW);
    IslandCard& C = app.ic;
    if (!C.open()) {
        openIslandCard(app, bar);
        if (!C.open()) g_icLive.active = false;
        return;
    }
    if (popIsOut(C.anim)) popReverse(C.anim, std::chrono::steady_clock::now());
    C.hover = C.pressed = -1;
    drawIslandCard(app);
}

bool islandLiveOsd(App& app, int kind) {
    if (!islandActive(app)) return false;
    if (kind == OsdBrightness) {
        islandLive(app, "brightness", "sun", "", std::to_string(std::clamp(app.brightness, 0, 100)), "on_surface", "on_surface", app.cfg.osdTimeout / 1000.0);
    } else {
        const int v = app.muted ? 0 : std::max(0, app.volume);
        islandLive(app, "volume", v == 0 ? "volume-cross" : v < 40 ? "volume-small" : "volume-loud", "", std::to_string(v), "on_surface", "on_surface", app.cfg.osdTimeout / 1000.0);
    }
    return true;
}

void islandLiveBattery(App& app, int pct, int what) {
    const std::string v = std::to_string(pct) + "%";
    if (what == 1) islandLive(app, "battery", "battery-charge", i18n::tr("Charging"), v, "primary", "primary", 3.5);
    else if (what == 4) islandLive(app, "battery", "battery-full", i18n::tr("Fully charged"), v, "primary", "primary", 3.5);
    else islandLive(app, "battery", "battery-low", i18n::tr(what == 3 ? "Battery critical" : "Low Battery"), v, "error", "error", 5.0);
}

void islandLiveRecording(App& app, bool started) {
    if (started) { g_icRecShown = true; g_recStartAt = std::chrono::steady_clock::now(); }
    else g_icRecShown = false;
    if (started) islandLive(app, "recording", "", i18n::tr("Recording"), i18n::tr("Started"), "error", "on_surface_variant", 2.5, true);
    else islandLive(app, "recording", "check-circle", i18n::tr("Recording"), i18n::tr("Saved"), "primary", "primary", 3.0);
}

void islandLiveDnd(App& app, bool on) {
    islandLive(app, "dnd", "moon", i18n::tr("Do Not Disturb"), i18n::tr(on ? "On" : "Off"), on ? "primary" : "on_surface_variant", on ? "primary" : "on_surface_variant", 2.5);
}

void islandLiveTrack(App& app) {
    const bool again = app.ic.open() && g_icLive.active && g_icLive.track;
    if (!islandActive(app) || (app.ic.open() && !again) || app.musicTitle.empty()) return;
    if (g_icPowerMode || islandNotifShown(app)) return;
    Bar* bar = mainBar(app);
    if (!bar || !hasSlot(*bar, "island")) return;
    g_icLive = IcLive{true, false, "track", "", app.musicTitle, app.musicArtist, "on_surface", "on_surface", true};
    g_icLiveUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(4500);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* L = pango_cairo_create_layout(cr);
    const double text = std::max(clkTextWidth(L, app.musicTitle, 13, true), clkTextWidth(L, app.musicArtist, 11, false));
    g_object_unref(L);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    g_icLiveW = std::clamp(kIcTrackPad + kIcTrackCover + kIcTrackGap + text + kIcTrackGap + kIcTrackCover + kIcTrackPad, kIcLiveMinW, kIcLiveMaxW);
    g_icTickKey.clear();
    g_icTickSince = std::chrono::steady_clock::now();
    if (again) {
        if (popIsOut(app.ic.anim)) popReverse(app.ic.anim, std::chrono::steady_clock::now());
        drawIslandCard(app);
        return;
    }
    openIslandCard(app, bar);
    if (!app.ic.open()) g_icLive.active = false;
}

// ---- Running a notification's action ----
// The action is sent to the app (ActionInvoked). For the default one ("Show", a click on the card) the app is also brought forward from here:
// an app that asks the compositor for focus by itself (Discord, Telegram...) is refused when the compositor does not focus on activation
// (Hyprland's misc:focus_on_activate off) and nothing would seem to happen. The window is found by the desktop entry or the app name.
void focusNotifApp(App& app, uint32_t id) {
    const Notif* n = nullptr;
    for (const Notif& x : app.notifs) if (x.id == id) { n = &x; break; }
    if (!n) return;
    auto stem = [](std::string v) {
        v = lowerCopy(v);
        if (v.size() > 8 && v.compare(v.size() - 8, 8, ".desktop") == 0) v.resize(v.size() - 8);
        return v;
    };
    const std::string de = stem(n->desktopEntry), an = stem(n->app);
    std::string best;
    int bestRank = 1 << 30;
    std::istringstream in(wmWindowsTsv());
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
        if (f.size() < 3) continue;
        const std::string cls = lowerCopy(f[1]);
        const bool match = !cls.empty() && ((!de.empty() && (cls == de || cls.find(de) != std::string::npos)) || (!an.empty() && an.size() >= 3 && (cls == an || cls.find(an) != std::string::npos)));
        const int rank = std::atoi(f[2].c_str());
        if (match && rank < bestRank) { best = f[0]; bestRank = rank; }
    }
    if (!best.empty()) wmFocusWindow(best);
}

void alarmAction(App& app, const Notif& n, const std::string& key);  // (14u_timers.inc)
void notifInvoke(App& app, uint32_t id, const std::string& key) {
    if (const Notif* n = findNotif(app, id); n && n->desktopEntry == "end4pC-alarm") { alarmAction(app, *n, key); return; }  // (the alarm of a timer or a reminder is ours)
    if (app.bus) notifyd_emit_action(app.bus, id, key.c_str());
    if (key == "default") focusNotifApp(app, id);
}
