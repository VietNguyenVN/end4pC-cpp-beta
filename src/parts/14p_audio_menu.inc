
constexpr double kAmW = 360, kAmPad = 16, kAmRadius = 28, kAmGap = 8, kAmSliderGap = 4;
constexpr double kAmHeadBlockH = 48, kAmHeadGap = 14, kAmTabH = 44, kAmLabelH = 32, kAmNoteH = 56;
constexpr double kAmCardTopH = 72, kAmSliderRowH = 44, kAmCardBottom = 6, kAmCardGap = 8, kAmCardRadius = 24;
constexpr double kAmMuteBox = 36, kAmPctW = 52, kAmSliderInset = 6, kAmScrollStep = 48, kAmScreenMargin = 160, kAmMaxH = 640;
constexpr double kAmSpringK = 180, kAmSpringC = 14, kAmSubStep = 1.0 / 240, kAmSlide = 14;

enum AmAlign { kAmLeft, kAmRight, kAmCenter };

void amPush(AudioMenu& A, AudioMenu::Item::Kind kind, int section, int dev, Rect r, bool fixed = false) {
    AudioMenu::Item it;
    it.kind = kind;
    it.section = section;
    it.dev = dev;
    it.fixed = fixed;
    it.r = r;
    A.items.push_back(it);
}

void amPushVolumeRow(AudioMenu& A, int section, int dev, double cardY) {
    const double rowY = cardY + kAmCardTopH, left = kAmPad + 16;
    const double sliderX = left + kAmMuteBox + kAmGap, pctRight = kAmW - kAmPad - 12;
    amPush(A, AudioMenu::Item::Mute, section, dev, {left, rowY + (kAmSliderRowH - kAmMuteBox) / 2, kAmMuteBox, kAmMuteBox});
    amPush(A, AudioMenu::Item::Slider, section, dev, {sliderX, rowY, pctRight - kAmPctW - kAmSliderGap - sliderX, kAmSliderRowH});
}

void amLayout(App& app) {
    AudioMenu& A = app.amenu;
    using K = AudioMenu::Item::Kind;
    A.items.clear();
    const double w = kAmW - 2 * kAmPad;
    double y = kAmPad;
    amPush(A, K::Head, 0, -1, {kAmPad, y, w, kAmHeadBlockH}, true);
    y += kAmHeadBlockH + kAmHeadGap;
    for (int t = 0; t < 3; t++) amPush(A, K::Tab, 0, t, {kAmPad + t * w / 3.0, y, w / 3.0, kAmTabH}, true);
    y += kAmTabH + kAmHeadGap;
    A.headH = static_cast<int>(y);

    const bool apps = A.tab == AudioMenu::TabApps;
    bool any = false;
    if (apps) {
        for (size_t i = 0; i < A.apps.size(); i++) {
            amPush(A, K::AppRow, 1, static_cast<int>(i), {kAmPad, y, w, kAmCardTopH + kAmSliderRowH + kAmCardBottom});
            amPushVolumeRow(A, 1, static_cast<int>(i), y);
            y += kAmCardTopH + kAmSliderRowH + kAmCardBottom + kAmCardGap;
            any = true;
        }
    } else {
        const bool sink = A.tab == AudioMenu::TabOutput;
        for (int group = 0; group < 2; group++) {
            bool header = false;
            for (size_t i = 0; i < A.devs.size(); i++) {
                const AudioDevice& d = A.devs[i];
                if (d.sink != sink || (d.action == "set") != (group == 0)) continue;
                if (!header) {
                    amPush(A, K::Label, group == 0 ? (sink ? 0 : 1) : 2, -1, {kAmPad, y, w, kAmLabelH});
                    y += kAmLabelH;
                    header = true;
                }
                const bool slider = d.action == "set" && d.active && d.vol >= 0;
                const double h = kAmCardTopH + (slider ? kAmSliderRowH + kAmCardBottom : 0);
                amPush(A, K::Row, 0, static_cast<int>(i), {kAmPad, y, w, h});
                if (slider) amPushVolumeRow(A, 0, static_cast<int>(i), y);
                y += h + kAmCardGap;
                any = true;
            }
        }
    }
    if (!any) {
        amPush(A, K::Note, A.tab, -1, {kAmPad, y, w, kAmNoteH});
        y += kAmNoteH + kAmCardGap;
    }
    A.contentH = static_cast<int>(y - kAmCardGap + kAmPad);
    const int screenH = app.screenH > 0 ? app.screenH : 1080;
    A.cardW = static_cast<int>(kAmW);
    A.regionH = std::max(std::min(screenH - static_cast<int>(kAmScreenMargin), static_cast<int>(kAmMaxH)), A.headH + static_cast<int>(kAmNoteH));
    A.cardH = std::min(A.contentH, A.regionH);
    A.cardH = std::max(A.cardH, A.headH + static_cast<int>(kAmNoteH));
    A.scroll = std::clamp(A.scroll, 0, std::max(0, A.contentH - A.cardH));
}

bool amInteractive(AudioMenu::Item::Kind k) {
    return k == AudioMenu::Item::Tab || k == AudioMenu::Item::Mute || k == AudioMenu::Item::Slider || k == AudioMenu::Item::Row;
}

int amItemAt(const AudioMenu& A, double x, double y) {
    for (size_t k = A.items.size(); k-- > 0;) {
        const AudioMenu::Item& it = A.items[k];
        if (!amInteractive(it.kind)) continue;
        if (it.fixed) {
            if (it.r.contains(x, y)) return static_cast<int>(k);
            continue;
        }
        if (y < A.headH) continue;
        Rect r = it.r;
        r.y -= A.scroll;
        if (it.kind == AudioMenu::Item::Row) r.h = std::min(r.h, kAmCardTopH);
        if (r.contains(x, y)) return static_cast<int>(k);
    }
    return -1;
}

