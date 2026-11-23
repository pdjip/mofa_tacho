// Compile-time fault injection executes the same write policy as FileOps.
#include "../../src/gnss_log_fifo.h"
#include "../../src/usb_io_diag.h"
#include "../../src/heap_diag.h"

template<size_t N>
constexpr bool heapHistoryOrdering()
{
    heap_diag::History<unsigned, N> history{};
    if (history.size()) return false;
    for (unsigned value = 0; value < 35; ++value) {
        history.push(value);
        const size_t count = value + 1 < N ? value + 1 : N;
        if (history.size() != count) return false;
        for (size_t index = 0; index < count; ++index)
            if (history.at(index) != value + 1 - count + index) return false;
    }
    return true;
}

constexpr bool frozenHeapHistoryOwnsSnapshots()
{
    heap_diag::History<heap_diag::Snapshot, 8> recent{}, frozen{};
    heap_diag::Snapshot snapshot{};
    snapshot.stats = {10, 20, 3, 4, 5, 100};
    snapshot.point = heap_diag::Point::IoError;
    snapshot.id = 7; snapshot.ms = UINT32_MAX; snapshot.error = 12;
    snapshot.delta = -17; snapshot.allocationDelta = -2;
    usb_io_diag::copy(snapshot.path, "/usb/DRZ.txt");
    recent.push(snapshot);
    frozen = recent;
    snapshot.path[0] = '\0';
    snapshot.stats.current = 999;
    for (unsigned i = 0; i < 20; ++i) { snapshot.id = i; recent.push(snapshot); }
    const auto &saved = frozen.at(0);
    return frozen.size() == 1 && recent.size() == 8 &&
        usb_io_diag::equal(saved.path, "/usb/DRZ.txt") && saved.id == 7 &&
        saved.point == heap_diag::Point::IoError && saved.ms == UINT32_MAX && saved.error == 12 &&
        saved.stats.current == 10 && saved.stats.peak == 20 && saved.stats.allocations == 3 &&
        saved.stats.failures == 4 && saved.stats.overhead == 5 && saved.stats.reserved == 100 &&
        saved.delta == -17 && saved.allocationDelta == -2;
}

constexpr bool twoFrozenHeapHistoriesStaySeparate()
{
    heap_diag::History<unsigned, 8> recent{}, firstError{}, firstEnomem{};
    for (unsigned i = 0; i < 8; ++i) recent.push(i);
    firstError = recent;
    recent.push(8);
    firstEnomem = recent;
    for (unsigned i = 9; i < 40; ++i) recent.push(i);
    for (size_t i = 0; i < 8; ++i)
        if (firstError.at(i) != i || firstEnomem.at(i) != i + 1 || recent.at(i) != i + 32)
            return false;
    return true;
}

static_assert(heapHistoryOrdering<8>(), "Heap history retains chronological order through full and repeated wrap");
static_assert(heapHistoryOrdering<1>(), "Single-slot history retains only the latest snapshot");
static_assert(frozenHeapHistoryOwnsSnapshots(), "Frozen heap snapshots retain owned path, counters and signed deltas");
static_assert(twoFrozenHeapHistoriesStaySeparate(), "First error, first ENOMEM and live history remain independent copies");
static_assert(heap_diag::difference(0, 0) == 0, "Equal heap observations have zero delta");
static_assert(heap_diag::difference(UINT32_MAX, 0) == INT64_C(4294967295), "Positive heap delta spans full uint32 range");
static_assert(heap_diag::difference(0, UINT32_MAX) == -INT64_C(4294967295), "Negative heap delta has no unsigned wrap");
static_assert(heap_diag::difference(UINT32_C(2147483648), 0) == INT64_C(2147483648), "Positive delta exceeds signed 32 bits");
static_assert(heap_diag::difference(0, UINT32_C(2147483648)) == -INT64_C(2147483648), "Negative delta reaches signed 32-bit boundary");
static_assert(heap_diag::difference(UINT32_MAX, UINT32_MAX - 1) == 1, "Adjacent high counters differ by one");

