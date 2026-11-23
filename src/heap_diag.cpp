/** @file heap_diag.cpp
 * Bounded heap evidence collection. The allocator callback records only; logger drains after cleanup. Diagnostic verbosity affects output, never allocation failure capture.
 *
 * Dependencies: installed Mbed heap statistics/tracing, fixed History storage and logger serial output. Setup registers capture; allocator callbacks publish a bounded ring; logger reads it using release/acquire ordering. Snapshots observe system-wide concurrency rather than attributing a leak to one subsystem.
 */
#include "diagnostics_config.h"
#include "heap_diag.h"
#include "gnss_log_fifo.h"
#include <Arduino.h>
#include <platform/mbed_stats.h>
#include <platform/mbed_mem_trace.h>
#include <atomic>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>

namespace heap_diag {
namespace {
struct PreserveErrno {
    int saved = errno;
    ~PreserveErrno() { errno = saved; }
};
constexpr size_t HISTORY = 8, FAILURES = 8;
History<Snapshot, HISTORY> recent, firstError, firstEnomem;
bool errorFrozen = false, enomemFrozen = false, errorPending = false, enomemPending = false;
bool roundGood = true, roundClosed = false, baselineSet = false;
uint32_t nextId = 0, lastPairPrint = 0, printedPairs = 0;
Token roundBaseline;
// Logger/setup-owned output and scratch space; never touched by the tracer.
#if MOFA_DEBUG_MEMORY >= 3
char line[384], deltaText[21], countText[21], baselineText[21], requestedText[21];
#endif

struct AllocationFailure {
    uintptr_t caller = 0;
    size_t count = 0, size = 0;
    uint8_t operation = 0;
};
AllocationFailure failures[FAILURES];
std::atomic<uint32_t> failureHead{0}, failureTail{0}, failureLost{0};
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Heap failure ring needs lock-free counters");

// Mbed's existing mem_trace_mutex serializes producers and its depth guard
// suppresses nested traces. This is therefore one serialized producer and one
// logger consumer. Release/acquire publishes slots without an additional lock.
// No stats getter, clock, serial, allocation or blocking API is used here.
/** Mbed allocation callback, serialized by the tracer lock. Record only nonzero-size NULL results into the fixed ring; never fetch heap stats, print, allocate or block here. Logger is the single consumer. */
void allocationTrace(uint8_t operation, void *result, void *caller, ...)
{
    if (result || operation == MBED_MEM_TRACE_FREE) return;
    const int saved = errno;
    va_list args;
    va_start(args, caller);
    AllocationFailure failure;
    failure.operation = operation;
    failure.caller = reinterpret_cast<uintptr_t>(caller);
    failure.count = 1;
    if (operation == MBED_MEM_TRACE_MALLOC) {
        failure.size = va_arg(args, size_t);
    } else if (operation == MBED_MEM_TRACE_REALLOC) {
        (void)va_arg(args, void *);
        failure.size = va_arg(args, size_t);
    } else if (operation == MBED_MEM_TRACE_CALLOC) {
        failure.count = va_arg(args, size_t);
        failure.size = va_arg(args, size_t);
    } else {
        va_end(args);
        errno = saved;
        return;
    }
    va_end(args);
    // malloc(0), calloc with a zero factor, and realloc(ptr, 0) may legally
    // return NULL; these are not evidence of allocation failure/ENOMEM.
    if (!failure.count || !failure.size) { errno = saved; return; }
    const uint32_t head = failureHead.load(std::memory_order_relaxed);
    if (uint32_t(head - failureTail.load(std::memory_order_acquire)) < FAILURES) {
        failures[head % FAILURES] = failure;
        failureHead.store(head + 1, std::memory_order_release);
    } else {
        const uint32_t lost = failureLost.load(std::memory_order_relaxed);
        if (lost != UINT32_MAX) failureLost.store(lost + 1, std::memory_order_relaxed);
    }
    errno = saved;
}

Stats readStats()
{
    mbed_stats_heap_t stats;
    // The real linked Mbed implementation copies counters under its existing
    // heap-stat mutex. Only call after libc/FatFs returns, never from the tracer.
    mbed_stats_heap_get(&stats);
    return {stats.current_size, stats.max_size, stats.alloc_cnt, stats.alloc_fail_cnt,
            stats.overhead_size, stats.reserved_size};
}

Snapshot capture(Point point, const char *path, Token token = {}, int error = 0)
{
    PreserveErrno preserve;
    Snapshot snapshot;
    snapshot.stats = readStats();
    snapshot.id = token.id ? token.id : ++nextId;
    snapshot.ms = millis();
    snapshot.point = point;
    snapshot.error = error;
    size_t i = 0;
    if (path) for (; i + 1 < sizeof(snapshot.path) && path[i]; ++i) snapshot.path[i] = path[i];
    snapshot.path[i] = '\0';
    if (token.id) {
        snapshot.delta = difference(snapshot.stats.current, token.current);
        snapshot.allocationDelta = difference(snapshot.stats.allocations, token.allocations);
    }
    recent.push(snapshot);
    return snapshot;
}

Token tokenFor(const Snapshot &snapshot) {
    return {snapshot.id, snapshot.stats.current, snapshot.stats.allocations};
}

void printSnapshot(const Snapshot &snapshot, const char *group)
{
#if MOFA_DEBUG_MEMORY >= 3
    static const char *const names[] = {"init-before-usb", "before-open", "after-close",
        "before-mount", "after-mount", "after-unmount", "first-successful-round", "io-error", "ENOMEM",
        "open-failed-no-close", "after-error-cleanup"};
    gnss_log::decimalSigned(deltaText, sizeof(deltaText), snapshot.delta);
    gnss_log::decimalSigned(countText, sizeof(countText), snapshot.allocationDelta);
    gnss_log::decimalSigned(baselineText, sizeof(baselineText),
        baselineSet ? difference(snapshot.stats.current, roundBaseline.current) : 0);
    snprintf(line, sizeof(line),
        "[HEAP DIAG] %s id=%lu point=%s path=%s ms=%lu current=%lu max=%lu alloc=%lu fail=%lu overhead=%lu reserved=%lu delta=%s deltaAlloc=%s baseline=%u baselineDelta=%s errno=%d\n",
        group, (unsigned long)snapshot.id, names[static_cast<unsigned>(snapshot.point)], snapshot.path,
        (unsigned long)snapshot.ms, (unsigned long)snapshot.stats.current, (unsigned long)snapshot.stats.peak,
        (unsigned long)snapshot.stats.allocations, (unsigned long)snapshot.stats.failures,
        (unsigned long)snapshot.stats.overhead, (unsigned long)snapshot.stats.reserved,
        deltaText, countText, unsigned(baselineSet), baselineText, snapshot.error);
    Serial.print(line);

#endif
}

void printHistory(const History<Snapshot, HISTORY> &history, const char *group) {
    for (size_t i = 0; i < history.size(); ++i) printSnapshot(history.at(i), group);
}
} // namespace

void initialize()
{
    PreserveErrno preserve;
    printSnapshot(capture(Point::Initialized, "-"), "milestone");
    // Some peripheral internals already own threads. The existing tracer lock
    // serializes callback registration with all wrapper activity, even before
    // application threads are started. No change to their startup order.
    mbed_mem_trace_lock();
    mbed_mem_trace_set_callback(allocationTrace);
    mbed_mem_trace_unlock();
}

Token beforeOpen(const char *path) {
    PreserveErrno preserve;
    return tokenFor(capture(Point::BeforeOpen, path));
}

void afterClose(Token token, const char *path)
{
    PreserveErrno preserve;
    const Snapshot closed = capture(Point::AfterClose, path, token);
    roundClosed = true;
    const uint32_t now = millis();
    if (printedPairs < 2 || uint32_t(now - lastPairPrint) >= 30000) {
        // Print both the matching before snapshot and the closed delta, rather
        // than attributing a system-wide concurrent delta to just this file.
        for (size_t i = 0; i < recent.size(); ++i)
            if (recent.at(i).id == token.id && recent.at(i).point == Point::BeforeOpen)
                printSnapshot(recent.at(i), "pair");
        printSnapshot(closed, "pair");
        if (printedPairs < 2) ++printedPairs;
        lastPairPrint = now;
    }
}

void openFailed(Token token, const char *path, int error) {
    PreserveErrno preserve;
    capture(Point::OpenFailed, path, token, error);
}

Token beforeMount() {
    PreserveErrno preserve;
    return tokenFor(capture(Point::BeforeMount, "/usb"));
}

void afterMount(Token token, bool success)
{
    PreserveErrno preserve;
    const Snapshot mounted = capture(Point::AfterMount, "/usb", token);
    static uint32_t printed = 0, last = 0;
    const uint32_t now = millis();
    if (success && (printed < 2 || uint32_t(now - last) >= 5000)) {
        if (printed < 2) ++printed;
        last = now;
        for (size_t i = 0; i < recent.size(); ++i)
            if (recent.at(i).id == token.id && recent.at(i).point == Point::BeforeMount)
                printSnapshot(recent.at(i), "milestone");
        printSnapshot(mounted, "milestone");
    }
}

void afterUnmount(bool success) {
    PreserveErrno preserve;
    const Snapshot snapshot = capture(Point::AfterUnmount, "/usb");
    static uint32_t printed = 0, last = 0;
    const uint32_t now = millis();
    if (success && (printed < 2 || uint32_t(now - last) >= 5000)) {
        if (printed < 2) ++printed;
        last = now;
        printSnapshot(snapshot, "milestone");
    }
}

/** Logger-owned first-error/first-ENOMEM freezing. Copy bounded owned histories once without printing; later cleanup reporting cannot mutate the frozen evidence. */
void ioError(const char *path, int error, Token token)
{
    PreserveErrno preserve;
    if (!errorFrozen) {
        capture(Point::IoError, path, token, error);
        firstError = recent;
        errorFrozen = errorPending = true;
    }
    if (error == ENOMEM && !enomemFrozen) {
        capture(Point::Enomem, path, token, error);
        firstEnomem = recent;
        enomemFrozen = enomemPending = true;
    }
}

void operationFinished(bool success) { roundGood = roundGood && success; }
void startRound() { roundGood = true; roundClosed = false; }
/** Logger end-of-round observation. Establish baseline only after a successful ready round with a closed stream, then service pending evidence; values include concurrent system allocations. */
void finishRound(bool ready)
{
    PreserveErrno preserve;
    if (!baselineSet && ready && roundGood && roundClosed) {
        const Snapshot snapshot = capture(Point::FirstRound, "-");
        roundBaseline = tokenFor(snapshot);
        baselineSet = true;
        printSnapshot(snapshot, "milestone");
    }
    reportPending();
}

void reportPending()
{
    PreserveErrno preserve;
    if (errorPending) {
        errorPending = false;
        const Snapshot &failed = firstError.at(firstError.size() - 1);
        // Separate current snapshot after cleanup. Neither this capture nor
        // printing mutates the frozen pre-cleanup history.
        const Snapshot cleanup = capture(Point::AfterErrorCleanup, failed.path, tokenFor(failed), failed.error);
        printHistory(firstError, "first-error-frozen");
        printSnapshot(cleanup, "first-error-post-cleanup");
    }
    if (enomemPending) {
        enomemPending = false;
        const Snapshot &failed = firstEnomem.at(firstEnomem.size() - 1);
        const Snapshot cleanup = capture(Point::AfterErrorCleanup, failed.path, tokenFor(failed), failed.error);
        printHistory(firstEnomem, "first-ENOMEM-frozen");
        printSnapshot(cleanup, "first-ENOMEM-post-cleanup");
    }
    static bool printed = false;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (printed && uint32_t(now - last) < 5000) return;
    const uint32_t head = failureHead.load(std::memory_order_acquire);
    uint32_t tail = failureTail.load(std::memory_order_relaxed);
    if (tail == head) return;
    printed = true; last = now;
#if MOFA_DEBUG_MEMORY >= 1 && MOFA_DEBUG_MEMORY < 3
    Serial.println("[MEMORY:E_ALLOC] allocation failure observed");
#endif
    // Bounded to one ring's worth, even if another thread keeps failing.
    for (unsigned n = 0; tail != head && n < FAILURES; ++n, ++tail) {
        const AllocationFailure failure = failures[tail % FAILURES];
        failureTail.store(tail + 1, std::memory_order_release);
#if MOFA_DEBUG_MEMORY >= 3
        const uint64_t requested = uint64_t(failure.count) * failure.size;
        gnss_log::decimalUnsigned(requestedText, sizeof(requestedText), requested);
        snprintf(line, sizeof(line),
            "[HEAP DIAG] allocation-null op=%u requested=%s count=%lu size=%lu caller=0x%lx callerReliable=0 lost=%lu\n",
            unsigned(failure.operation), requestedText, (unsigned long)failure.count,
            (unsigned long)failure.size, (unsigned long)failure.caller,
            (unsigned long)failureLost.load(std::memory_order_relaxed));
        Serial.print(line);
#endif
    }
}
} // namespace heap_diag
