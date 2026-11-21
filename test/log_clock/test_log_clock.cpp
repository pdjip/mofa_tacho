// Compile-only policy tests: constexpr executes the actual firmware policy at
// compile time, independently of Arduino, the RTC and hardware.
#include "../../src/log_clock.h"

using namespace log_clock;

constexpr int64_t UTC_2026 = 1795262400000LL; // 2026-11-21 12:00:00.000 UTC

constexpr bool beforeSync()
{
    Clock c;
    return !c.synchronized() && c.timeMs(0) == 0 && c.timeMs(1234) == 1234 && c.timeMs(86400000) == 86400000;
}
// The review's failing case: RTC seconds that tick at millis ending in .500 made
// rows go 01.600 -> 01.000. One clock keeps exact spacing whatever the phase.
constexpr bool monotonicAcrossSecondEdges()
{
    Clock c;
    c.sync(UTC_2026 + 600, 10600);
    bool ok = c.synchronized() && c.timeMs(10600) == UTC_2026 + 600;
    int64_t last = c.timeMs(10600);
    for (uint32_t host = 10700; host <= 14000; host += 100) {
        const int64_t now = c.timeMs(host);
        ok = ok && now == last + 100 && now == UTC_2026 + 600 + (host - 10600);
        last = now;
    }
    return ok;
}
// The sample may be older than the first query (snapshot age): add the age.
constexpr bool sampleAge()
{
    Clock c;
    c.sync(UTC_2026, 5000);
    return c.timeMs(5400) == UTC_2026 + 400;
}
// Only the first GNSS sample anchors the clock; later samples must not step it.
constexpr bool firstSampleOnly()
{
    Clock c;
    c.sync(UTC_2026, 1000);
    c.sync(UTC_2026 + 50000, 2000);
    return c.timeMs(2000) == UTC_2026 + 1000;
}
// millis() wraps after 49.7 days; the clock must run straight through it.
constexpr bool hostWrap()
{
    Clock c;
    c.sync(UTC_2026, 0xFFFFFF00u);
    return c.timeMs(0xFFFFFF00u) == UTC_2026 && c.timeMs(0xFFFFFFFFu) == UTC_2026 + 255 &&
        c.timeMs(0x100u) == UTC_2026 + 512 && c.timeMs(0x1000u) == UTC_2026 + 0x1100;
}
// Whole seconds come from the same millisecond value, so they roll over exactly
// when the milliseconds wrap past 999 (no separate second counter to disagree).
constexpr bool seconds()
{
    Clock c;
    c.sync(UTC_2026 + 999, 777);
    return c.timeMs(777) / 1000 == UTC_2026 / 1000 && c.timeMs(778) / 1000 == UTC_2026 / 1000 + 1;
}
static_assert(beforeSync(), "Before GNSS time the clock is the uptime placeholder");
static_assert(monotonicAcrossSecondEdges(), "Log time must advance exactly with host time after sync");
static_assert(sampleAge(), "Host time elapsed since the sample must be added");
static_assert(firstSampleOnly(), "Only the first sample anchors the clock");
static_assert(hostWrap(), "The clock must survive the millis() wrap");
static_assert(seconds(), "Whole seconds follow the anchor's millisecond phase");