constexpr usb_io_diag::Event ioEvent(usb_io_diag::Operation operation = usb_io_diag::Operation::Write)
{
    usb_io_diag::Event event{};
    event.operation = operation;
    event.context = event.branch = usb_io_diag::Context::HeaderWrite;
    usb_io_diag::copy(event.path, "/usb/DRZ.txt");
    usb_io_diag::copy(event.mode, "a");
    event.result = 7;
    event.error = 5;
    event.streamError = 1;
    event.requested = 12;
    event.actual = 7;
    event.row = 3;
    event.before = {true, true, false, false, false, 4, 4};
    event.after = event.before;
    return event;
}

constexpr bool firstFailureSurvivesCleanup()
{
    usb_io_diag::FirstFailure first{};
    auto write = ioEvent();
    first.capture(write);
    auto flush = ioEvent(usb_io_diag::Operation::Flush);
    flush.error = 28;
    first.capture(flush);
    auto close = ioEvent(usb_io_diag::Operation::Close);
    close.error = 9;
    first.capture(close);
    if (!first.valid || first.event.operation != usb_io_diag::Operation::Write ||
        first.event.error != 5 || first.event.row != 3 || first.event.requested != 12 ||
        first.event.actual != 7 || first.event.before.detached != 4) return false;
    first.event.after.fault = true; // Post-cleanup snapshot must not replace pre.
    if (first.event.before.fault || !first.event.after.fault) return false;
    first.reset();
    if (first.valid) return false;
    first.capture(close);
    return first.valid && first.event.operation == usb_io_diag::Operation::Close &&
        first.event.error == 9;
}

constexpr bool firstFailureOwnsPath()
{
    char path[] = "/usb/GN000001.tsv";
    auto event = ioEvent();
    usb_io_diag::copy(event.path, path);
    usb_io_diag::FirstFailure first{};
    first.capture(event);
    path[0] = '\0';
    event.path[0] = '\0';
    return usb_io_diag::equal(first.event.path, "/usb/GN000001.tsv") &&
        usb_io_diag::equal(first.event.mode, "a");
}

constexpr bool boundedIoCopies()
{
    struct Guarded { char pre = '<'; char text[5]{}; char post = '>'; } guard{};
    usb_io_diag::copy(guard.text, "123456789");
    if (!usb_io_diag::equal(guard.text, "1234") || guard.pre != '<' || guard.post != '>')
        return false;
    usb_io_diag::copy(guard.text, nullptr);
    if (guard.text[0] != '\0') return false;
    char one[1] = {'!'};
    usb_io_diag::copy(one, "long");
    return one[0] == '\0';
}

constexpr bool repeatDeadlineAndCount()
{
    usb_io_diag::Repeats repeats{};
    auto event = ioEvent();
    uint32_t count = 0;
    if (!repeats.emit(event, 100, count) || count != 1) return false;
    if (repeats.emit(event, 101, count) || count != 2) return false;
    if (repeats.emit(event, 5099, count) || count != 3) return false;
    if (!repeats.emit(event, 5100, count) || count != 4) return false;
    return !repeats.emit(event, 5101, count) && count == 5;
}

constexpr bool observationsDoNotChangeIdentity()
{
    const auto original = ioEvent();
    auto changed = original;
    changed.result = 2; changed.actual = 2; changed.requested = 100; changed.row = 19;
    changed.before.detached = 200; changed.after.fault = true;
    changed.hasStat = true; changed.fileSize = INT64_MAX;
    changed.ioOk = true; changed.mediumOk = true;
    return usb_io_diag::sameFailure(original, changed);
}

