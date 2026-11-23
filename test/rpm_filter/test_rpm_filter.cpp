// Compile-only policy tests, see test/README.
#include "../../src/rpm_display_filter.h"

constexpr int32_t settle(float rpm)
{
    RpmDisplayFilter f;
    int32_t shown = 0;
    for (uint32_t t = 0; t < 20000; t += 10) shown = f.update(rpm, t);
    return shown;
}

static_assert(settle(3000.0f) == 3000, "integer input is shown exactly");
static_assert(settle(3000.4f) == 3000, "rounds down");
static_assert(settle(3000.7f) == 3001, "steady value is not left one RPM off");
static_assert(settle(0.0f) == 0, "zero stays zero");
