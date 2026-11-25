// Only the lowest group uses the nonzero-prefix model.
using DeadlineGateCount = uint16_t;
using DeadlineHistory = uint16_t;

static constexpr int deadline_rank_rows(int bits) {
    return 16 + 20 * ((bits + 1) / 2);
}

static inline int rank_context_row(int plane, bool prefix, int64_t neighbor, int64_t temporal) {
    const int64_t shifted_temporal = temporal >> (2 * plane);
    const int nearby = int((neighbor >> (2 * plane)) & 3);
    const int low = int(shifted_temporal & 3);
    return prefix ? 4 * nearby + low : 16 + 20 * plane + 4 * min64(4, shifted_temporal) + nearby;
}

#include "low_memory_context.inc"