constexpr bool identityChangesRespectBudget(unsigned field)
{
    usb_io_diag::Repeats repeats{};
    const auto original = ioEvent();
    auto changed = original;
    if (field == 0) changed.operation = usb_io_diag::Operation::Flush;
    if (field == 1) changed.context = usb_io_diag::Context::MediumChanged;
    if (field == 2) changed.branch = usb_io_diag::Context::StatError;
    if (field == 3) changed.error = 28;
    if (field == 4) changed.streamError = 0;
    if (field == 5) usb_io_diag::copy(changed.path, "/usb/SENSOR.txt");
    if (field == 6) usb_io_diag::copy(changed.mode, "w");
    uint32_t count = 0;
    if (!repeats.emit(original, 100, count) || repeats.emit(changed, 101, count)) return false;
    if (count != (field == 5 ? 2u : 1u) || repeats.suppressed != 1 || repeats.classSuppressed != 1 ||
        repeats.latest.error != changed.error || !usb_io_diag::equal(repeats.latest.path, changed.path)) return false;
    return !repeats.emit(original, 102, count) && count == (field == 5 ? 3u : 2u) &&
        repeats.suppressed == 2 && repeats.classSuppressed == (field == 5 ? 2u : 1u);
}

constexpr bool rawReturnAndErrnoIdentity()
{
    auto first = ioEvent(usb_io_diag::Operation::Mount);
    auto second = first;
    second.error = 999; // Mbed return code, not incidental errno, is identity.
    if (!usb_io_diag::sameFailure(first, second)) return false;
    second.result = -5;
    if (usb_io_diag::sameFailure(first, second)) return false;
    first.operation = second.operation = usb_io_diag::Operation::Close;
    second.result = first.result;
    return !usb_io_diag::sameFailure(first, second);
}

constexpr bool eightAlternatingFailuresAndEviction()
{
    usb_io_diag::Repeats repeats{};
    uint32_t count = 0;
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned cause = 0; cause < 8; ++cause) {
            auto event = ioEvent();
            event.error = cause;
            if (repeats.emit(event, round * 100 + cause, count) != (round == 0 && cause == 0) ||
                count != round + 1) return false;
        }
    }
    auto ninth = ioEvent();
    ninth.error = 99;
    if (repeats.emit(ninth, 400, count) || count != 1) return false;
    auto evicted = ioEvent();
    evicted.error = 0;
    return !repeats.emit(evicted, 401, count) && count == 1 && repeats.suppressed == 25;
}

constexpr bool repeatMillisWrap()
{
    usb_io_diag::Repeats repeats{};
    const auto event = ioEvent();
    uint32_t count = 0;
    const uint32_t start = UINT32_MAX - 2000;
    return repeats.emit(event, start, count) && count == 1 &&
        !repeats.emit(event, uint32_t(start + 4999), count) && count == 2 &&
        repeats.classSuppressed == 1 && repeats.suppressed == 1 &&
        repeats.emit(event, uint32_t(start + 5000), count) && count == 3 &&
        repeats.classSuppressed == 1 && repeats.suppressed == 1;
}

static_assert(firstFailureSurvivesCleanup(), "Cleanup failures never replace first failure; reset permits next operation");
static_assert(firstFailureOwnsPath(), "Captured segment path survives source and candidate mutation");
static_assert(boundedIoCopies(), "Owned strings are bounded and NUL terminated with intact canaries");
static_assert(repeatDeadlineAndCount(), "First cause is immediate; identical repeats retain cumulative count and 5s deadline");
static_assert(observationsDoNotChangeIdentity(), "Changing counts, flags and observations must not defeat throttling");
static_assert(identityChangesRespectBudget(0), "New operation obeys global budget and retains context");
static_assert(identityChangesRespectBudget(1), "New context obeys global budget");
static_assert(identityChangesRespectBudget(2), "New branch obeys global budget");
static_assert(identityChangesRespectBudget(3), "New errno obeys global budget");
static_assert(identityChangesRespectBudget(4), "New stream error obeys global budget");
static_assert(identityChangesRespectBudget(5), "Changing path retains class count and latest owned path");
static_assert(identityChangesRespectBudget(6), "New mode obeys global budget");
static_assert(rawReturnAndErrnoIdentity(), "Raw Mbed return code controls identity; FILE errno remains significant");
static_assert(eightAlternatingFailuresAndEviction(), "Eight alternating causes retain counts; ninth evicts oldest slot");
static_assert(repeatMillisWrap(), "Repeat deadline remains valid across millis wrap");

