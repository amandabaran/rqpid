#!/bin/bash
# setup_env.sh

# Detect nodes from SSH config
NODES=($(grep -iP "^Host w\d+$" ~/.ssh/config | awk '{print $2}' | sort -V))
PACKAGES="coreutils gawk python3 zip tmux gcc numactl libmemcached-dev memcached openjdk-8-jre-headless infiniband-diags perftest rdma-core libibverbs-dev ibverbs-utils"
DMTREE_PACKAGES="g++ cmake libssl-dev libsnappy-dev memcached libboost-all-dev python3-matplotlib libtbb-dev build-essential autoconf automake libtool git gdb"

echo "Installing dependencies and updating memlock limits on ${#NODES[@]} nodes in parallel..."
echo

# CityHash install function — runs on each remote node
INSTALL_SCRIPT='
set -eux

if [ -f /usr/local/include/city.h ] && ldconfig -p | grep -q libcityhash; then
    echo "CityHash already installed."
    exit 0
fi

sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential autoconf automake libtool git pkg-config

WORK=$(mktemp -d)
cd "$WORK"
git clone --depth 1 https://github.com/google/cityhash.git
cd cityhash

autoreconf -fiv

if ! ./configure --enable-sse4.2; then
    ./configure
fi

make -j"$(nproc)" CXXFLAGS="-g -O3 -fPIC"
sudo make install
sudo ldconfig

ldconfig -p | grep libcityhash
echo "CityHash installed OK on $(hostname)"
'

for node in "${NODES[@]}"; do
    {
        echo "=== Installing CityHash on $node ==="
        # NOTE: no -n flag — we need stdin for bash -s
        if ssh "$node" "bash -s" <<< "$INSTALL_SCRIPT" \
                &> "/tmp/cityhash_${node}.log"; then
            echo "✅ $node"
        else
            echo "❌ $node — see /tmp/cityhash_${node}.log"
        fi
    } &
done
wait

echo
echo "=== Verification ==="
for n in "${NODES[@]}"; do
    printf "%-6s " "$n"
    ssh "$n" 'ldconfig -p | grep -q libcityhash && echo OK || echo MISSING'
done


for node in "${NODES[@]}"; do
    {
        echo "Starting $node..."
        CMD="sudo apt-get update -qq && \
             sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $PACKAGES && \
             sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $DMTREE_PACKAGES"
        
        if ssh -n -o ConnectTimeout=10 "$node" "bash -s" <<< "$CMD" &> "/tmp/install_${node}.log"; then
            echo "✅ $node complete"
        else
            echo "❌ $node failed (check /tmp/install_${node}.log)"
        fi
    } &
done

wait

echo
echo "All node installations finished."
echo

# Step 5: shared keypair on every node
DRIVER="w7"
ssh "$DRIVER" 'test -f ~/.ssh/id_ed25519 || ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519'

PUBKEY=$(ssh "$DRIVER" 'cat ~/.ssh/id_ed25519.pub')
ssh "$DRIVER" 'cat ~/.ssh/id_ed25519' > /tmp/cluster_key
ssh "$DRIVER" 'cat ~/.ssh/id_ed25519.pub' > /tmp/cluster_key.pub

for node in "${NODES[@]}"; do
    if [[ -z "${NODE_IPS[$node]}" ]]; then continue; fi
    echo "  Distributing keypair to $node..."
    scp -q /tmp/cluster_key /tmp/cluster_key.pub "$node":~/.ssh/
    ssh "$node" "mv ~/.ssh/cluster_key ~/.ssh/id_ed25519 && \
                 mv ~/.ssh/cluster_key.pub ~/.ssh/id_ed25519.pub && \
                 chmod 600 ~/.ssh/id_ed25519 && \
                 grep -qxF '$PUBKEY' ~/.ssh/authorized_keys || echo '$PUBKEY' >> ~/.ssh/authorized_keys"
done

# Pre-accept host keys everywhere
for src in "${NODES[@]}"; do
    ssh "$src" "for n in ${NODES[*]}; do ssh-keyscan -H \$n >> ~/.ssh/known_hosts 2>/dev/null; done"
done

rm -f /tmp/cluster_key /tmp/cluster_key.pub

#Step 6: Setup Hugepages

for n in "${NODES[@]}"; do
    ssh $n 'echo "vm.nr_hugepages = 51200" | sudo tee /etc/sysctl.d/99-hugepages.conf && sudo sysctl -w vm.nr_hugepages=51200'
done