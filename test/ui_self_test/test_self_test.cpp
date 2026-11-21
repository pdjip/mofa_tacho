// Compile-only policy tests: constexpr executes the actual firmware policy at
// compile time, independently of Arduino, LVGL and hardware.
#include "../../src/ui_self_test.h"

using namespace ui_self_test;

static_assert(frame(0).rpm == 0 && frame(0).speedKph == 0, "starts at zero");
static_assert(frame(1500).rpm == RPM_MAX && frame(1500).speedKph == SPEED_MAX_KPH, "reaches full scale");
static_assert(frame(DURATION_MS).rpm == 0, "returns to zero");
static_assert(frame(1500).temperatureC == TEMPERATURE_MAX_C, "temperature reaches maximum");
static_assert(frame(0).temperatureC == TEMPERATURE_MIN_C && TEMPERATURE_MIN_C < 0.0f,
              "temperature starts below zero, which shows the minus glyph");
static_assert(frame(DURATION_MS).temperatureC == TEMPERATURE_MIN_C, "temperature returns to minimum");
static_assert(!frame(500).rpmWarning && frame(1500).rpmWarning && !frame(2500).rpmWarning, "warning around peak");
static_assert(frame(0).gps == GpsStep::Searching && frame(DURATION_MS - 1).gps == GpsStep::ThreadError, "all GPS states visited");
static_assert(static_cast<uint8_t>(frame(DURATION_MS).gps) < GPS_STEPS, "GPS step in range");
static_assert(active(DURATION_MS - 1) && !active(DURATION_MS), "three second window");