int amPctAt(const Rect& track, double x) {
    const double travel = std::max(1.0, track.w - 2 * kAmSliderInset);
    const double frac = (x - track.x - kAmSliderInset) / travel;
    return static_cast<int>(std::clamp(frac, 0.0, 1.0) * 100 + 0.5);
}

bool amHasTag(const AudioDevice& d, const std::string& tag) { return ("," + d.tags + ",").find("," + tag + ",") != std::string::npos; }
std::string amTagValue(const AudioDevice& d, const std::string& key) {
    const std::string list = "," + d.tags + ",", want = "," + key + "=";
    const size_t at = list.find(want);
    if (at == std::string::npos) return "";
    const size_t end = list.find(',', at + want.size());
    return list.substr(at + want.size(), end - at - want.size());
}

void amText(cairo_t* cr, PangoLayout* layout, const std::string& text, Rect box, Color c, double alpha, AmAlign align = kAmLeft, const char* font = "Sans 11") {
    PangoFontDescription* fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, static_cast<int>(box.w * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, align == kAmRight ? PANGO_ALIGN_RIGHT : align == kAmCenter ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, c, alpha);
    cairo_move_to(cr, box.x, box.y + (box.h - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
}

double amTextWidth(PangoLayout* layout, const std::string& text, const char* font) {
    PangoFontDescription* fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    return tw;
}

void amPieces(cairo_t* cr, PangoLayout* layout, const std::vector<std::string>& pieces, Rect box, Color c, double alpha) {
    const char* font = "Sans 10";
    cairo_save(cr);
    roundedRect(cr, box.x, box.y, box.w, box.h, 0);
    cairo_clip(cr);
    double x = box.x;
    for (size_t k = 0; k < pieces.size(); k++) {
        if (k > 0) {
            amText(cr, layout, " · ", {x, box.y, 12, box.h}, c, alpha, kAmLeft, font);
            x += amTextWidth(layout, " · ", font);
        }
        amText(cr, layout, pieces[k], {x, box.y, box.w, box.h}, c, alpha, kAmLeft, font);
        x += amTextWidth(layout, pieces[k], font);
    }
    cairo_restore(cr);
}

void amItemState(const App& app, const AudioMenu::Item& it, int& pct, bool& muted, bool& source) {
    const AudioMenu& A = app.amenu;
    source = false;
    pct = 0;
    muted = false;
    if (it.section == 1) {
        if (it.dev < 0 || it.dev >= static_cast<int>(A.apps.size())) return;
        pct = A.apps[static_cast<size_t>(it.dev)].vol;
        muted = A.apps[static_cast<size_t>(it.dev)].muted;
    } else {
        if (it.dev < 0 || it.dev >= static_cast<int>(A.devs.size())) return;
        const AudioDevice& d = A.devs[static_cast<size_t>(it.dev)];
        pct = std::max(0, d.vol);
        muted = d.muted;
        source = !d.sink;
    }
}

void amDrawSlider(App& app, cairo_t* cr, const AudioMenu::Item& it, size_t k, Rect r) {
    int pct = 0;
    bool muted = false, source = false;
    amItemState(app, it, pct, muted, source);
    const bool dragging = app.amenu.drag == static_cast<int>(k);
    const double value = (dragging ? app.amenu.dragPct : pct) / 100.0;
    constexpr double kTrackH = 6, kHandleW = 4, kHandleH = 24;
    const double cy = r.y + r.h / 2, x0 = r.x, x1 = r.x + r.w;
    const double hx = x0 + kAmSliderInset + std::clamp(value, 0.0, 1.0) * (x1 - x0 - 2 * kAmSliderInset);
    const Color primary = app.color("primary"), inactive = app.color("secondary_container"), outline = app.color("outline");
    if (hx - kAmSliderInset - x0 > 0) {
        roundedRect(cr, x0, cy - kTrackH / 2, hx - kAmSliderInset - x0, kTrackH, kTrackH / 2);
        setSource(cr, muted ? outline : primary, muted ? 0.5 : 1);
        cairo_fill(cr);
    }
    if (x1 - (hx + kAmSliderInset) > 0) {
        roundedRect(cr, hx + kAmSliderInset, cy - kTrackH / 2, x1 - (hx + kAmSliderInset), kTrackH, kTrackH / 2);
        setSource(cr, inactive);
        cairo_fill(cr);
    }
    roundedRect(cr, hx - kHandleW / 2, cy - kHandleH / 2, kHandleW, kHandleH, kHandleW / 2);
    setSource(cr, muted ? outline : primary);
    cairo_fill(cr);
}

void amDrawMute(App& app, cairo_t* cr, const AudioMenu::Item& it, Rect r, bool hot) {
    int pct = 0;
    bool muted = false, source = false;
    amItemState(app, it, pct, muted, source);
    if (muted) {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.w / 2);
        setSource(cr, app.color("secondary"));
        cairo_fill(cr);
    } else {
        roundedRect(cr, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, (r.w - 1) / 2);
        setSource(cr, app.color("outline"));
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
    }
    if (hot) {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.w / 2);
        setSource(cr, muted ? app.color("on_secondary") : app.color("on_surface"), 0.08);
        cairo_fill(cr);
    }
    const char* icon = source ? "microphone" : muted || pct == 0 ? "volume-cross" : pct < 50 ? "volume-small" : "volume-loud";
    const Color ink = muted ? app.color("on_secondary") : app.color("on_surface_variant");
    drawIcon(cr, icon, r.x + (r.w - 22) / 2, r.y + (r.h - 22) / 2, 22, ink);
}

