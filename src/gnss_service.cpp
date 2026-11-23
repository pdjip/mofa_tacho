/** @file gnss_service.cpp
 * GNSS-thread-owned I2C receiver, epoch qualification and publication. FIFO admission is independent of fix validity and serial verbosity.
 *
 * Dependencies: Wire/SparkFun UBX and Mbed RTC/thread primitives; public contracts are in gnss_service.h. One GNSS task owns the receiver and publishes independent snapshot/FIFO copies. It cannot promise lossless delivery before the acceptance point or hard I2C timing.
 */
#include "diagnostics_config.h"
#include "gnss_service.h"
#include "gnss_epoch.h"

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <mbed.h>
#include <mbed_mktime.h>
#include <cmath>
#include <chrono>
#include <cstdio>

namespace {
constexpr uint16_t COMMAND_TIMEOUT_MS = 250;
constexpr uint16_t MEASUREMENT_MS = 200;
constexpr uint32_t STALE_MS = 1500;
constexpr uint32_t RATE_CHECK_MS = 10000;
constexpr uint32_t STACK_BYTES = 4096;

// The device and all library state are accessed exclusively by gnssTask().
SFE_UBLOX_GNSS gnss;
rtos::Mutex snapshotMutex;
rtos::Mutex fifoMutex;
gnss_log::Fifo logFifo; // Boot lifetime: never reset on GNSS reconnect.
GnssSnapshot published;
alignas(8) unsigned char gnssStack[STACK_BYTES];
rtos::Thread gnssThread(osPriorityNormal, STACK_BYTES, gnssStack, "gnss");

// Owned exclusively by gnssTask(); never read/reset by another thread.
/** GNSS-thread-owned observation window. Counters never control epoch admission. Retry waiting sleeps in bounded slices so diagnostics can be serviced even without a receiver. */
struct GnssDiagnostics {
    uint32_t windowStart = millis();
    uint32_t epochs = 0;
    uint32_t validEpochs = 0;
    uint32_t duplicates = 0;
    uint32_t lastDeltaMs = 0;

    void reportIfDue()
    {
        const uint32_t now = millis();
        const uint32_t elapsed = now - windowStart;
        if (elapsed < 10000) return;
#if MOFA_DEBUG_GNSS >= 3
        // Fixed-point Hz avoids floating-point printf and uses the actual window.
        const uint32_t hz100 = (uint64_t(epochs) * 100000 + elapsed / 2) / elapsed;
        const uint32_t validHz100 = (uint64_t(validEpochs) * 100000 + elapsed / 2) / elapsed;
        char line[192];
        snprintf(line, sizeof(line),
            "GNSS DIAG win_ms=%lu epochs=%lu Hz=%lu.%02lu valid=%lu validHz=%lu.%02lu dt_ms=%lu dup=%lu\n",
            (unsigned long)elapsed, (unsigned long)epochs,
            (unsigned long)(hz100 / 100), (unsigned long)(hz100 % 100),
            (unsigned long)validEpochs,
            (unsigned long)(validHz100 / 100), (unsigned long)(validHz100 % 100),
            (unsigned long)lastDeltaMs, (unsigned long)duplicates);
        Serial.print(line);
#endif
        windowStart = now;
        epochs = validEpochs = duplicates = lastDeltaMs = 0;
    }

