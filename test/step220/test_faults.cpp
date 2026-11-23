// Native executable tests of the production application policies.
// Example: g++ -std=c++17 test/step220/test_faults.cpp -o faults && ./faults
#include "../../src/usb_mount_state.h"
#include "../../src/usb_io_diag.h"
#include "../../src/display_dma_contract.h"
#include "../../src/display_rotation_buffer.h"
#include "../../src/gnss_log_fifo.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static unsigned assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (0)

static bool fatRecovery() {
    usb_mount::State state;
    gnss_log::Fifo fifo;
    GnssLogRecord record{};
    for (unsigned i = 0; i < 20; ++i) CHECK(fifo.push(record));
    CHECK(state.mountDue(1000, 0));
    state.begin(); state.mounted(false, true); // Partial FAT registration.
    CHECK(state.registered && state.pending && !state.usable);
    CHECK(!state.mountDue(9000, 0));
    CHECK(state.cleanupDue(1000, true));
    state.lastCleanup = 1000; state.cleaned(-EIO);
    CHECK(state.registered && state.pending && !state.usable);
    CHECK(!state.cleanupDue(1999, true));
    CHECK(state.cleanupDue(2000, true));
    state.cleaned(0);
    CHECK(!state.registered && !state.pending && !state.usable);
    state.cleaned(-EINVAL); // Already unregistered: idempotent cleanup.
    CHECK(state.mountDue(3000, 1000));
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        state.begin(); state.mounted(true, true);
        CHECK(state.usable && state.registered && !state.pending);
        CHECK(!state.cleanupDue(5000, true));
        CHECK(state.cleanupDue(5000, false)); // Detach after mount.
        state.cleaned(0);
        state.begin(); state.mounted(true, false); // Detach during mount.
        CHECK(!state.usable && state.pending && state.registered);
        state.cleaned(0);
    }
    state.begin(); state.mounted(false, false);
    state.lastCleanup = UINT32_MAX - 500;
    CHECK(!state.cleanupDue(498, false));
    CHECK(state.cleanupDue(499, false));
    CHECK(fifo.stats().queued == 20 && fifo.stats().accepted == 20 &&
        !fifo.stats().uncertain && !fifo.stats().overflow && !fifo.stats().written);
    return true;
}

struct Dma {
    display_dma::Result outcome = display_dma::Result::Complete;
    display_dma::Result startOutcome = display_dma::Result::Complete;
    bool active = false, abortOk = true, abortStops = true;
    unsigned starts = 0, polls = 0, aborts = 0;
    bool inactive() { return !active; }
    display_dma::Result start() { ++starts; active = true; return startOutcome; }
    display_dma::Result poll() { ++polls; if (outcome == display_dma::Result::Complete) active = false; return outcome; }
    bool abort() { ++aborts; if (abortStops) active = false; return abortOk; }
};

static bool dmaRecovery() {
    using display_dma::Result;
    display_dma::Ownership owner;
    Dma dma;
    CHECK(owner.transfer(dma) == Result::Complete && !owner.pinned && dma.aborts == 0);
    for (Result fault : {Result::Timeout, Result::Error}) {
        dma.outcome = fault;
        CHECK(owner.transfer(dma) == fault && !owner.pinned && !dma.active);
        dma.outcome = Result::Complete;
        CHECK(owner.transfer(dma) == Result::Complete); // Recovery after safe abort.
    }
    for (unsigned failure = 0; failure < 2; ++failure) {
        display_dma::Ownership stuck;
        Dma failed;
        failed.outcome = Result::Timeout;
        failed.abortOk = failure != 0;
        failed.abortStops = failure == 0;
        CHECK(stuck.transfer(failed) == Result::Pinned && stuck.pinned);
        unsigned starts = failed.starts, aborts = failed.aborts;
        CHECK(stuck.transfer(failed) == Result::Pinned);
        CHECK(failed.starts == starts && failed.aborts == aborts);
    }
    display_dma::Ownership busy;
    Dma foreign; foreign.active = true;
    CHECK(busy.transfer(foreign) == Result::Pinned && foreign.starts == 0);
    display_dma::Ownership startOwner;
    Dma startFailure;
    startFailure.startOutcome = Result::Error;
    CHECK(startOwner.transfer(startFailure) == Result::Error);
    CHECK(!startOwner.pinned && !startFailure.active && startFailure.polls == 0 && startFailure.aborts == 1);
    return true;
}

