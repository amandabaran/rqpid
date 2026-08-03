# SkipVectorDM

## Quick Reference

All cluster-specific tunables live in [`cluster.conf`](cluster.conf). When
switching clusters, edit that file and rerun the two "apply" scripts below.

## Cluster Setup

From the `setup/` directory, run the following on your driver machine:

1. `./setup_local_hosts.sh manifest.xml`
2. `./setup_ssh_hosts.sh manifest.xml`
3. `./setup_env.sh`

## Adapting to a New Cluster

All cluster-specific values live in `cluster.conf`:

- RDMA transport (`NET_DEV_NAME`, `IB_DEV_NAME_IDX`, `MLX_PORT`, `GID_INDEX`)
- Memory sizing (`RDMA_BUFFER_GB`, `DSM_GB`, `LEAF_POOL_MB`, `HUGEPAGES`)
- Memcached endpoint (`MEMCACHED_HOST`, `MEMCACHED_PORT`)

To switch to a new cluster:

1. Edit `cluster.conf`. Use the reference table at the bottom of that file, or
   the discovery recipe in [Finding RDMA Config](#finding-rdma-config) below.
2. Patch the source tree:
   ```bash
   ./setup/apply_cluster_conf.sh
   ```
3. Push to all nodes and apply runtime state (hugepages):
   ```bash
   ./sync.sh
   ./setup/apply_cluster_runtime.sh
   ```
4. Rebuild:
   ```bash
   ./build_all.sh
   ```

`cluster.conf` is under version control; each commit captures the exact
tunables in effect. No drift between "what I changed" and "what runs."

## Finding RDMA Config

Run on any active cluster node:

```bash
ibv_devinfo                            # pick the ACTIVE port
ip -o -4 addr show | grep 10.10.1.     # find its Ethernet iface
```

From `ibv_devinfo`, pick the port with `state: PORT_ACTIVE (4)`:
- `hca_id: mlx4_0` → `IB_DEV_NAME_IDX = "0"` (char at index 5)
- `port: 1` → `MLX_PORT = 1`
- `link_layer: InfiniBand` → `GID_INDEX = 0`
- `link_layer: Ethernet` (RoCE) → find the `type=RoCE v2` GID with an
  IPv4-mapped suffix `::ffff:XXXX:YYYY`:
  ```bash
  DEV=mlx5_2 PORT=1
  for i in $(seq 0 7); do
    echo "gid[$$i] type=$$(cat /sys/class/infiniband/$$DEV/ports/$$PORT/gid_attrs/types/$$i 2>/dev/null) gid=$$(cat /sys/class/infiniband/$$DEV/ports/$$PORT/gids/$i 2>/dev/null)"
  done
  ```

`NET_DEV_NAME` is the Ethernet iface (not the RDMA device) that has the node's
private-subnet IP; the code uses it to advertise itself to memcached.

**Prerequisites** (if `ibv_devinfo` prints nothing):
- Load kernel modules: `sudo modprobe mlx5_ib` (or `mlx4_ib` for ConnectX-3) `ib_uverbs rdma_ucm`
- InfiniBand only: subnet manager must be running somewhere: `sudo apt install opensm && sudo systemctl start opensm`

## Verifying the Config

After `apply_cluster_conf.sh`:

```bash
./sync.sh
./build_all.sh test_rdma_context
for n in w1 w2 w3 w4; do ssh $n '~/skipvecdm/SkipVectorDM/build/test_rdma_context'; done
```

Each node should print nonzero `lkey`/`rkey` and a nonzero GID matching what
`ibv_devinfo` reported.

Then the multi-node smoke test:
```bash
./build_all.sh test_rdma_smoke
./run_smoke.sh
```

Expected: writes/reads/CAS across 3 memory servers, all verified, sub-10 μs
read latency.

## Reference Configurations

| Hardware              | ETH iface   | Link            | DEV_IDX | PORT | GID | RDMA_BUF GB | DSM GB | HUGEPAGES |
|-----------------------|-------------|-----------------|---------|------|-----|-------------|--------|-----------|
| r650 (ConnectX-6)     | `ens1f1np1` | Ethernet/RoCEv2 | `"2"`   | 1    | 3   | 32          | 8      | 51200     |
| r320 (ConnectX-3)     | `enp8s0d1`  | InfiniBand      | `"0"`   | 1    | 0   | 2           | 2      | 3000      |

## Memcached

If memcached needs to run on a different host/port, set `MEMCACHED_HOST` and
`MEMCACHED_PORT` in `cluster.conf` and rerun `apply_cluster_conf.sh` — the
script rewrites `memcached.conf` in each system subdirectory.

## Build and Sync

Sync source and build across all cluster nodes in parallel:
```bash
cd ~/skipvecdm
./sync.sh
./build_all.sh [target]        # default: everything under SkipVectorDM
```

To build every baseline (DMTree, FPTree, Sherman, etc.):
```bash
for n in w1 w2 w3 w4 w5 w6 w7; do
    ssh $n 'rm -f /tmp/build_ae.flag'
    ssh $n 'cd ~/skipvecdm && bash build_ae.sh 2>&1 | tail -5'
done
```

## Common Issues

### `mmap` segfault in `DMVerbs::DMVerbs`
Hugepages aren't allocated on that node. Reapply:
```bash
./setup/apply_cluster_runtime.sh
```
If that still fails after allocation, memory is fragmented. Reboot the node.

### `NOT FOUND` in `serverEnter()` retry loop
Memcached wasn't seeded with `serverNum=0`. Either the `restart_memc.sh` step
failed, or a previous run left stale state. Rerun:
```bash
ssh w1 'bash ~/skipvecdm/SkipVectorDM/script/restart_memc.sh'
```

### Silent hang at cluster startup
Compute node is racing memory nodes. Confirm all N nodes registered:
```bash
ssh w1 'printf "get serverNum\r\nquit\r\n" | nc -w2 10.10.1.1 18888'
```
Should equal `machineNR` from the test's `DMConfig`.

## Typical Run Steps

1. Sync updated scripts:
   ```bash
   cd ~/skipvecdm && ./sync.sh
   ```

2. Clean leftover state:
   ```bash
   for n in w1 w2 w3 w4 w5 w6 w7; do
       ssh $n 'pkill -9 -f ycsbc; pkill -9 -f "./server"; pkill -9 -f numactl; pkill -9 memcached; sleep 1'
   done
   ```

3. Reset hugepages on memory node:
   ```bash
   ssh w1 'sudo sysctl -w vm.nr_hugepages=0 && sleep 1 && sudo sysctl -w vm.nr_hugepages=51200'
   ssh w1 'grep HugePages_Free /proc/meminfo'
   ```

4. Launch a baseline (example: DMTree):
   ```bash
   ssh w7 'rm -f /tmp/simple_exp.flag'
   ssh w7 'cd ~/skipvecdm/DMTree/script && bash run_exp0.sh 2>&1 | tee /tmp/dmtree_smoke.log'
   ```

5. Monitor:
   ```bash
   for n in w1 w2 w3 w4 w5 w6 w7; do
       echo -n "$n: "
       ssh $n "pgrep -af ycsbc 2>/dev/null | head -1 || pgrep -af '\\./server' 2>/dev/null | head -1 || echo idle"
   done
   ```