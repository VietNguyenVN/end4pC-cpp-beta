// Implementation of the StatusNotifierWatcher declared in tray_dbus.h: tracks the registered tray items.
#include "tray_dbus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WATCH_PATH "/StatusNotifierWatcher"
#define WATCH_IFACE "org.kde.StatusNotifierWatcher"
#define MAX_ITEMS 64

static sd_bus* g_bus;
static TrayChanged g_changed;
static void* g_ud;
static char* g_items[MAX_ITEMS];
static char* g_ids[MAX_ITEMS];    // what the client registered (the string the signals carry)
static int g_n;

static void notify_changed(void) {
    if (g_changed) g_changed(g_ud);
}

static void remove_at(int i) {
    free(g_items[i]);
    free(g_ids[i]);
    for (int k = i; k + 1 < g_n; k++) { g_items[k] = g_items[k + 1]; g_ids[k] = g_ids[k + 1]; }
    g_n--;
}

static int on_register_item(sd_bus_message* m, void* ud, sd_bus_error* err) {
    (void)ud;
    (void)err;
    const char* arg = NULL;
    int r = sd_bus_message_read(m, "s", &arg);
    if (r < 0) return r;
    const char* sender = sd_bus_message_get_sender(m);
    if (!arg || !sender) return sd_bus_reply_method_return(m, "");
    char entry[512];
    if (arg[0] == '/') snprintf(entry, sizeof entry, "%s|%s", sender, arg);
    else snprintf(entry, sizeof entry, "%s|%s", arg, "/StatusNotifierItem");
    if (arg[0] != '/') {
        const char* slash = strchr(arg, '/');
        if (slash) snprintf(entry, sizeof entry, "%.*s|%s", (int)(slash - arg), arg, slash);
    }
    int known = 0;
    for (int i = 0; i < g_n; i++) if (!strcmp(g_items[i], entry)) known = 1;
    if (!known && g_n < MAX_ITEMS) {
        g_items[g_n] = strdup(entry);
        g_ids[g_n] = strdup(arg);
        g_n++;
        sd_bus_emit_signal(g_bus, WATCH_PATH, WATCH_IFACE, "StatusNotifierItemRegistered", "s", arg);
        notify_changed();
    }
    return sd_bus_reply_method_return(m, "");
}

static int on_register_host(sd_bus_message* m, void* ud, sd_bus_error* err) {
    (void)ud;
    (void)err;
    return sd_bus_reply_method_return(m, "");
}

static int get_items(sd_bus* bus, const char* path, const char* iface, const char* prop, sd_bus_message* reply, void* ud, sd_bus_error* err) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)ud; (void)err;
    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) return r;
    for (int i = 0; i < g_n; i++) {
        r = sd_bus_message_append(reply, "s", g_ids[i]);
        if (r < 0) return r;
    }
    return sd_bus_message_close_container(reply);
}

static int get_host_registered(sd_bus* bus, const char* path, const char* iface, const char* prop, sd_bus_message* reply, void* ud, sd_bus_error* err) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)ud; (void)err;
    return sd_bus_message_append(reply, "b", 1);
}

static int get_version(sd_bus* bus, const char* path, const char* iface, const char* prop, sd_bus_message* reply, void* ud, sd_bus_error* err) {
    (void)bus; (void)path; (void)iface; (void)prop; (void)ud; (void)err;
    return sd_bus_message_append(reply, "i", 0);
}

static const sd_bus_vtable kVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", on_register_item, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", on_register_host, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", get_items, 0, 0),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", get_host_registered, 0, 0),
    SD_BUS_PROPERTY("ProtocolVersion", "i", get_version, 0, 0),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_VTABLE_END};

static void scan_names(void) {
    char** names = NULL;
    if (sd_bus_list_names(g_bus, &names, NULL) >= 0 && names) {
        for (char** n = names; *n; n++) {
            if ((!strncmp(*n, "org.kde.StatusNotifierItem-", 27) || !strncmp(*n, "org.freedesktop.StatusNotifierItem-", 35)) && g_n < MAX_ITEMS) {
                char entry[512];
                snprintf(entry, sizeof entry, "%s|/StatusNotifierItem", *n);
                int known = 0;
                for (int i = 0; i < g_n; i++) if (!strcmp(g_items[i], entry)) known = 1;
                if (!known) { g_items[g_n] = strdup(entry); g_ids[g_n] = strdup(*n); g_n++; }
            }
            free(*n);
        }
        free(names);
    }
}

