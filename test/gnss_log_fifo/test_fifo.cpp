// Execute the actual hardware-independent firmware policy during compilation.
#include "../../src/gnss_log_fifo.h"

using gnss_log::Fifo;

constexpr bool balanced(const GnssLogStats &s)
{
    return s.accepted == s.queued + s.written + s.overflow + s.uncertain;
}

constexpr GnssLogRecord sample(uint32_t id, uint32_t host = 0)
{
    GnssLogRecord r{};
    r.sampleId = id;
    r.receivedMs = host;
    r.utcMs = 1795305599800LL + int64_t(id) * 200;
    r.iTowMs = id * 200;
    r.epochDeltaMs = 200;
    return r;
}

constexpr bool emptyAndInvalidOperations()
{
    Fifo f{};
    GnssLogRecord out[20]{};
    return f.reserve(out, 20, 1000) == 0 && f.reserve(nullptr, 20, 1000) == 0 &&
        !f.complete(1, 0, true) && f.stats().highWater == 0 && balanced(f.stats());
}

constexpr bool capacityAndLossAccounting()
{
    Fifo f{};
    for (uint32_t i = 0; i < 350; ++i) {
        if (f.push(sample(i, i * 200)) != (i < 320)) return false;
        if (!balanced(f.stats())) return false;
    }
    const auto full = f.stats();
    if (full.accepted != 350 || full.queued != 320 || full.highWater != 320 ||
        full.overflow != 30) return false; // 70 seconds at 5 Hz, 64 seconds stored.
    GnssLogRecord out[20]{};
    uint64_t expected = 1;
    for (unsigned block = 0; block < 16; ++block) {
        const size_t n = f.reserve(out, 20, 70000);
        if (n != 20 || !balanced(f.stats())) return false;
        for (size_t i = 0; i < n; ++i, ++expected)
            if (out[i].sequence != expected || out[i].sampleId != expected - 1) return false;
        if (!f.complete(out[0].sequence, n, true) || !balanced(f.stats())) return false;
    }
    if (f.stats().written != 320 || f.stats().queued || f.stats().uncertain) return false;
    if (!f.push(sample(350))) return false;
    return f.reserve(out, 20, 71000) == 1 && out[0].sequence == 351 && balanced(f.stats());
}

constexpr bool reservationProtectsOldest()
{
    Fifo f{};
    for (uint32_t i = 0; i < 320; ++i) if (!f.push(sample(i))) return false;
    GnssLogRecord out[20]{}, other[20]{};
    if (f.reserve(out, 100, 0) != 20 || out[0].sequence != 1) return false;
    if (f.push(sample(320)) || f.reserve(other, 20, 1000)) return false;
    if (f.complete(2, 20, true) || f.complete(1, 19, true) || !balanced(f.stats())) return false;
    if (!f.complete(1, 20, false) || f.complete(1, 20, false)) return false;
    if (f.stats().uncertain != 20 || f.stats().queued != 300 || f.stats().overflow != 1) return false;
    if (!f.push(sample(321))) return false;
    return f.reserve(other, 20, 1000) == 20 && other[0].sequence == 21 &&
        out[0].sequence == 1 && balanced(f.stats());
}

constexpr bool ringWrapAndOrderedCopies()
{
    Fifo f{};
    GnssLogRecord out[20]{};
    for (uint32_t round = 0; round < 40; ++round) {
        for (uint32_t i = 0; i < 20; ++i)
            if (!f.push(sample(round * 20 + i))) return false;
        if (f.reserve(out, 20, 1000) != 20) return false;
        for (uint32_t i = 0; i < 20; ++i)
            if (out[i].sampleId != round * 20 + i || out[i].sequence != round * 20 + i + 1)
                return false;
        if (!f.complete(out[0].sequence, 20, true)) return false;
    }
    return f.stats().written == 800 && !f.stats().queued && balanced(f.stats());
}

constexpr bool partialDeadlineAndHostWrap()
{
    Fifo f{};
    constexpr uint32_t received = 0xffffff00u;
    f.push(sample(0, received));
    GnssLogRecord out[20]{};
    return f.reserve(out, 0, uint32_t(received + 1000)) == 0 &&
        f.reserve(out, 20, uint32_t(received + 999)) == 0 &&
        f.reserve(out, 20, uint32_t(received + 1000)) == 1 &&
        out[0].receivedMs == received && f.complete(out[0].sequence, 1, true) && balanced(f.stats());
}

constexpr bool callerLimit()
{
    Fifo f{};
    for (unsigned i = 0; i < 20; ++i) f.push(sample(i));
    GnssLogRecord out[20]{};
    return f.reserve(nullptr, 20, 1000) == 0 && f.reserve(out, 3, 1000) == 3 &&
        f.complete(out[0].sequence, 3, true) && f.stats().queued == 17 &&
        f.reserve(out, 20, 1000) == 17 && out[0].sequence == 4 && balanced(f.stats());
}

