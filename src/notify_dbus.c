#include "notify_dbus.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFY_PATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"

static NotifyHandlers g_handlers;

static int on_get_capabilities(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    return sd_bus_reply_method_return(m, "as", 4, "body", "body-markup", "actions", "icon-static");
}

static int on_get_server_information(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    return sd_bus_reply_method_return(m, "ssss", "end4pC", "end4pC", "1.0", "1.2");
}

static int on_close_notification(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    uint32_t id = 0;
    int r = sd_bus_message_read(m, "u", &id);
    if (r < 0) return r;
    if (g_handlers.close) g_handlers.close(g_handlers.userdata, id);
    return sd_bus_reply_method_return(m, "");
}

static int read_hints(sd_bus_message* m, NotifyRequest* req) {
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
        const char* key = NULL;
        r = sd_bus_message_read(m, "s", &key);
        if (r < 0) return r;
        char type = 0;
        const char* contents = NULL;
        r = sd_bus_message_peek_type(m, &type, &contents);
        if (r < 0) return r;

        if (!strcmp(key, "urgency") && contents && !strcmp(contents, "y")) {
            uint8_t u = 1;
            sd_bus_message_enter_container(m, 'v', "y");
            sd_bus_message_read(m, "y", &u);
            sd_bus_message_exit_container(m);
            req->urgency = u > 2 ? 2 : u;
        } else if ((!strcmp(key, "image-path") || !strcmp(key, "image_path")) && contents && !strcmp(contents, "s")) {
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &req->imagePath);
            sd_bus_message_exit_container(m);
        } else if (!strcmp(key, "desktop-entry") && contents && !strcmp(contents, "s")) {
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &req->desktopEntry);
            sd_bus_message_exit_container(m);
        } else if ((!strcmp(key, "image-data") || !strcmp(key, "image_data") || !strcmp(key, "icon_data")) && contents &&
                   !strcmp(contents, "(iiibiiay)")) {
            int w = 0, h = 0, rs = 0, alpha = 0, bits = 0, ch = 0;
            const void* data = NULL;
            size_t len = 0;
            sd_bus_message_enter_container(m, 'v', "(iiibiiay)");
            sd_bus_message_enter_container(m, 'r', "iiibiiay");
            sd_bus_message_read(m, "iiibii", &w, &h, &rs, &alpha, &bits, &ch);
            sd_bus_message_read_array(m, 'y', &data, &len);
            sd_bus_message_exit_container(m);
            sd_bus_message_exit_container(m);
            if (bits == 8 && data && w > 0 && h > 0 && rs > 0 && (ch == 3 || ch == 4) && len >= (size_t)rs * (size_t)(h - 1) + (size_t)w * (size_t)ch) {
                req->hasImage = 1;
                req->imgW = w;
                req->imgH = h;
                req->imgRowstride = rs;
                req->imgHasAlpha = alpha;
                req->imgChannels = ch;
                req->imgData = data;
                req->imgLen = len;
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        r = sd_bus_message_exit_container(m);
        if (r < 0) return r;
    }
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
}

static int on_notify(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    NotifyRequest req;
    memset(&req, 0, sizeof req);
    req.urgency = 1;

    int r = sd_bus_message_read(m, "susss", &req.app, &req.replaces, &req.icon, &req.summary, &req.body);
    if (r < 0) return r;

    NotifyAction actions[16];
    int n = 0;
    r = sd_bus_message_enter_container(m, 'a', "s");
    if (r < 0) return r;
    for (;;) {
        const char *key = NULL, *label = NULL;
        r = sd_bus_message_read_basic(m, 's', &key);
        if (r <= 0) break;
        r = sd_bus_message_read_basic(m, 's', &label);
        if (r <= 0) break;
        if (n < 16) {
            actions[n].key = key;
            actions[n].label = label;
            n++;
        }
    }
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    if (r < 0) return r;
    req.nactions = n;
    req.actions = actions;

    r = read_hints(m, &req);
    if (r < 0) return r;
    r = sd_bus_message_read(m, "i", &req.expire);
    if (r < 0) return r;

    uint32_t id = g_handlers.notify ? g_handlers.notify(g_handlers.userdata, &req) : 0;
    return sd_bus_reply_method_return(m, "u", id);
}

static const sd_bus_vtable kVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCapabilities", "", "as", on_get_capabilities, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", on_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", on_close_notification, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", on_get_server_information, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
    SD_BUS_SIGNAL("ActionInvoked", "us", 0),
    SD_BUS_VTABLE_END};

int notifyd_start(sd_bus** out, const NotifyHandlers* handlers) {
    g_handlers = *handlers;
    sd_bus* bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) return r;
    r = sd_bus_add_object_vtable(bus, NULL, NOTIFY_PATH, NOTIFY_IFACE, kVtable, NULL);
    if (r < 0) goto fail;
    r = sd_bus_request_name(bus, NOTIFY_IFACE, 0);
    if (r < 0) goto fail;
    *out = bus;
    return 0;
fail:
    sd_bus_unref(bus);
    return r;
}

int notifyd_emit_closed(sd_bus* bus, uint32_t id, uint32_t reason) {
    return sd_bus_emit_signal(bus, NOTIFY_PATH, NOTIFY_IFACE, "NotificationClosed", "uu", id, reason);
}

int notifyd_emit_action(sd_bus* bus, uint32_t id, const char* key) {
    return sd_bus_emit_signal(bus, NOTIFY_PATH, NOTIFY_IFACE, "ActionInvoked", "us", id, key);
}
