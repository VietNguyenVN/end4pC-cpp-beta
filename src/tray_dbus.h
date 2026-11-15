// org.kde.StatusNotifierWatcher over sd-bus (the system tray protocol). Plain C: sd-bus's vtable macros need C-only initialisers.
// The shell (C++) reads the item list here and talks to each item itself.
#ifndef TRAY_DBUS_H
#define TRAY_DBUS_H

#include <systemd/sd-bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TrayChanged)(void* userdata);

// Exports the watcher on `bus` and registers the shell as the tray host. Returns 0 on success, otherwise a negative errno
// when another watcher already owns the name (the other one keeps the tray).
int traywatch_start(sd_bus* bus, TrayChanged changed, void* userdata);

int traywatch_count(void);
const char* traywatch_item(int i);

#ifdef __cplusplus
}
#endif
#endif