constexpr bool sameDecimal(const char *actual, const char *expected)
{
    while (*expected) if (*actual++ != *expected++) return false;
    return *actual == '\0';
}

constexpr bool unsignedDecimal(uint64_t value, const char *expected)
{
    char guarded[23]{};
    guarded[0] = '<';
    guarded[22] = '>';
    return gnss_log::decimalUnsigned(guarded + 1, 21, value) &&
        sameDecimal(guarded + 1, expected) && guarded[0] == '<' && guarded[22] == '>';
}

constexpr bool signedDecimal(int64_t value, const char *expected)
{
    char guarded[23]{};
    guarded[0] = '<';
    guarded[22] = '>';
    return gnss_log::decimalSigned(guarded + 1, 21, value) &&
        sameDecimal(guarded + 1, expected) && guarded[0] == '<' && guarded[22] == '>';
}

constexpr bool decimalBufferBoundaries(bool signedValue)
{
    // Every advertised capacity gets adjacent canaries, including capacity 0.
    for (size_t capacity = 0; capacity <= 21; ++capacity) {
        char guarded[23]{};
        for (char &byte : guarded) byte = '!';
        const bool ok = signedValue
            ? gnss_log::decimalSigned(guarded + 1, capacity, INT64_MIN)
            : gnss_log::decimalUnsigned(guarded + 1, capacity, UINT64_MAX);
        if (ok != (capacity == 21) || guarded[0] != '!' || guarded[capacity + 1] != '!')
            return false;
        if (capacity && !ok && guarded[1] != '\0') return false;
    }
    char unchanged = '!';
    return !gnss_log::decimalUnsigned(nullptr, 21, 0) &&
        !gnss_log::decimalSigned(nullptr, 21, -1) &&
        !gnss_log::decimalUnsigned(&unchanged, 0, 0) &&
        !gnss_log::decimalSigned(&unchanged, 0, -1) && unchanged == '!';
}

constexpr bool smallDecimalBuffers()
{
    char zero[2]{}, negative[3]{}, tooSmall[2] = {'!', '!'};
    return gnss_log::decimalUnsigned(zero, sizeof(zero), 0) && sameDecimal(zero, "0") &&
        gnss_log::decimalSigned(negative, sizeof(negative), -1) && sameDecimal(negative, "-1") &&
        !gnss_log::decimalSigned(tooSmall, sizeof(tooSmall), -1) && tooSmall[0] == '\0';
}

static_assert(unsignedDecimal(0, "0"), "Unsigned zero is a decimal digit");
static_assert(unsignedDecimal(UINT32_MAX, "4294967295"), "32-bit boundary remains exact");
static_assert(unsignedDecimal(uint64_t(UINT32_MAX) + 1, "4294967296"), "No truncation above 32 bits");
static_assert(unsignedDecimal(UINT64_MAX, "18446744073709551615"), "Full unsigned 64-bit range");
static_assert(unsignedDecimal(uint64_t(INT64_MAX) + 1, "9223372036854775808"), "Unsigned high bit survives");
static_assert(signedDecimal(0, "0"), "Signed zero has no sign");
static_assert(signedDecimal(INT64_MAX, "9223372036854775807"), "Full signed positive range");
static_assert(signedDecimal(INT64_MIN, "-9223372036854775808"), "Signed minimum has no negation overflow");
static_assert(signedDecimal(-1, "-1"), "Negative UTC millisecond before epoch");
static_assert(signedDecimal(-1001, "-1001"), "Negative UTC across second boundary");
static_assert(signedDecimal(-4294967297LL, "-4294967297"), "Negative UTC exceeds 32-bit range");
static_assert(signedDecimal(4294967296LL, "4294967296"), "Positive UTC exceeds 32-bit range");
static_assert(decimalBufferBoundaries(false), "Unsigned capacities and canaries prevent overwrite");
static_assert(decimalBufferBoundaries(true), "Signed capacities and canaries prevent overwrite");
static_assert(smallDecimalBuffers(), "Small exact buffers succeed and short buffers fail empty");