std::string amDefaultName(const AudioMenu& A) {
    const bool sink = A.tab != AudioMenu::TabInput;
    for (const auto& d : A.devs)
        if (d.sink == sink && d.isDefault) return d.title;
    return "";
}

void amDrawHead(App& app, cairo_t* cr, PangoLayout* layout, Rect r) {
    const double box = r.h;
    shapePath(cr, kM3IconShape, r.x, r.y, box);
    setSource(cr, app.color("primary_container"));
    cairo_fill(cr);
    constexpr double kIcon = 26;
    drawIcon(cr, "speaker", r.x + (box - kIcon) / 2, r.y + (box - kIcon) / 2, static_cast<int>(kIcon), app.color("on_primary_container"));
    const double tx = r.x + box + 14, tw = r.w - box - 14;
    amText(cr, layout, "Sound", {tx, r.y + 3, tw, 24}, app.color("on_surface"), 1, kAmLeft, "Sans Bold 16");
    const std::string name = amDefaultName(app.amenu);
    if (name.empty()) return;
    const char* font = "Sans 10";
    const char* prefix = app.amenu.tab == AudioMenu::TabInput ? "Record from" : "Playing on";
    const double pw = amTextWidth(layout, prefix, font) + 4;
    amText(cr, layout, prefix, {tx, r.y + 27, tw, 18}, app.color("on_surface_variant"), 1, kAmLeft, font);
    amText(cr, layout, name, {tx + pw, r.y + 27, std::max(0.0, tw - pw), 18}, app.color("on_surface"), 1, kAmLeft, font);
}

void amDrawTab(App& app, cairo_t* cr, PangoLayout* layout, const AudioMenu::Item& it, bool hot, bool pressed) {
    const AudioMenu& A = app.amenu;
    const Rect r = it.r;
    if (it.dev == 0) {
        roundedRect(cr, r.x, r.y, r.w * 3, r.h, r.h / 2);
        setSource(cr, app.color("surface_container_high"));
        cairo_fill(cr);
        const Rect on{r.x + 4 + A.ta.drawPos(3, r.w) * r.w, r.y + 4, r.w - 8, r.h - 8};
        roundedRect(cr, on.x, on.y, on.w, on.h, on.h / 2);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
    }
    const bool on = std::fabs(A.ta.pos - it.dev) < 0.5;
    const Rect seg{r.x + 4, r.y + 4, r.w - 8, r.h - 8};
    if (hot || pressed) {
        roundedRect(cr, seg.x, seg.y, seg.w, seg.h, seg.h / 2);
        setSource(cr, app.color(on ? "on_secondary_container" : "on_surface"), pressed ? 0.12 : 0.08);
        cairo_fill(cr);
    }
    const char* label = it.dev == AudioMenu::TabOutput ? "Output" : it.dev == AudioMenu::TabInput ? "Input" : "Apps";
    const char* font = on ? "Sans Bold 11" : "Sans 11";
    const Color ink = app.color(on ? "on_secondary_container" : "on_surface_variant");
    const int count = it.dev == AudioMenu::TabApps ? static_cast<int>(A.apps.size()) : 0;
    const double tw = amTextWidth(layout, label, font), badge = count > 0 ? 20 : 0, gap = count > 0 ? 6 : 0;
    const double x = seg.x + (seg.w - (tw + gap + badge)) / 2;
    amText(cr, layout, label, {x, seg.y, tw + 2, seg.h}, ink, 1, kAmLeft, font);
    if (count > 0) {
        const Rect b{x + tw + gap, seg.y + (seg.h - badge) / 2, badge, badge};
        roundedRect(cr, b.x, b.y, b.w, b.h, badge / 2);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        amText(cr, layout, std::to_string(count), b, app.color("on_primary"), 1, kAmCenter, "Sans Bold 9");
    }
}

void amDrawLabel(App& app, cairo_t* cr, PangoLayout* layout, const AudioMenu::Item& it, Rect r) {
    const char* text = it.section == 0 ? "Play to" : it.section == 1 ? "Record from" : "Other devices";
    amText(cr, layout, text, {r.x + 6, r.y, r.w - 12, r.h}, app.color("primary"), 1, kAmLeft, "Sans Bold 10");
}

void amCardBackground(App& app, cairo_t* cr, Rect r, bool strong, bool dim, bool hot, bool pressed) {
    roundedRect(cr, r.x, r.y, r.w, r.h, kAmCardRadius);
    setSource(cr, strong ? app.color("surface_container_high") : app.color(dim ? "surface_container_low" : "surface_container"), dim && !strong ? 0.7 : 1);
    cairo_fill(cr);
    if (hot || pressed) {
        roundedRect(cr, r.x, r.y, r.w, r.h, kAmCardRadius);
        setSource(cr, app.color("on_surface"), pressed ? 0.12 : 0.08);
        cairo_fill(cr);
    }
}