    void waitRetry(uint32_t durationMs)
    {
        const uint32_t start = millis();
        while (static_cast<uint32_t>(millis() - start) < durationMs) {
            reportIfDue();
            const uint32_t elapsed = millis() - start;
            if (elapsed >= durationMs) break;
            const uint32_t remaining = durationMs - elapsed;
            rtos::ThisThread::sleep_for(std::chrono::milliseconds(remaining < 100 ? remaining : 100));
        }
        reportIfDue();
    }
};

/** Invalidate presentation fields without discarding session/clock holdover metadata. Used on connection loss and on stale reader copies; caller owns the supplied snapshot. */
void invalidate(GnssSnapshot &s)
{
    s.positionValid = false;
    s.altitudeValid = false;
    s.timeValid = false;
    s.latitudeDeg = s.longitudeDeg = NAN;
    s.altitudeM = s.speedKph = s.courseDeg = s.horizontalAccuracyM = NAN;
}

/** Copy one complete snapshot under snapshotMutex. Release it before FIFO admission or any hardware/serial work to avoid lock nesting. */
void publish(const GnssSnapshot &s)
{
    snapshotMutex.lock();
    published = s;
    snapshotMutex.unlock();
}

// UTC conversion without mktime's timezone/DST interpretation. Reject invalid
// calendars and leap seconds; this RTC application supports years 2000..2099.
/** Convert resolved PVT calendar/fraction to normalized UTC outputs; false rejects unsupported or inconsistent dates. GNSS thread only; uses Mbed RTC conversion without local timezone interpretation. */
bool pvtUtc(const UBX_NAV_PVT_data_t &p, time_t &seconds, uint32_t &nanoseconds)
{
    if (!p.valid.bits.validDate || !p.valid.bits.validTime || !p.valid.bits.fullyResolved ||
        p.year < 2000 || p.year > 2099 || p.month < 1 || p.month > 12 ||
        p.day < 1 || p.day > 31 || p.hour > 23 || p.min > 59 || p.sec > 59 ||
        p.nano < -1000000000 || p.nano > 1000000000)
        return false;

    tm calendar = {};
    calendar.tm_year = p.year - 1900;
    calendar.tm_mon = p.month - 1;
    calendar.tm_mday = p.day;
    calendar.tm_hour = p.hour;
    calendar.tm_min = p.min;
    calendar.tm_sec = p.sec;
    tm checked = {};
    if (!_rtc_maketime(&calendar, &seconds, RTC_4_YEAR_LEAP_YEAR_SUPPORT) ||
        !_rtc_localtime(seconds, &checked, RTC_4_YEAR_LEAP_YEAR_SUPPORT) ||
        checked.tm_year != calendar.tm_year || checked.tm_mon != calendar.tm_mon ||
        checked.tm_mday != calendar.tm_mday || checked.tm_hour != calendar.tm_hour ||
        checked.tm_min != calendar.tm_min || checked.tm_sec != calendar.tm_sec)
        return false;

    int64_t fraction = p.nano;
    while (fraction < 0) { --seconds; fraction += 1000000000; }
    while (fraction >= 1000000000) { ++seconds; fraction -= 1000000000; }
    nanoseconds = static_cast<uint32_t>(fraction);
    return true;
}

/** GNSS-thread synchronous CFG-RATE check. Query fresh receiver state with bounded command timeout; cached getters alone cannot prove success. Serial verbosity does not affect the returned 200-ms/rate-1 comparison. */
bool verifyRate()
{
    // Require a successful fresh CFG-RATE response. The simple getters alone
    // can return cached values even after an unsuccessful communication attempt.
    gnss.flushCFGRATE();
    if (!gnss.getNavigationFrequencyInternal(COMMAND_TIMEOUT_MS))
        return false;
    const uint16_t measurement = gnss.getMeasurementRate(0);
    const uint16_t navigation = gnss.getNavigationRate(0);
#if MOFA_DEBUG_GNSS >= 2
    Serial.print("GNSS CFG-RATE: measRate_ms=");
    Serial.print(measurement);
    Serial.print(" navRate=");
    Serial.println(navigation);
#endif
    return measurement == MEASUREMENT_MS && navigation == 1;
}

/** Configure UBX output and auto-PVT after explicit rate verification. Return false at the first rejected command; the outer task publishes ConfigError and backs off rather than blocking other tasks. */
bool setupGPS()
{
    if (!gnss.setI2COutput(COM_TYPE_UBX, COMMAND_TIMEOUT_MS) ||
        !gnss.setAutoPVT(false, true, COMMAND_TIMEOUT_MS) ||
        !gnss.setMeasurementRate(MEASUREMENT_MS, COMMAND_TIMEOUT_MS) ||
        !gnss.setNavigationRate(1, COMMAND_TIMEOUT_MS) || !verifyRate())
        return false;

    // setNavigationFrequency(1) would overwrite 200 ms with 1000 ms. Do not use it.
    if (!gnss.setAutoPVT(true, true, COMMAND_TIMEOUT_MS))
        return false;
    gnss.flushPVT();
    return true;
}

/** Own receiver/library state for boot lifetime. Discover/configure, qualify fresh epochs, publish latest snapshot, then admit one raw-unit record under fifoMutex. Retry invalidates fix data but does not reset FIFO or accepted sequence. */
void gnssTask()
{
    using std::chrono::milliseconds;
    GnssSnapshot current;
    invalidate(current);
    uint32_t retryMs = 1000;
    // Keep ordering history across reconnects, separately from public session IDs.
    gnss_policy::EpochTracker epochs;
    GnssDiagnostics diagnostics;

    while (true) {
        diagnostics.reportIfDue();
        current.connection = GnssConnection::Searching;
        invalidate(current);
        publish(current);
        // begin() performs at most three protocol attempts with this timeout.
        // Underlying Wire timeouts still depend on the installed hardware driver.
        if (!gnss.begin(Wire, 0x42, COMMAND_TIMEOUT_MS)) {
            current.connection = GnssConnection::RetryWait;
        } else {
            current.connection = GnssConnection::Configuring;
            publish(current);
            if (!setupGPS()) {
                current.connection = GnssConnection::ConfigError;
                MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println("GNSS configuration failed; retrying"));
            } else {
                const uint32_t nextSession = current.sessionId + 1;
                const bool clockSynchronized = current.clockSynchronized;
                current = GnssSnapshot{};
                current.sessionId = nextSession;
                current.clockSynchronized = clockSynchronized;
                invalidate(current);
                current.connection = GnssConnection::Receiving;
                publish(current);
                uint32_t lastNewPvt = millis();
                uint32_t lastRateCheck = lastNewPvt;
                uint8_t rateCheckFailures = 0;
                gnss_policy::RateMonitor rate;

                while (static_cast<uint32_t>(millis() - lastNewPvt) < STALE_MS) {
                    diagnostics.reportIfDue();
                    // Auto-PVT checks available data instead of polling/waiting
                    // for a reply. Stage 1 intentionally keeps only the latest PVT.
                    if (gnss.getPVT(0)) {
                        const UBX_NAV_PVT_data_t p = gnss.packetUBXNAVPVT->data;
                        gnss.flushPVT();
                        time_t utcSeconds = 0;
                        uint32_t utcNanoseconds = 0;
                        const bool timeValid = pvtUtc(p, utcSeconds, utcNanoseconds);
                        const uint32_t now = millis();
                        const auto epoch = epochs.observe(p.iTOW, now, timeValid,
                            int64_t(utcSeconds) * 1000 + utcNanoseconds / 1000000);
                        if (epoch == gnss_policy::EpochResult::Duplicate)
                            ++diagnostics.duplicates;
                        if (epoch == gnss_policy::EpochResult::Baseline ||
                            epoch == gnss_policy::EpochResult::New) {
                            lastNewPvt = now;
                        }
                        if (epoch == gnss_policy::EpochResult::Baseline) {
                            rate = gnss_policy::RateMonitor{};
                            if (current.sampleAvailable) {
                                // Transition from provisional receiver time to
                                // UTC: quarantine it through a fresh session.
                                current.connection = GnssConnection::Stale;
                                break;
                            }
                        }
                        if (epoch == gnss_policy::EpochResult::New) {
                            rate.observe(epochs.deltaMs(), epochs.hostDeltaMs());
                            if (rate.faulty()) {
                                current.connection = GnssConnection::RateError;
                                invalidate(current);
                                publish(current);
                                MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println("GNSS persistent PVT epoch-rate mismatch; reconnecting"));
                                // Distinguish wrong CFG-RATE from reception gaps in diagnostics.
                                verifyRate();
                                break;
                            }
                            // Confirm three consecutive 200-ms steps before publishing
                            // a new session. Never expose a lone buffered startup packet.
                            if (!current.sampleAvailable && !rate.ready()) {
                                rtos::ThisThread::sleep_for(milliseconds(10));
                                continue;
                            }
                            current.epochDeltaMs = epochs.deltaMs();
                            current.iTowMs = p.iTOW;
                            current.receivedMs = lastNewPvt;
                            current.sampleAvailable = true;
                            ++current.sampleId;
                            current.fixType = p.fixType;
                            current.positionValid = p.flags.bits.gnssFixOK && !p.flags3.bits.invalidLlh &&
                                (p.fixType == 2 || p.fixType == 3 || p.fixType == 4);
                            current.altitudeValid = current.positionValid && p.fixType != 2;
                            current.latitudeDeg = current.positionValid ? p.lat * 1e-7 : NAN;
                            current.longitudeDeg = current.positionValid ? p.lon * 1e-7 : NAN;
                            current.altitudeM = current.altitudeValid ? p.hMSL * 0.001f : NAN;
                            current.speedKph = current.positionValid ? p.gSpeed * 0.0036f : NAN;
                            current.courseDeg = current.positionValid ? p.headMot * 1e-5f : NAN;
                            current.horizontalAccuracyM = current.positionValid ? p.hAcc * 0.001f : NAN;
                            current.timeValid = timeValid;
                            current.utcSeconds = timeValid ? utcSeconds : 0;
                            current.utcNanoseconds = timeValid ? utcNanoseconds : 0;
                            // Set the RTC once per firmware boot, not per receiver
                            // connection. Reconnects cannot step the running clock
                            // backwards (or forwards); later drift correction is deferred.
                            if (current.timeValid && !current.clockSynchronized) {
                                // Round to the nearest second; truncation would always run behind.
                                set_time(current.utcSeconds + (current.utcNanoseconds >= 500000000u ? 1 : 0));
                                current.clockSynchronized = true;
                            }
                            retryMs = 1000;
                            publish(current);
                            GnssLogRecord record;
                            record.utcMs = int64_t(current.utcSeconds) * 1000 + current.utcNanoseconds / 1000000;
                            record.sessionId = current.sessionId;
                            record.sampleId = current.sampleId;
                            record.iTowMs = current.iTowMs;
                            record.receivedMs = current.receivedMs;
                            record.epochDeltaMs = current.epochDeltaMs;
                            record.latitude = p.lat;
                            record.longitude = p.lon;
                            record.altitudeMslMm = p.hMSL;
                            record.speedMmS = p.gSpeed;
                            record.courseE5 = p.headMot;
                            record.horizontalAccuracyMm = p.hAcc;
                            record.timeValid = current.timeValid;
                            record.positionValid = current.positionValid;
                            record.altitudeValid = current.altitudeValid;
                            record.fixType = current.fixType;
                            // No snapshot mutex is held, and no I/O/formatting
                            // occurs under the FIFO mutex.
                            fifoMutex.lock();
                            logFifo.push(record);
                            fifoMutex.unlock();
                            // Count only accepted, published new iTOW epochs.
                            // Position/time validity is independent of packet freshness.
                            ++diagnostics.epochs;
                            if (current.positionValid && current.timeValid)
                                ++diagnostics.validEpochs;
                            diagnostics.lastDeltaMs = current.epochDeltaMs;
                        }
                    }
                    if (static_cast<uint32_t>(millis() - lastRateCheck) >= RATE_CHECK_MS) {
                        lastRateCheck = millis();
                        // One lost/late CFG response must not reset the receiver.
                        rateCheckFailures = verifyRate() ? 0 : rateCheckFailures + 1;
                        if (rateCheckFailures >= 2) {
                            current.connection = GnssConnection::RateError;
                            break;
                        }
                    }
                    rtos::ThisThread::sleep_for(milliseconds(10));
                }
                if (current.connection != GnssConnection::RateError) {
                    current.connection = GnssConnection::Stale;
                    MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, Serial.println("GNSS PVT stream stale; reconnecting"));
                }
                invalidate(current);
            }
        }
        publish(current);
        diagnostics.waitRetry(retryMs);
        if (retryMs < 8000) retryMs *= 2;
    }
}
} // namespace

