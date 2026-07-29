Setup Steps for RQPID

# 1. Run ./setup_local_hosts.sh manifest.xml
 
# 2. Run ./setup_ssh_hosts.sh manifest.xml

# 3. Run ./setup_env.sh

# 4. Update memcached.conf files
    If memcached is not running on 10.10.1.1 at port 18888, update memcached.conf file within each system subdirectory. 

# 5. Edit include/RdmaVerbs.h within each desired repo (e.g., SkipVector) to reflect cluster.
    On r650:
    #define NET_DEV_NAME "enp202s0f0np0"    // [CONFIG]
    #define IB_DEV_NAME_IDX '2'             // [CONFIG]
    #define MLX_PORT 1                      // [CONFIG]


# 6. Build and Sync to all nodes.
    for n in w1 w2 w3 w4 w5 w6 w7; do
        ssh w$n 'rm -f /tmp/build_ae.flag'
        cd ~/rqpid && ./sync.sh
        ssh w$n 'cd ~/rqpid && bash build_ae.sh 2>&1 | tail -5'
    done


# Common Issues

## Fragmented memory (mmap seg fault in DMVerbs)
    FIX: Reboot all nodes in experiment!

# Typical Run Steps:
## Sync the updated scripts
cd ~/rqpid && ./sync.sh

## Clean any leftover state
# Nuke ALL ycsbc / server / memcached / numactl everywhere
for n in w1 w2 w3 w4 w5 w6 w7; do
    ssh $n 'pkill -9 -f ycsbc; pkill -9 -f "./server"; pkill -9 -f numactl; pkill -9 memcached; sleep 1'
done


## Reset hugepages cleanly on memory node
ssh w1 'sudo sysctl -w vm.nr_hugepages=0 && sleep 1 && sudo sysctl -w vm.nr_hugepages=51200'
ssh w1 'cat /proc/meminfo | grep HugePages_Free'  # confirm 51200

## Launch DMTree
ssh w7 'rm -f /tmp/simple_exp.flag'
ssh w7 'cd ~/rqpid/DMTree/script && bash run_exp0.sh 2>&1 | tee /tmp/dmtree_smoke.log'


# Monitor a Run

In a second shell run:

watch -n 10 'echo "=== $(date +%H:%M:%S) ==="                       
for n in w1 w2 w3 w4 w5 w6 w7; do
    echo -n "$n: "
    ssh $n "pgrep -af ycsbc 2>/dev/null | head -1 || pgrep -af \"\\./server\" 2>/dev/null | head -1 || echo idle"
done
echo ""
echo "=== Output file sizes ==="
for n in w2 w3 w4 w5 w6 w7; do
    echo -n "$n: "
    ssh $n "ls -la ~/rqpid/DMTree/data/ 2>/dev/null | grep dmtree | tail -1 | awk \"{print \\\$5,\\\$9}\""
done'
