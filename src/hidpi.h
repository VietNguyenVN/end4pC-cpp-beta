// Fractional scaling (wp_fractional_scale_v1 + wp_viewporter): a surface is drawn into a buffer of its logical size times the
// scale the compositor prefers for it (at 1.5, a 1000x40 surface gets a 1500x60 buffer), and the viewport shows that buffer at the
// logical size. Without this the compositor stretches a logical-size buffer to the screen's pixels, and everything looks soft.
//
// Drawing code does not change: the Cairo surface of a frame carries the scale as its device scale, so every coordinate, path and
// Pango call stays in logical units (device scale lives below the matrix, so cairo_set_matrix / cairo_identity_matrix keep it).
// Everything the compositor gets in surface coordinates (layer sizes, margins, input and blur regions, pointer positions) is
// logical too and is left alone. Only the buffer's pixel count changes.
//
// Without the two globals (an older compositor, or END4PC_NO_FRACTIONAL=1) the scale stays 1 and no viewport is made: exactly the
// old behaviour. Include after wayland-client.h, cairo.h and the two generated protocol headers.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sys/mman.h>
#include <unistd.h>
#include <cairo/cairo.h>
#include <wayland-client.h>
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

namespace hidpi {

// The scale is counted in 120ths, as the protocol sends it: 120 = 1.0, 180 = 1.5.
constexpr uint32_t kBase = 120;

// The last scale any surface was told. A new surface starts from it, so its first frame (drawn before the compositor has said
// anything about it) is already sharp instead of flashing soft once.
inline uint32_t g_seed = kBase;

// END4PC_NO_FRACTIONAL=1: do not bind the two globals, and draw at scale 1 as before.
inline bool disabled() {
    static const bool off = [] { const char* e = std::getenv("END4PC_NO_FRACTIONAL"); return e && *e && std::strcmp(e, "0") != 0; }();
    return off;
}

// The one place a logical size becomes a buffer size, so the buffer, its damage and the viewport always agree.
inline int bufferExtent(int logical, double scale) { return std::max(1, static_cast<int>(std::lround(logical * scale))); }

// Rounds a logical coordinate to the nearest buffer pixel (for 1 px lines that should land on whole pixels).
inline double snap(double v, double scale) { return std::round(v * scale) / scale; }

// The scale state of one wl_surface. Owned by whatever owns the surface; it must not move while attached (the protocol
// listener points at it).
struct SurfaceScale {
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fscale = nullptr;
    uint32_t num = kBase;
    std::function<void()> onChange;  // the compositor asked for a different scale: draw the surface again
    double factor() const { return viewport ? num / static_cast<double>(kBase) : 1.0; }
};

inline void onPreferredScale(void* data, wp_fractional_scale_v1*, uint32_t num) {
    auto* s = static_cast<SurfaceScale*>(data);
    if (num == 0) return;
    g_seed = num;
    if (num == s->num) return;
    s->num = num;
    if (s->onChange) s->onChange();
}
inline const wp_fractional_scale_v1_listener kListener = {onPreferredScale};

// Call right after wl_compositor_create_surface (before the first commit). Does nothing without the globals.
inline void attach(SurfaceScale& s, wl_surface* surface, wp_viewporter* viewporter, wp_fractional_scale_manager_v1* manager,
                   std::function<void()> onChange = {}) {
    s.onChange = std::move(onChange);
    if (!surface || !viewporter || !manager || s.viewport) return;
    s.viewport = wp_viewporter_get_viewport(viewporter, surface);
    s.fscale = wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface);
    wp_fractional_scale_v1_add_listener(s.fscale, &kListener, &s);
    s.num = g_seed;
    wl_surface_set_buffer_scale(surface, 1);  // the protocol requires 1: the viewport does the scaling
}

