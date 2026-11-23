// Compile-only policy tests: constexpr executes the actual firmware policy at
// compile time, independently of Arduino, I2C, the scheduler and hardware.
#include "../../src/gnss_epoch.h"

using namespace gnss_policy;

constexpr bool ordering()
{
    EpochTracker t;
    return t.observe(1000, 0, false, 0) == EpochResult::Baseline &&
        t.observe(1200, 200, false, 0) == EpochResult::New &&
        t.observe(1200, 300, false, 0) == EpochResult::Duplicate &&
        t.observe(1000, 400, false, 0) == EpochResult::Old &&
        t.observe(1400, 450, false, 0) == EpochResult::New;
}
constexpr bool weekAndHostWrap()
{
    EpochTracker t;
    return t.observe(WEEK_MS - 200, 0xffffff80, false, 0) == EpochResult::Baseline &&
        t.observe(0, 72, false, 0) == EpochResult::New && t.deltaMs() == 200 &&
        t.observe(WEEK_MS - 200, 172, false, 0) == EpochResult::Old;
}
constexpr bool staleAndRestart()
{
    EpochTracker t;
    return t.observe(10000, 0, true, 100000) == EpochResult::Baseline &&
        t.observe(10200, 200, true, 100200) == EpochResult::New &&
        t.observe(10400, 1600, true, 100400) == EpochResult::Old &&
        t.observe(0, 1800, false, 0) == EpochResult::Old &&
        t.observe(12000, 2000, true, 102000) == EpochResult::New &&
        t.observe(12200, 2200, true, 100000) == EpochResult::Old;
}
constexpr bool longOutage()
{
    EpochTracker t;
    return t.observe(1000, 0, true, 100000) == EpochResult::Baseline &&
        t.observe(1000, WEEK_MS, true, 100000LL + WEEK_MS) == EpochResult::New;
}
constexpr bool rateChecks()
{
    RateMonitor normal, delayed, oneHz, alternating, slowArrival, delayedArrival;
    for (unsigned i = 0; i < 16; ++i) {
        normal.observe(200, 200);
        delayed.observe(i == 4 ? 600 : 200, i == 4 ? 600 : 200);
        oneHz.observe(1000, 1000);
        alternating.observe(i % 2 ? 200 : 400, i % 2 ? 200 : 400);
        slowArrival.observe(200, 400);
        delayedArrival.observe(200, i == 4 ? 1300 : 200);
    }
    return normal.ready() && !normal.faulty() && delayed.ready() && !delayed.faulty() &&
        oneHz.faulty() && alternating.faulty() && slowArrival.faulty() && !delayedArrival.faulty();
}
constexpr bool utcAcquisition()
{
    EpochTracker t;
    return t.observe(0, 0, false, 0) == EpochResult::Baseline &&
        t.observe(100000000, 200, true, 1000000000) == EpochResult::Baseline &&
        t.observe(100000200, 400, true, 1000000200) == EpochResult::New &&
        t.observe(100000200, 500, true, 1000000200) == EpochResult::Duplicate &&
        t.observe(0, 600, false, 0) == EpochResult::Old &&
        t.observe(WEEK_MS, 700, true, 1000000400) == EpochResult::Invalid &&
        t.observe(100000400, 600, true, 1000000400) == EpochResult::New;
}
// One packet stamped 700 ms late (thread/I2C stall) is accepted and shifts the
// host reference; the on-time stream after it must not be rejected forever.
constexpr bool lateStampRecovers()
{
    EpochTracker t;
    bool ok = t.observe(1000, 0, false, 0) == EpochResult::Baseline &&
        t.observe(1200, 200, false, 0) == EpochResult::New &&
        t.observe(1400, 1100, false, 0) == EpochResult::New;
    for (uint32_t i = 0; i < 3; ++i)
        ok = ok && t.observe(2200 + 200 * i, 1200 + 200 * i, false, 0) == EpochResult::Old;
    return ok && t.observe(2800, 1800, false, 0) == EpochResult::Baseline &&
        t.observe(3000, 2000, false, 0) == EpochResult::New && t.deltaMs() == 200;
}
// Cold start: provisional iTOW, then the receiver jumps to real GPS time long
// before UTC is fully resolved (leap seconds can take 12.5 minutes).
constexpr bool coldStartTimeJump()
{
    EpochTracker t;
    bool ok = t.observe(0, 0, false, 0) == EpochResult::Baseline &&
        t.observe(200, 200, false, 0) == EpochResult::New &&
        t.observe(400, 400, false, 0) == EpochResult::New;
    for (uint32_t i = 0; i < 3; ++i)
        ok = ok && t.observe(300000000 + 200 * i, 600 + 200 * i, false, 0) == EpochResult::Old;
    return ok && t.observe(300000600, 1200, false, 0) == EpochResult::Baseline &&
        t.observe(300000800, 1400, false, 0) == EpochResult::New &&
        t.observe(300001000, 1600, true, 1000000000) == EpochResult::Baseline &&
        t.observe(300001200, 1800, true, 1000000200) == EpochResult::New;
}
// Receiver unplugged for 4 h while the host clock runs 50 ppm slow (~0.7 s).
// A duplicate inside the confirming run must not restart it.
constexpr bool driftAfterOutage()
{
    constexpr uint32_t H = 14400000, DRIFT = 700;
    EpochTracker t;
    bool ok = t.observe(1000, 0, true, 100000) == EpochResult::Baseline &&
        t.observe(1200, 200, true, 100200) == EpochResult::New;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t tow = 1200 + H + DRIFT + 200 * i;
        ok = ok && t.observe(tow, 200 + H + 200 * i, true, 100200 + H + DRIFT + 200 * i) == EpochResult::Old;
        if (i == 1)
            ok = ok && t.observe(tow, 210 + H + 200 * i, true, 100200 + H + DRIFT + 200 * i) == EpochResult::Duplicate;
    }
    return ok && t.observe(1800 + H + DRIFT, 800 + H, true, 100800 + H + DRIFT) == EpochResult::Baseline &&
        t.observe(2000 + H + DRIFT, 1000 + H, true, 101000 + H + DRIFT) == EpochResult::New;
}
// A burst of buffered older epochs is not a live stream and must not re-anchor.
constexpr bool burstStaysRejected()
{
    EpochTracker t;
    bool ok = t.observe(1000, 0, false, 0) == EpochResult::Baseline &&
        t.observe(1200, 200, false, 0) == EpochResult::New &&
        t.observe(1400, 400, false, 0) == EpochResult::New;
    for (uint32_t i = 0; i < 6; ++i)
        ok = ok && t.observe(200 + 200 * i, 410 + 10 * i, false, 0) == EpochResult::Old;
    return ok && t.observe(1600, 600, false, 0) == EpochResult::New && t.deltaMs() == 200;
}
static_assert(ordering(), "Duplicates and backwards epochs must not advance the tracker");
static_assert(lateStampRecovers(), "A late-stamped packet must not reject the on-time stream forever");
static_assert(coldStartTimeJump(), "A receiver time jump before UTC must re-anchor on the live stream");
static_assert(driftAfterOutage(), "Host clock drift over a long outage must re-anchor on the live stream");
static_assert(burstStaysRejected(), "A burst of buffered old epochs must stay rejected");
static_assert(weekAndHostWrap(), "GPS week and millis wrap must preserve epoch ordering");
static_assert(staleAndRestart(), "Stale packets and receiver-reset epochs must be rejected");
static_assert(longOutage(), "UTC must distinguish a new week from a duplicate epoch");
static_assert(rateChecks(), "Rate monitoring must tolerate isolated delays and detect persistent faults");
static_assert(utcAcquisition(), "Acquiring UTC needs a new baseline; old restart data must stay rejected");
