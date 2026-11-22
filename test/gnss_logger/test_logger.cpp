// Compile-time fault injection executes the same write policy as FileOps.
#include "../../src/gnss_log_fifo.h"

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
