#!/bin/bash
# Apply cluster.conf to the source tree. Rerun after every edit of cluster.conf.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/setup/cluster.conf"

echo "Applying cluster.conf to source tree at $ROOT ..."

# ---- 1. RdmaVerbs.h across all baselines ----
find "$ROOT" -name RdmaVerbs.h -not -path '*/build/*' | while read f; do
    sed -i \
        -e "s|^#define NET_DEV_NAME .*|#define NET_DEV_NAME \"$NET_DEV_NAME\"|" \
        -e "s|^#define IB_DEV_NAME_IDX .*|#define IB_DEV_NAME_IDX '$IB_DEV_NAME_IDX'|" \
        -e "s|^#define MLX_PORT .*|#define MLX_PORT $MLX_PORT|" \
        -e "s|int gidIndex = [0-9]\+|int gidIndex = $GID_INDEX|g" \
        "$f"
    echo "  patched $f"
done

# ---- 2. RdmaConfig.h (rdmaBufferSize) — only SkipVectorDM for now ----
sed -i "s|constexpr uint64_t rdmaBufferSize .*= [0-9]\+;|constexpr uint64_t rdmaBufferSize     = $RDMA_BUFFER_GB;|" \
    "$ROOT/SkipVectorDM/include/RdmaConfig.h"
sed -i "s|^#define MAX_THREAD_NUM .*|#define MAX_THREAD_NUM $MAX_THREAD_NUM|" \
    "$ROOT/SkipVectorDM/include/RdmaConfig.h"
echo "  patched SkipVectorDM/include/RdmaConfig.h (rdmaBufferSize=$RDMA_BUFFER_GB, maxThreadNum=$MAX_THREAD_NUM) "

# ---- 3. skipvector/Config.h (leaf pool size) ----
sed -i "s|constexpr uint64_t kSlabLeafBytes\s*=.*;|constexpr uint64_t kSlabLeafBytes   = ${LEAF_POOL_MB}ull * define::MB;|" \
    "$ROOT/SkipVectorDM/include/skipvector/Config.h"
echo "  patched skipvector/Config.h (kSlabLeafBytes=${LEAF_POOL_MB}MB)"

# ---- 4. test binaries (dsmSize) ----
for f in "$ROOT"/SkipVectorDM/test/*.cc; do
    if grep -q "config.dsmSize" "$f"; then
        sed -i "s|config.dsmSize\s*=\s*[0-9]\+;|config.dsmSize      = $DSM_GB;|" "$f"
        echo "  patched $(basename $f) (dsmSize=$DSM_GB)"
    fi
done

# ---- 5. memcached.conf ----
cat > "$ROOT/SkipVectorDM/memcached.conf" <<EOF
$MEMCACHED_HOST
$MEMCACHED_PORT
EOF
echo "  wrote memcached.conf"

echo ""
echo "Done. Next: ./sync.sh && ./setup/appy_cluster_runtime.sh"