/** @file usb_io_diag.h
 * Owned first-failure snapshots and filename-independent global throttling. Update class/global counters even when output is disabled. Eight-class retention; totals saturate and deadlines use unsigned elapsed time.
 *
 * Header-only value policies, independent of USB hardware. The logger is sole state owner and supplies event/millis observations. Return values decide print eligibility, not whether a file operation succeeded; driver races remain outside this policy.
 */
#ifndef MOFA_USB_IO_DIAG_H
#define MOFA_USB_IO_DIAG_H

#include <stdint.h>
#include <stddef.h>

namespace usb_io_diag {
enum class Operation : uint8_t { Open, Write, Flush, Close, Stat, ExclusiveOpen, Fdopen, Fdclose, Mount, Unmount, Medium, Header };
enum class Context : uint8_t { File, StatError, ExistingNonempty, HeaderWrite, MediumChanged, Mount, Unmount };

struct State {
    bool attached = false, mounted = false, pending = false, fault = false, headers = false;
    uint32_t detached = 0, mountedGeneration = 0;
};

struct Event {
    Operation operation = Operation::Open;
    Context context = Context::File;
    Context branch = Context::File;
    char path[32] = {}, mode[4] = {};
    long result = 0;
    int error = 0, streamError = -1; // -1: no valid FILE status was observed.
    size_t requested = 0, actual = 0, row = 0;
    int64_t fileSize = 0;
    uint32_t fileType = 0;
    bool hasStat = false;
    bool ioOk = false, mediumOk = false;
    State before, after;
};

template<size_t N>
constexpr void copy(char (&out)[N], const char *source) {
    size_t i = 0;
    if (source) for (; i + 1 < N && source[i]; ++i) out[i] = source[i];
    out[i] = '\0';
}

// One operation owns this until cleanup and reporting finish. Later successful
// or failed cleanup calls cannot replace the original failure.
struct FirstFailure {
    Event event;
    bool valid = false;
    constexpr void reset() { valid = false; }
    constexpr void capture(const Event &candidate) {
        if (!valid) { event = candidate; valid = true; }
    }
};

constexpr bool equal(const char *a, const char *b) {
    for (size_t i = 0;; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
}

constexpr bool sameFailure(const Event &a, const Event &b) {
    // Byte counts, row, generations, flags and stat size are observations, not
    // identity: a recurring error must not flood serial as these change.
    const bool byteResult = a.operation == Operation::Write;
    const bool snapshotErrno = a.operation == Operation::Mount || a.operation == Operation::Unmount ||
                               a.operation == Operation::Medium;
    return a.operation == b.operation && a.context == b.context && a.branch == b.branch &&
           (byteResult || a.result == b.result) && (snapshotErrno || a.error == b.error) && a.streamError == b.streamError &&
           equal(a.mode, b.mode);
}

class Repeats {
public:
    /** Logger-owned accounting and output-budget decision. Always capture latest and increment retained class counts first; false suppresses output, not fault detection. Filename is excluded from identity; counters saturate. */
    constexpr bool emit(const Event &event, uint32_t now, uint32_t &count) {
        latest = event;
        size_t slot = 0;
        for (; slot < SLOTS; ++slot) {
            if (entries_[slot].used && sameFailure(entries_[slot].event, event)) break;
        }
        if (slot == SLOTS) {
            slot = next_;
            next_ = (next_ + 1) % SLOTS;
            entries_[slot] = Entry{};
            entries_[slot].used = true;
        }
        Entry &entry = entries_[slot];
        entries_[slot].event = event;
        if (entry.count != UINT32_MAX) ++entry.count;
        count = entry.count;
        // Account before applying the global budget, including new classes.
        if (emitted_ && uint32_t(now - lastEmission_) < 5000) {
            if (suppressed != UINT32_MAX) ++suppressed;
            if (entry.suppressed != UINT32_MAX) ++entry.suppressed;
            classSuppressed = entry.suppressed;
            return false;
        }
        classSuppressed = entry.suppressed;
        emitted_ = true; lastEmission_ = now;
        return true;
    }
    Event latest;
    uint32_t suppressed = 0;
    uint32_t classSuppressed = 0;
private:
    bool emitted_ = false;
    uint32_t lastEmission_ = 0;
    static constexpr size_t SLOTS = 8;
    // Class totals have an eight-class retention window; the global suppressed
    // total survives eviction. All counters saturate instead of wrapping.
    struct Entry { Event event; uint32_t count = 0, suppressed = 0; bool used = false; };
    Entry entries_[SLOTS] = {};
    size_t next_ = 0;
};
} // namespace usb_io_diag
#endif
