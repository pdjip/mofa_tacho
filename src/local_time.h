/** @file local_time.h
 * Pure CET/CEST display conversion using the encoded EU transition rule. Stored timestamps remain UTC; future rule changes require a deliberate source update.
 *
 * Pure integer calendar helpers consumed by vars.cpp. Inputs are application-range UTC seconds; output is a local day/time offset. No RTC update, timezone database, allocation or thread state is involved.
 */
#ifndef MOFA_LOCAL_TIME_H
#define MOFA_LOCAL_TIME_H

#include <stdint.h>

// Wall-clock time for the dashboard: UTC plus the Central European offset
// (CET = UTC+1, CEST = UTC+2). Pure policy, independent of Arduino and LVGL.
//
// EU rule: summer time starts at 01:00 UTC on the last Sunday of March and ends
// at 01:00 UTC on the last Sunday of October. Arithmetic is 64-bit so it stays
// correct beyond the 32-bit time_t limit of 2038.
namespace local_time {

constexpr int64_t SECONDS_PER_DAY = 86400;

// Days since 1970-01-01 of a proleptic Gregorian calendar date (Hinnant).
constexpr int64_t daysFromCivil(int64_t year, int month, int day)
{
    const int64_t y = month <= 2 ? year - 1 : year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yearOfEra = y - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

// Calendar year of a day number (inverse of daysFromCivil, year part only).
constexpr int64_t yearFromDays(int64_t days)
{
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t dayOfEra = z - era * 146097;
    const int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t mp = (5 * dayOfYear + 2) / 153;
    const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    return yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
}

// Day number of the last Sunday of a 31-day month (March or October).
constexpr int64_t lastSundayDays(int64_t year, int month)
{
    const int64_t lastDay = daysFromCivil(year, month, 31);
    const int64_t weekday = (lastDay % 7 + 11) % 7; // 0 = Sunday; 1970-01-01 was a Thursday.
    return lastDay - weekday;
}

/** Return the encoded CET/CEST offset for UTC seconds using EU last-Sunday transitions. Intended for the application calendar range, not arbitrary int64 extremes or future political rule changes. */
constexpr int utcOffsetHours(int64_t utcSeconds)
{
    const int64_t days = utcSeconds >= 0 ? utcSeconds / SECONDS_PER_DAY
                                         : -((-utcSeconds + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY);
    const int64_t year = yearFromDays(days);
    const int64_t summerStart = lastSundayDays(year, 3) * SECONDS_PER_DAY + 3600;
    const int64_t summerEnd = lastSundayDays(year, 10) * SECONDS_PER_DAY + 3600;
    return utcSeconds >= summerStart && utcSeconds < summerEnd ? 2 : 1;
}

// Seconds since local midnight, 0..86399.
constexpr int32_t localSecondOfDay(int64_t utcSeconds)
{
    const int64_t local = utcSeconds + utcOffsetHours(utcSeconds) * 3600;
    return static_cast<int32_t>(((local % SECONDS_PER_DAY) + SECONDS_PER_DAY) % SECONDS_PER_DAY);
}

} // namespace local_time

#endif