static int g_foreign;
static void foreign_sync(void) {
    char** list = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    if (sd_bus_get_property_strv(g_bus, WATCH_IFACE, WATCH_PATH, WATCH_IFACE, "RegisteredStatusNotifierItems", &err, &list) < 0) {
        sd_bus_error_free(&err);
        return;
    }
    while (g_n > 0) remove_at(g_n - 1);
    for (char** it = list; it && *it; it++) {
        if (g_n < MAX_ITEMS && (*it)[0] != '/') {
            const char* slash = strchr(*it, '/');
            char entry[512];
            if (slash) snprintf(entry, sizeof entry, "%.*s|%s", (int)(slash - *it), *it, slash);
            else snprintf(entry, sizeof entry, "%s|/StatusNotifierItem", *it);
            g_items[g_n] = strdup(entry);
            g_ids[g_n] = strdup(*it);
            g_n++;
        }
        free(*it);
    }
    free(list);
    scan_names();
}
static int on_foreign_signal(sd_bus_message* m, void* ud, sd_bus_error* err) {
    (void)m; (void)ud; (void)err;
    foreign_sync();
    notify_changed();
    return 0;
}

static int on_name_owner(sd_bus_message* m, void* ud, sd_bus_error* err) {
    (void)ud;
    (void)err;
    const char *name = NULL, *oldo = NULL, *newo = NULL;
    if (sd_bus_message_read(m, "sss", &name, &oldo, &newo) < 0) return 0;
    if (!newo || newo[0]) return 0;
    if (g_foreign && !strcmp(name, WATCH_IFACE)) {
        if (sd_bus_request_name(g_bus, WATCH_IFACE, 0) >= 0) {
            g_foreign = 0;
            sd_bus_emit_signal(g_bus, WATCH_PATH, WATCH_IFACE, "StatusNotifierHostRegistered", "");
            notify_changed();
        }
        return 0;
    }
    int changed = 0;
    for (int i = g_n - 1; i >= 0; i--) {
        const char* bar = strchr(g_items[i], '|');
        size_t len = bar ? (size_t)(bar - g_items[i]) : strlen(g_items[i]);
        if (strlen(name) == len && !strncmp(name, g_items[i], len)) {
            sd_bus_emit_signal(g_bus, WATCH_PATH, WATCH_IFACE, "StatusNotifierItemUnregistered", "s", g_ids[i]);
            remove_at(i);
            changed = 1;
        }
    }
    if (changed) notify_changed();
    return 0;
}

// Item signals (NewIcon, NewStatus, NewTitle, ...) make the shell read the items again.
static int on_item_signal(sd_bus_message* m, void* ud, sd_bus_error* err) {
    (void)m; (void)ud; (void)err;
    notify_changed();
    return 0;
}

int traywatch_start(sd_bus* bus, TrayChanged changed, void* userdata) {
    g_bus = bus;
    g_changed = changed;
    g_ud = userdata;
    int r = sd_bus_add_object_vtable(bus, NULL, WATCH_PATH, WATCH_IFACE, kVtable, NULL);
    if (r < 0) return r;
    r = sd_bus_request_name(bus, WATCH_IFACE, 0);
    g_foreign = r < 0;
    char host[128];
    snprintf(host, sizeof host, "org.kde.StatusNotifierHost-%d", (int)getpid());
    sd_bus_request_name(bus, host, 0);
    sd_bus_match_signal(bus, NULL, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", on_name_owner, NULL);
    sd_bus_add_match(bus, NULL, "type='signal',interface='org.kde.StatusNotifierItem'", on_item_signal, NULL);
    if (g_foreign) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_call_method(bus, WATCH_IFACE, WATCH_PATH, WATCH_IFACE, "RegisterStatusNotifierHost", &err, NULL, "s", host);
        sd_bus_error_free(&err);
        sd_bus_match_signal(bus, NULL, WATCH_IFACE, WATCH_PATH, WATCH_IFACE, "StatusNotifierItemRegistered", on_foreign_signal, NULL);
        sd_bus_match_signal(bus, NULL, WATCH_IFACE, WATCH_PATH, WATCH_IFACE, "StatusNotifierItemUnregistered", on_foreign_signal, NULL);
        foreign_sync();
        return 0;
    }
    sd_bus_emit_signal(bus, WATCH_PATH, WATCH_IFACE, "StatusNotifierHostRegistered", "");
    scan_names();
    return 0;
}

int traywatch_count(void) { return g_n; }
const char* traywatch_item(int i) { return i >= 0 && i < g_n ? g_items[i] : NULL; }
