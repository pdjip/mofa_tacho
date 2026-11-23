#ifndef MOFA_USB_MOUNT_STATE_H
#define MOFA_USB_MOUNT_STATE_H
#include <errno.h>
#include <stdint.h>
namespace usb_mount {
// Logger-thread owned. A mount attempt may retain registration on ANY error.
struct State {
    bool registered = false, usable = false, pending = false;
    uint32_t lastCleanup = 0;
    constexpr bool cleanupDue(uint32_t now, bool unchanged) const {
        return registered && (pending || !unchanged) && uint32_t(now - lastCleanup) >= 1000;
    }
    constexpr bool mountDue(uint32_t now, uint32_t lastMount) const {
        return !registered && !pending && uint32_t(now - lastMount) >= 1000;
    }
    constexpr void begin() { registered = pending = true; usable = false; }
    constexpr void mounted(bool ok, bool unchanged) {
        usable = ok && unchanged;
        pending = !usable;
    }
    constexpr void cleaned(int result) {
        usable = false;
        // Installed FAT unmount returns -EINVAL iff its _id is already -1.
        pending = result != 0 && result != -EINVAL;
        registered = pending;
    }
};
}
#endif
