#ifndef _SKIPVECTOR_REPLICA_GROUP_H_
#define _SKIPVECTOR_REPLICA_GROUP_H_

#include <cstdint>
#include <vector>
#include "DMVerbs.h"                    // Gaddr, DMVerbs
#include "skipvector/Config.h"

namespace sv {

// Fans out a single logical operation to all replicas in parallel and waits
// for a quorum of successful completions. All replicas share the same slab
// layout: logical offset X maps to Gaddr{replica_i, X} for each replica i.
//
// Phase 1a: quorum semantics + fault-injection hooks. Writeback repair is
// deferred (Phase 1c/1d).
//
// Thread model: one ReplicaGroup per client thread (each thread already has
// its own DMVerbs QPs; sharing across threads would introduce completion-
// queue races).

class ReplicaGroup {
public:
    // Constructor. The compute server calls dmv->registerThread() before
    // constructing this. num_replicas defaults to kNumReplicas (=3).
    explicit ReplicaGroup(DMVerbs* dmv, int num_replicas = kNumReplicas);

    // -------- Fault injection (tests only) --------
    // If bit i is set in the mask, verbs to replica i are silently dropped
    // (no post, no completion), simulating that replica being dead.
    void set_drop_mask(uint32_t mask) { drop_mask_ = mask; }
    uint32_t drop_mask() const { return drop_mask_; }

    // -------- Read a fixed-size buffer from every replica --------
    // Posts N reads in parallel, waits for `quorum` successful completions.
    //
    // `bufs[i]` (i in [0, num_replicas)) receives the bytes from replica i.
    // Each buf must be `size` bytes and lie in RDMA-registered memory.
    //
    // `success_mask` (out): bit i set iff replica i's read completed
    //   successfully. popcount(success_mask) >= quorum on return true.
    //
    // Returns true on quorum success, false if too many replicas failed
    // (either dropped by fault injection or actual RDMA errors).
    bool read_all(uint64_t offset, char** bufs, size_t size,
                  int quorum, uint32_t* success_mask);

    // -------- Write a fixed-size buffer to every replica --------
    // Posts N writes in parallel, waits for `quorum` successful completions.
    // Same semantics as read_all but the buffer is shared across replicas
    // (writers always send identical bytes to every replica in Phase 1).
    bool write_all(uint64_t offset, const char* buf, size_t size,
                   int quorum, uint32_t* success_mask);

    // -------- 8-byte CAS on every replica --------
    // Posts N CAS(expected -> desired) in parallel, waits for `quorum`
    // completions.
    //
    // `prev_vals[i]` receives the previous 8B value observed at replica i.
    // Buffers for prev_vals must be RDMA-registered (they receive verb
    // output). `won_mask` (out): bit i set iff replica i's CAS returned
    // prev == expected (i.e., the CAS won on that replica).
    // Returns true iff `popcount(won_mask) >= quorum`.
    bool cas_all(uint64_t offset, uint64_t expected, uint64_t desired,
                 uint64_t** prev_vals, int quorum, uint32_t* won_mask);

    int num_replicas() const { return num_replicas_; }

private:
    DMVerbs* dmv_;
    int      num_replicas_;
    uint32_t drop_mask_;

    // Wait for `quorum` completions in `expected_wr_ids`. Returns bitmask
    // of successful replicas. Poll loop uses poll_rdma_cq_once.
    uint32_t wait_quorum_(int posted_count, int quorum);
};

} // namespace sv

#endif