// The text widget of the desktop: words written on the wallpaper, with no card. Click it to type (the editor is the one of the Notes of Tools),
// resize it from the handles like any widget, and pick the face, color, alignment and size in Settings > Desktop > Text. By default the text
// grows to fill the box; turn that off to give it a size of its own.
constexpr double kTwPad = 10, kTwMinPx = 8, kTwMaxPx = 420;

struct TextW : DeskWidget {
    notes_ui::TextEditor ed;
    bool typingOn = false;
    double ox = 0, oy = 0;       // where the text starts inside the widget (for the caret of a click)
    std::string look;            // face, size, alignment and width the layout was built with
    PangoFontDescription* fd = nullptr;
};
TextW g_textW;
DeskWidget* deskTextWidget() { return &g_textW; }

std::string twEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c != '\r') o += c;
    }
    return o;
}
std::string twUnescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; o += s[i] == 'n' ? '\n' : s[i]; }
        else o += s[i];
    }
    return o;
}

PangoFontDescription* twFont(const App& app, double px) {
    const std::string& f = app.cfg.customTextFont;
    PangoFontDescription* d = pango_font_description_new();
    if (!app.cfg.customTextFontName.empty()) pango_font_description_set_family(d, app.cfg.customTextFontName.c_str());  // (a name typed in Settings wins)
    else pango_font_description_set_family(d, f == "Display" ? kDisplayFamily : f == "Sans" ? "Sans" : f == "Serif" ? "Serif" : f == "Mono" ? "Monospace" : f.c_str());
    if (app.cfg.customTextBold) pango_font_description_set_weight(d, PANGO_WEIGHT_BOLD);
    else if (f == "Display" && app.cfg.customTextFontName.empty()) pango_font_description_set_weight(d, PANGO_WEIGHT_MEDIUM);
    if (app.cfg.customTextItalic) pango_font_description_set_style(d, PANGO_STYLE_ITALIC);
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    return d;
}

PangoAlignment twAlign(const App& app) {
    return app.cfg.customTextAlign == "left" ? PANGO_ALIGN_LEFT : app.cfg.customTextAlign == "right" ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_CENTER;
}