struct FaultIo {
    using Handle = unsigned;
    bool openOk = true, flushOk = true, closeOk = true;
    unsigned failWrite = 0;
    unsigned opens = 0, writes = 0, flushes = 0, closes = 0, bytes = 0;
    constexpr Handle open(const char *, const char *) { ++opens; return openOk ? 1 : 0; }
    constexpr bool write(Handle, const char *, size_t size) {
        ++writes;
        if (writes == failWrite) return false; // Includes short write/ferror contract.
        bytes += size;
        return true;
    }
    constexpr bool flush(Handle) { ++flushes; return flushOk; }
    constexpr bool close(Handle) { ++closes; return closeOk; }
};

constexpr bool successfulWrite()
{
    FaultIo io{};
    const bool ok = gnss_log::writeFile(io, "segment", "a", 20, [](size_t) { return "PVT\n"; });
    return ok && io.opens == 1 && io.writes == 20 && io.bytes == 80 &&
        io.flushes == 1 && io.closes == 1;
}

constexpr bool openFailure()
{
    FaultIo io{};
    io.openOk = false;
    return !gnss_log::writeFile(io, "segment", "a", 20, [](size_t) { return "PVT\n"; }) &&
        io.opens == 1 && !io.writes && !io.flushes && !io.closes;
}

constexpr bool writeFailure(unsigned at)
{
    FaultIo io{};
    io.failWrite = at;
    return !gnss_log::writeFile(io, "segment", "a", 20, [](size_t) { return "PVT\n"; }) &&
        io.writes == at && io.bytes == (at - 1) * 4 && io.flushes == 1 && io.closes == 1;
}

constexpr bool flushAndCloseFailures(bool flush, bool close)
{
    FaultIo io{};
    io.flushOk = flush;
    io.closeOk = close;
    return !gnss_log::writeFile(io, "segment", "a", 2, [](size_t) { return "PVT\n"; }) &&
        io.writes == 2 && io.flushes == 1 && io.closes == 1;
}

constexpr bool invalidFormattedLine()
{
    FaultIo io{};
    return !gnss_log::writeFile(io, "segment", "a", 20,
        [](size_t i) -> const char * { return i == 2 ? nullptr : "PVT\n"; }) &&
        io.writes == 2 && io.flushes == 1 && io.closes == 1;
}

constexpr bool alreadyOpenedHeader()
{
    FaultIo io{};
    return gnss_log::writeOpened(io, 1, 1, [](size_t) { return "# GNSS_LOG_V1\n"; }) &&
        !io.opens && io.writes == 1 && io.bytes == 14 && io.flushes == 1 && io.closes == 1;
}

