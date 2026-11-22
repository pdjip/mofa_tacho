#ifndef MOFA_GNSS_SERVICE_H
#define MOFA_GNSS_SERVICE_H

#include <stdint.h>
#include <time.h>
#include "gnss_log_fifo.h"

enum class GnssConnection : uint8_t {
    Searching, Configuring, Receiving, Stale, RetryWait, ConfigError, RateError, ThreadError
};

// Latest-value snapshot only: this is deliberately not a queue of PVT epochs.
struct GnssSnapshot {
    GnssConnection connection = GnssConnection::Searching;
    bool sampleAvailable = false;
    bool positionValid = false;
    bool altitudeValid = false;
    bool timeValid = false;
    bool clockSynchronized = false; // Remains true during RTC holdover.
    uint8_t fixType = 0;
    uint32_t sessionId = 0;
    uint32_t sampleId = 0;
    uint32_t iTowMs = 0;
    uint32_t receivedMs = 0; // Host processing time, not the GNSS measurement time.
    uint32_t epochDeltaMs = 0;
    time_t utcSeconds = 0;
    uint32_t utcNanoseconds = 0;
    double latitudeDeg = 0;
    double longitudeDeg = 0;
    float altitudeM = 0;
    float speedKph = 0;
    float courseDeg = 0;
    float horizontalAccuracyM = 0;
};

bool startGnss();
GnssSnapshot readGnssSnapshot();

// loggingTask is the sole consumer. Complete exactly the reserved block once;
// false success marks the whole attempted block uncertain, with no replay.
size_t reserveGnssLog(GnssLogRecord *out, size_t limit, uint32_t now);
bool completeGnssLog(uint64_t firstSequence, size_t count, bool success);
GnssLogStats readGnssLogStats();

#endif
