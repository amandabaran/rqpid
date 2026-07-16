#ifndef _SKIPVECTOR_SLAB_ALLOCATOR_H_
#define _SKIPVECTOR_SLAB_ALLOCATOR_H_

#include <cstdint>
#include <cassert>
#include <mutex>
#include <vector>
#include <limits>
#include "skipvector/Config.h"

namespace sv {

// Per-compute-server bitmap allocator over a fixed-size slot pool.
// Same offset refers to the same logical slot on every replica because MSes
// have identical slab layouts. Purely local: no RDMA, no coordination.
//
// Slots [0, first_reserved) are pre-marked used (e.g., slot 0 = head-leaf handle).
class SlabAllocator {
public:
    static constexpr uint64_t kBad = std::numeric_limits<uint64_t>::max();

    SlabAllocator(uint64_t base_offset,
                  uint64_t slot_size,
                  uint64_t num_slots,
                  uint64_t first_reserved = 0)
        : base_(base_offset)
        , slot_size_(slot_size)
        , num_slots_(num_slots)
        , bitmap_(num_slots, false)
        , next_hint_(first_reserved)
        , first_free_(first_reserved)
        , used_count_(first_reserved)
    {
        assert(first_reserved <= num_slots);
        for (uint64_t i = 0; i < first_reserved; ++i) bitmap_[i] = true;
    }

    // Returns absolute offset of the allocated slot, or kBad if full.
    uint64_t alloc() {
        std::lock_guard<std::mutex> g(mu_);
        for (uint64_t tries = 0; tries < num_slots_; ++tries) {
            uint64_t i = next_hint_;
            next_hint_ = (next_hint_ + 1) % num_slots_;
            if (next_hint_ < first_free_) next_hint_ = first_free_;
            if (!bitmap_[i]) {
                bitmap_[i] = true;
                ++used_count_;
                return base_ + i * slot_size_;
            }
        }
        return kBad;
    }

    // free() is called during reclamation (Phase 8). Safe to call now for tests.
    void free(uint64_t offset) {
        std::lock_guard<std::mutex> g(mu_);
        assert(offset >= base_);
        uint64_t rel = offset - base_;
        assert(rel % slot_size_ == 0);
        uint64_t idx = rel / slot_size_;
        assert(idx < num_slots_);
        assert(idx >= first_free_ && "cannot free reserved slot");
        assert(bitmap_[idx] && "double free");
        bitmap_[idx] = false;
        --used_count_;
    }

    // Introspection (for tests + metrics)
    uint64_t base()       const { return base_; }
    uint64_t slot_size()  const { return slot_size_; }
    uint64_t num_slots()  const { return num_slots_; }
    uint64_t used_count() const { return used_count_; }

    bool owns(uint64_t offset) const {
        return offset >= base_ && offset < base_ + num_slots_ * slot_size_;
    }

    // Convert offset <-> slot index (useful for retire lists later).
    uint64_t slot_of(uint64_t offset) const {
        assert(owns(offset));
        return (offset - base_) / slot_size_;
    }
    uint64_t offset_of(uint64_t slot_idx) const {
        assert(slot_idx < num_slots_);
        return base_ + slot_idx * slot_size_;
    }

private:
    const uint64_t base_;
    const uint64_t slot_size_;
    const uint64_t num_slots_;

    std::mutex             mu_;
    std::vector<bool>      bitmap_;
    uint64_t               next_hint_;
    const uint64_t         first_free_;
    uint64_t               used_count_;
};

// -------- Convenience: the three pools this compute server owns --------
struct SlabPools {
    SlabAllocator handles;   // 8B slots
    SlabAllocator leaves;    // kLeafContentSize slots
    SlabAllocator indices;   // kIndexNodeSize slots

    SlabPools()
        : handles(kHandlePoolBase, kLeafHandleSize,  kNumHandleSlots,
                  /*first_reserved=*/1)   // slot 0 is the head-leaf handle
        , leaves (kLeafPoolBase,   kLeafContentSize, kNumLeafSlots)
        , indices(kIndexPoolBase,  kIndexNodeSize,   kNumIndexSlots)
    {}
};

} // namespace sv

#endif