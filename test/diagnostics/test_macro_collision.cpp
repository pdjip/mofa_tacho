// Mimic generic names exported by unrelated board/library headers.
#define DISPLAY 0x1
#define GNSS 0x2
#define RPM 0x3
#define USB 0x4
#define LOGGER 0x5
#define MEMORY 0x6
#define RTOS 0x7
#define MOFA_DEBUG_LEVEL 1
#define MOFA_DEBUG_DISPLAY 3
#define MOFA_DEBUG_USB 0
#define MOFA_DEBUG_RTOS 4
#include "../../src/diagnostics_config.h"
static_assert(DISPLAY == 0x1 && GNSS == 0x2 && RPM == 0x3 && USB == 0x4 &&
    LOGGER == 0x5 && MEMORY == 0x6 && RTOS == 0x7, "foreign macros remain untouched");
static_assert(MOFA_DIAG_ENABLED(MOFA_SUBSYS_DISPLAY, 3), "display override survives collision");
static_assert(!MOFA_DIAG_ENABLED(MOFA_SUBSYS_USB, 1), "disabled override survives collision");
constexpr unsigned expansionProof() {
    unsigned calls = 0;
    MOFA_DIAG(MOFA_SUBSYS_DISPLAY, 3, ++calls);
    MOFA_DIAG(MOFA_SUBSYS_DISPLAY, 4, calls += 100);
    MOFA_DIAG(MOFA_SUBSYS_USB, 1, calls += 100);
    MOFA_DIAG(MOFA_SUBSYS_GNSS, 1, ++calls);
    MOFA_DIAG(MOFA_SUBSYS_RPM, 1, ++calls);
    MOFA_DIAG(MOFA_SUBSYS_LOGGER, 1, ++calls);
    MOFA_DIAG(MOFA_SUBSYS_MEMORY, 1, ++calls);
    MOFA_DIAG(MOFA_SUBSYS_RTOS, 4, ++calls);
    return calls;
}
static_assert(expansionProof() == 6, "compile-time filtering and disabled argument elimination");
int main() { return expansionProof() == 6 ? 0 : 1; }
