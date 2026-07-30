#!/bin/bash
# Sync this skipvecdm/ directory to w1..w7
# Usage: ./sync.sh           # sync to all nodes (parallel)
#        ./sync.sh w3 w5     # sync to specific nodes

set -e

# Resolve the directory this script lives in (works regardless of cwd)
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/"
DEST="~/skipvecdm/"
NODES=("w1" "w2" "w3" "w4" "w5" "w6" "w7")

# Override node list if args given
if [ $# -gt 0 ]; then
    NODES=("$@")
fi

RSYNC_OPTS=(
    -azh
    --delete
    --exclude='.git/'
    --exclude='build/'
    --exclude='*.o'
    --exclude='*.a'
    --exclude='AE/Data/'
    --exclude='compile.output'
    --exclude='*.output'
    --exclude='sync.sh.log'
)

sync_node() {
    local node=$1
    echo "[$node] starting sync..."
    if rsync "${RSYNC_OPTS[@]}" "$SRC" "$node:$DEST" > /tmp/sync_${node}.log 2>&1; then
        echo "[$node] OK"
    else
        echo "[$node] FAILED (see /tmp/sync_${node}.log)"
    fi
}

for node in "${NODES[@]}"; do
    sync_node "$node" &
done
wait

echo "---- All sync jobs done ----"