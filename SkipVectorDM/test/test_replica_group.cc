// ReplicaGroup end-to-end test on the real cluster.
// - w1..w3 = memory servers (park on barriers)
// - w4     = compute server (runs the tests)
//
// Scenarios:
//   1. Happy path: write to all 3 replicas, read back from all 3
//   2. Fault injection: drop replica 1 on write; quorum still met (2/3)
//   3. Fault injection: drop replicas 1+2 on write; quorum fails (1/3)
//   4. CAS: parallel CAS(0 -> magic) on all 3 replicas from zeroed memory

#include "SkipVectorDM.h"
#include "skipvector/Config.h"
#include "skipvector/ReplicaGroup.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>

static constexpr int kNodeCount        = 4;
static constexpr int kMemoryNodeCount  = 3;
static constexpr int kComputeNodeCount = 1;

DMVerbs* dmv;

#define CHECK(cond) do {                                              \
    if (!(cond)) {                                                    \
        std::fprintf(stderr, "FAIL %s:%d  %s\n",                      \
                     __FILE__, __LINE__, #cond);                      \
        std::abort();                                                 \
    }                                                                 \
} while (0)

static void run_ms() {
    std::printf("[MS %u] parked\n", dmv->getMyNodeID());
    dmv->set_barrier("init");
    dmv->set_barrier("done");
    while (true) sleep(3600);
}