constexpr bool uncertainWholeAttemptOnly(unsigned failure)
{
    gnss_log::Fifo f{};
    for (unsigned i = 0; i < 40; ++i) {
        GnssLogRecord r{};
        r.sampleId = i;
        f.push(r);
    }
    // Missing medium/mount failure: the consumer has not reserved/attempted.
    if (f.stats().queued != 40 || f.stats().uncertain || f.stats().written) return false;
    GnssLogRecord out[20]{};
    const size_t n = f.reserve(out, 20, 1000);
    if (n != 20) return false;
    FaultIo io{};
    if (failure == 0) io.openOk = false;
    if (failure == 1) io.failWrite = 7;
    if (failure == 2) io.flushOk = false;
    if (failure == 3) io.closeOk = false;
    bool ok = gnss_log::writeFile(io, "segment", "a", n, [](size_t) { return "PVT\n"; });
    if (failure == 4) ok = false; // Completed I/O invalidated by detach generation.
    if (ok || !f.complete(out[0].sequence, n, ok)) return false;
    const auto s = f.stats();
    if (s.accepted != 40 || s.queued != 20 || s.uncertain != 20 || s.written || s.overflow)
        return false;
    // Recovery uses a fresh segment. Only never-attempted entries are eligible.
    if (f.reserve(out, 20, 1100) != 20 || out[0].sequence != 21) return false;
    FaultIo recovered{};
    ok = gnss_log::writeFile(recovered, "fresh-segment", "a", 20, [](size_t) { return "PVT\n"; });
    return ok && f.complete(out[0].sequence, 20, ok) && f.stats().written == 20 &&
        f.stats().uncertain == 20 && !f.stats().queued &&
        f.stats().accepted == f.stats().written + f.stats().uncertain;
}

constexpr bool exhaustedNamesKeepLegacyWriting()
{
    using gnss_log::SegmentResult;
    uint32_t next = 0, attempts = 0;
    gnss_log::Fifo fifo{};
    FaultIo rpm{}, sensor{};
    for (unsigned round = 0; round < 400; ++round) {
        const uint32_t before = attempts;
        bool ordered = true;
        const auto result = gnss_log::findSegment(next, [&](uint32_t index) {
            if (index != attempts) ordered = false;
            ++attempts;
            return SegmentResult::Exists; // Every existing file is preserved.
        });
        if (!ordered || attempts - before > 32 || attempts > 10000) return false;
        if (next < 10000 ? result != SegmentResult::Pending : result != SegmentResult::Exhausted)
            return false;
        // These are the production failure policy and checked writers. The
        // Python source contract separately verifies their logger call sites.
        if (gnss_log::sharedSegmentFailure(result, true)) return false;
        if (!gnss_log::writeFile(rpm, "/usb/DRZ.txt", "a", 1, [](size_t) { return "RPM\n"; }) ||
            !gnss_log::writeFile(sensor, "/usb/SENSOR.txt", "a", 1, [](size_t) { return "TEMP\n"; }))
            return false;
        GnssLogRecord record{};
        record.sampleId = round;
        if (fifo.push(record) != (round < 320)) return false;
        const auto s = fifo.stats();
        if (s.written || s.uncertain || s.accepted != s.queued + s.overflow) return false;
    }
    if (next != 10000 || attempts != 10000 || rpm.writes != 400 || sensor.writes != 400 ||
        fifo.stats().queued != 320 || fifo.stats().overflow != 80) return false;
    GnssLogRecord out[20]{};
    return fifo.reserve(out, 20, 50000) == 20 && out[0].sequence == 1 && out[19].sequence == 20;
}

constexpr bool segmentCreationStopsOnReadyOrIoError(bool error)
{
    using gnss_log::SegmentResult;
    uint32_t next = 0, attempts = 0;
    const auto result = gnss_log::findSegment(next, [&](uint32_t index) {
        ++attempts;
        return index < 3 ? SegmentResult::Exists : (error ? SegmentResult::IoError : SegmentResult::Ready);
    });
    return attempts == 4 && next == 4 &&
        result == (error ? SegmentResult::IoError : SegmentResult::Ready) &&
        gnss_log::sharedSegmentFailure(result, false) == error &&
        !gnss_log::sharedSegmentFailure(result, true);
}


