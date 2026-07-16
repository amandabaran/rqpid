#ifndef _SKIPVECTOR_LEAF_HANDLE_H_
#define _SKIPVECTOR_LEAF_HANDLE_H_

#include <cstdint>
#include "skipvector/Config.h"

namespace sv {

union LeafHandle {
    uint64_t raw;
    struct {
        uint32_t tag;
        uint32_t offset;
    };

    LeafHandle() : raw(0) {}
    explicit LeafHandle(uint64_t r) : raw(r) {}
    LeafHandle(uint32_t t, uint32_t o) : tag(t), offset(o) {}

    static constexpr uint32_t kContentVerBits = 20;
    static constexpr uint32_t kStructVerBits  = 12;
    static constexpr uint32_t kContentVerMask = (1u << kContentVerBits) - 1u;
    static constexpr uint32_t kStructVerMask  = (1u << kStructVerBits)  - 1u;
    static constexpr uint32_t kStructVerShift = kContentVerBits;

    uint32_t struct_ver()  const { return (tag >> kStructVerShift) & kStructVerMask; }
    uint32_t content_ver() const { return  tag & kContentVerMask; }

    static uint32_t make_tag(uint32_t sv_, uint32_t cv_) {
        return ((sv_ & kStructVerMask) << kStructVerShift) | (cv_ & kContentVerMask);
    }

    void set_tag(uint32_t sv_, uint32_t cv_) { tag = make_tag(sv_, cv_); }

    bool newer_than(LeafHandle o) const {
        if (struct_ver() != o.struct_ver())
            return struct_ver() > o.struct_ver();
        return content_ver() > o.content_ver();
    }

    LeafHandle bump_content() const {
        return LeafHandle(make_tag(struct_ver(), content_ver() + 1), offset);
    }
    LeafHandle bump_struct(uint32_t new_offset) const {
        return LeafHandle(make_tag(struct_ver() + 1, 0), new_offset);
    }

    bool operator==(LeafHandle o) const { return raw == o.raw; }
    bool operator!=(LeafHandle o) const { return raw != o.raw; }
};

static_assert(sizeof(LeafHandle) == 8, "LeafHandle must be 8 bytes for CAS");

} // namespace sv

#endif