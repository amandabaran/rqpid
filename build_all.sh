#!/bin/bash
# Build SkipVectorDM on w1..w4 in parallel.
# Usage: ./build_all.sh [target1 target2 ...]
#        default target: everything

set -u

NODES=("w1" "w2" "w3" "w4")
TARGETS="${*:-all}"

build_node() {
    local node=$1
    local tgts=$2
    echo "[$node] building: $tgts"
    if ssh $node "cd ~/skipvecdm/SkipVectorDM && mkdir -p build && cd build && \
                  cmake .. >/tmp/cmake_$node.log 2>&1 && \
                  make -j $tgts >/tmp/build_$node.log 2>&1"; then
        echo "[$node] BUILD OK"
    else
        echo "[$node] BUILD FAILED — see /tmp/build_$node.log on $node"
    fi
}

for node in "${NODES[@]}"; do
    build_node "$node" "$TARGETS" &
done
wait

echo "---- all builds done ----"