#ifndef MOFA_HEAP_DIAG_H
#define MOFA_HEAP_DIAG_H

#include <stddef.h>
#include <stdint.h>

namespace heap_diag {
enum class Point : uint8_t { Initialized, BeforeOpen, AfterClose, BeforeMount,
    AfterMount, AfterUnmount, FirstRound, IoError, Enomem, OpenFailed, AfterErrorCleanup };
struct Stats {
    uint32_t current = 0, peak = 0, allocations = 0, failures = 0, overhead = 0, reserved = 0;
};
struct Token { uint32_t id = 0, current = 0, allocations = 0; };
struct Snapshot {
    Stats stats;
    uint32_t id = 0, ms = 0;
    Point point = Point::Initialized;
    char path[32] = {};
    int error = 0;
    int64_t delta = 0, allocationDelta = 0;
};
constexpr int64_t difference(uint32_t after, uint32_t before) {
    return int64_t(after) - int64_t(before);
}
template<class T, size_t N>
class History {
public:
    constexpr void push(const T &value) {
        items_[next_] = value;
        next_ = (next_ + 1) % N;
        if (count_ < N) ++count_;
    }
    constexpr size_t size() const { return count_; }
    constexpr const T &at(size_t index) const {
        return items_[(next_ + N - count_ + index) % N];
    }
private:
    T items_[N] = {};
    size_t next_ = 0, count_ = 0;
};

void initialize();
Token beforeOpen(const char *path);
void afterClose(Token token, const char *path);
void openFailed(Token token, const char *path, int error);
Token beforeMount();
void afterMount(Token token, bool success);
void afterUnmount(bool success);
void ioError(const char *path, int error, Token token = {});
void operationFinished(bool success);
void startRound();
void finishRound(bool ready);
void reportPending(); // Logger only, after cleanup and outside all library locks.
} // namespace heap_diag
#endif
