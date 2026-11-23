/** @file diagnostics_config.h
 * Compile-time serial policy. Never gate acquisition, counters or USB file I/O.
 * Levels: 0 RELEASE, 1 ERROR, 2 INFO, 3 DEBUG, 4 TRACE.
 * Overrides replace the global level for that subsystem, including override 0.
 * MOFA_DIAG does not evaluate a disabled statement or its arguments. Constant
 * branches are removed by the release optimizer; binary checks verify this.
 * Call only from thread context, after cleanup and outside application locks.
 *
 * Header-only build policy, not a serial transport. Inputs are compiler definitions; outputs are constant statement gates used only from thread context. Selectors are project-prefixed numeric levels to avoid foreign macro collisions. Serial blocking and errno semantics remain properties of each call site.
 */
#ifndef MOFA_DIAGNOSTICS_CONFIG_H
#define MOFA_DIAGNOSTICS_CONFIG_H
#ifndef MOFA_DEBUG_LEVEL
#define MOFA_DEBUG_LEVEL 0
#endif
#ifndef MOFA_DEBUG_GNSS
#define MOFA_DEBUG_GNSS MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_RPM
#define MOFA_DEBUG_RPM MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_DISPLAY
#define MOFA_DEBUG_DISPLAY MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_USB
#define MOFA_DEBUG_USB MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_LOGGER
#define MOFA_DEBUG_LOGGER MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_MEMORY
#define MOFA_DEBUG_MEMORY MOFA_DEBUG_LEVEL
#endif
#ifndef MOFA_DEBUG_RTOS
#define MOFA_DEBUG_RTOS MOFA_DEBUG_LEVEL
#endif

#if (MOFA_DEBUG_LEVEL < 0 || MOFA_DEBUG_LEVEL > 4) || (MOFA_DEBUG_GNSS < 0 || MOFA_DEBUG_GNSS > 4) || (MOFA_DEBUG_RPM < 0 || MOFA_DEBUG_RPM > 4) || (MOFA_DEBUG_DISPLAY < 0 || MOFA_DEBUG_DISPLAY > 4) || (MOFA_DEBUG_USB < 0 || MOFA_DEBUG_USB > 4) || (MOFA_DEBUG_LOGGER < 0 || MOFA_DEBUG_LOGGER > 4) || (MOFA_DEBUG_MEMORY < 0 || MOFA_DEBUG_MEMORY > 4) || (MOFA_DEBUG_RTOS < 0 || MOFA_DEBUG_RTOS > 4)
#error "MOFA diagnostic levels must be in 0..4"
#endif
// Project-reserved subsystem selectors expand directly to numeric levels.
// Do not concatenate forwarded arguments: Arduino headers may define DISPLAY,
// USB or other generic subsystem words as macros before this header is used.
#define MOFA_SUBSYS_GNSS MOFA_DEBUG_GNSS
#define MOFA_SUBSYS_RPM MOFA_DEBUG_RPM
#define MOFA_SUBSYS_DISPLAY MOFA_DEBUG_DISPLAY
#define MOFA_SUBSYS_USB MOFA_DEBUG_USB
#define MOFA_SUBSYS_LOGGER MOFA_DEBUG_LOGGER
#define MOFA_SUBSYS_MEMORY MOFA_DEBUG_MEMORY
#define MOFA_SUBSYS_RTOS MOFA_DEBUG_RTOS
#define MOFA_DIAG_ENABLED(subsystem, level) ((subsystem) >= (level))
#define MOFA_DIAG(subsystem, level, ...) \
    do { if (MOFA_DIAG_ENABLED(subsystem, level)) { __VA_ARGS__; } } while (0)
#endif
