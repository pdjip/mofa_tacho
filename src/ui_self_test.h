#ifndef MOFA_UI_SELF_TEST_H
#define MOFA_UI_SELF_TEST_H

#include <stdint.h>

// Startup display test, like the instrument-cluster sweep in older cars: every
// UI element is driven through its full range so a dead segment, a missing
// glyph or a broken widget is visible before the first real measurement.
// Pure policy, independent of Arduino and LVGL; applied by set_ui_self_test().
namespace ui_self_test {

constexpr uint32_t DURATION_MS = 3000;
constexpr int32_t RPM_MAX = 20000;
constexpr int32_t SPEED_MAX_KPH = 199;
constexpr float TEMPERATURE_MIN_C = -40.0f; // Below zero so the '-' glyph is shown.
constexpr float TEMPERATURE_MAX_C = 120.0f;
constexpr uint8_t GPS_STEPS = 10; // One step per GNSS status text, in this order.

enum class GpsStep : uint8_t {
    Searching, Configuring, NoFix, Fix2D, Fix3D, Stale, RetryWait, ConfigError, RateError, ThreadError
};

struct Frame {
    int32_t rpm;
    int32_t speedKph;
    float temperatureC;
    bool rpmWarning;
    GpsStep gps;
};

constexpr bool active(uint32_t elapsedMs)
{
    return elapsedMs < DURATION_MS;
}

// Triangle sweep: 0 -> full scale in the first half, back to 0 in the second.
constexpr Frame frame(uint32_t elapsedMs)
{
    const uint32_t t = elapsedMs < DURATION_MS ? elapsedMs : DURATION_MS;
    const uint32_t half = DURATION_MS / 2;
    const uint32_t up = t < half ? t : DURATION_MS - t;
    const float progress = float(up) / float(half);
    return Frame{
        static_cast<int32_t>(progress * float(RPM_MAX)),
        static_cast<int32_t>(progress * float(SPEED_MAX_KPH)),
        TEMPERATURE_MIN_C + progress * (TEMPERATURE_MAX_C - TEMPERATURE_MIN_C),
        t >= 1000 && t < 2000, // Warning icon visible around the peak.
        static_cast<GpsStep>(t * GPS_STEPS / (DURATION_MS + 1)),
    };
}

} // namespace ui_self_test

#endif