constexpr bool reconnectInvalidityAndMeasurementTime()
{
    Fifo f{};
    auto a = sample(91, 500);
    a.sessionId = 7;
    a.latitude = -123456789;
    a.longitude = 987654321;
    a.altitudeMslMm = -400;
    a.horizontalAccuracyMm = 1234;
    a.fixType = 0; // Invalid position/UTC is still admitted.
    auto b = sample(1, 9000);
    b.sessionId = 8;
    b.timeValid = b.positionValid = b.altitudeValid = 1;
    b.utcMs = a.utcMs + 200;
    if (!f.push(a) || !f.push(b)) return false;
    a.utcMs = 0; // Published/caller storage changes must not mutate the ring.
    GnssLogRecord out[20]{};
    if (f.reserve(out, 20, 20000) != 2) return false;
    return out[0].sequence == 1 && out[1].sequence == 2 && out[0].sessionId == 7 &&
        out[1].sessionId == 8 && out[0].sampleId == 91 && out[1].sampleId == 1 &&
        !out[0].timeValid && !out[0].positionValid && !out[0].altitudeValid &&
        out[1].timeValid && out[0].latitude == -123456789 && out[0].longitude == 987654321 &&
        out[0].altitudeMslMm == -400 && out[0].horizontalAccuracyMm == 1234 &&
        out[1].utcMs - out[0].utcMs == 200 && out[1].receivedMs - out[0].receivedMs == 8500 &&
        balanced(f.stats());
}

constexpr bool boundedDrainFasterThanProducer()
{
    Fifo f{};
    for (uint32_t i = 0; i < 300; ++i) f.push(sample(i));
    GnssLogRecord out[20]{};
    for (uint32_t round = 0; round < 10; ++round) {
        if (round % 2 == 0) f.push(sample(300 + round)); // Five arrivals per second.
        const size_t n = f.reserve(out, 20, 10000 + round * 100);
        if (n != 20 || !f.complete(out[0].sequence, n, true)) return false;
    }
    return f.stats().accepted == 305 && f.stats().written == 200 && f.stats().queued == 105 &&
        !f.stats().overflow && balanced(f.stats()); // Policy capacity, not USB timing proof.
}

constexpr bool newerReceivedTimeWaits(uint32_t received, uint32_t staleNow)
{
    Fifo f{};
    f.push(sample(0, received));
    GnssLogRecord out[20]{};
    if (f.reserve(out, 20, staleNow) || f.reserve(out, 20, received) ||
        f.reserve(out, 20, uint32_t(received + 999))) return false;
    if (f.stats().queued != 1 || f.stats().written || f.stats().uncertain) return false;
    return f.reserve(out, 20, uint32_t(received + 1000)) == 1 &&
        out[0].receivedMs == received && f.complete(out[0].sequence, 1, true) &&
        balanced(f.stats());
}

constexpr bool fullBlockIgnoresPartialDeadline()
{
    Fifo f{};
    for (unsigned i = 0; i < 20; ++i) f.push(sample(i, 2000));
    GnssLogRecord out[20]{};
    return f.reserve(out, 20, 1000) == 20 && f.complete(out[0].sequence, 20, true) &&
        f.stats().written == 20 && balanced(f.stats());
}

constexpr bool ambiguousHalfCycleWaits()
{
    Fifo f{};
    f.push(sample(0, 100));
    GnssLogRecord out[20]{};
    return !f.reserve(out, 20, uint32_t(100u + 0x80000000u)) &&
        f.reserve(out, 20, uint32_t(100u + 0x7fffffffu)) == 1;
}

static_assert(newerReceivedTimeWaits(1500, 1000), "Stale now cannot release a newer partial block");
static_assert(newerReceivedTimeWaits(100, 0xffffff00u), "Future timestamp across wrap must wait for actual age");
static_assert(newerReceivedTimeWaits(0xffffff00u, 0xfffffe00u), "Fresh block before wrap releases only after 1000 ms across wrap");
static_assert(fullBlockIgnoresPartialDeadline(), "Full blocks remain immediately eligible");
static_assert(ambiguousHalfCycleWaits(), "Half-cycle boundary must conservatively retain partial blocks");
static_assert(sizeof(GnssLogRecord) == 64, "Target record ABI must be 64 bytes");
static_assert(sizeof(GnssLogStats{}.accepted) == 8, "Acceptance counter must be 64 bit");
static_assert(gnss_log::CAPACITY == 320 && gnss_log::BLOCK == 20, "Exact usable capacities");
static_assert(emptyAndInvalidOperations(), "Empty FIFO and invalid acknowledgements");
static_assert(capacityAndLossAccounting(), "70-second outage, drop-newest, exact balance and sequence gap");
static_assert(reservationProtectsOldest(), "Reservations survive full-ring producer and wrong acknowledgements");
static_assert(ringWrapAndOrderedCopies(), "Ring index wraps without reordering or duplicate admission");
static_assert(partialDeadlineAndHostWrap(), "Partial block deadline is exact across millis wrap");
static_assert(callerLimit(), "Reservation respects consumer limit and 20-record ceiling");
static_assert(reconnectInvalidityAndMeasurementTime(), "Historical records, invalid flags and UTC survive reconnect/backlog");
static_assert(boundedDrainFasterThanProducer(), "Bounded 20-record rounds have surplus drain capacity");
