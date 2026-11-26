// The monitor that has the focus (the one the pointer is on): its name, asked of the compositor at most every 300 ms.
std::string focusedOutputName() {
    static std::string name;
    static std::chrono::steady_clock::time_point at{};
    const auto now = std::chrono::steady_clock::now();
    if (now - at < std::chrono::milliseconds(300) && !name.empty()) return name;
    const std::string info = wmFocusedOutputInfo();
    name = info.substr(0, info.find_first_of(" \n"));
    at = now;
    return name;
}

struct BarPopupAnchor {
    Bar* bar;
    double gap, length, position;
    bool followsWidget;
};

template<class WidgetCentre>
BarPopupAnchor barPopupAnchor(App& app, Bar* clickedBar, const std::string& preference, WidgetCentre widgetCentre) {
    Bar* bar = clickedBar;
    if (!bar) {
        const std::string focused = focusedOutputName();
        for (const auto& candidate : app.bars)
            if (!focused.empty() && candidate->connector == focused) { bar = candidate.get(); break; }
        if (!bar) bar = mainBar(app);
    }
    const bool vertical = sideBar(app);
    const double gap = app.cfg.barStyle == "float" || app.cfg.barStyle == "island" ? kBarGap : 0;
    const bool haveBar = bar && bar->surface && bar->configured && bar->width > 0;
    const double outputLength = bar ? (vertical ? bar->wpH : bar->wpW) : 0;
    const double edge = app.cfg.frame + gap;  // popup surfaces are measured from the output edge; the bar surface sits inside the frame strips
    const double length = haveBar ? bar->width + 2 * edge : outputLength > 0 ? outputLength : vertical ? 1080 : 1920;
    double centre = -1;
    if (haveBar && preference == "widget") centre = widgetCentre(*bar, vertical);
    double position = length / 2;
    if (centre >= 0) position = centre + edge;
    else if (preference == "start") position = 0;
    else if (preference == "end") position = length;
    return {bar, gap, length, position, centre >= 0};
}

// Measure from the anchored edge so a taller card grows toward the middle of the output.
double barPopupNearEdge(double position, double length, double size, double maxSize, double inset) {
    return std::clamp(position - size / 2, inset, std::max(inset, length - inset - maxSize));
}

// Reserve a stable surface and place the visible card using its current height.
BarPopupSide barPopupSide(const BarPopupAnchor& anchor, double inset, int room, int surfaceHeight) {
    const double low = inset - room;
    const double high = std::max(low, anchor.length - low - surfaceHeight);
    const int top = static_cast<int>(std::round(std::clamp(anchor.position - surfaceHeight / 2.0, low, high)));
    return {anchor.length, anchor.position, inset, top};
}

double barPopupSideCardY(const BarPopupSide& side, double height) {
    return barPopupNearEdge(side.position, side.length, height, height, side.inset) - side.surfaceTop;
}

double notifWidgetCentre(const Bar& bar, bool vertical) {
    if (vertical) {
        for (const auto& item : sideItems(bar))
            if (item.id == HitNotif) return item.r.y + item.r.h / 2;
    } else if (hasSlot(bar, "notifications")) {
        return notifX(bar) + notifW(*bar.app) / 2.0;
    }
    return -1;
}
