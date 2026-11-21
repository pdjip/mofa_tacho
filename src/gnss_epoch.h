#ifndef MOFA_GNSS_EPOCH_H
#define MOFA_GNSS_EPOCH_H

#include <stdint.h>

namespace gnss_policy {
constexpr uint32_t WEEK_MS = 604800000;
constexpr uint32_t PERIOD_MS = 200;
// Regular steps of a rejected stream that re-anchor the tracker on it.
constexpr uint8_t REANCHOR_STEPS = 3;

enum class EpochResult { Baseline, New, Duplicate, Old, Invalid };

// Retained across reconnects: changing a session must not erase the high-water
// mark. Host-time checks allow jitter, but reject stale or implausible advances.
// Rejected packets are only compared with the mark, which they never move. If
// the receiver-to-host offset changes for good (receiver time jump before UTC,
// a late-stamped accepted packet, host clock drift over a long outage), a live
// stream would be rejected forever. So rejected packets are followed as a
// candidate: REANCHOR_STEPS regular 200-ms steps that match host time are a
// live stream, and the tracker starts over from it (Baseline). Buffered old
// packets arrive in bursts and never qualify.
class EpochTracker {
public:
    constexpr EpochResult observe(uint32_t tow, uint32_t hostMs,
                                  bool utcValid, int64_t utcMs)
    {
        if (tow >= WEEK_MS) return EpochResult::Invalid;
        if (!haveEpoch_) {
            remember(tow, hostMs, utcValid, utcMs);
            return EpochResult::Baseline; // Never publish an unconfirmed first packet.
        }
        if (utcValid && !haveUtc_) {
            // A cold receiver may initially use a provisional iTOW. Establish
            // the first trusted UTC anchor without publishing this transition.
            remember(tow, hostMs, true, utcMs);
            return EpochResult::Baseline;
        }

        const uint32_t elapsed = hostMs - hostMs_;
        const uint32_t delta = (tow + WEEK_MS - tow_) % WEEK_MS;
        bool absoluteAdvance = false;
        if (utcValid && haveUtc_) {
            const int64_t utcDelta = utcMs - utcMs_;
            const uint32_t hostDelta = hostMs - utcHostMs_;
            if (utcDelta == 0 && delta == 0) return EpochResult::Duplicate;
            if (utcDelta <= 0 || utcDelta > int64_t(hostDelta) + 1000 ||
                utcDelta + 1000 < hostDelta)
                return followCandidate(tow, hostMs, utcValid, utcMs);
            // Absolute time disambiguates an outage of half a GPS week or more.
            absoluteAdvance = utcDelta >= WEEK_MS / 2;
        }
        if (!absoluteAdvance) {
            if (delta == 0) return EpochResult::Duplicate;
            if (delta >= WEEK_MS / 2 || uint64_t(delta) > uint64_t(elapsed) + 500 ||
                uint64_t(elapsed) > uint64_t(delta) + 1000)
                return followCandidate(tow, hostMs, utcValid, utcMs);
        }
        deltaMs_ = delta;
        hostDeltaMs_ = elapsed;
        remember(tow, hostMs, utcValid, utcMs);
        return EpochResult::New;
    }

    constexpr uint32_t deltaMs() const { return deltaMs_; }
    constexpr uint32_t hostDeltaMs() const { return hostDeltaMs_; }

private:
    // Called for each packet the mark rejects; returns Old until re-anchoring.
    constexpr EpochResult followCandidate(uint32_t tow, uint32_t hostMs,
                                          bool utcValid, int64_t utcMs)
    {
        if (haveCandidate_ && tow == candTow_) return EpochResult::Duplicate;
        const uint32_t elapsed = hostMs - candHostMs_;
        const int64_t utcDelta = utcMs - candUtcMs_;
        const bool regular = haveCandidate_ &&
            (tow + WEEK_MS - candTow_) % WEEK_MS == PERIOD_MS &&
            elapsed >= PERIOD_MS / 2 && elapsed <= 2 * PERIOD_MS &&
            (!utcValid || !candUtcValid_ ||
             (utcDelta >= PERIOD_MS / 2 && utcDelta <= 3 * PERIOD_MS / 2));
        candSteps_ = regular ? uint8_t(candSteps_ + 1) : uint8_t(0);
        haveCandidate_ = true;
        candTow_ = tow;
        candHostMs_ = hostMs;
        candUtcValid_ = utcValid;
        candUtcMs_ = utcMs;
        if (candSteps_ < REANCHOR_STEPS) return EpochResult::Old;
        // Start over from the live stream, including its UTC state.
        haveUtc_ = false;
        remember(tow, hostMs, utcValid, utcMs);
        return EpochResult::Baseline;
    }

    constexpr void remember(uint32_t tow, uint32_t hostMs, bool utcValid, int64_t utcMs)
    {
        haveCandidate_ = false; // An accepted packet ends any rejected run.
        candSteps_ = 0;
        haveEpoch_ = true;
        tow_ = tow;
        hostMs_ = hostMs;
        if (utcValid) {
            haveUtc_ = true;
            utcMs_ = utcMs;
            utcHostMs_ = hostMs;
        }
    }

    bool haveEpoch_ = false;
    bool haveUtc_ = false;
    uint32_t tow_ = 0;
    uint32_t hostMs_ = 0;
    uint32_t deltaMs_ = 0;
    uint32_t hostDeltaMs_ = 0;
    int64_t utcMs_ = 0;
    uint32_t utcHostMs_ = 0;
    bool haveCandidate_ = false;
    bool candUtcValid_ = false;
    uint8_t candSteps_ = 0;
    uint32_t candTow_ = 0;
    uint32_t candHostMs_ = 0;
    int64_t candUtcMs_ = 0;
};

// Four off-rate intervals in the last eight are persistent, one delayed/missed
// message is not. iTOW spacing is measured, not the arrival time of an I2C burst.
class RateMonitor {
public:
    constexpr void observe(uint32_t deltaMs, uint32_t hostDeltaMs)
    {
        history_ = uint8_t((history_ << 1) | (deltaMs != PERIOD_MS));
        if (intervals_ < 8) ++intervals_;
        if (deltaMs == PERIOD_MS) {
            if (consecutiveGood_ < 3) ++consecutiveGood_;
        } else {
            consecutiveGood_ = 0;
        }
        hostWindowMs_ += hostDeltaMs;
        if (++hostIntervals_ == 8) {
            // Two disjoint windows prevent a single delayed packet from
            // appearing as several consecutive failing sliding windows.
            if (hostWindowMs_ < 1100 || hostWindowMs_ > 2100) {
                if (badHostWindows_ < 2) ++badHostWindows_;
            } else {
                badHostWindows_ = 0;
            }
            hostIntervals_ = 0;
            hostWindowMs_ = 0;
        }
    }
    constexpr bool ready() const { return consecutiveGood_ >= 3; }
    constexpr bool faulty() const
    {
        unsigned bad = 0;
        for (uint8_t bits = history_; bits; bits >>= 1) bad += bits & 1;
        return (intervals_ >= 8 && bad >= 4) || badHostWindows_ >= 2;
    }
private:
    uint8_t history_ = 0;
    uint8_t intervals_ = 0;
    uint8_t consecutiveGood_ = 0;
    uint8_t hostIntervals_ = 0;
    uint8_t badHostWindows_ = 0;
    uint64_t hostWindowMs_ = 0;
};
} // namespace gnss_policy
#endif