void amDrawRow(App& app, cairo_t* cr, PangoLayout* layout, const AudioMenu::Item& it, Rect r, bool hot, bool pressed) {
    const AudioDevice& d = app.amenu.devs[static_cast<size_t>(it.dev)];
    const bool isDef = d.isDefault, virt = amHasTag(d, "virtual");
    amCardBackground(app, cr, r, isDef, virt, hot, pressed);
    const double cy = r.y + kAmCardTopH / 2;
    const double iconW = isDef ? 56 : 44;
    const Rect box{r.x + 12, cy - 22, iconW, 44};
    roundedRect(cr, box.x, box.y, box.w, box.h, 22);
    setSource(cr, isDef ? app.color("primary_container") : app.color(virt ? "surface_container_highest" : "secondary_container"));
    cairo_fill(cr);
    const Color iconInk = isDef ? app.color("primary") : app.color(virt ? "outline" : "on_secondary_container");
    drawIcon(cr, d.icon, box.x + (box.w - 24) / 2, cy - 12, 24, iconInk);

    const char* pillText = d.action == "connect" ? "Connect" : d.action == "profile" ? "Enable" : nullptr;
    double textEnd = r.x + r.w - 16;
    if (pillText) {
        const double pw = amTextWidth(layout, pillText, "Sans Bold 10") + 24;
        const Rect pill{textEnd - pw, cy - 16, pw, 32};
        roundedRect(cr, pill.x, pill.y, pill.w, pill.h, 16);
        setSource(cr, app.color(d.action == "connect" ? "primary" : "secondary_container"));
        cairo_fill(cr);
        amText(cr, layout, pillText, pill, app.color(d.action == "connect" ? "on_primary" : "on_secondary_container"), 1, kAmCenter, "Sans Bold 10");
        textEnd = pill.x - 8;
    } else if (isDef) {
        drawIcon(cr, "check-circle", textEnd - 22, cy - 11, 22, app.color("primary"));
        textEnd -= 30;
    } else {
        cairo_arc(cr, textEnd - 11, cy, 9, 0, 2 * M_PI);
        setSource(cr, app.color("outline"));
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
        textEnd -= 30;
    }

    const double tx = box.x + box.w + 14;
    const Color ink = app.color(virt && !isDef ? "on_surface_variant" : "on_surface");
    amText(cr, layout, d.title, {tx, cy - 24, textEnd - tx, 24}, ink, 1, kAmLeft, "Sans 12");
    std::vector<std::string> pieces;
    if (!d.sub.empty()) pieces.push_back(d.sub);
    if (amHasTag(d, "bt")) pieces.push_back("Bluetooth");
    if (amHasTag(d, "usb")) pieces.push_back("USB");
    if (amHasTag(d, "hdmi")) pieces.push_back("HDMI");
    if (virt) pieces.push_back("Virtual");
    const std::string battery = amTagValue(d, "battery");
    if (!battery.empty()) pieces.push_back(battery + "%");
    amPieces(cr, layout, pieces, {tx, cy + 2, textEnd - tx, 22}, app.color("on_surface_variant"), 1);
}

std::shared_ptr<cairo_surface_t> amAppIcon(App& app, const std::string& name) {
    if (name.empty()) return nullptr;
    AudioMenu& A = app.amenu;
    if (auto hit = A.icons.find(name); hit != A.icons.end()) return hit->second;
    std::shared_ptr<cairo_surface_t> surf;
    const std::string path = lookupIcon(name, picturePx(28));
    if (!path.empty())
        if (cairo_surface_t* raw = loadIconScaled(path, 28)) surf = std::shared_ptr<cairo_surface_t>(raw, cairo_surface_destroy);
    A.icons[name] = surf;
    return surf;
}

void amDrawAppRow(App& app, cairo_t* cr, PangoLayout* layout, const AudioMenu::Item& it, Rect r) {
    const AudioApp& a = app.amenu.apps[static_cast<size_t>(it.dev)];
    amCardBackground(app, cr, r, false, false, false, false);
    const double cy = r.y + kAmCardTopH / 2;
    const Rect box{r.x + 12, cy - 22, 44, 44};
    roundedRect(cr, box.x, box.y, box.w, box.h, 22);
    setSource(cr, app.color("secondary_container"));
    cairo_fill(cr);
    if (auto icon = amAppIcon(app, a.icon)) {
        paintPictureBox(cr, icon.get(), box.x + 8, box.y + 8, 28);
    } else {
        drawIcon(cr, "speaker", box.x + 10, box.y + 10, 24, app.color("on_secondary_container"));
    }
    const double tx = box.x + box.w + 14, tw = r.x + r.w - 16 - tx;
    amText(cr, layout, a.name, {tx, cy - 24, tw, 24}, app.color("on_surface"), 1, kAmLeft, "Sans 12");
    amText(cr, layout, a.corked ? "Paused" : a.media.empty() ? "Playing" : a.media, {tx, cy + 2, tw, 22}, app.color("on_surface_variant"), 1, kAmLeft, "Sans 10");
}

constexpr size_t kAmOldItem = 1u << 30;
void amDrawItem(App& app, cairo_t* cr, PangoLayout* layout, const AudioMenu::Item& it, size_t k, int scroll) {
    const AudioMenu& A = app.amenu;
    Rect r = it.r;
    if (!it.fixed) {
        r.y -= scroll;
        if (r.y + r.h < A.headH || r.y > A.regionH) return;
    }
    const bool hot = A.hover == static_cast<int>(k), pressed = A.pressed == static_cast<int>(k);
    switch (it.kind) {
    case AudioMenu::Item::Head: amDrawHead(app, cr, layout, r); break;
    case AudioMenu::Item::Tab: amDrawTab(app, cr, layout, it, hot, pressed); break;
    case AudioMenu::Item::Label: amDrawLabel(app, cr, layout, it, r); break;
    case AudioMenu::Item::Mute: amDrawMute(app, cr, it, r, hot || pressed); break;
    case AudioMenu::Item::Slider: {
        amDrawSlider(app, cr, it, k, r);
        int pct = 0;
        bool muted = false, source = false;
        amItemState(app, it, pct, muted, source);
        const int shown = A.drag == static_cast<int>(k) ? A.dragPct : pct;
        amText(cr, layout, muted ? "Muted" : std::to_string(shown) + "%", {r.x + r.w + kAmSliderGap, r.y, kAmPctW, r.h},
               app.color(muted ? "outline" : "on_surface_variant"), 1, kAmCenter, "Sans 11");
        break;
    }
    case AudioMenu::Item::Row: amDrawRow(app, cr, layout, it, r, hot, pressed); break;
    case AudioMenu::Item::AppRow: amDrawAppRow(app, cr, layout, it, r); break;
    case AudioMenu::Item::Note:
        amText(cr, layout, !A.loaded ? "Loading" : it.section == AudioMenu::TabApps ? "No apps playing sound" : "No devices", {r.x + 16, r.y, r.w - 32, r.h},
               app.color("on_surface_variant"), 1, kAmCenter, "Sans 11");
        break;
    }
}

