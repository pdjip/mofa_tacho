#ifndef MOFA_GNSS_LOG_FIFO_H
#define MOFA_GNSS_LOG_FIFO_H

#include <stdint.h>
#include <stddef.h>

// Original UBX units. Validity flags govern interpretation, not admission.
struct GnssLogRecord {
    uint64_t sequence = 0;
    int64_t utcMs = 0;
    uint32_t sessionId = 0, sampleId = 0, iTowMs = 0, receivedMs = 0, epochDeltaMs = 0;
    int32_t latitude = 0, longitude = 0, altitudeMslMm = 0, speedMmS = 0, courseE5 = 0;
    uint32_t horizontalAccuracyMm = 0;
    uint8_t timeValid = 0, positionValid = 0, altitudeValid = 0, fixType = 0;
};
static_assert(sizeof(GnssLogRecord) == 64, "GNSS FIFO record must be 64 bytes");

struct GnssLogStats {
    uint64_t accepted = 0, written = 0, overflow = 0, uncertain = 0;
    uint32_t queued = 0, highWater = 0;
};

namespace gnss_log {
// No printf length-modifier dependency, no heap, and no signed negation of
// INT64_MIN. Failure leaves an empty string whenever a destination is writable.
constexpr bool decimalUnsigned(char *out, size_t capacity, uint64_t value) {
    if (!out || !capacity) return false;
    out[0] = '\0';
    size_t digits = 1;
    for (uint64_t rest = value; rest >= 10; rest /= 10) ++digits;
    if (capacity <= digits) return false;
    out[digits] = '\0';
    do {
        out[--digits] = char('0' + value % 10);
        value /= 10;
    } while (digits);
    return true;
}

constexpr bool decimalSigned(char *out, size_t capacity, int64_t value) {
    if (!out || !capacity) return false;
    out[0] = '\0';
    if (value >= 0) return decimalUnsigned(out, capacity, uint64_t(value));
    const uint64_t magnitude = uint64_t(0) - uint64_t(value);
    if (capacity < 2 || !decimalUnsigned(out + 1, capacity - 1, magnitude)) return false;
    out[0] = '-';
    return true;
}

constexpr size_t CAPACITY = 320;
constexpr size_t BLOCK = 20;
constexpr uint32_t PARTIAL_MS = 1000;
constexpr uint32_t SEGMENT_LIMIT = 10000;
constexpr unsigned SEGMENT_ATTEMPTS = 32;

enum class SegmentResult { Ready, Exists, Pending, Exhausted, IoError };

// A segment I/O error alone does not prove the legacy files are unusable.
// Medium changes and failures of checked legacy I/O retain shared recovery.
constexpr bool sharedSegmentFailure(SegmentResult result, bool mediumAvailable) {
    return result == SegmentResult::IoError && !mediumAvailable;
}

// Create(index) exclusively creates one segment and returns Ready, Exists or
// IoError. The boot-wide cursor never revisits a name, including after errors.
template<class Create>
constexpr SegmentResult findSegment(uint32_t &next, Create create) {
    for (unsigned attempt = 0; attempt < SEGMENT_ATTEMPTS && next < SEGMENT_LIMIT; ++attempt) {
        const SegmentResult result = create(next++);
        if (result != SegmentResult::Exists) return result;
    }
    return next < SEGMENT_LIMIT ? SegmentResult::Pending : SegmentResult::Exhausted;
}

// Single producer/consumer policy; the service supplies its own mutex. Reserved
// entries remain in queued and cannot be overwritten by a full-ring producer.
class Fifo {
public:
    constexpr bool push(GnssLogRecord record) {
        record.sequence = ++stats_.accepted;
        if (stats_.queued == CAPACITY) { ++stats_.overflow; return false; }
        records_[(head_ + stats_.queued) % CAPACITY] = record;
        if (++stats_.queued > stats_.highWater) stats_.highWater = stats_.queued;
        return true;
    }
    constexpr size_t reserve(GnssLogRecord *out, size_t limit, uint32_t now) {
        if (!out || reserved_ || !stats_.queued || !limit) return 0;
        // A producer can publish after the caller sampled now. Reject future
        // timestamps as well as fresh entries, using modular half-range order.
        // Unambiguous elapsed times must be below 2^31 ms (about 24.9 days).
        const uint32_t age = uint32_t(now - records_[head_].receivedMs);
        if (stats_.queued < BLOCK && (age >= 0x80000000u || age < PARTIAL_MS))
            return 0;
        reserved_ = stats_.queued < BLOCK ? stats_.queued : BLOCK;
        if (reserved_ > limit) reserved_ = limit;
        for (size_t i = 0; i < reserved_; ++i) out[i] = records_[(head_ + i) % CAPACITY];
        return reserved_;
    }
    constexpr bool complete(uint64_t firstSequence, size_t count, bool success) {
        if (!reserved_ || count != reserved_ || records_[head_].sequence != firstSequence)
            return false;
        if (success) stats_.written += count; else stats_.uncertain += count;
        head_ = (head_ + count) % CAPACITY;
        stats_.queued -= count;
        reserved_ = 0;
        return true;
    }
    constexpr GnssLogStats stats() const { return stats_; }
private:
    GnssLogRecord records_[CAPACITY] = {};
    GnssLogStats stats_;
    size_t head_ = 0, reserved_ = 0;
};

// Ops is injectable: open(path,mode), write(handle,data,size), flush(handle),
// close(handle). Success requires all four operations, including full writes.
// Always close an opened stream even after a short write or flush failure.
template<class Ops, class Lines>
constexpr bool writeOpened(Ops &io, typename Ops::Handle file, size_t count, Lines line) {
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        const char *data = line(i);
        size_t length = 0;
        if (data) while (data[length]) ++length;
        ok = data && io.write(file, data, length);
    }
    const bool flushed = io.flush(file);
    const bool closed = io.close(file);
    return ok && flushed && closed;
}

template<class Ops, class Lines>
constexpr bool writeFile(Ops &io, const char *path, const char *mode, size_t count, Lines line) {
    const auto file = io.open(path, mode);
    return file && writeOpened(io, file, count, line);
}
} // namespace gnss_log
#endif
