// A scroll position with a rubber band, for the windows that scroll (Settings, Tools): at either end the wheel stretches the content a little
// and it eases back by itself. It also tells how visible a scroll thumb should be: shown while the wheel moves, faded out soon after.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

struct Elastic {
    using Clock = std::chrono::steady_clock;
    double pos = 0, over = 0, max = 0;  // over: positive at the bottom end, negative at the top one
    Clock::time_point wheel{}, last = Clock::now();
    static constexpr double kLimit = 26, kPull = 0.25, kEase = 9;
    static constexpr double kThumbHold = 0.6, kThumbFade = 0.3;  // seconds: fully shown after the last wheel step, then fading out

    double off() const { return pos + over; }
    void reset() { pos = over = 0; }
    void fit() { pos = std::clamp(pos, 0.0, max); }
    double since() const { return wheel == Clock::time_point{} ? 1e9 : std::chrono::duration<double>(Clock::now() - wheel).count(); }
    double thumbAlpha() const {
        const double s = since();
        return s < kThumbHold ? 1.0 : s < kThumbHold + kThumbFade ? 1.0 - (s - kThumbHold) / kThumbFade : 0.0;
    }
    bool busy() const { return over != 0 || since() < kThumbHold + kThumbFade + 0.05; }  // (needs frames: the band is easing or the thumb fading)

    // The wheel moved the content by d pixels.
    void by(double d) {
        if (max <= 0) return;
        wheel = Clock::now();
        if (over != 0 && (over > 0) != (d > 0)) {  // going back: the stretch is given back first
            const double s = over > 0 ? 1 : -1, back = std::min(std::fabs(over), std::fabs(d));
            over -= s * back;
            d += s * back;
        }
        const double np = pos + d, edge = std::clamp(np, 0.0, max);
        pos = edge;
        if (np != edge) over = kLimit * std::tanh((over + (np - edge) * kPull) / kLimit);
    }
    // Once per loop turn; true when something changed on screen.
    bool step() {
        const auto now = Clock::now();
        const double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
        last = now;
        bool changed = since() < kThumbHold + kThumbFade + 0.05 && since() >= kThumbHold;  // (the thumb is fading)
        if (over != 0 && now - wheel >= std::chrono::milliseconds(90)) {
            over *= std::exp(-dt * kEase);
            if (std::fabs(over) < 0.3) over = 0;
            changed = true;
        }
        return changed;
    }
};

// The wheel moved a page's own scroll variable v (kept in [0, max]) by d; the rubber band lives in el.
inline void elasticWheel(Elastic& el, double& v, double max, double d) {
    el.pos = v;
    el.max = max;
    el.by(d);
    v = el.pos;
}
