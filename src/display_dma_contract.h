#ifndef MOFA_DISPLAY_DMA_CONTRACT_H
#define MOFA_DISPLAY_DMA_CONTRACT_H
namespace display_dma {
enum class Result { Complete, Timeout, Error, Pinned };
// All DMA sources are project-owned. No LVGL buffer is ever lent to DMA.
// A failed abort permanently quarantines the source until reboot; no polling
// loop, realloc, pixel modification, or new DMA is allowed in that state.
struct Ownership {
    bool pinned = false;
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