// Small test driver mirrors the logger's admission/deadline gates. It invokes
// the actual firmware search, failure policy and checked file writer. Python
// contracts verify those gates and their ordering in the RTOS logger source.
constexpr bool segmentIoErrorKeepsLegacyProcessing()
{
    using gnss_log::SegmentResult;
    uint32_t next = 0, attempts = 0, bufferStart = 0;
    unsigned buffered = 0, sampled = 0, processed = 0;
    const char *rpmLines[20]{}, *sensorLines[20]{};
    FaultIo rpm{}, sensor{};
    gnss_log::Fifo fifo{};
    for (unsigned round = 0; round < 400; ++round) {
        const uint32_t now = round * 100;
        const auto segment = gnss_log::findSegment(next, [&](uint32_t index) {
            if (index != attempts) return SegmentResult::Ready; // Ordering failure.
            ++attempts;
            return SegmentResult::IoError;
        });
        if (segment != SegmentResult::IoError || attempts != round + 1 ||
            gnss_log::sharedSegmentFailure(segment, true)) return false;
        // New current samples are admitted after every failed GNSS creation.
        if (buffered == 20) return false;
        if (!buffered) bufferStart = now;
        rpmLines[buffered] = round % 2 ? "RPM1\n" : "RPM0\n";
        sensorLines[buffered++] = round % 2 ? "TEMP1\n" : "TEMP0\n";
        ++sampled;
        if (buffered == 20 || uint32_t(now - bufferStart) >= 1000) {
            unsigned rpmProcessed = 0, sensorProcessed = 0;
            if (!gnss_log::writeFile(rpm, "/usb/DRZ.txt", "a", buffered,
                [&](size_t i) { ++rpmProcessed; return rpmLines[i]; }) ||
                !gnss_log::writeFile(sensor, "/usb/SENSOR.txt", "a", buffered,
                [&](size_t i) { ++sensorProcessed; return sensorLines[i]; })) return false;
            if (rpmProcessed != buffered || sensorProcessed != buffered) return false;
            processed += buffered;
            buffered = 0;
        }
        GnssLogRecord record{};
        record.sampleId = round;
        fifo.push(record); // No active segment: no reserve or false completion.
        const auto stats = fifo.stats();
        if (stats.accepted != stats.queued + stats.overflow || stats.written || stats.uncertain)
            return false;
    }
    return sampled == 400 && processed == 396 && buffered == 4 &&
        rpm.opens == 36 && sensor.opens == 36 && rpm.writes == 396 && sensor.writes == 396 &&
        rpm.bytes == 1980 && sensor.bytes == 2376 &&
        rpm.flushes == 36 && rpm.closes == 36 && sensor.flushes == 36 && sensor.closes == 36 &&
        fifo.stats().queued == 320 && fifo.stats().overflow == 80;
}

constexpr bool repeatedIoErrorsHaveFiniteSearch()
{
    using gnss_log::SegmentResult;
    uint32_t next = 0, attempts = 0;
    for (unsigned round = 0; round < 10002; ++round) {
        const auto result = gnss_log::findSegment(next, [&](uint32_t) {
            ++attempts;
            return SegmentResult::IoError;
        });
        if (round < 10000 ? result != SegmentResult::IoError : result != SegmentResult::Exhausted)
            return false;
        if (attempts != (round < 10000 ? round + 1 : 10000)) return false;
    }
    return next == 10000 && attempts == 10000;
}

constexpr bool commonMediaFailureStopsFollowingWrites(unsigned failure)
{
    using gnss_log::SegmentResult;
    // No detach callback: a GNSS error alone permits the legacy attempt.
    bool failed = gnss_log::sharedSegmentFailure(SegmentResult::IoError, true);
    if (failed) return false;
    FaultIo rpm{}, sensor{};
    if (failure == 0) rpm.openOk = false;
    if (failure == 1) rpm.failWrite = 2;
    if (failure == 2) rpm.flushOk = false;
    if (failure == 3) rpm.closeOk = false;
    unsigned rpmBuffered = 11, sensorBuffered = 11, uncertain = 0;
    const bool ok = gnss_log::writeFile(rpm, "/usb/DRZ.txt", "a", rpmBuffered,
        [](size_t) { return "RPM\n"; });
    if (!ok) uncertain += rpmBuffered;
    rpmBuffered = 0;
    failed = !ok;
    if (!failed) {
        gnss_log::writeFile(sensor, "/usb/SENSOR.txt", "a", sensorBuffered,
            [](size_t) { return "TEMP\n"; });
        sensorBuffered = 0;
    }
    // Shared recovery is now required. The unattempted SENSOR block is retained.
    return failed && uncertain == 11 && !rpmBuffered && sensorBuffered == 11 &&
        rpm.opens == 1 && !sensor.opens && !sensor.writes &&
        rpm.flushes == (failure ? 1u : 0u) && rpm.closes == (failure ? 1u : 0u);
}

