#include "polkit_dbus.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#define AGENT_PATH "/org/end4pC/PolkitAgent"
#define AGENT_IFACE "org.freedesktop.PolicyKit1.AuthenticationAgent"

static PolkitHandlers g_handlers;

static int read_identities(sd_bus_message* m, uint32_t* uids, int max, int* count) {
    int r = sd_bus_message_enter_container(m, 'a', "(sa{sv})");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(m, 'r', "sa{sv}")) > 0) {
        const char* kind = NULL;
        r = sd_bus_message_read(m, "s", &kind);
        if (r < 0) return r;
        r = sd_bus_message_enter_container(m, 'a', "{sv}");
        if (r < 0) return r;
        while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
            const char* key = NULL;
            r = sd_bus_message_read(m, "s", &key);
            if (r < 0) return r;
            if (kind && !strcmp(kind, "unix-user") && !strcmp(key, "uid")) {
                uint32_t uid = 0;
                r = sd_bus_message_read(m, "v", "u", &uid);
                if (r < 0) return r;
                if (*count < max) uids[(*count)++] = uid;
            } else {
                r = sd_bus_message_skip(m, "v");
                if (r < 0) return r;
            }
            sd_bus_message_exit_container(m);
        }
        if (r < 0) return r;
        sd_bus_message_exit_container(m);
        sd_bus_message_exit_container(m);
    }
    if (r < 0) return r;
    return sd_bus_message_exit_container(m);
}

static int on_begin(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    const char *action = NULL, *message = NULL, *icon = NULL, *cookie = NULL;
    int r = sd_bus_message_read(m, "sss", &action, &message, &icon);
    if (r < 0) { fprintf(stderr, "polkit: read sss failed %d\n", r); return r; }
    r = sd_bus_message_skip(m, "a{ss}");
    if (r < 0) { fprintf(stderr, "polkit: skip details failed %d\n", r); return r; }
    r = sd_bus_message_read(m, "s", &cookie);
    if (r < 0) { fprintf(stderr, "polkit: read cookie failed %d\n", r); return r; }
    uint32_t uids[16];
    int n = 0;
    r = read_identities(m, uids, 16, &n);
    if (r < 0) { fprintf(stderr, "polkit: identities failed %d\n", r); return r; }
    PolkitRequest req = {action, message, icon, cookie, n, uids, NULL};
    req.handle = sd_bus_message_ref(m);
    if (g_handlers.begin) g_handlers.begin(g_handlers.userdata, &req);
    return 1;
}

static int on_cancel(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    (void)userdata;
    (void)err;
    const char* cookie = NULL;
    int r = sd_bus_message_read(m, "s", &cookie);
    if (r < 0) return r;
    if (g_handlers.cancel) g_handlers.cancel(g_handlers.userdata, cookie);
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable kVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("BeginAuthentication", "sssa{ss}sa(sa{sv})", "", on_begin, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CancelAuthentication", "s", "", on_cancel, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

void polkitd_finish(void* handle, int ok) {
    sd_bus_message* m = handle;
    if (!m) return;
    if (ok) sd_bus_reply_method_return(m, "");
    else sd_bus_reply_method_errorf(m, "org.freedesktop.PolicyKit1.Error.Cancelled", "The authentication was cancelled");
    sd_bus_message_unref(m);
}

int polkitd_start(sd_bus** out, const PolkitHandlers* handlers) {
    g_handlers = *handlers;
    sd_bus* bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) return r;
    r = sd_bus_add_object_vtable(bus, NULL, AGENT_PATH, AGENT_IFACE, kVtable, NULL);
    if (r < 0) goto fail;

    char* session = NULL;
    const char* env = getenv("XDG_SESSION_ID");
    if (env && *env) session = strdup(env);
    else if (sd_pid_get_session(getpid(), &session) < 0) session = NULL;
    if (!session) { r = -ENOENT; goto fail; }

    sd_bus_message* call = NULL;
    r = sd_bus_message_new_method_call(bus, &call, "org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                                       "org.freedesktop.PolicyKit1.Authority", "RegisterAuthenticationAgent");
    if (r < 0) { free(session); goto fail; }
    r = sd_bus_message_open_container(call, 'r', "sa{sv}");
    if (r >= 0) r = sd_bus_message_append(call, "s", "unix-session");
    if (r >= 0) r = sd_bus_message_open_container(call, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_open_container(call, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(call, "s", "session-id");
    if (r >= 0) r = sd_bus_message_open_container(call, 'v', "s");
    if (r >= 0) r = sd_bus_message_append(call, "s", session);
    if (r >= 0) r = sd_bus_message_close_container(call);
    if (r >= 0) r = sd_bus_message_close_container(call);
    if (r >= 0) r = sd_bus_message_close_container(call);
    if (r >= 0) r = sd_bus_message_close_container(call);
    if (r >= 0) r = sd_bus_message_append(call, "ss", "en_US.UTF-8", AGENT_PATH);
    free(session);
    if (r < 0) { sd_bus_message_unref(call); goto fail; }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    r = sd_bus_call(bus, call, 0, &err, NULL);
    sd_bus_message_unref(call);
    sd_bus_error_free(&err);
    if (r < 0) goto fail;
    *out = bus;
    return 0;
fail:
    sd_bus_unref(bus);
    return r;
}