double amShownH(const AudioMenu& A) {
    return std::round(std::clamp(A.viewH, static_cast<double>(A.headH + 24), static_cast<double>(std::max(A.regionH, A.headH + 24))));
}

void amStep(AudioMenu& A, std::chrono::steady_clock::time_point now) {
    const double dt = A.layoutMoving ? std::min(0.1, std::chrono::duration<double>(now - A.stepLast).count()) : 0.016;
    A.stepLast = now;
    bool moving = false;
    auto spring = [&](double& x, double& v, double target, double eps, double epsV) {
        if (std::fabs(target - x) < eps && std::fabs(v) < epsV) { x = target; v = 0; return; }
        for (double left = dt; left > 1e-9; left -= kAmSubStep) {
            const double h = std::min(kAmSubStep, left);
            v += (kAmSpringK * (target - x) - kAmSpringC * v) * h;
            x += v * h;
        }
        moving = true;
    };
    spring(A.viewH, A.viewV, A.cardH, 0.3, 2.0);
    const bool wasFading = A.ta.fading;
    if (A.ta.step(dt)) moving = true;
    if (wasFading && !A.ta.fading) A.prevItems.clear();
    A.layoutMoving = moving;
}

void amCardPaint(App& app, cairo_t* cr, double h) {
    AudioMenu& A = app.amenu;
    const int hi = static_cast<int>(h);
    if (!A.shadow || A.shadowH != hi) {
        if (A.shadow) cairo_surface_destroy(A.shadow);
        A.shadow = makeShadowMask(A.cardW, hi, kAmRadius);
        A.shadowH = hi;
    }
    setSource(cr, app.color("shadow"), 0.3);
    cairo_mask_surface(cr, A.shadow, -kLaunShadow, -kLaunShadow + 2);
    cairo_save(cr);
    roundedRect(cr, 0, 0, A.cardW, h, kAmRadius);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill(cr);
    cairo_restore(cr);
    roundedRect(cr, 0, 0, A.cardW, h, kAmRadius);
    setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    cairo_fill(cr);
}

void amDrawList(App& app, cairo_t* cr, PangoLayout* layout, const std::vector<AudioMenu::Item>& items, bool live, int scroll, double h, double dx, double alpha) {
    if (alpha <= 0.003) return;
    const AudioMenu& A = app.amenu;
    cairo_save(cr);
    cairo_rectangle(cr, 0, A.headH, A.cardW, h - A.headH);
    cairo_clip(cr);
    const bool group = alpha < 0.999 || std::fabs(dx) > 0.01;
    if (group) cairo_push_group(cr);
    cairo_translate(cr, dx, 0);
    for (size_t k = 0; k < items.size(); k++)
        if (!items[k].fixed) amDrawItem(app, cr, layout, items[k], live ? k : kAmOldItem, scroll);
    if (group) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
    }
    cairo_restore(cr);
}

void amCommit(App& app, PopupFrame& f, double rev, double h) {
    AudioMenu& A = app.amenu;
    const double room = kTmRoom;
    wl_buffer* buffer = popupFrameBuffer(app, f);
    if (!buffer) return;
    updateBlurRegion(app, A.surface, A.blur, f.W, f.H, [&](cairo_t* m) {
        if (rev <= 0.001) return;
        popPaintGrown(m, rev, A.ox + room, A.oy + room, 1.0, [&] {
            cairo_translate(m, room, room + A.cardTop);
            roundedRect(m, 0, 0, A.cardW, h, kAmRadius);
            cairo_fill(m);
        });
    });
    const int top = static_cast<int>(room + A.cardTop), hh = static_cast<int>(h);
    if (top != A.inTop || hh != A.inH) {
        wl_region* region = wl_compositor_create_region(app.compositor);
        wl_region_add(region, static_cast<int>(room), top, A.cardW, hh);
        wl_surface_set_input_region(A.surface, region);
        wl_region_destroy(region);
        A.inTop = top;
        A.inH = hh;
    }
    popupFramePresent(A.surface, buffer, f);
}

void drawAudioMenu(App& app) {
    AudioMenu& A = app.amenu;
    if (!A.open() || !A.configured) return;
    amStep(A, std::chrono::steady_clock::now());
    const double room = kTmRoom, h = amShownH(A), area = std::max<double>(A.regionH, h);
    if (sideBar(app)) A.cardTop = std::clamp(A.oy - h / 2.0, 0.0, area - h);
    else A.cardTop = app.cfg.barPosition == "bottom" ? area - h : 0.0;
    A.cardTop = std::round(A.cardTop);
    PopupFrame frame;
    if (!popupCanvasBegin(frame, "end4pC-audio-menu", A.cardW, static_cast<int>(area), A.scale)) return;
    const double rev = std::max(0.0, A.anim.reveal);
    if (rev > 0.001) {
        cairo_identity_matrix(frame.cr);
        popPaintGrown(frame.cr, rev, A.ox + room, A.oy + room, A.anim.phase == kPopOut ? 1.0 : std::min(1.0, rev * 8), [&] {
            cairo_t* cr = frame.cr;
            cairo_save(cr);
            cairo_translate(cr, room, room + A.cardTop);
            amCardPaint(app, cr, h);
            PangoLayout* layout = pango_cairo_create_layout(cr);
            cairo_save(cr);
            roundedRect(cr, 0, 0, A.cardW, h, kAmRadius);
            cairo_clip(cr);
            if (A.ta.fading) {
                amDrawList(app, cr, layout, A.prevItems, false, A.prevScroll, h, A.ta.outDx(kAmSlide), A.ta.outAlpha());
                amDrawList(app, cr, layout, A.items, true, A.scroll + static_cast<int>(std::lround(A.band.over)), h, A.ta.inDx(kAmSlide), A.ta.inAlpha());
            } else {
                amDrawList(app, cr, layout, A.items, true, A.scroll + static_cast<int>(std::lround(A.band.over)), h, 0, 1);
            }
            for (size_t k = 0; k < A.items.size(); k++)
                if (A.items[k].fixed) amDrawItem(app, cr, layout, A.items[k], k, A.scroll);
            cairo_restore(cr);
            g_object_unref(layout);
            cairo_restore(cr);
        });
    }
    amCommit(app, frame, rev, h);
}

