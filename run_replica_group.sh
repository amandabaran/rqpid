#!/bin/bash
# Run test_replica_group: w1/w2/w3 = MSes, w4 = CS.
set -u

trap 'echo "Interrupt"; for n in 1 2 3 4; do ssh w$n "pkill -9 -f test_replica_group" 2>/dev/null; done; exit 1' SIGINT

echo "==> Restarting memcached + seeding coordinator keys"
if ! ssh w1 'bash ~/skipvecdm/SkipVectorDM/script/restart_memc.sh' > /tmp/memc_init.log 2>&1; then
    echo "ERROR: memcached restart failed"
    cat /tmp/memc_init.log
    exit 1
fi

echo "==> Killing any lingering processes"
for n in 1 2 3 4; do
    ssh w$n "pkill -9 -f test_replica_group" 2>/dev/null
done
sleep 1

echo "==> Launching 3 memory servers"
for n in 1 2 3; do
    ssh w$n "cd ~/skipvecdm/SkipVectorDM/build && stdbuf -oL -eL ./test_replica_group > /tmp/ms_$n.log 2>&1" &
    sleep 0.2
done

sleep 2

echo "==> Launching compute server on w4 (foreground)"
ssh w4 "cd ~/skipvecdm/SkipVectorDM/build && ./test_replica_group" | tee /tmp/cs.log
CS_RC=${PIPESTATUS[0]}

echo "==> CS exited (rc=$CS_RC); stopping MSes"
for n in 1 2 3 4; do
    ssh w$n "pkill -9 -f test_replica_group" 2>/dev/null
done

echo "==> MS logs at wN:/tmp/ms_N.log"
exit $CS_RC