#ifndef _SKIPVECTOR_LEAF_CONTENTS_H_
#define _SKIPVECTOR_LEAF_CONTENTS_H_

#include <cstdint>
#include <cstring>
#include <cstddef>
#include <boost/crc.hpp>
#include "KVConfig.h"
#include "RdmaConfig.h"
#include "skipvector/Config.h"

namespace sv {

struct LeafContents {
    Key      kmin;
    Key      kmax;
    uint64_t next_handle_offset;
    uint32_t is_orphan;
    uint32_t entry_count;

    Key   keys[kFanoutData];
    Value vals[kFanoutData];

    uint32_t crc;
    uint32_t _pad0;

    static constexpr size_t crc_span_bytes() {
        return offsetof(LeafContents, crc);
    }

    void compute_and_set_crc() {
        boost::crc_32_type c;
        c.process_bytes(this, crc_span_bytes());
        crc = c.checksum();
    }

    bool verify_crc() const {
        boost::crc_32_type c;
        c.process_bytes(this, crc_span_bytes());
        return crc == c.checksum();
    }

    void init_empty() {
        std::memset(this, 0, sizeof(*this));
    }

    bool covers(const Key& k) const {
        return !(k < kmin) && !(kmax < k);
    }

    int find_index(const Key& k) const {
        for (uint32_t i = 0; i < entry_count; ++i) {
            if (keys[i] == k) return static_cast<int>(i);
        }
        return -1;
    }
} __attribute__((packed));

static_assert(sizeof(LeafContents) <= kLeafContentSize,
              "LeafContents overflows kLeafContentSize; bump it in Config.h");

} // namespace sv

#endif