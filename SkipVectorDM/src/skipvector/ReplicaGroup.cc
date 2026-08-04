#include "skipvector/ReplicaGroup.h"
#include <cassert>
#include <cstdio>

namespace sv {

ReplicaGroup::ReplicaGroup(DMVerbs* dmv, int num_replicas)
    : dmv_(dmv), num_replicas_(num_replicas), drop_mask_(0) {
    assert(num_replicas > 0 && num_replicas <= 32);
}

// Poll the CQ until either (a) `quorum` successes have arrived, or (b) all
// `posted` operations have completed. Returns the bitmask of replicas that
// completed successfully.
uint32_t ReplicaGroup::wait_quorum_(int posted, int quorum) {
    uint32_t success_mask = 0;
    int successes = 0, completions = 0;

    while (completions < posted) {
        uint64_t wr_id = 0;
        if (dmv_->poll_rdma_cq_once(wr_id)) {
            assert(wr_id < 32 && "wr_id must be a valid replica index");
            success_mask |= (1u << wr_id);
            ++successes;
            ++completions;
        }
        // else: pollOnce returned 0 (nothing yet); loop and try again.
        // We do NOT increment `completions` -- the verb is still in flight.
    }
    // At exit: `completions == posted`, so all N verbs have completed.
    // `success_mask` reflects which ones succeeded (IBV_WC_SUCCESS).
    return success_mask;
}

bool ReplicaGroup::read_all(uint64_t offset, char** bufs, size_t size,
                            int quorum, uint32_t* success_mask) {
    int posted = 0;
    for (int i = 0; i < num_replicas_; ++i) {
        if (drop_mask_ & (1u << i)) continue;
        Gaddr addr(static_cast<uint16_t>(i), offset);
        dmv_->read_tagged(bufs[i], addr, size, /*wr_id=*/i, /*signal=*/true);
        ++posted;
    }
    uint32_t mask = wait_quorum_(posted, quorum);
    if (success_mask) *success_mask = mask;
    return __builtin_popcount(mask) >= quorum;
}

bool ReplicaGroup::write_all(uint64_t offset, const char* buf, size_t size,
                             int quorum, uint32_t* success_mask) {
    int posted = 0;
    for (int i = 0; i < num_replicas_; ++i) {
        if (drop_mask_ & (1u << i)) continue;
        Gaddr addr(static_cast<uint16_t>(i), offset);
        dmv_->write_tagged(buf, addr, size, /*wr_id=*/i, /*signal=*/true);
        ++posted;
    }
    uint32_t mask = wait_quorum_(posted, quorum);
    if (success_mask) *success_mask = mask;
    return __builtin_popcount(mask) >= quorum;
}

bool ReplicaGroup::cas_all(uint64_t offset, uint64_t expected, uint64_t desired,
                           uint64_t** prev_vals, int quorum,
                           uint32_t* won_mask) {
    int posted = 0;
    for (int i = 0; i < num_replicas_; ++i) {
        if (drop_mask_ & (1u << i)) continue;
        Gaddr addr(static_cast<uint16_t>(i), offset);
        dmv_->cas_tagged(addr, expected, desired, prev_vals[i],
                         /*wr_id=*/i, /*signal=*/true);
        ++posted;
    }
    uint32_t completed = wait_quorum_(posted, quorum);
    // "Success" = verb completed. "Won" = prev == expected.
    uint32_t wins = 0;
    for (int i = 0; i < num_replicas_; ++i) {
        if (!(completed & (1u << i))) continue;
        if (*(prev_vals[i]) == expected) wins |= (1u << i);
    }
    if (won_mask) *won_mask = wins;
    return __builtin_popcount(wins) >= quorum;
}

} // namespace sv