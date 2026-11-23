// Native test of the same configuration and accounting policies as firmware.
#include "../../src/diagnostics_config.h"
#include "../../src/usb_io_diag.h"
#include "../../src/gnss_log_fifo.h"
#include <stdio.h>
#ifndef EXPECT_LEVEL
#define EXPECT_LEVEL 0
#endif
#ifndef EXPECT_USB
#define EXPECT_USB EXPECT_LEVEL
#endif
#ifndef EXPECT_GNSS
#define EXPECT_GNSS EXPECT_LEVEL
#endif
static_assert(MOFA_DEBUG_LEVEL == EXPECT_LEVEL, "global level selection");
static_assert(MOFA_DEBUG_USB == EXPECT_USB, "USB override replaces global");
static_assert(MOFA_DEBUG_GNSS == EXPECT_GNSS, "GNSS override replaces global");
static_assert(MOFA_DEBUG_RPM == EXPECT_LEVEL && MOFA_DEBUG_DISPLAY == EXPECT_LEVEL &&
    MOFA_DEBUG_MEMORY == EXPECT_LEVEL && MOFA_DEBUG_LOGGER == EXPECT_LEVEL &&
    MOFA_DEBUG_RTOS == EXPECT_LEVEL, "subsystem defaults inherit global");
int main() {
    unsigned calls = 0;
    MOFA_DIAG(MOFA_SUBSYS_USB, 3, ++calls);
    if (calls != (EXPECT_USB >= 3 ? 1u : 0u)) return 1;
    MOFA_DIAG(MOFA_SUBSYS_GNSS, 4, ++calls);
    if (calls != (EXPECT_USB >= 3 ? 1u : 0u) + (EXPECT_GNSS >= 4 ? 1u : 0u)) return 2;
    // Fault collection belongs outside the serial gate, including across wrap.
    usb_io_diag::Repeats repeats;
    usb_io_diag::Event event;
    uint32_t count = 0;
    if (!repeats.emit(event, UINT32_MAX - 10, count)) return 3;
    if (repeats.emit(event, 0, count) || count != 2 || repeats.suppressed != 1 || repeats.classSuppressed != 1) return 4;
    MOFA_DIAG(MOFA_SUBSYS_USB, 3, ++calls);
    if (repeats.emit(event, 1, count) || count != 3 || repeats.suppressed != 2 || repeats.classSuppressed != 2) return 5;
    gnss_log::Fifo fifo;
    GnssLogRecord record;
    for (unsigned n = 0; n < gnss_log::CAPACITY + 1; ++n) fifo.push(record);
    const auto stats = fifo.stats();
    if (stats.accepted != stats.written + stats.queued + stats.overflow + stats.uncertain || stats.overflow != 1) return 6;
    puts("PASS diagnostic configuration, disabled arguments, filtering and accounting");
}
