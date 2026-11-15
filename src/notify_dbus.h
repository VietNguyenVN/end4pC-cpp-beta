#ifndef NOTIFY_DBUS_H
#define NOTIFY_DBUS_H

#include <stddef.h>
#include <stdint.h>
#include <systemd/sd-bus.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* key;
    const char* label;
} NotifyAction;

typedef struct {
    const char* app;
    uint32_t replaces;
    const char* icon;
    const char* summary;
    const char* body;
    int nactions;
    const NotifyAction* actions;
    int urgency;
    const char* imagePath;
    const char* desktopEntry;
    int hasImage;
    int imgW, imgH, imgRowstride, imgHasAlpha, imgChannels;
    const unsigned char* imgData;
    size_t imgLen;
    int expire;
} NotifyRequest;

typedef struct {
    uint32_t (*notify)(void* userdata, const NotifyRequest* req);
    void (*close)(void* userdata, uint32_t id);
    void* userdata;
} NotifyHandlers;

// Connects to the session bus, exports the interface and takes the well-known name. 0 on success, negative errno
// otherwise (for instance when another notification daemon already owns the name).
int notifyd_start(sd_bus** bus, const NotifyHandlers* handlers);

int notifyd_emit_closed(sd_bus* bus, uint32_t id, uint32_t reason);
int notifyd_emit_action(sd_bus* bus, uint32_t id, const char* key);

#ifdef __cplusplus
}
#endif

#endif
