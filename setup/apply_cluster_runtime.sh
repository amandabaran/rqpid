#!/bin/bash
# Apply runtime cluster config (hugepages, memcached).
# Run after cluster boot or reservation refresh.
set -eu

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/setup/cluster.conf"

NODES=($(grep -iP "^Host w\d+$" ~/.ssh/config | awk '{print $2}' | sort -V))

echo "=== Setting $HUGEPAGES hugepages on ${#NODES[@]} nodes ==="
for n in "${NODES[@]}"; do
    ssh $n "sudo sysctl -w vm.nr_hugepages=$HUGEPAGES >/dev/null && \
            echo \"  \$(hostname): \$(grep HugePages_Total /proc/meminfo | awk '{print \$2}') pages\""
done