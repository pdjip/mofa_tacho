// Compile-only policy tests: constexpr executes the actual firmware policy at
// compile time, independently of Arduino, LVGL and hardware.
// Reference timestamps were generated with Python's datetime (UTC).
#include "../../src/local_time.h"

using namespace local_time;

// EU rule: summer time (UTC+2) from 01:00 UTC on the last Sunday of March until
// 01:00 UTC on the last Sunday of October; otherwise winter time (UTC+1).
static_assert(utcOffsetHours(1774746000 - 1) == 1, "2026: last second of winter time");
static_assert(utcOffsetHours(1774746000) == 2, "2026-03-29 01:00 UTC switches to summer time");
static_assert(utcOffsetHours(1792890000 - 1) == 2, "2026: last second of summer time");
static_assert(utcOffsetHours(1792890000) == 1, "2026-10-25 01:00 UTC switches to winter time");

static_assert(utcOffsetHours(1795262400) == 1, "2026-11-21 12:00 UTC is winter time");
static_assert(utcOffsetHours(1782864000) == 2, "2026-07-01 is summer time");
static_assert(utcOffsetHours(1767225600) == 1, "2026-01-01 is winter time");
static_assert(utcOffsetHours(946684800) == 1, "2000-01-01 (start of RTC range) is winter time");

// Last Sunday falls on different dates; 2027-03-28 and 2030-03-31 (31st) are the edges.
static_assert(utcOffsetHours(1806195600 - 1) == 1 && utcOffsetHours(1806195600) == 2, "2027 start");
static_assert(utcOffsetHours(1824944400 - 1) == 2 && utcOffsetHours(1824944400) == 1, "2027 end (Oct 31)");
static_assert(utcOffsetHours(1901149200 - 1) == 1 && utcOffsetHours(1901149200) == 2, "2030 start (Mar 31)");
static_assert(utcOffsetHours(4078429200LL - 1) == 1 && utcOffsetHours(4078429200LL) == 2, "2099 start, beyond 32-bit time_t");

// Clock face = UTC plus offset, including the day rollover.
static_assert(localSecondOfDay(1795262400) == 13 * 3600, "12:00 UTC in winter shows 13:00");
static_assert(localSecondOfDay(1782864000) == 2 * 3600, "00:00 UTC in summer shows 02:00");
static_assert(localSecondOfDay(1798759800) == 30 * 60, "23:30 UTC in winter wraps to 00:30");
static_assert(localSecondOfDay(1782862200) == 1 * 3600 + 30 * 60, "23:30 UTC in summer wraps to 01:30");
static_assert(localSecondOfDay(1709208000) == 13 * 3600, "leap day 2024-02-29 12:00 UTC is winter time");