struct FlushOps {
    uint8_t storage[32]{};
    display_rotation::Buffer &buffer;
    unsigned allocations = 0, copies = 0, readyCount = 0, dmaCount = 0, failedCount = 0;
    bool quarantine = false, allocationFailure = false;
    const uint8_t *dmaSource = nullptr;
    uint32_t stride(uint32_t width) { return width * 2; }
    uint8_t *resize(uint8_t *, size_t bytes) { ++allocations; return allocationFailure || bytes > sizeof(storage) ? nullptr : storage; }
    void rotate(const display_rotation::Request &request, const display_rotation::Plan &plan, uint8_t *dest) {
        ++copies; memcpy(dest, request.source, plan.bytes);
    }
    display_rotation::Error dma(const uint8_t *source, const display_rotation::Plan &) {
        ++dmaCount; dmaSource = source; buffer.pinned = quarantine;
        return quarantine ? display_rotation::Error::Transfer : display_rotation::Error::None;
    }
    void failed(display_rotation::Error, size_t) { ++failedCount; }
    void ready() { ++readyCount; }
};

static bool bufferLifetime() {
    using display_rotation::Error;
    uint8_t source[32] = {42};
    display_rotation::Request request{};
    request.source = source; request.sourceAvailable = sizeof(source);
    request.width = request.height = 2; request.sourceStride = 4;
    request.framebufferWidth = request.framebufferHeight = 8; request.rgb565 = true;
    display_rotation::Buffer buffer;
    FlushOps ops{ {}, buffer };
    CHECK(display_rotation::flush(buffer, request, ops) == Error::None);
    CHECK(ops.readyCount == 1 && ops.dmaSource == buffer.data && ops.dmaSource != source);
    source[0] = 99;
    CHECK(buffer.data[0] == 42); // Zero rotation never lends the LVGL buffer.
    ops.quarantine = true;
    CHECK(display_rotation::flush(buffer, request, ops) == Error::Transfer && buffer.pinned);
    const unsigned allocations = ops.allocations, copies = ops.copies, transfers = ops.dmaCount;
    request.width = 4; request.sourceStride = 8; // Growth while quarantined.
    CHECK(display_rotation::flush(buffer, request, ops) == Error::Transfer);
    CHECK(ops.allocations == allocations && ops.copies == copies && ops.dmaCount == transfers);
    CHECK(ops.readyCount == 3 && ops.failedCount == 2);
    return true;
}

static bool diagnosticAccounting() {
    usb_io_diag::Repeats repeats;
    usb_io_diag::Event event;
    event.operation = usb_io_diag::Operation::Write;
    uint32_t count = 0;
    CHECK(repeats.emit(event, 0, count) && count == 1);
    for (unsigned i = 1; i < 5000; ++i) {
        snprintf(event.path, sizeof(event.path), "/usb/GN%06u.tsv", i);
        CHECK(!repeats.emit(event, i, count));
        CHECK(count == i + 1 && repeats.suppressed == i && repeats.classSuppressed == i);
        CHECK(usb_io_diag::equal(repeats.latest.path, event.path));
    }
    CHECK(repeats.emit(event, 5000, count) && count == 5001 && repeats.suppressed == 4999);
    event.error = 28;
    CHECK(!repeats.emit(event, 5001, count) && count == 1 && repeats.classSuppressed == 1);
    CHECK(repeats.latest.error == 28 && repeats.suppressed == 5000);
    usb_io_diag::Repeats wrap;
    CHECK(wrap.emit(event, UINT32_MAX - 2000, count));
    CHECK(!wrap.emit(event, 2998, count) && count == 2 && wrap.classSuppressed == 1 && wrap.suppressed == 1);
    CHECK(wrap.emit(event, 2999, count) && count == 3 && wrap.classSuppressed == 1 && wrap.suppressed == 1);
    event.error = 5;
    CHECK(!wrap.emit(event, 3000, count) && count == 1 && wrap.classSuppressed == 1 && wrap.suppressed == 2);
    event.error = 28;
    CHECK(!wrap.emit(event, 3001, count) && count == 4 && wrap.classSuppressed == 2 && wrap.suppressed == 3);
    wrap.suppressed = UINT32_MAX;
    CHECK(!wrap.emit(event, 3002, count) && count == 5 && wrap.classSuppressed == 3 && wrap.suppressed == UINT32_MAX);
    return true;
}

int main() {
    if (!fatRecovery() || !dmaRecovery() || !bufferLifetime() || !diagnosticAccounting()) return 1;
    printf("PASS STEP 220 application fault tests: %u assertions\n", assertions);
    return 0;
}