// Call before wl_surface_destroy. Safe on a surface that was never attached.
inline void detach(SurfaceScale& s) {
    if (s.fscale) wp_fractional_scale_v1_destroy(s.fscale);
    if (s.viewport) wp_viewport_destroy(s.viewport);
    s.fscale = nullptr;
    s.viewport = nullptr;
    s.num = kBase;
    s.onChange = nullptr;
}

// One shared-memory frame: begin() maps a buffer of the scaled size and gives a Cairo surface that draws in logical units;
// the caller makes its cairo_t on `surface`, draws, destroys the cairo_t, then calls buffer() and present().
struct Frame {
    int lw = 0, lh = 0;       // logical size
    int pw = 0, ph = 0;       // buffer size in pixels
    int stride = 0;
    size_t size = 0;
    int fd = -1;
    void* data = nullptr;
    double scale = 1;
    cairo_surface_t* surface = nullptr;
};

// The buffer starts fully transparent (a fresh memfd is zero-filled).
inline bool begin(Frame& f, int logicalW, int logicalH, double scale, const char* name) {
    f.lw = logicalW;
    f.lh = logicalH;
    f.scale = scale > 0 ? scale : 1;
    f.pw = bufferExtent(logicalW, f.scale);
    f.ph = bufferExtent(logicalH, f.scale);
    f.stride = f.pw * 4;
    f.size = static_cast<size_t>(f.stride) * f.ph;
    f.fd = memfd_create(name, MFD_CLOEXEC);
    if (f.fd < 0) return false;
    if (ftruncate(f.fd, static_cast<off_t>(f.size)) < 0) { close(f.fd); f.fd = -1; return false; }
    f.data = mmap(nullptr, f.size, PROT_READ | PROT_WRITE, MAP_SHARED, f.fd, 0);
    if (f.data == MAP_FAILED) { f.data = nullptr; close(f.fd); f.fd = -1; return false; }
    f.surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(f.data), CAIRO_FORMAT_ARGB32, f.pw, f.ph, f.stride);
    cairo_surface_set_device_scale(f.surface, f.scale, f.scale);
    return true;
}

// Gives up a frame that will not be shown.
inline void abandon(Frame& f) {
    if (f.surface) cairo_surface_destroy(f.surface);
    if (f.data) munmap(f.data, f.size);
    if (f.fd >= 0) close(f.fd);
    f.surface = nullptr;
    f.data = nullptr;
    f.fd = -1;
}

// Turns the drawn frame into a wl_buffer (the Cairo surface and the mapping are released). nullptr on failure.
inline wl_buffer* buffer(Frame& f, wl_shm* shm) {
    if (f.surface) { cairo_surface_flush(f.surface); cairo_surface_destroy(f.surface); f.surface = nullptr; }
    wl_buffer* b = nullptr;
    if (f.fd >= 0) {
        wl_shm_pool* pool = wl_shm_create_pool(shm, f.fd, static_cast<int32_t>(f.size));
        b = wl_shm_pool_create_buffer(pool, 0, f.pw, f.ph, f.stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }
    abandon(f);
    return b;
}

// Attaches, damages the whole buffer, sizes the viewport to the logical size, and commits.
inline void present(wl_surface* surface, wl_buffer* b, const Frame& f, const SurfaceScale& s) {
    wl_surface_attach(surface, b, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, f.pw, f.ph);
    if (s.viewport) wp_viewport_set_destination(s.viewport, f.lw, f.lh);
    wl_surface_commit(surface);
}

// The device scale a cairo_t draws at, for caches that pre-render at buffer pixels. Only the surface's device scale counts, not
// scaling in the matrix: an animation that scales (the dock, a popup reveal) would otherwise make a new cached copy every frame.
inline double scaleOf(cairo_t* cr) {
    double sx = 1, sy = 1;
    cairo_surface_get_device_scale(cairo_get_group_target(cr), &sx, &sy);
    return std::clamp(sx, 1.0, 8.0);
}

}  // namespace hidpi
