#!/bin/bash
# Update RDMA config in every RdmaVerbs.h under this tree.
# Usage: ./set_rdma_config.sh <NET_DEV> <IDX_CHAR> <PORT> <GID_IDX>
# Example: ./set_rdma_config.sh enp8s0d1 0 1 0
set -e

if [ $# -ne 4 ]; then
    echo "Usage: $0 <NET_DEV_NAME> <IB_DEV_NAME_IDX> <MLX_PORT> <gidIndex>"
    echo "Example: $0 enp8s0d1 0 1 0"
    exit 1
fi

NET=$1; IDX=$2; PORT=$3; GID=$4
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FILES=$(find "$ROOT" -name RdmaVerbs.h -not -path '*/build/*')

if [ -z "$FILES" ]; then
    echo "No RdmaVerbs.h files found under $ROOT"
    exit 1
fi

echo "Updating:"
for f in $FILES; do
    echo "  $f"
    sed -i \
      -e "s|^#define NET_DEV_NAME .*|#define NET_DEV_NAME \"$NET\"|" \
      -e "s|^#define IB_DEV_NAME_IDX .*|#define IB_DEV_NAME_IDX '$IDX'|" \
      -e "s|^#define MLX_PORT .*|#define MLX_PORT $PORT|" \
      -e "s|int gidIndex = [0-9]\+|int gidIndex = $GID|g" \
      "$f"
done

echo ""
echo "Result (grep from all files):"
for f in $FILES; do
    echo "--- $f ---"
    grep -E "^#define (NET_DEV_NAME|IB_DEV_NAME_IDX|MLX_PORT)|int gidIndex = " "$f" | head
done