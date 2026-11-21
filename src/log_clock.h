#ifndef MOFA_LOG_CLOCK_H
#define MOFA_LOG_CLOCK_H

#include <stdint.h>

// Timestamps for the USB log files. Pure policy, independent of Arduino and
// the RTC.
//
// Seconds from the RTC combined with milliseconds from millis() % 1000 are two
// unrelated clocks, so a row could carry an earlier time than the row before it.
// This is one clock: the first GNSS UTC sample anchors it, and from then on
// log time = anchor + elapsed host time, so rows are strictly monotonic. It runs
// at the host crystal rate, like millis(), and is not re-synchronised, so
// reconnects of the receiver can never step it.
namespace log_clock {

class Clock {
public:
    constexpr bool synchronized() const { return synchronized_; }

    // Offer a GNSS sample: UTC in milliseconds at host time hostMs. Only the
    // first sample is used.
    constexpr void sync(int64_t utcMs, uint32_t hostMs)
    {
        if (synchronized_) return;
        synchronized_ = true;
        timeMs_ = utcMs;
        hostMs_ = hostMs;
    }

    // Log time in milliseconds for the host time hostMs. Before the first
    // sample this is the uptime, i.e. 1970 + uptime, an explicit placeholder
    // that is not trusted UTC. hostMs must not go backwards between calls and
    // must be passed at least once per 49 days (millis() is 32-bit; the elapsed
    // time is accumulated, so the wrap is harmless).
    constexpr int64_t timeMs(uint32_t hostMs)
    {
        if (!synchronized_) return hostMs;
        timeMs_ += static_cast<uint32_t>(hostMs - hostMs_);
        hostMs_ = hostMs;
        return timeMs_;
    }

private:
    bool synchronized_ = false;
    int64_t timeMs_ = 0;
    uint32_t hostMs_ = 0;
};

} // namespace log_clock

#endif
