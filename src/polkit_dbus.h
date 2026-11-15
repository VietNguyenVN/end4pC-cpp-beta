#ifndef POLKIT_DBUS_H
#define POLKIT_DBUS_H

#include <stdint.h>
#include <systemd/sd-bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* action_id;
    const char* message;
    const char* icon_name;
    const char* cookie;
    int n_uids;
    const uint32_t* uids;
    void* handle;
} PolkitRequest;

typedef struct {
    void (*begin)(void* userdata, const PolkitRequest* req);
    void (*cancel)(void* userdata, const char* cookie);
    void* userdata;
} PolkitHandlers;

// Connects to the system bus, exports the agent and registers it for this login session. 0 on success, negative errno
// otherwise (for instance when another agent is already registered).
int polkitd_start(sd_bus** bus, const PolkitHandlers* handlers);

void polkitd_finish(void* handle, int ok);

#ifdef __cplusplus
}
#endif

#endif