void amSetTab(App& app, int tab) {
    AudioMenu& A = app.amenu;
    if (tab == A.tab) return;
    if (!A.open() || !A.configured) {
        A.tab = tab;
        A.scroll = 0;
        A.viewH = A.cardH;
        A.ta.snap(tab);
        return;
    }
    A.prevItems.clear();
    for (const auto& it : A.items)
        if (!it.fixed) A.prevItems.push_back(it);
    A.prevScroll = A.scroll;
    A.ta.select(tab);
    A.tab = tab;
    A.scroll = 0;
    amLayout(app);
    A.hover = -1;
    A.pressed = -1;
    drawAudioMenu(app);
}

void closeAudioMenu(App& app) {
    AudioMenu& A = app.amenu;
    if (!A.open()) return;
    if (A.layer) zwlr_layer_surface_v1_destroy(A.layer);
    releaseBlurSurface(A.blur);
    hidpi::detach(A.scale);
    wl_surface_destroy(A.surface);
    A.surface = nullptr;
    A.layer = nullptr;
    A.configured = false;
    A.bar = nullptr;
    A.items.clear();
    A.icons.clear();
    A.cardW = A.cardH = A.regionH = 0;
    A.viewH = A.viewV = A.cardTop = 0;
    A.prevItems.clear();
    A.ta.snap(A.tab);
    A.layoutMoving = false;
    A.inTop = A.inH = -1;
    if (A.shadow) { cairo_surface_destroy(A.shadow); A.shadow = nullptr; }
    A.shadowH = -1;
    A.hover = -1;
    A.pressed = -1;
    A.drag = -1;
    A.anim = PopAnim{};
    app.onAmenu = false;
    updateCursor(app);
    malloc_trim(0);
}

void onAmenuConfigure(void* d, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t, uint32_t) {
    auto* app = static_cast<App*>(d);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!app->amenu.configured) app->amenu.anim.animStart = std::chrono::steady_clock::now();
    app->amenu.configured = true;
    drawAudioMenu(*app);
}
void onAmenuClosed(void* d, zwlr_layer_surface_v1*) { closeAudioMenu(*static_cast<App*>(d)); }
const zwlr_layer_surface_v1_listener kAmenuListener = {onAmenuConfigure, onAmenuClosed};

