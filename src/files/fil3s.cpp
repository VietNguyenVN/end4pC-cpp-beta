// Fil3s: the file manager of the shell. A window of its own (a separate program, like Play3r) that is light on purpose: one process, folders read in
// a thread, thumbnails decoded in workers, no toolkit. It looks like the rest of the shell and does the everyday things: browse, search, copy, move,
// rename, trash, convert images / video / audio with one click, make and extract archives.
//   end4pC-fil3s [folder]     without arguments it opens your home folder
#include <cairo.h>
#include <fcntl.h>
#include <stdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../elastic.h"
#include "../hidpi.h"
#include "../i18n.h"
#include "../shellfont.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;

#include "../gui/draw.h"
#include "model.h"
#include "convert.h"

struct Field {
    std::string s;
    size_t cur = 0;
};

constexpr int kDefaultW = 1180, kDefaultH = 720, kMinW = 760, kMinH = 480;
constexpr double kPad = 16, kHeaderH = 56, kSideW = 248, kRadius = 28;

enum Id { None, BtnBack, BtnForward, BtnUp, BtnClose, BtnView, BtnSort, BtnHidden, BtnNew, Crumb, PlaceRow, Item, SearchField, SearchClear, MenuRow, DialogOk, DialogCancel, DialogField, Scrim, StatusCancel, Empty };
struct Hit { Rect r; int id, arg; };

// One entry of the right-click menu; an entry with `children` opens a second panel beside it.
enum class Act { Open, Cut, Copy, Paste, Rename, Trash, Delete, Restore, EmptyTrash, NewFolder, NewFile, Convert, Compress, Extract, ExtractHere, SelectAll, Refresh, Hidden, None };
struct MenuItem {
    std::string label;
    const char* icon = "";
    Act act = Act::None;
    int arg = 0;
    bool enabled = true, separator = false;
    std::vector<MenuItem> children;
};
enum class Dlg { None, NewFolder, NewFile, Rename, ConfirmDelete };

struct Buffer {
    hidpi::Frame frame;
    wl_buffer* buffer = nullptr;
    bool busy = false;
    void clear() {
        if (buffer) wl_buffer_destroy(buffer);
        hidpi::abandon(frame);
        buffer = nullptr;
        busy = false;
    }
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fscaleMgr = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wl_callback* frameCallback = nullptr;
    hidpi::SurfaceScale scale;
    Buffer buffers[3];
    int width = kDefaultW, height = kDefaultH;
    bool configured = false, running = true, dirty = true;

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None, hoverArg = 0;
    std::vector<Hit> hits;

    // Where we are.
    std::string cwd;
    std::vector<std::string> back, forward;
    std::vector<Entry> entries;          // the folder, sorted
    std::vector<int> view;               // the entries that pass the filter and the hidden switch
    bool loading = false;
    std::string loadError;
    uint64_t generation = 0;
    bool viewDirty = true;
    Sort sort = Sort::Name;
    bool descending = false, showHidden = false, grid = true;
    Field search;
    int focus = 0;                       // 1 search field, 2 field of a dialog
    bool caretOn = true;
    Clock::time_point caretReset = Clock::now();

    // Selection: by path, so it survives a refresh.
    std::set<std::string> selected;
    int cursor = -1, anchor = -1;        // positions in `view`
    std::vector<std::string> clip;
    bool clipCut = false;
    std::set<std::string> selectAfter;   // names made by a conversion: selected when the folder is read again

    // Scrolling and layout (set while drawing).
    Elastic band;
    double scroll = 0, scrollMax = 0;
    Rect content, sidebar;
    int columns = 1;
    double cellW = 0, cellH = 0;
    // The lasso: dragging on the empty part of the folder selects what the rectangle touches (positions in the content, so it follows the scroll).
    bool lasso = false;
    double lassoX0 = 0, lassoY0 = 0, lassoX1 = 0, lassoY1 = 0;
    std::set<std::string> lassoBase;
    int lastClickIndex = -1;
    Clock::time_point lastClick{};

    // Menu and dialog.
    bool menuOpen = false;
    double menuX = 0, menuY = 0;
    std::vector<MenuItem> menu;
    int menuSub = -1;                    // the entry whose panel is open
    int subPending = -2;                 // the entry the pointer rests on while another one's panel is open (-2: none): it takes over after a moment
    Clock::time_point subPendingAt{};
    Rect menuRect, subRect;
    Dlg dialog = Dlg::None;
    Field dialogField;
    std::string dialogTarget;
    std::string toast;
    Clock::time_point toastAt{};

    std::vector<Place> places, drives;
    Clock::time_point drivesAt{};
    int inotifyFd = -1, watch = -1;

    Lister lister;
    Thumbnailer thumbs;
    Worker worker;
    std::string status;
    Clock::time_point start = Clock::now(), lastTick = Clock::now();
} g;

int g_wakeFd = -1;

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) { return run(argc, argv); }
