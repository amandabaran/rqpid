#!/bin/bash
# setup_env.sh

set -e

NODES=($(grep -iP "^Host w\d+$" ~/.ssh/config | awk '{print $2}' | sort -V))
PACKAGES="coreutils gawk python3 zip tmux gcc numactl libmemcached-dev memcached openjdk-8-jre-headless infiniband-diags perftest rdma-core libibverbs-dev ibverbs-utils build-essential autoconf automake libtool git pkg-config gdb"
DMTREE_PACKAGES="g++ cmake libssl-dev libsnappy-dev libboost-all-dev python3-matplotlib libtbb-dev"

echo "=== Step 0: Fix DNS on all nodes ==="
for n in "${NODES[@]}"; do
    ssh "$n" 'sudo sed -i "s|^#\?DNS=.*|DNS=8.8.8.8 1.1.1.1|" /etc/systemd/resolved.conf && sudo systemctl restart systemd-resolved && getent hosts github.com >/dev/null && echo "  $(hostname) DNS ok" || echo "  $(hostname) DNS FAIL"'
done

echo
echo "=== Step 1: apt install packages on ${#NODES[@]} nodes in parallel ==="
for node in "${NODES[@]}"; do
    {
        echo "  Starting apt on $node..."
        if ssh -n "$node" "sudo apt-get update -qq && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $PACKAGES $DMTREE_PACKAGES" &> "/tmp/install_${node}.log"; then
            echo "  ✅ $node apt done"
        else
            echo "  ❌ $node apt FAILED (see /tmp/install_${node}.log)"
        fi
    } &
done
wait

echo
echo "=== Step 2: Install CityHash on ${#NODES[@]} nodes in parallel ==="
INSTALL_SCRIPT='
set -eux
if [ -f /usr/local/include/city.h ] && ldconfig -p | grep -q libcityhash; then
    echo "CityHash already installed."
    exit 0
fi
WORK=$(mktemp -d)
cd "$WORK"
git clone --depth 1 https://github.com/google/cityhash.git
cd cityhash
autoreconf -fiv
./configure --enable-sse4.2 || ./configure
make -j"$(nproc)" CXXFLAGS="-g -O3 -fPIC"
sudo make install
sudo ldconfig
ldconfig -p | grep libcityhash
'

for node in "${NODES[@]}"; do
    {
        if ssh "$node" "bash -s" <<< "$INSTALL_SCRIPT" &> "/tmp/cityhash_${node}.log"; then
            echo "  ✅ $node"
        else
            echo "  ❌ $node — see /tmp/cityhash_${node}.log"
        fi
    } &
done
wait

echo
echo "=== Step 3: Verify CityHash ==="
for n in "${NODES[@]}"; do
    printf "%-6s " "$n"
    ssh "$n" 'ldconfig -p | grep -q libcityhash && echo OK || echo MISSING'
done

echo
echo "=== Step 4: Distribute shared SSH keypair ==="
DRIVER=w1
ssh "$DRIVER" 'test -f ~/.ssh/id_ed25519 || ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519'
PUBKEY=$(ssh "$DRIVER" 'cat ~/.ssh/id_ed25519.pub')
ssh "$DRIVER" 'cat ~/.ssh/id_ed25519' > /tmp/cluster_key
ssh "$DRIVER" 'cat ~/.ssh/id_ed25519.pub' > /tmp/cluster_key.pub

for node in "${NODES[@]}"; do
    echo "  → $node"
    scp -q /tmp/cluster_key /tmp/cluster_key.pub "$node":~/.ssh/
    ssh "$node" "mv ~/.ssh/cluster_key ~/.ssh/id_ed25519 && \
                 mv ~/.ssh/cluster_key.pub ~/.ssh/id_ed25519.pub && \
                 chmod 600 ~/.ssh/id_ed25519 && \
                 (grep -qxF '$PUBKEY' ~/.ssh/authorized_keys || echo '$PUBKEY' >> ~/.ssh/authorized_keys)"
done

# Pre-accept host keys
for src in "${NODES[@]}"; do
    ssh "$src" "for n in ${NODES[*]}; do ssh-keyscan -H \$n >> ~/.ssh/known_hosts 2>/dev/null; done"
done
rm -f /tmp/cluster_key /tmp/cluster_key.pub

echo
echo "=== Step 5: Configure hugepages ==="
source "$(dirname "$0")/../cluster.conf"
for n in "${NODES[@]}"; do
    ssh "$n" "echo 'vm.nr_hugepages = $HUGEPAGES' | sudo tee /etc/sysctl.d/99-hugepages.conf >/dev/null && \
              sudo sysctl -w vm.nr_hugepages=$HUGEPAGES >/dev/null"
done

echo "=== Step 6: Raise memlock limits ==="
for n in "${NODES[@]}"; do
    ssh "$n" 'sudo bash -c "cat > /etc/security/limits.d/99-rdma.conf" <<EOF
* soft memlock unlimited
* hard memlock unlimited
EOF'
    echo "  $n: memlock raised"
done

echo
echo "=== Setup complete ==="