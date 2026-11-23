/** @file usb_mount_state.h
 * Logger-owned FAT registration state. Any mount attempt may register before failing. Cleanup retries at one-second intervals; remount is forbidden until registration is released.
 *
 * Header-only FAT recovery state, driven synchronously by the sole logger. Attachment/generation observations arrive from main.cpp atomics. State transitions do not synchronize USBHostMSD internals or confirm controller quiescence.
 */
#ifndef MOFA_USB_MOUNT_STATE_H
#define MOFA_USB_MOUNT_STATE_H
#include <errno.h>
#include <stdint.h>
namespace usb_mount {
// Logger-thread owned. A mount attempt may retain registration on ANY error.
struct State {
    bool registered = false, usable = false, pending = false;
    uint32_t lastCleanup = 0;
    /** Logger scheduling predicate: require registration plus pending cleanup or changed medium, and at least one second since cleanup. Caller records lastCleanup before the blocking unmount attempt. */
    constexpr bool cleanupDue(uint32_t now, bool unchanged) const {
        return registered && (pending || !unchanged) && uint32_t(now - lastCleanup) >= 1000;
    }
    /** Logger scheduling predicate: registration and pending cleanup must both be clear; lastMount is caller-owned and updated before a new attempt. */
    constexpr bool mountDue(uint32_t now, uint32_t lastMount) const {
        return !registered && !pending && uint32_t(now - lastMount) >= 1000;
    }
    /** Conservatively claim registration before FAT mount, even though mount may fail partway; writes remain disabled. */
    constexpr void begin() { registered = pending = true; usable = false; }
    /** Record mount outcome against post-call medium validity. Only unchanged success is usable; every other outcome requires cleanup. */
    constexpr void mounted(bool ok, bool unchanged) {
        usable = ok && unchanged;
        pending = !usable;
    }
    /** Process the installed FAT unmount result. Success/already-unregistered EINVAL release registration; other failures retain pending state. No hardware channel-halt guarantee is implied. */
    constexpr void cleaned(int result) {
        usable = false;
        // Installed FAT unmount returns -EINVAL iff its _id is already -1.
        pending = result != 0 && result != -EINVAL;
        registered = pending;
    }
};
}
#endif
