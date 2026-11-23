/** @file heap_diag.h
 * Fixed-capacity heap observation types. APIs belong to setup/logger. Deltas observe the whole system and cannot attribute allocations to one file.
 *
 * Inputs are operation boundaries, paths and error observations; output is bounded owned evidence. History references remain valid only until overwritten; no internal mutex is provided. Header policies are host-testable without the hardware adapter.
 */
#ifndef MOFA_HEAP_DIAG_H
#define MOFA_HEAP_DIAG_H

#include <stddef.h>
#include <stdint.h>

namespace heap_diag {
enum class Point : uint8_t { Initialized, BeforeOpen, AfterClose, BeforeMount,
    AfterMount, AfterUnmount, FirstRound, IoError, Enomem, OpenFailed, AfterErrorCleanup };
struct Stats {
    uint32_t current = 0, peak = 0, allocations = 0, failures = 0, overhead = 0, reserved = 0;
};
struct Token { uint32_t id = 0, current = 0, allocations = 0; };
struct Snapshot {
    Stats stats;
    uint32_t id = 0, ms = 0;
    Point point = Point::Initialized;
    char path[32] = {};
    int error = 0;
    int64_t delta = 0, allocationDelta = 0;
};
constexpr int64_t difference(uint32_t after, uint32_t before) {
    return int64_t(after) - int64_t(before);
}
template<class T, size_t N>
class History {
public:
    constexpr void push(const T &value) {
        items_[next_] = value;
        next_ = (next_ + 1) % N;
        if (count_ < N) ++count_;
    }
    constexpr size_t size() const { return count_; }
    /** Borrow chronological entry index, oldest first; require index < size and N > 0. A later push may overwrite it, so freeze histories by value when evidence must survive. */
    constexpr const T &at(size_t index) const {
        return items_[(next_ + N - count_ + index) % N];
    }
private:
    T items_[N] = {};
    size_t next_ = 0, count_ = 0;
};

/** Setup-only after peripheral/UI initialization, before logger dispatch. Capture initial heap evidence and register the existing Mbed allocator callback under its tracer lock. */
void initialize();
/** Logger: capture pre-open heap evidence and return a correlation token; path is copied into bounded storage, not retained by pointer. */
Token beforeOpen(const char *path);
/** Logger: observe heap after stream cleanup using the matching token. This does not prove which task caused a system-wide allocation delta. */
void afterClose(Token token, const char *path);
/** Logger: record an open failure that has no matching FILE close. Keep the original errno snapshot supplied by the caller. */
void openFailed(Token token, const char *path, int error);
/** Logger: capture state before FAT registration; return a correlation token, not ownership of filesystem resources. */
Token beforeMount();
/** Logger: record post-mount evidence and optional success report; application mount state is maintained separately. */
void afterMount(Token token, bool success);
/** Logger: observe completed unmount result; no driver cancellation guarantee follows from this diagnostic. */
void afterUnmount(bool success);
/** Logger: freeze first failure and first ENOMEM histories once. Does not print here or replace later recovery decisions. */
void ioError(const char *path, int error, Token token = {});
/** Logger: accumulate whether the current round succeeded; caller must not infer file durability from this observation. */
void operationFinished(bool success);
/** Logger: reset only round success/closed gates; frozen histories and counters survive. */
void startRound();
/** Logger: establish first successful closed-round baseline once, then service pending evidence. ready is the caller filesystem/logging gate. */
void finishRound(bool ready);
/** Logger only after cleanup/outside library locks. Drain bounded failure evidence; print according to MEMORY level while maintaining capture/accounting at every level. */
void reportPending(); // Logger only, after cleanup and outside all library locks.
} // namespace heap_diag
#endif
