// Accessibility: the screen magnifier (Hyprland's cursor:zoom_factor, what SUPER + and SUPER - do).
constexpr double kZoomMin = 1.0, kZoomMax = 3.0, kZoomStep = 0.3;

double magnifierGet() {
    if (wmKind() != Wm::Hyprland) return 1.0;
    const std::string j = hyprRequest("j/getoption cursor:zoom_factor");
    const size_t at = j.find("\"float\":");
    return at == std::string::npos ? 1.0 : std::atof(j.c_str() + at + 8);
}

std::string magnifierText(double z) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f", z);
    return buf;
}

bool magnifierSet(App& app, double zoom) {
    if (wmKind() != Wm::Hyprland) return false;
    zoom = std::round(std::clamp(zoom, kZoomMin, kZoomMax) * 10) / 10;
    const std::string v = magnifierText(zoom);
    const std::string r = hyprRequest("eval hl.config({cursor={zoom_factor=" + v + "}})");  // Lua configs (Hyprland >= 0.55); older ones take the classic keyword
    if (r.rfind("ok", 0) != 0) hyprRequest("keyword cursor:zoom_factor " + v);
    islandLive(app, "zoom", zoom > kZoomMin ? "magnifer-zoom-in" : "magnifer", "", v + "\xC3\x97", "on_surface", "on_surface", 1.6);
    return true;
}

// `magnifier in | out | reset | get | set N`
std::string magnifierCommand(App& app, const std::string& arg) {
    if (wmKind() != Wm::Hyprland) return "the magnifier needs Hyprland (its cursor zoom)";
    const double now = magnifierGet();
    if (arg == "get" || arg.empty()) return magnifierText(now);
    if (arg == "in") magnifierSet(app, now + kZoomStep);
    else if (arg == "out") magnifierSet(app, now - kZoomStep);
    else if (arg == "reset") magnifierSet(app, kZoomMin);
    else if (arg.rfind("set ", 0) == 0) magnifierSet(app, std::atof(arg.c_str() + 4));
    else return "usage: magnifier in|out|reset|get|set N";
    return magnifierText(magnifierGet());
}
