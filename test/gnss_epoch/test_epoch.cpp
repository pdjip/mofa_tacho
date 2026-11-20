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
static_assert(ordering(), "Duplicates and backwards epochs must not advance the tracker");
static_assert(weekAndHostWrap(), "GPS week and millis wrap must preserve epoch ordering");
static_assert(staleAndRestart(), "Stale packets and receiver-reset epochs must be rejected");
static_assert(longOutage(), "UTC must distinguish a new week from a duplicate epoch");
static_assert(rateChecks(), "Rate monitoring must tolerate isolated delays and detect persistent faults");
static_assert(utcAcquisition(), "Acquiring UTC needs a new baseline; old restart data must stay rejected");
