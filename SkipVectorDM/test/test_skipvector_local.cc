// Local-only unit tests: LeafHandle tag arithmetic, LeafContents CRC,
// SlabAllocator alloc/free. Runs on any machine with `./test_skipvector_local`.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "skipvector/Config.h"
#include "skipvector/LeafHandle.h"
#include "skipvector/LeafContents.h"
#include "skipvector/SlabAllocator.h"

using namespace sv;

#define CHECK(cond) do {                                             \
    if (!(cond)) {                                                   \
        std::fprintf(stderr, "FAIL %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                     \
        std::abort();                                                \
    }                                                                \
} while (0)

// -------------------- LeafHandle --------------------
static void test_handle_bitpack() {
    LeafHandle h(LeafHandle::make_tag(0xABC, 0x12345), 0xDEADBEEF);
    CHECK(h.struct_ver()  == 0xABC);
    CHECK(h.content_ver() == 0x12345);
    CHECK(h.offset        == 0xDEADBEEF);

    // Round-trip through raw
    LeafHandle h2(h.raw);
    CHECK(h == h2);

    // Bit-field bounds
    LeafHandle h3(LeafHandle::make_tag(LeafHandle::kStructVerMask,
                                       LeafHandle::kContentVerMask), 0);
    CHECK(h3.struct_ver()  == LeafHandle::kStructVerMask);
    CHECK(h3.content_ver() == LeafHandle::kContentVerMask);

    std::puts("  test_handle_bitpack: OK");
}

static void test_handle_ordering() {
    LeafHandle a(LeafHandle::make_tag(1, 5), 100);
    LeafHandle b(LeafHandle::make_tag(1, 6), 100);
    LeafHandle c(LeafHandle::make_tag(2, 0), 200);

    CHECK(b.newer_than(a));
    CHECK(!a.newer_than(b));
    CHECK(c.newer_than(b));      // struct_ver dominates
    CHECK(c.newer_than(a));
    CHECK(!a.newer_than(a));     // reflexive: not strictly newer

    // Bumps
    LeafHandle a2 = a.bump_content();
    CHECK(a2.struct_ver()  == a.struct_ver());
    CHECK(a2.content_ver() == a.content_ver() + 1);
    CHECK(a2.offset        == a.offset);

    LeafHandle a3 = a.bump_struct(999);
    CHECK(a3.struct_ver()  == a.struct_ver() + 1);
    CHECK(a3.content_ver() == 0);
    CHECK(a3.offset        == 999);

    std::puts("  test_handle_ordering: OK");
}

// -------------------- LeafContents --------------------
static Key mk_key(const char* s) { return Key(s); }

static void test_leaf_contents_crc() {
    LeafContents lc;
    lc.init_empty();
    lc.kmin = mk_key("aaaaaaaaaaaaaaaaaaaaaaaa");
    lc.kmax = mk_key("mmmmmmmmmmmmmmmmmmmmmmmm");
    lc.next_handle_offset = 0;
    lc.is_orphan = 0;
    lc.entry_count = 3;
    lc.keys[0] = mk_key("aaaaaaaaaaaaaaaaaaaaaaaa"); lc.vals[0] = Value(1);
    lc.keys[1] = mk_key("gggggggggggggggggggggggg"); lc.vals[1] = Value(2);
    lc.keys[2] = mk_key("mmmmmmmmmmmmmmmmmmmmmmmm"); lc.vals[2] = Value(3);
    lc.compute_and_set_crc();

    CHECK(lc.verify_crc());

    // Bit-flip must invalidate CRC
    lc.vals[1].value ^= 1;
    CHECK(!lc.verify_crc());
    lc.vals[1].value ^= 1;
    CHECK(lc.verify_crc());

    // covers()
    CHECK(lc.covers(mk_key("aaaaaaaaaaaaaaaaaaaaaaaa")));
    CHECK(lc.covers(mk_key("gggggggggggggggggggggggg")));
    CHECK(lc.covers(mk_key("mmmmmmmmmmmmmmmmmmmmmmmm")));
    CHECK(!lc.covers(mk_key("nnnnnnnnnnnnnnnnnnnnnnnn")));

    // find_index()
    CHECK(lc.find_index(mk_key("gggggggggggggggggggggggg")) == 1);
    CHECK(lc.find_index(mk_key("zzzzzzzzzzzzzzzzzzzzzzzz")) == -1);

    std::puts("  test_leaf_contents_crc: OK");
}

static void test_leaf_contents_size_fits() {
    // If this ever fails, kLeafContentSize in Config.h needs to grow.
    std::printf("  sizeof(LeafContents) = %zu, kLeafContentSize = %zu\n",
                sizeof(LeafContents), (size_t)kLeafContentSize);
    CHECK(sizeof(LeafContents) <= kLeafContentSize);
    std::puts("  test_leaf_contents_size_fits: OK");
}

// -------------------- SlabAllocator --------------------
static void test_slab_basic() {
    // Small local pool (16 slots of 64B at fake base 0x10000).
    SlabAllocator sa(/*base=*/0x10000, /*slot=*/64, /*num=*/16,
                     /*reserved=*/2);
    CHECK(sa.used_count() == 2);

    std::set<uint64_t> got;
    for (int i = 0; i < 14; ++i) {
        uint64_t o = sa.alloc();
        CHECK(o != SlabAllocator::kBad);
        CHECK(o >= 0x10000 + 2 * 64);            // no reserved slot handed out
        CHECK((o - 0x10000) % 64 == 0);          // aligned
        CHECK(got.insert(o).second);             // uniqueness
    }
    // Pool now full
    CHECK(sa.alloc() == SlabAllocator::kBad);
    CHECK(sa.used_count() == 16);

    // Free one, reallocate, verify it comes back
    uint64_t victim = *got.begin();
    sa.free(victim);
    CHECK(sa.used_count() == 15);
    uint64_t reissued = sa.alloc();
    CHECK(reissued == victim);
    CHECK(sa.used_count() == 16);

    std::puts("  test_slab_basic: OK");
}

static void test_slab_pools() {
    SlabPools p;
    CHECK(p.handles.slot_size() == kLeafHandleSize);
    CHECK(p.leaves.slot_size()  == kLeafContentSize);
    CHECK(p.indices.slot_size() == kIndexNodeSize);
    CHECK(p.handles.num_slots() == kNumHandleSlots);

    // Slot 0 of the handle pool is reserved for the head leaf.
    CHECK(p.handles.used_count() == 1);
    uint64_t first = p.handles.alloc();
    CHECK(first != SlabAllocator::kBad);
    CHECK(first != kHeadLeafHandleOffset);   // head is protected
    CHECK(first == kHandlePoolBase + 1 * kLeafHandleSize);

    // Sanity: allocations across pools land in the correct address ranges
    uint64_t leaf_off  = p.leaves.alloc();
    uint64_t index_off = p.indices.alloc();
    CHECK(leaf_off  >= kLeafPoolBase  && leaf_off  <  kLeafPoolBase  + kSlabLeafBytes);
    CHECK(index_off >= kIndexPoolBase && index_off <  kIndexPoolBase + kSlabIndexBytes);

    // Round-trip conversions
    CHECK(p.handles.offset_of(p.handles.slot_of(first)) == first);

    std::puts("  test_slab_pools: OK");
}

static void test_slab_exhaustion_and_free_all() {
    SlabAllocator sa(0, 8, 4, 0);
    std::vector<uint64_t> offs;
    for (int i = 0; i < 4; ++i) {
        uint64_t o = sa.alloc();
        CHECK(o != SlabAllocator::kBad);
        offs.push_back(o);
    }
    CHECK(sa.alloc() == SlabAllocator::kBad);
    for (uint64_t o : offs) sa.free(o);
    CHECK(sa.used_count() == 0);
    for (int i = 0; i < 4; ++i) CHECK(sa.alloc() != SlabAllocator::kBad);

    std::puts("  test_slab_exhaustion_and_free_all: OK");
}

// -------------------- driver --------------------
int main() {
    std::puts("== LeafHandle ==");
    test_handle_bitpack();
    test_handle_ordering();

    std::puts("== LeafContents ==");
    test_leaf_contents_size_fits();
    test_leaf_contents_crc();

    std::puts("== SlabAllocator ==");
    test_slab_basic();
    test_slab_pools();
    test_slab_exhaustion_and_free_all();

    std::puts("\nAll local unit tests passed.");
    return 0;
}