// The biggest size at which the text fits the box (a bisection over the height of the wrapped layout).
double twFitPx(const App& app, const std::string& text, double w, double h) {
    PangoLayout* m = pango_layout_new(pango_layout_get_context(g_measure));
    pango_layout_set_width(m, static_cast<int>(w * PANGO_SCALE));
    pango_layout_set_wrap(m, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(m, text.data(), static_cast<int>(text.size()));
    double lo = kTwMinPx, hi = kTwMaxPx;
    for (int i = 0; i < 9; i++) {
        const double mid = (lo + hi) / 2;
        PangoFontDescription* d = twFont(app, mid);
        pango_layout_set_font_description(m, d);
        pango_font_description_free(d);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(m, &tw, &th);
        if (th <= h) lo = mid; else hi = mid;
    }
    g_object_unref(m);
    return lo;
}

Color twColor(const App& app) {
    const std::string& c = app.cfg.customTextColor;
    return c == "white" ? Color{1, 1, 1} : c == "black" ? Color{0, 0, 0} : app.color(c.c_str());
}

void textWPaint(App& app, DeskWidget& d, cairo_t* cr, int W, int H) {
    TextW& X = static_cast<TextW&>(d);
    gnInitMeasure();
    if (app.cfg.widgetsLocked) X.typingOn = false;  // (locked: the text is only shown)
    const double w = std::max(20.0, W - 2 * kTwPad), h = std::max(10.0, H - 2 * kTwPad);
    const bool empty = X.ed.s.empty(), hint = empty && !app.cfg.widgetsLocked;
    const std::string placeholder = i18n::tr("Click to write");
    const double px = app.cfg.customTextFit ? twFitPx(app, empty ? placeholder : X.ed.s, w, h) : static_cast<double>(app.cfg.customTextSize);
    const std::string look = app.cfg.customTextFont + app.cfg.customTextFontName + "|" + std::to_string(static_cast<int>(px * 10)) + "|" + std::to_string(app.cfg.customTextBold) + std::to_string(app.cfg.customTextItalic) + "|" + app.cfg.customTextAlign + "|" + std::to_string(static_cast<int>(w));
    if (look != X.look) {
        X.look = look;
        if (X.fd) pango_font_description_free(X.fd);
        X.fd = twFont(app, px);
        X.ed.font = X.fd;
        X.ed.align = twAlign(app);
        X.ed.stale = true;
    }
    X.ed.px = px;
    PangoLayout* lay = X.ed.layout(w);
    const double th = X.ed.contentHeight();
    X.ox = kTwPad;
    X.oy = std::max(kTwPad, (H - th) / 2);
    const Color ink = twColor(app);

    cairo_save(cr);
    roundedRect(cr, 0, 0, W, H, d.radius);
    cairo_clip(cr);
    cairo_translate(cr, X.ox, X.oy);
    if (hint) {
        PangoLayout* ph = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(ph, X.fd);
        pango_layout_set_width(ph, static_cast<int>(w * PANGO_SCALE));
        pango_layout_set_wrap(ph, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(ph, twAlign(app));
        pango_layout_set_text(ph, placeholder.c_str(), -1);
        setSource(cr, ink, 0.35);
        cairo_move_to(cr, 0, 0);
        pango_cairo_show_layout(cr, ph);
        g_object_unref(ph);
    }
    if (!empty) {
        pango_cairo_update_layout(cr, lay);
        if (app.cfg.customTextShadow) {  // a soft shadow so the words read over any wallpaper
            for (int k = 3; k >= 1; k--) {
                setSource(cr, app.color("shadow"), 0.16);
                cairo_move_to(cr, 0, k);
                pango_cairo_show_layout(cr, lay);
            }
        }
        if (X.typingOn)
            for (const Rect& s : X.ed.selectionRects()) {
                setSource(cr, app.color("primary"), 0.35);
                cairo_rectangle(cr, s.x, s.y, s.w, s.h);
                cairo_fill(cr);
            }
        setSource(cr, ink);
        cairo_move_to(cr, 0, 0);
        pango_cairo_show_layout(cr, lay);
    }
    if (X.typingOn) {
        double cx = 0, cy = 0, ch = 0;
        X.ed.caret(X.ed.cur, cx, cy, ch);
        if (empty) cx = w / 2 * (app.cfg.customTextAlign == "left" ? 0 : app.cfg.customTextAlign == "right" ? 2 : 1);
        roundedRect(cr, cx, cy, 2, ch, 1);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
    }
    cairo_restore(cr);
    if (X.typingOn) {  // the box, while typing
        roundedRect(cr, 1, 1, W - 2, H - 2, d.radius);
        setSource(cr, app.color("primary"), 0.55);
        cairo_set_line_width(cr, 1.5);
        const double dash[2] = {6, 5};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0);
    }
}

void textWTap(App& app, DeskWidget& d) {
    TextW& X = static_cast<TextW&>(d);
    if (app.cfg.widgetsLocked) return;  // writing is for when the widgets are unlocked
    X.typingOn = true;
    X.ed.layout(std::max(20.0, d.w - 2 * kTwPad));
    X.ed.moveTo(X.ed.indexAt(app.pointerX - kDeskRoom - X.ox, app.pointerY - kDeskRoom - X.oy), false);
    deskDraw(app, d);
}
bool textWTyping(const DeskWidget& d) { return static_cast<const TextW&>(d).typingOn; }
void textWBlur(App& app, DeskWidget& d) {
    TextW& X = static_cast<TextW&>(d);
    if (!X.typingOn) return;
    X.typingOn = false;
    X.ed.anc = X.ed.cur;
    deskSave(d);
    deskDraw(app, d);
}
bool textWKey(App& app, DeskWidget& d, uint32_t key) {
    TextW& X = static_cast<TextW&>(d);
    if (!app.xkbState) return false;
    if (xkb_state_key_get_one_sym(app.xkbState, key + 8) == XKB_KEY_Escape) { textWBlur(app, d); return true; }
    X.ed.layout(std::max(20.0, d.w - 2 * kTwPad));
    const std::string before = X.ed.s;
    if (!editorKey(app, X.ed, key, 3)) return false;
    if (X.ed.s != before) deskSave(d);
    deskDraw(app, d);
    return true;
}
void textWSave(const DeskWidget& d, std::ostream& out) { out << "text\t" << twEscape(static_cast<const TextW&>(d).ed.s) << "\n"; }
void textWLoad(DeskWidget& d, const std::string& line) {
    if (line.rfind("text\t", 0) == 0) static_cast<TextW&>(d).ed.set(twUnescape(line.substr(5)));
}

void textWInit() {
    TextW& X = g_textW;
    X.name = "text";
    X.minW = 60; X.minH = 30;
    X.maxW = 2400; X.maxH = 1400;
    X.w = 360; X.h = 120;
    X.radius = 14;
    X.noShadow = true;
    X.lightHandle = true;
    X.wantsKeys = true;
    X.paint = textWPaint;
    X.tap = textWTap;
    X.typing = textWTyping;
    X.key = textWKey;
    X.blur = textWBlur;
    X.saveExtra = textWSave;
    X.loadLine = textWLoad;
}
