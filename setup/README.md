## Cluster Setup Steps for skipvecdm

Fromt the setup/ directory, run the following commands:

1. Run `./setup_local_hosts.sh manifest.xml`
 
2. Run `./setup_ssh_hosts.sh manifest.xml`

3. Run `./setup_env.sh`

## Cluster RDMA Config

`include/RdmaVerbs.h` hardcodes four values that must match the target cluster:

```cpp
#define NET_DEV_NAME    "enp8s0d1"    // Ethernet iface on the private subnet
#define IB_DEV_NAME_IDX '0'           // char at index 5 of the RDMA device name
#define MLX_PORT        1             // 1-indexed port on that device
// and, in the same file, the createContext() declaration:
bool createContext(..., int gidIndex = 0, ...);
```

To find the right values, run on any cluster node:

```bash
ibv_devinfo                                    # pick the ACTIVE port
ip -o -4 addr show | grep 10.10.1.             # find its Ethernet iface
```

Pick the `ibv_devinfo` port with `state: PORT_ACTIVE (4)`:
- `hca_id: mlx4_0` → `IB_DEV_NAME_IDX = '0'`
- `port: 1` → `MLX_PORT = 1`
- `link_layer: InfiniBand` → `gidIndex = 0`
If `link_layer: Ethernet` (RoCE) → find the `type=RoCE v2` GID with an IPv4-mapped
  suffix (`::ffff:0a0a:XXXX`):
  ```bash
  DEV=mlx5_2; PORT=1
  for i in $(seq 0 7); do
    echo "gid[$$i] $$(cat /sys/class/infiniband/$$DEV/ports/$$PORT/gid_attrs/types/$$i 2>/dev/null) $$(cat /sys/class/infiniband/$$DEV/ports/$$PORT/gids/$i 2>/dev/null)"
  done
  ```

`NET_DEV_NAME` is the Ethernet interface (not the RDMA device) that has the
node's private-subnet IP; the code uses it to advertise itself to memcached.

To apply the same config to all baselines at once:
```bash
./set_rdma_config.sh <NET_DEV_NAME> <IB_DEV_NAME_IDX> <MLX_PORT> <gidIndex>
```

**Verify:**

After updating `include/RdmaVerbs.h`, verify RDMA is working correctly:

```bash
./sync.sh
./build_all.sh test_rdma_context
for n in w1 w2 w3 w4; do ssh $n '~/rqpid/SkipVectorDM/build/test_rdma_context'; done
```
Each node should print nonzero `lkey`/`rkey` and a nonzero GID.

After confirming RDMA contexts are correct, run the RDMA smoke test. 
```bash
./build_all.sh test_rdma_smoke
./run_smoke.sh
```

**Prerequisites** (if `ibv_devinfo` is empty):
- `sudo modprobe mlx5_ib` (or `mlx4_ib`) `ib_uverbs rdma_ucm`
- InfiniBand only: `sudo apt install opensm && sudo systemctl start opensm`

**Reference configs seen so far:**

| Hardware              | ETH iface   | Link            | DEV_IDX | PORT | GID | COMMAND                                    |
|-----------------------|-------------|-----------------|---------|------|-----|--------------------------------------------|
| r650 (ConnectX-6)     | `ens1f1np1` | Ethernet/RoCEv2 | `'2'`   | 1    | 3   | ./setup/set_rdma_config.sh ens1f1np1 2 1 3 |
| r320 (ConnectX-3)     | `enp8s0d1`  | InfiniBand      | `'0'`   | 1    | 0   | ./setup/set_rdma_config.sh enp8s0d1 0 1 0  |

**Note on Memcached:**
If memcached is not running on 10.10.1.1 at port 18888, update memcached.conf file within each system subdirectory. 



## Build and Sync

To sync local repo to cluster and build, run:
```bash
    for n in w1 w2 w3 w4 w5 w6 w7; do
        ssh w$n 'rm -f /tmp/build_ae.flag'
        cd ~/skipvecdm && ./sync.sh
        ssh w$n 'cd ~/skipvecdm && bash build_ae.sh 2>&1 | tail -5'
    done
```

## Common Issues

# Fragmented memory (mmap seg fault in DMVerbs)
    FIX: Reboot all nodes in experiment!

# Typical Run Steps:
1. Sync the updated scripts
`cd ~/skipvecdm && ./sync.sh`

2. Clean any leftover state
Nuke ALL ycsbc / server / memcached / numactl everywhere:
```bash
for n in w1 w2 w3 w4 w5 w6 w7; do
    ssh $n 'pkill -9 -f ycsbc; pkill -9 -f "./server"; pkill -9 -f numactl; pkill -9 memcached; sleep 1'
done
```

3. Reset hugepages cleanly on memory node
`ssh w1 'sudo sysctl -w vm.nr_hugepages=0 && sleep 1 && sudo sysctl -w vm.nr_hugepages=51200'`
`ssh w1 'cat /proc/meminfo | grep HugePages_Free'  # confirm 51200`

4. Launch DMTree
`ssh w7 'rm -f /tmp/simple_exp.flag'`
`ssh w7 'cd ~/skipvecdm/DMTree/script && bash run_exp0.sh 2>&1 | tee /tmp/dmtree_smoke.log'`

5. Monitor a Run

In a second shell run:  
```bash                   
    for n in w1 w2 w3 w4 w5 w6 w7; do
        echo -n "$n: "
        ssh $n "pgrep -af ycsbc 2>/dev/null | head -1 || pgrep -af \"\\./server\" 2>/dev/null | head -1 || echo idle"
    done
    echo ""
    echo "=== Output file sizes ==="
    for n in w2 w3 w4 w5 w6 w7; do
        echo -n "$n: "
        ssh $n "ls -la ~/skipvecdm/DMTree/data/ 2>/dev/null | grep dmtree | tail -1 | awk \"{print \\\$5,\\\$9}\""
    done
```