// Wayland delivers a batch of preedit, commit and deletion events together at done.
void imeCommit() {
    zwp_text_input_v3_commit(g.ime);
    ++g.imeSerial;
}

void resetImePage() {
    if (g.imePage) g.imePage->imeCancel();
    g.imePage = nullptr;
    g.imeNeedsDone = false;
    g.imePending = {};
    g.imePreedit.clear();
    g.dirty = true;
}

void disableIme() {
    if (!g.imePage) return;
    zwp_text_input_v3_disable(g.ime);
    imeCommit();
    resetImePage();
}

void clipImeText(ImeContext& state) {
    constexpr size_t limit = 4000;
    if (state.text.size() <= limit) return;
    const size_t low = std::min(state.cursor, state.anchor), high = std::max(state.cursor, state.anchor);
    if (high - low > limit) {
        state.text.clear();
        state.cursor = state.anchor = 0;
        return;
    }
    const size_t extra = limit - (high - low);
    size_t first = low - std::min(low, extra / 2);
    size_t last = std::min(state.text.size(), first + limit);
    while (last < high) { ++first; ++last; }
    while (first < low && (static_cast<unsigned char>(state.text[first]) & 0xc0) == 0x80) ++first;
    while (last > high && last < state.text.size() && (static_cast<unsigned char>(state.text[last]) & 0xc0) == 0x80) --last;
    state.text = state.text.substr(first, last - first);
    state.cursor -= first;
    state.anchor -= first;
}

void syncIme() {
    if (!g.imeEntered || g.pages.empty()) return;
    ImeContext next;
    ToolPage* page = !g.tabMenu && g.switchT >= 1 && cur().imeContext(next) ? &cur() : nullptr;
    if (page != g.imePage || (page && next.field != g.imeSent.field)) disableIme();
    if (!page || g.imeNeedsDone) return;
    const bool enable = !g.imePage;
    if (enable) {
        g.imePage = page;
        zwp_text_input_v3_enable(g.ime);
        zwp_text_input_v3_set_content_type(g.ime, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    }
    clipImeText(next);
    const Rect r = contentRect();
    next.caret = {std::round(r.x + next.caret.x), std::round(r.y + next.caret.y),
                  std::max(1.0, std::round(next.caret.w)), std::max(1.0, std::round(next.caret.h))};
    const auto& old = g.imeSent;
    const bool textChanged = enable || next.text != old.text || next.cursor != old.cursor || next.anchor != old.anchor;
    const bool caretChanged = enable || next.caret.x != old.caret.x || next.caret.y != old.caret.y
        || next.caret.w != old.caret.w || next.caret.h != old.caret.h;
    if (textChanged) {
        zwp_text_input_v3_set_surrounding_text(g.ime, next.text.c_str(), next.cursor, next.anchor);
        // a click, a key or a paste is not the input method's doing: say so, or it may keep composing over text that moved
        zwp_text_input_v3_set_text_change_cause(g.ime, g.imeFromIme ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
        g.imeFromIme = false;
    }
    if (caretChanged) zwp_text_input_v3_set_cursor_rectangle(g.ime, next.caret.x, next.caret.y, next.caret.w, next.caret.h);
    if (textChanged || caretChanged) {
        imeCommit();
        if (enable) g.imeActivation = g.imeSerial;
        g.imeSent = std::move(next);
    }
}

const zwp_text_input_v3_listener kImeListener = {
    [](void*, zwp_text_input_v3*, wl_surface* surface) {
        g.imeEntered = surface == g.surface;
        g.dirty = true;
    },
    [](void*, zwp_text_input_v3*, wl_surface*) { g.imeEntered = false; resetImePage(); },
    [](void*, zwp_text_input_v3*, const char* text, int32_t begin, int32_t) {
        g.imePending.preedit = text ? text : "";
        g.imePending.preeditCursor = begin;
    },
    [](void*, zwp_text_input_v3*, const char* text) { g.imePending.commit = text ? text : ""; },
    [](void*, zwp_text_input_v3*, uint32_t before, uint32_t after) {
        g.imePending.before = before;
        g.imePending.after = after;
    },
    [](void*, zwp_text_input_v3*, uint32_t serial) {
        ImeContext context;
        if (g.imeEntered && g.imePage && static_cast<int32_t>(serial - g.imeActivation) >= 0 && g.imePage->imeContext(context)
            && context.field == g.imeSent.field) {
            g.imePage->imeEdit(g.imePending);
            g.imeFromIme = true;
            g.imePreedit = g.imePending.preedit;
            g.imeNeedsDone = serial != g.imeSerial;
            g.dirty = true;
        }
        g.imePending = {};
    },
};