void openAudioMenu(App& app, Bar* bar) {
    if (!bar || !app.layerShell || !app.compositor || !app.shm) return;
    closeAllPopups(app);
    AudioMenu& A = app.amenu;
    A.bar = bar;
    A.ax = app.pointerX;
    A.ay = app.pointerY;
    A.hover = -1;
    A.pressed = -1;
    A.drag = -1;
    A.scroll = 0;
    A.configured = false;
    A.apps.clear();
    amLayout(app);
    A.viewH = A.cardH;
    A.viewV = 0;
    A.ta.snap(A.tab);
    A.prevItems.clear();
    A.layoutMoving = false;
    A.inTop = A.inH = -1;
    A.surface = wl_compositor_create_surface(app.compositor);
    attachScale(app, A.scale, A.surface, [&app] { drawAudioMenu(app); });
    A.layer = zwlr_layer_shell_v1_get_layer_surface(app.layerShell, A.surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "end4pC-audio-menu");
    const PopOrigin origin = placePopupBeside(app, A.layer, A.ax, A.ay, A.cardW, A.regionH, bar);
    A.ox = origin.x;
    A.oy = origin.y;
    popIconCentre(app, A.ax, A.ay, A.ix, A.iy);
    popStart(A.anim);
    zwlr_layer_surface_v1_set_exclusive_zone(A.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(A.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    zwlr_layer_surface_v1_add_listener(A.layer, &kAmenuListener, &app);
    wl_surface_commit(A.surface);
    startAudioDevicesJob(app);
}

void retractAudioMenu(App& app) {
    AudioMenu& A = app.amenu;
    if (!A.open() || popIsOut(A.anim)) return;
    if (!A.configured) { closeAudioMenu(app); return; }
    popRetract(A.anim, std::chrono::steady_clock::now());
    A.hover = -1;
    A.pressed = -1;
    drawAudioMenu(app);
}

void toggleAudioMenu(App& app, Bar* bar) {
    AudioMenu& A = app.amenu;
    if (!A.open()) { openAudioMenu(app, bar); return; }
    if (popIsOut(A.anim)) {
        popReverse(A.anim, std::chrono::steady_clock::now());
        drawAudioMenu(app);
        return;
    }
    retractAudioMenu(app);
}

bool audioIconPressed(const App& app) {
    return app.amenu.open() && std::fabs(app.pointerX - app.amenu.ix) <= 20 && std::fabs(app.pointerY - app.amenu.iy) <= 20;
}

void tickAudioMenu(App& app, std::chrono::steady_clock::time_point now) {
    AudioMenu& A = app.amenu;
    if (!A.open() || !A.configured) return;
    bool moving = false;
    if (popTick(A.anim, now, moving)) { closeAudioMenu(app); return; }
    if (A.band.step() || moving || (A.layoutMoving && now - A.stepLast >= std::chrono::milliseconds(8))) drawAudioMenu(app);
}

std::chrono::steady_clock::time_point audioWake(const App& app) {
    if (!app.amenu.open() || !app.amenu.configured) return std::chrono::steady_clock::time_point::max();
    const auto now = std::chrono::steady_clock::now();
    const auto wake = popWake(app.amenu.anim.phase, app.amenu.anim.settleAt, now);
    return app.amenu.layoutMoving || app.amenu.band.over != 0 ? std::min(wake, now + std::chrono::milliseconds(16)) : wake;
}

void amRefresh(App& app) {
    AudioMenu& A = app.amenu;
    amLayout(app);
    if (!A.open()) return;
    A.hover = -1;
    A.pressed = -1;
    if (A.ta.fading) {
        A.ta.endFade();
        A.prevItems.clear();
    }
    drawAudioMenu(app);
}

bool audioEventsChangeDevices(const std::string& events) {
    return events.find("on sink #") != std::string::npos || events.find("on source #") != std::string::npos || events.find("on server") != std::string::npos ||
           events.find("on card #") != std::string::npos || events.find("on sink-input #") != std::string::npos;
}

bool audioEventsChangeVolume(const std::string& events) {
    return events.find("on sink #") != std::string::npos || events.find("on server") != std::string::npos;
}
bool audioEventsChangeMic(const std::string& events) {
    return events.find("on source #") != std::string::npos || events.find("on server") != std::string::npos;
}

std::string amScriptPath() { return assetsDir() + "/../scripts/audio_devices.py"; }

void startAudioDevicesJob(App& app) {
    AudioMenu& A = app.amenu;
    if (A.job.fd >= 0) { A.job.dirty = true; return; }
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return;
    const std::string script = amScriptPath();
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDERR_FILENO);
        execlp("python3", "python3", "-I", script.c_str(), "list", nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    A.job.fd = p[0];
    A.job.pid = pid;
    A.job.buf.clear();
    A.job.dirty = false;
}

void amParse(App& app, const std::string& text) {
    AudioMenu& A = app.amenu;
    std::vector<AudioDevice> devs;
    std::vector<AudioApp> apps;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::vector<std::string> f = splitTabs(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (f.size() >= 9 && (f[0] == "sink" || f[0] == "source")) {
            AudioDevice d;
            d.sink = f[0] == "sink";
            d.action = f[1];
            d.target = f[2];
            d.extra = f[3];
            d.title = f[4];
            d.sub = f[5];
            d.icon = f[6];
            d.tags = f[7];
            d.isDefault = f[8] == "1";
            d.vol = f.size() > 9 ? std::atoi(f[9].c_str()) : -1;
            d.muted = f.size() > 10 && f[10] == "1";
            d.active = f.size() <= 11 || f[11] == "1";
            devs.push_back(d);
        } else if (f.size() >= 7 && f[0] == "app") {
            AudioApp a;
            a.index = std::atoi(f[1].c_str());
            a.name = f[2];
            a.icon = f[3];
            a.vol = std::atoi(f[4].c_str());
            a.muted = f[5] == "1";
            a.corked = f[6] == "1";
            a.media = f.size() > 7 ? f[7] : "";
            apps.push_back(a);
        } else if (f.size() >= 3 && f[0] == "input") {
            A.inVol = std::atoi(f[1].c_str());
            A.inMuted = f[2] == "1";
        }
    }
    A.devs = devs;
    A.apps = apps;
}

void audioJobDone(App& app) {
    AudioMenu& A = app.amenu;
    close(A.job.fd);
    A.job.fd = -1;
    A.job.pid = 0;
    const bool again = A.job.dirty;
    A.job.dirty = false;
    if (!A.job.buf.empty() && A.drag < 0) amParse(app, A.job.buf);
    A.job.buf.clear();
    A.loaded = true;
    if (A.open()) amRefresh(app);
    if (again && A.open()) startAudioDevicesJob(app);
}

void audioJobReadable(App& app) {
    AudioMenu& A = app.amenu;
    if (A.job.fd < 0) return;
    char buf[1024];
    ssize_t n;
    while ((n = read(A.job.fd, buf, sizeof buf)) > 0) A.job.buf.append(buf, static_cast<size_t>(n));
    if (n == 0) audioJobDone(app);
}

void amRunScript(const std::vector<std::string>& args) {
    std::vector<std::string> full = {"python3", "-I", amScriptPath()};
    full.insert(full.end(), args.begin(), args.end());
    std::vector<const char*> argv;
    for (const auto& a : full) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    if (fork() == 0) {
        childSignalReset();
        setsid();
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, STDIN_FILENO);
        dup2(nul, STDOUT_FILENO);
        dup2(nul, STDERR_FILENO);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
}

void amActivate(App& app, size_t item) {
    AudioMenu& A = app.amenu;
    const AudioDevice chosen = A.devs[static_cast<size_t>(A.items[item].dev)];
    const std::string kind = chosen.sink ? "sink" : "source";
    if (chosen.action == "set") {
        if (chosen.isDefault) return;
        for (auto& d : A.devs)
            if (d.sink == chosen.sink && d.action == "set") d.isDefault = d.target == chosen.target && d.extra == chosen.extra;
        drawAudioMenu(app);
        amRunScript({"set", kind, chosen.target, chosen.extra});
    } else if (chosen.action == "profile") {
        amRunScript({"profile", kind, chosen.target, chosen.extra});
    } else if (chosen.action == "connect") {
        amRunScript({"connect", kind, chosen.target});
    }
}

void amToggleMute(App& app, size_t item) {
    AudioMenu& A = app.amenu;
    const AudioMenu::Item it = A.items[item];
    if (it.section == 1) {
        if (it.dev < 0 || it.dev >= static_cast<int>(A.apps.size())) return;
        AudioApp& a = A.apps[static_cast<size_t>(it.dev)];
        a.muted = !a.muted;
        const std::string idx = std::to_string(a.index);
        runDetached({"pactl", "set-sink-input-mute", idx.c_str(), a.muted ? "1" : "0"});
    } else {
        if (it.dev < 0 || it.dev >= static_cast<int>(A.devs.size())) return;
        const AudioDevice chosen = A.devs[static_cast<size_t>(it.dev)];
        const bool mute = !chosen.muted;
        for (auto& d : A.devs)
            if (d.sink == chosen.sink && d.target == chosen.target) d.muted = mute;
        runDetached({"pactl", chosen.sink ? "set-sink-mute" : "set-source-mute", chosen.target.c_str(), mute ? "1" : "0"});
    }
    drawAudioMenu(app);
}

void amFinishDrag(App& app) {
    AudioMenu& A = app.amenu;
    if (A.drag < 0 || A.drag >= static_cast<int>(A.items.size())) { A.drag = -1; return; }
    const AudioMenu::Item it = A.items[static_cast<size_t>(A.drag)];
    const int pct = std::min(A.dragPct, std::max(1, app.cfg.volumeMax));
    const std::string value = std::to_string(pct) + "%";
    A.drag = -1;
    if (it.section == 1 && it.dev >= 0 && it.dev < static_cast<int>(A.apps.size())) {
        AudioApp& a = A.apps[static_cast<size_t>(it.dev)];
        a.vol = pct;
        const std::string idx = std::to_string(a.index);
        runDetached({"pactl", "set-sink-input-volume", idx.c_str(), value.c_str()});
    } else if (it.section == 0 && it.dev >= 0 && it.dev < static_cast<int>(A.devs.size())) {
        const AudioDevice chosen = A.devs[static_cast<size_t>(it.dev)];
        for (auto& d : A.devs)
            if (d.sink == chosen.sink && d.target == chosen.target) d.vol = pct;
        runDetached({"pactl", chosen.sink ? "set-sink-volume" : "set-source-volume", chosen.target.c_str(), value.c_str()});
    }
    drawAudioMenu(app);
    startAudioDevicesJob(app);
}

double amCardX(const App& app) { return app.pointerX - kTmRoom; }
double amCardY(const App& app) { return app.pointerY - kTmRoom - app.amenu.cardTop; }

void audioMenuMotion(App& app) {
    AudioMenu& A = app.amenu;
    const double x = amCardX(app), y = amCardY(app);
    if (A.drag >= 0) {
        if (A.drag < static_cast<int>(A.items.size())) {
            const int pct = amPctAt(A.items[static_cast<size_t>(A.drag)].r, x);
            if (pct != A.dragPct) { A.dragPct = pct; drawAudioMenu(app); }
        }
        return;
    }
    const int hover = amItemAt(A, x, y);
    if (hover == A.hover) return;
    A.hover = hover;
    drawAudioMenu(app);
    updateCursor(app);
}

void audioMenuScroll(App& app, int dir) {
    AudioMenu& A = app.amenu;
    const int maxScroll = std::max(0, A.contentH - A.cardH);
    double v = A.scroll;
    elasticWheel(A.band, v, maxScroll, dir * static_cast<double>(kAmScrollStep));
    if (static_cast<int>(std::lround(v)) == A.scroll && A.band.over == 0) return;
    A.scroll = static_cast<int>(std::lround(v));
    A.hover = amItemAt(A, amCardX(app), amCardY(app));
    drawAudioMenu(app);
    updateCursor(app);
}

void audioMenuButton(App& app, uint32_t button, bool pressed) {
    AudioMenu& A = app.amenu;
    constexpr uint32_t kBtnLeft = 0x110;
    if (button != kBtnLeft) return;
    if (popIsOut(A.anim)) return;
    const double x = amCardX(app), y = amCardY(app);
    if (!pressed) {
        if (A.drag >= 0) { amFinishDrag(app); return; }
        if (A.pressed < 0) return;
        const int held = A.pressed;
        A.pressed = -1;
        if (held < static_cast<int>(A.items.size()) && amItemAt(A, x, y) == held) {
            const AudioMenu::Item it = A.items[static_cast<size_t>(held)];
            if (it.kind == AudioMenu::Item::Mute) amToggleMute(app, static_cast<size_t>(held));
            else if (it.kind == AudioMenu::Item::Row) amActivate(app, static_cast<size_t>(held));
            else if (it.kind == AudioMenu::Item::Tab && it.dev != A.tab) {
                amSetTab(app, it.dev);
                return;
            }
        }
        drawAudioMenu(app);
        return;
    }
    if (!Rect{0, 0, double(A.cardW), amShownH(A)}.contains(x, y)) { closeAudioMenu(app); return; }
    const int k = amItemAt(A, x, y);
    if (k < 0) return;
    const AudioMenu::Item it = A.items[static_cast<size_t>(k)];
    if (it.kind == AudioMenu::Item::Slider) {
        A.drag = k;
        A.dragPct = amPctAt(it.r, x);
        drawAudioMenu(app);
        return;
    }
    A.pressed = k;
    drawAudioMenu(app);
}