constexpr bool detachedSegmentFailurePreventsLegacyWrites()
{
    FaultIo rpm{}, sensor{};
    const bool failed = gnss_log::sharedSegmentFailure(gnss_log::SegmentResult::IoError, false);
    if (!failed) {
        gnss_log::writeFile(rpm, "/usb/DRZ.txt", "a", 1, [](size_t) { return "RPM\n"; });
        gnss_log::writeFile(sensor, "/usb/SENSOR.txt", "a", 1, [](size_t) { return "TEMP\n"; });
    }
    return failed && !rpm.opens && !sensor.opens;
}

static_assert(segmentIoErrorKeepsLegacyProcessing(), "GNSS creation IoError preserves sampling and checked processing of both legacy files");
static_assert(repeatedIoErrorsHaveFiniteSearch(), "Repeated segment errors stop after 10000 exclusive candidates");
static_assert(commonMediaFailureStopsFollowingWrites(0), "Shared open failure stops subsequent writes and retains unattempted data");
static_assert(commonMediaFailureStopsFollowingWrites(1), "Shared write failure stops subsequent writes and retains unattempted data");
static_assert(commonMediaFailureStopsFollowingWrites(2), "Shared flush failure stops subsequent writes and retains unattempted data");
static_assert(commonMediaFailureStopsFollowingWrites(3), "Shared close failure stops subsequent writes and retains unattempted data");
static_assert(detachedSegmentFailurePreventsLegacyWrites(), "A changed medium retains immediate shared recovery without legacy I/O");

static_assert(exhaustedNamesKeepLegacyWriting(), "10000 existing GNSS names retain FIFO while RPM/temperature writes continue");
static_assert(segmentCreationStopsOnReadyOrIoError(false), "First free exclusive name ends bounded search");
static_assert(segmentCreationStopsOnReadyOrIoError(true), "Segment I/O failure is shared only when the medium changed");
static_assert(successfulWrite(), "Success includes open, all bytes, flush and close");
static_assert(openFailure(), "Open failure must never report success or use a missing handle");
static_assert(writeFailure(1), "First short/error write still closes and flushes");
static_assert(writeFailure(7), "Partial block stops at first error and is not replayed");
static_assert(writeFailure(20), "Last write failure remains failure");
static_assert(flushAndCloseFailures(false, true), "Flush failure cannot become success after close");
static_assert(flushAndCloseFailures(true, false), "Close failure cannot become success");
static_assert(flushAndCloseFailures(false, false), "Close is attempted even after flush failure");
static_assert(invalidFormattedLine(), "Formatting failure stops writes and still closes");
static_assert(alreadyOpenedHeader(), "Exclusive segment stream uses the checked writer");
static_assert(uncertainWholeAttemptOnly(0), "Open error accounts exactly one attempted block");
static_assert(uncertainWholeAttemptOnly(1), "Short write accounts exactly one attempted block");
static_assert(uncertainWholeAttemptOnly(2), "Flush error accounts exactly one attempted block");
static_assert(uncertainWholeAttemptOnly(3), "Close error accounts exactly one attempted block");
static_assert(uncertainWholeAttemptOnly(4), "Detach-invalidated success is uncertain; unattempted records survive");
