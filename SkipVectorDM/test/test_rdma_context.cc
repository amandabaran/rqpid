// Minimal RDMA context sanity check: just open the device and log what we got.
// Runs on any single node.

#include "RdmaVerbs.h"
#include <cstdio>

int main() {
    RdmaContext ctx;
    bool ok = createContext(&ctx);
    if (!ok) {
        std::fprintf(stderr, "createContext failed\n");
        return 1;
    }
    std::printf("createContext OK\n");
    std::printf("  devIndex = %u\n", ctx.devIndex);
    std::printf("  port     = %u\n", ctx.port);
    std::printf("  gidIndex = %d\n", ctx.gidIndex);
    std::printf("  lid      = %u\n", ctx.lid);
    std::printf("  gid      = ");
    for (int i = 0; i < 16; ++i) {
        std::printf("%02x", ctx.gid.raw[i]);
        if (i == 7) std::printf(":");
    }
    std::printf("\n");

    // Test MR registration on some anonymous memory.
    void* p = malloc(4096);
    ibv_mr* mr = createMemoryRegion((uint64_t)p, 4096, &ctx);
    if (!mr) {
        std::fprintf(stderr, "createMemoryRegion failed\n");
        return 1;
    }
    std::printf("MR OK: lkey=0x%x rkey=0x%x\n", mr->lkey, mr->rkey);

    ibv_dereg_mr(mr);
    destoryContext(&ctx);
    free(p);
    std::printf("done\n");
    return 0;
}