/** Setup-only, one-shot task launch. Publish an invalid initial snapshot before starting the static-stack thread; return false and publish ThreadError if RTOS dispatch fails. */
bool startGnss()
{
    GnssSnapshot initial;
    invalidate(initial);
    publish(initial);
    if (gnssThread.start(gnssTask) == osOK) return true;
    initial.connection = GnssConnection::ThreadError;
    publish(initial);
    return false;
}

/** Return an owned latest-value copy under snapshotMutex. Apply stale/no-sample invalidation to the copy; never mutate the producer or hold the lock while consumers render/write. */
GnssSnapshot readGnssSnapshot()
{
    snapshotMutex.lock();
    GnssSnapshot copy = published;
    snapshotMutex.unlock();
    if (!copy.sampleAvailable) invalidate(copy);
    if (copy.connection == GnssConnection::Receiving && copy.sampleAvailable &&
        static_cast<uint32_t>(millis() - copy.receivedMs) >= STALE_MS) {
        copy.connection = GnssConnection::Stale;
        invalidate(copy);
    }
    return copy;
}

/** Sole logger consumer: briefly lock FIFO and copy up to limit due records into caller storage. Returned count remains queued/reserved until exactly one matching completion; do I/O after this returns. */
size_t reserveGnssLog(GnssLogRecord *out, size_t limit, uint32_t now)
{
    fifoMutex.lock();
    const size_t count = logFifo.reserve(out, limit, now);
    fifoMutex.unlock();
    return count;
}

/** Sole logger consumer: verify sequence/count against outstanding reservation. A failed attempt removes the entire block as uncertain, without replay; false leaves the invalid completion uncommitted. */
bool completeGnssLog(uint64_t firstSequence, size_t count, bool success)
{
    fifoMutex.lock();
    const bool valid = logFifo.complete(firstSequence, count, success);
    fifoMutex.unlock();
    return valid;
}

/** Copy consistent boot-wide counters under fifoMutex; queued includes reserved records. Returned value needs no lock and may immediately become older than producer state. */
GnssLogStats readGnssLogStats()
{
    fifoMutex.lock();
    const GnssLogStats stats = logFifo.stats();
    fifoMutex.unlock();
    return stats;
}
