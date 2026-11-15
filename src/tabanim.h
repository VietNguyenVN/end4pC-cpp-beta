#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

constexpr double kTabSpringK = 180, kTabSpringC = 14, kTabSubStep = 1.0 / 240, kTabFadeSeconds = 0.2, kTabSlide = 14;

struct TabAnim {
    double pos = 0, vel = 0;
    int tab = 0;
    int prevTab = 0;
    int dir = 1;
    bool fading = false;
    double fadeT = 1;
    std::chrono::steady_clock::time_point last{};

    void snap(int t) { tab = prevTab = t; pos = t; vel = 0; fading = false; fadeT = 1; }
    bool select(int t, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        if (t == tab) return false;
        last = now;
        dir = t > tab ? 1 : -1;
        prevTab = tab;
        tab = t;
        fading = true;
        fadeT = 0;
        return true;
    }
    void endFade() { fading = false; fadeT = 1; }

    bool step(double dt) {
        bool moving = false;
        if (std::fabs(tab - pos) < 0.002 && std::fabs(vel) < 0.02) {
            pos = tab;
            vel = 0;
        } else {
            for (double left = dt; left > 1e-9; left -= kTabSubStep) {
                const double h = std::min(kTabSubStep, left);
                vel += (kTabSpringK * (tab - pos) - kTabSpringC * vel) * h;
                pos += vel * h;
            }
            moving = true;
        }
        if (fading) {
            fadeT += dt / kTabFadeSeconds;
            if (fadeT >= 1) endFade();
            else moving = true;
        }
        return moving;
    }
    bool stepNow(std::chrono::steady_clock::time_point now) {
        const double dt = last.time_since_epoch().count() == 0 ? 0.016 : std::min(0.1, std::chrono::duration<double>(now - last).count());
        last = now;
        return step(dt);
    }
    double drawPos(int count, double segW, double allow = 4) const {
        const double slack = segW > 0 ? allow / segW : 0;
        return std::clamp(pos, -slack, count - 1 + slack);
    }
    bool moving() const { return fading || std::fabs(tab - pos) >= 0.002 || std::fabs(vel) >= 0.02; }

    double ease() const { return 1 - std::pow(1 - fadeT, 3); }
    double outDx(double slide = kTabSlide) const { return -dir * slide * ease(); }
    double inDx(double slide = kTabSlide) const { return dir * slide * (1 - ease()); }
    double outAlpha() const { return 1 - ease(); }
    double inAlpha() const { return ease(); }
};