static void run_cs() {
    std::printf("[CS %u] ReplicaGroup tests\n", dmv->getMyNodeID());
    dmv->set_barrier("init");

    sv::ReplicaGroup rg(dmv);
    CHECK(rg.num_replicas() == 3);

    // Grab RDMA-registered buffers. get_page_buffer() rotates within the
    // coro buffer set; distinct calls yield distinct slots.
    auto& rbuf = dmv->get_rbuf(0);
    char* read_bufs[3] = {
        rbuf.get_page_buffer(),
        rbuf.get_page_buffer(),
        rbuf.get_page_buffer(),
    };
    char* write_buf = rbuf.get_page_buffer();

    // Test region is well inside the reserved SV region and disjoint from
    // the smoke test's usage.
    const uint64_t test_offset = sv::kHandlePoolBase + 16384;
    constexpr size_t kSize = 128;

    // ---------- Scenario 1: happy-path write + read ----------
    std::memset(write_buf, 0xAB, kSize);
    uint32_t mask = 0;
    CHECK(rg.write_all(test_offset, write_buf, kSize, sv::kQuorum, &mask));
    CHECK(__builtin_popcount(mask) == 3);   // all 3 succeeded
    std::printf("  [1a] write_all no-fault: OK (mask=0x%x)\n", mask);

    for (int i = 0; i < 3; ++i) std::memset(read_bufs[i], 0, kSize);
    CHECK(rg.read_all(test_offset, read_bufs, kSize, sv::kQuorum, &mask));
    CHECK(__builtin_popcount(mask) == 3);
    for (int i = 0; i < 3; ++i) {
        for (size_t j = 0; j < kSize; ++j) {
            CHECK(static_cast<uint8_t>(read_bufs[i][j]) == 0xAB);
        }
    }
    std::printf("  [1b] read_all no-fault: all 3 replicas returned 0xAB "
                "(mask=0x%x)\n", mask);

    // ---------- Scenario 2: write with replica 1 dropped ----------
    std::memset(write_buf, 0xCD, kSize);
    rg.set_drop_mask(1u << 1);   // drop replica 1
    CHECK(rg.write_all(test_offset, write_buf, kSize, sv::kQuorum, &mask));
    CHECK(__builtin_popcount(mask) == 2);   // 2/3 succeeded, quorum met
    CHECK(!(mask & (1u << 1)));              // replica 1 skipped
    std::printf("  [2a] write_all drop-replica-1: quorum OK "
                "(mask=0x%x, expected 2/3)\n", mask);
    rg.set_drop_mask(0);

    // Now read from all 3. Replicas 0 and 2 should show 0xCD (new value),
    // replica 1 should still show 0xAB (missed the write).
    for (int i = 0; i < 3; ++i) std::memset(read_bufs[i], 0, kSize);
    CHECK(rg.read_all(test_offset, read_bufs, kSize, sv::kQuorum, &mask));
    CHECK(__builtin_popcount(mask) == 3);
    for (int i = 0; i < 3; ++i) {
        uint8_t expected = (i == 1) ? 0xAB : 0xCD;
        for (size_t j = 0; j < kSize; ++j) {
            CHECK(static_cast<uint8_t>(read_bufs[i][j]) == expected);
        }
    }
    std::printf("  [2b] post-fault read: r0=0xCD r1=0xAB(stale) r2=0xCD "
                "(divergence confirmed — writeback comes later)\n");

    // ---------- Scenario 3: write with replicas 1+2 dropped ----------
    std::memset(write_buf, 0xEF, kSize);
    rg.set_drop_mask((1u << 1) | (1u << 2));   // drop 2 replicas
    bool ok = rg.write_all(test_offset, write_buf, kSize, sv::kQuorum, &mask);
    CHECK(!ok);   // only 1/3 posted, quorum not met
    CHECK(__builtin_popcount(mask) == 1);
    std::printf("  [3] write_all drop-2-replicas: correctly FAILED "
                "(mask=0x%x, quorum unmet)\n", mask);
    rg.set_drop_mask(0);

    // Repair the divergent state so the next scenario starts clean.
    std::memset(write_buf, 0x00, kSize);
    CHECK(rg.write_all(test_offset, write_buf, kSize, sv::kQuorum, &mask));

    // ---------- Scenario 4: CAS on all 3 replicas ----------
    // Use a fresh CAS offset (must be 8B-aligned and zeroed).
    const uint64_t cas_offset = sv::kHandlePoolBase + 16384 + 4096;

    // First, zero it via write_all so we're sure of the starting state.
    uint64_t zero = 0;
    CHECK(rg.write_all(cas_offset, (char*)&zero, sizeof(zero),
                       sv::kQuorum, &mask));

    // Prepare per-replica CAS response buffers. Reuse read_bufs' underlying
    // memory as uint64_t*.
    uint64_t* prev_vals[3] = {
        (uint64_t*)read_bufs[0],
        (uint64_t*)read_bufs[1],
        (uint64_t*)read_bufs[2],
    };
    for (int i = 0; i < 3; ++i) *prev_vals[i] = 0;

    uint32_t won_mask = 0;
    uint64_t magic = 0xCAFEBABE12345678ULL;
    CHECK(rg.cas_all(cas_offset, /*expected=*/0ULL, /*desired=*/magic,
                     prev_vals, sv::kQuorum, &won_mask));
    CHECK(__builtin_popcount(won_mask) == 3);   // all 3 saw prev=0 and swapped
    for (int i = 0; i < 3; ++i) CHECK(*prev_vals[i] == 0);
    std::printf("  [4a] cas_all 0->magic on zeroed: 3/3 won "
                "(won_mask=0x%x)\n", won_mask);

    // Retry the same CAS. All should FAIL because current value is `magic`.
    bool cas2 = rg.cas_all(cas_offset, /*expected=*/0ULL, /*desired=*/magic,
                           prev_vals, sv::kQuorum, &won_mask);
    CHECK(!cas2);                             // no quorum on wins
    CHECK(__builtin_popcount(won_mask) == 0); // nobody won
    for (int i = 0; i < 3; ++i) CHECK(*prev_vals[i] == magic);  // all read `magic`
    std::printf("  [4b] cas_all 0->magic after committed: 0/3 won "
                "(won_mask=0x%x, prev=magic everywhere)\n", won_mask);

    // ---------- Scenario 5: CAS with replica 0 dropped ----------
    // Reset to 0 on all three, then drop replica 0 and CAS again.
    CHECK(rg.write_all(cas_offset, (char*)&zero, sizeof(zero),
                       sv::kQuorum, &mask));
    for (int i = 0; i < 3; ++i) *prev_vals[i] = 0xDEAD;  // sentinel

    rg.set_drop_mask(1u << 0);
    uint64_t magic2 = 0xFEEDFACE00000001ULL;
    CHECK(rg.cas_all(cas_offset, 0ULL, magic2, prev_vals, sv::kQuorum, &won_mask));
    CHECK(__builtin_popcount(won_mask) == 2);   // replicas 1+2 won, quorum met
    CHECK(!(won_mask & (1u << 0)));             // replica 0 skipped
    CHECK(*prev_vals[0] == 0xDEAD);             // untouched by us
    CHECK(*prev_vals[1] == 0);
    CHECK(*prev_vals[2] == 0);
    std::printf("  [5] cas_all drop-replica-0: 2/3 won, quorum OK "
                "(won_mask=0x%x)\n", won_mask);
    rg.set_drop_mask(0);

    std::printf("[CS] ALL ReplicaGroup TESTS PASSED\n");
    dmv->set_barrier("done");
}

int main() {
    DMConfig config;
    config.dsmSize       = 2;
    config.machineNR     = kNodeCount;
    config.ComputeNumber = kComputeNodeCount;
    config.MemoryNumber  = kMemoryNodeCount;

    dmv = DMVerbs::getInstance(config);
    dmv->registerThread();

    if (dmv->getMyNodeID() < kMemoryNodeCount) run_ms();
    else                                        run_cs();
    return 0;
}