#ifndef _SKIPVECTOR_CONFIG_H_
#define _SKIPVECTOR_CONFIG_H_

#include <cstdint>
#include <cstddef>
#include "KVConfig.h"    // Key, Value, KEY_SIZE, VALUE_SIZE
#include "RdmaConfig.h"  // Gaddr, define::MB, define::GB

namespace sv {

// ---------- Skip-vector shape ----------
constexpr int      kFanoutData   = 32;   // T for leaf entries
constexpr int      kFanoutIndex  = 32;   // T for index entries
constexpr int      kMaxLayers    = 6;

// ---------- Replication ----------
constexpr int      kF            = 1;    // tolerate f failures
constexpr int      kNumReplicas  = 2 * kF + 1;   // 3
constexpr int      kQuorum       = kF + 1;       // 2

// ---------- Wire sizes ----------
constexpr size_t   kKeySize      = KEY_SIZE;     // 24
constexpr size_t   kValueSize    = VALUE_SIZE;   // 8
constexpr size_t   kLeafHandleSize   = 8;
constexpr size_t   kLeafContentSize  = 1152;     // fits header + 32*(24+8) + crc + pad
constexpr size_t   kIndexNodeSize    = 1152;

// ---------- Slab pool sizes per memory server ----------
// Reserve a fixed region on each MS at boot. Identical layout on all replicas.
constexpr uint64_t kSlabHandleBytes   = 256ull * define::MB;
constexpr uint64_t kSlabLeafBytes     =   2ull * define::GB;
constexpr uint64_t kSlabIndexBytes    = 512ull * define::MB;
constexpr uint64_t kSkipVectorRegionBytes =
    kSlabHandleBytes + kSlabLeafBytes + kSlabIndexBytes;

// Region base offset on each MS (fixed, well-known, identical across replicas).
// Placed at the top of the DSM region; DMTree chunk allocator gets what remains.
// We'll wire this into Directory init later.
constexpr uint64_t kSkipVectorRegionBase = 0;   // start of DSM
constexpr uint64_t kHandlePoolBase = kSkipVectorRegionBase;
constexpr uint64_t kLeafPoolBase   = kHandlePoolBase + kSlabHandleBytes;
constexpr uint64_t kIndexPoolBase  = kLeafPoolBase   + kSlabLeafBytes;

// ---------- Slot counts ----------
constexpr uint64_t kNumHandleSlots = kSlabHandleBytes / kLeafHandleSize;
constexpr uint64_t kNumLeafSlots   = kSlabLeafBytes   / kLeafContentSize;
constexpr uint64_t kNumIndexSlots  = kSlabIndexBytes  / kIndexNodeSize;

// ---------- Fault-injection knob (compute-side, tests only) ----------
struct FaultInjection {
    // If bit i is set, drop verbs to replica i.
    uint32_t drop_replicas_mask = 0;
    bool is_dropped(int replica_id) const {
        return (drop_replicas_mask >> replica_id) & 1u;
    }
};

// ---------- Well-known addresses (identical on all replicas) ----------
// The root of the skip vector points here; the head leaf's handle lives here.
constexpr uint64_t kHeadLeafHandleOffset = kHandlePoolBase;   // handle slot 0

} // namespace sv

#endif