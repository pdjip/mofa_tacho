/** @file display_dma_contract.h
 * Checked synchronous DMA lifetime policy. Failed abort or persistent START quarantines the project-owned source until reboot; LVGL never lends its own buffer to DMA.
 *
 * Pure injectable hardware policy used by display_rotation.cpp and native fault tests. Ops supplies inactive/start/poll/abort observations; Ownership retains permanent quarantine. Exclusive peripheral use and honest inactivity evidence are caller/hardware preconditions.
 */
#ifndef MOFA_DISPLAY_DMA_CONTRACT_H
#define MOFA_DISPLAY_DMA_CONTRACT_H
namespace display_dma {
enum class Result { Complete, Timeout, Error, Pinned };
// All DMA sources are project-owned. No LVGL buffer is ever lent to DMA.
// A failed abort permanently quarantines the source until reboot; no polling
// loop, realloc, pixel modification, or new DMA is allowed in that state.
struct Ownership {
    bool pinned = false;
    /** Require a project-owned source stable throughout start/poll/abort. Complete permits reuse only with inactive hardware; uncertain abort/START pins the owner until reboot. A pinned owner never starts another transfer. */
    template<class Ops> Result transfer(Ops &ops) {
        if (pinned) return Result::Pinned;
        if (!ops.inactive()) { pinned = true; return Result::Pinned; }
        Result result = ops.start();
        if (result == Result::Complete) result = ops.poll();
        if (result == Result::Complete && ops.inactive()) return result;
        const bool aborted = ops.abort();
        if (!aborted || !ops.inactive()) { pinned = true; return Result::Pinned; }
        return result == Result::Complete ? Result::Error : result;
    }
};
}
#endif
