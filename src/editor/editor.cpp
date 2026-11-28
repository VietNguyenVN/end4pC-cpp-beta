// T3xt: the text editor of the shell. A window of its own (a separate program, like Play3r and Fil3s): tabs, a sidebar with the open files and the
// folder, syntax highlighting for the usual languages in the colours of the shell, find and replace. No terminal, no plugins, no toolkit.
//   end4pC-t3xt [files...]     without arguments it opens an empty document
#include <cairo.h>
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <locale.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
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
#include "cursor-shape-v1-client-protocol.h"
#include "document.h"
#include "xdg-shell-client-protocol.h"

namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
using t3::Pos;

#include "../gui/draw.h"

struct Field {
    std::string s;
    size_t cur = 0;
};

constexpr int kDefaultW = 1180, kDefaultH = 740, kMinW = 700, kMinH = 460;
constexpr double kPad = 16, kRadius = 28, kSideW = 264, kStatusH = 34, kTabH = 40;

enum Id { None, BtnSidebar, BtnNew, BtnOpen, BtnSave, BtnFind, BtnClose, TabItem, TabClose, SideDoc, SideDocClose, SideFile, SideUp, TextArea, Gutter, Scrollbar, FindField, ReplaceField, FindClear,
          FindPrev, FindNext, FindCase, ReplaceOne, ReplaceAllBtn, FindClose, DlgSave, DlgDiscard, DlgCancel, Scrim };
struct Hit { Rect r; int id, arg; };
enum class Dlg { None, CloseTab, Quit };

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

struct SideEntry { std::string name, path; bool dir; };

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
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;
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
    xkb_compose_table* composeTable = nullptr;  // dead keys and the compose key (á é ñ ü...)
    xkb_compose_state* composeState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    double px = 0, py = 0;
    bool inside = false;
    int hoverId = None, hoverArg = 0;
    std::vector<Hit> hits;

    // The documents.
    std::vector<std::unique_ptr<t3::Doc>> docs;
    int active = 0;
    bool sidebar = true, restoreSession = true;  // restoreSession: the files of the last time open again (t3xt.txt: restore=0 turns it off)
    int fontPx = 14;
    double tabScroll = 0;

    // Layout, set while drawing.
    Rect textRect, sideRect;
    double gutterW = 0, lineH = 20, charW = 8;
    int viewLines = 1;
    Elastic band;
    double scrollMaxY = 0, scrollMaxX = 0;
    uint64_t widthRevision = ~0ull;
    size_t widthDoc = 0;
    double longestBytes = 0;
    Rect scrollTrack;
    double thumbTop = 0, thumbH = 0;
    bool dragScroll = false;
    double dragScrollOffset = 0;

    // Mouse selection.
    bool selecting = false;
    int clickCount = 0;
    Clock::time_point lastClick{};
    int lastClickLine = -1, lastClickCol = -1;

    // Find and replace.
    bool findOpen = false, replaceOpen = false, matchCase = false;
    Field find, replace;
    int focus = 0;  // 0 the text, 1 find, 2 replace
    bool caretOn = true;
    Clock::time_point caretReset = Clock::now();
    int matchCount = 0;
    uint64_t matchKeyRev = ~0ull;
    size_t matchKeyDoc = 0;
    std::string matchKeyNeedle;
    bool matchKeyCase = false;

    // The sidebar: the folder it shows and the scrolling of its list.
    std::string sideDir;
    std::vector<SideEntry> sideEntries;
    Elastic sideBand;
    double sidePos = 0, sideMax = 0;

    // The dialog that asks about unsaved changes.
    Dlg dialog = Dlg::None;
    int dialogDoc = -1;
    std::string toast;
    Clock::time_point toastAt{};
    std::string clipboard;  // what was copied here (also what is pasted when wl-clipboard is missing)

    std::mutex chosenMutex;
    std::vector<std::string> chosenOpen;
    std::string chosenSave;
    Clock::time_point start = Clock::now();
} g;

int g_wakeFd = -1;

t3::Doc& doc() { return *g.docs[static_cast<size_t>(g.active)]; }

#include "ui.h"
#include "window.h"
}  // namespace

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    return run(argc, argv);
}
