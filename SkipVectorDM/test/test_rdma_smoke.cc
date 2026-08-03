// RDMA smoke test — all 4 nodes launch this binary.
// Node IDs [0..MemoryNumber-1] are memory servers; they park on barriers.
// Node ID == MemoryNumber is the single compute server; it runs the test:
//
//   1. Write a distinct 64-byte pattern to every MS at a fixed offset
//      inside the reserved SkipVector region.
//   2. Read each MS back independently and verify the pattern.
//   3. Do the same via an 8-byte CAS (compare-and-swap 0 -> magic).
//   4. Report per-MS latency for a small burst.
//
// This proves: (a) the SV region really is reserved (write doesn't clobber
// anything), (b) multi-MS routing works, (c) atomics work.

#include "SkipVectorDM.h"          // brings in DMVerbs, KVConfig, RdmaConfig
#include "skipvector/Config.h"
#include "skipvector/LeafHandle.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>

static constexpr int kNodeCount        = 4;   // 3 MS + 1 CS
static constexpr int kMemoryNodeCount  = 3;
static constexpr int kComputeNodeCount = 1;

DMVerbs* dmv;

// ---- helpers ----
static void die(const char* msg) {
    std::fprintf(stderr, "FATAL: %s\n", msg);
    std::abort();
}

static uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static void run_memory_server() {
    std::printf("[MS %u] parked on barriers\n", dmv->getMyNodeID());
    dmv->set_barrier("init");
    dmv->set_barrier("smoke_write_done");
    dmv->set_barrier("smoke_read_done");
    dmv->set_barrier("smoke_cas_done");
    dmv->set_barrier("finish");

    // Park forever; CS will pkill us at end of run
    while (true) { sleep(3600); }
}

static void run_compute_server() {
    std::printf("[CS %u] starting smoke test against %d memory servers\n",
                dmv->getMyNodeID(), kMemoryNodeCount);
    dmv->set_barrier("init");

    // Scratch buffer in RDMA-registered memory.
    char* buf = dmv->get_rdma_buffer();

    // Fixed offset within the SkipVector region, well past slot 0.
    // Place it inside the handle-pool area but past our reserved head slot.
    const uint64_t test_offset =
        sv::kHandlePoolBase + 4096ULL;   // 4 KB in

    // ---------- Test 1: write a distinct 64B pattern per MS ----------
    constexpr size_t kPattBytes = 64;
    for (int ms = 0; ms < kMemoryNodeCount; ++ms) {
        std::memset(buf, 0, kPattBytes);
        // Pattern: repeating "MS<ms>___" style bytes so we can eyeball it.
        for (size_t i = 0; i < kPattBytes; ++i)
            buf[i] = static_cast<char>(0xA0 + ms + (i & 0x0F));

        Gaddr addr(static_cast<uint16_t>(ms), test_offset);
        dmv->write_sync(buf, addr, kPattBytes);
        std::printf("[CS] wrote %zuB pattern to MS %d @ offset 0x%lx\n",
                    kPattBytes, ms, test_offset);
    }
    dmv->set_barrier("smoke_write_done");

    // ---------- Test 2: read back each MS and verify ----------
    for (int ms = 0; ms < kMemoryNodeCount; ++ms) {
        std::memset(buf, 0, kPattBytes);
        Gaddr addr(static_cast<uint16_t>(ms), test_offset);
        dmv->read_sync(buf, addr, kPattBytes);

        // Verify
        for (size_t i = 0; i < kPattBytes; ++i) {
            char expect = static_cast<char>(0xA0 + ms + (i & 0x0F));
            if (buf[i] != expect) {
                std::fprintf(stderr,
                             "[CS] MISMATCH at MS %d byte %zu: got 0x%02x want 0x%02x\n",
                             ms, i, (unsigned)(uint8_t)buf[i], (unsigned)(uint8_t)expect);
                die("read verify failed");
            }
        }
        std::printf("[CS] MS %d readback verified\n", ms);
    }
    dmv->set_barrier("smoke_read_done");

    // ---------- Test 3: 8B CAS per MS ----------
    // CAS at an 8B-aligned offset. Precondition: memory zero at that offset.
    // (Fresh MS boot means DSM starts zeroed; and we haven't written here.)
    const uint64_t cas_offset = sv::kHandlePoolBase + 8192ULL;
    for (int ms = 0; ms < kMemoryNodeCount; ++ms) {
        uint64_t* cas_buf = (uint64_t*)buf;
        *cas_buf = 0;                              // will receive prev value
        Gaddr addr(static_cast<uint16_t>(ms), cas_offset);

        uint64_t magic = 0xDEADBEEF00000000ULL | (uint64_t)ms;
        bool ok = dmv->cas_sync(addr, /*equal=*/0ULL, /*val=*/magic, cas_buf);
        if (!ok) {
            std::fprintf(stderr, "[CS] CAS failed on MS %d; prev=0x%lx\n",
                         ms, *cas_buf);
            die("CAS 0->magic failed on fresh memory");
        }

        // Read back and check
        dmv->read_sync(buf, addr, 8);
        uint64_t got = *(uint64_t*)buf;
        if (got != magic) {
            std::fprintf(stderr, "[CS] MS %d readback after CAS: got 0x%lx want 0x%lx\n",
                         ms, got, magic);
            die("post-CAS readback mismatch");
        }
        std::printf("[CS] MS %d CAS 0 -> 0x%lx OK\n", ms, magic);
    }
    dmv->set_barrier("smoke_cas_done");

    // ---------- Test 4: per-MS read latency micro-bench ----------
    constexpr int kIters = 10000;
    for (int ms = 0; ms < kMemoryNodeCount; ++ms) {
        Gaddr addr(static_cast<uint16_t>(ms), test_offset);
        uint64_t t0 = now_ns();
        for (int i = 0; i < kIters; ++i) {
            dmv->read_sync(buf, addr, kPattBytes);
        }
        uint64_t t1 = now_ns();
        double avg_us = (double)(t1 - t0) / kIters / 1000.0;
        std::printf("[CS] MS %d read latency: %.2f us (%d iters, %zuB)\n",
                    ms, avg_us, kIters, kPattBytes);
    }

    std::printf("[CS] ALL SMOKE TESTS PASSED\n");
    dmv->set_barrier("finish");
}

int main(int argc, char* argv[]) {
    DMConfig config;
    config.dsmSize      = 2;                // GB per MS; SV region is ~2.75 GB
    config.machineNR    = kNodeCount;
    config.ComputeNumber = kComputeNodeCount;
    config.MemoryNumber  = kMemoryNodeCount;

    dmv = DMVerbs::getInstance(config);
    dmv->registerThread();

    uint16_t my_id = dmv->getMyNodeID();
    if (my_id < kMemoryNodeCount) {
        run_memory_server();
    } else {
        run_compute_server();
    }
    return 0;
}