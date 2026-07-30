#!/bin/bash
# Run test_rdma_smoke: w1/w2/w3 = memory servers, w4 = compute server.
set -u

trap 'echo "Interrupt — killing MSes"; for n in 1 2 3 4; do ssh w$n "pkill -9 test_rdma_smoke" 2>/dev/null; done; exit 1' SIGINT

echo "==> Restarting memcached + seeding coordinator keys"
if ! ssh w1 'bash ~/rqpid/SkipVectorDM/script/restart_memc.sh' > /tmp/memc_init.log 2>&1; then
    echo "ERROR: memcached restart failed — see /tmp/memc_init.log"
    cat /tmp/memc_init.log
    exit 1
fi

echo "==> Killing any lingering processes"
for n in 1 2 3 4; do
    ssh w$n "pkill -9 test_rdma_smoke" 2>/dev/null
done
sleep 1

echo "==> Launching 3 memory servers"
for n in 1 2 3; do
    ssh w$n "cd ~/rqpid/SkipVectorDM/build && stdbuf -oL -eL ./test_rdma_smoke > /tmp/ms_$n.log 2>&1" &
    sleep 0.2
done

sleep 2

echo "==> Launching compute server on w4 (foreground)"
ssh w4 "cd ~/rqpid/SkipVectorDM/build && ./test_rdma_smoke" | tee /tmp/cs.log
CS_RC=${PIPESTATUS[0]}

echo "==> CS exited (rc=$CS_RC); stopping MSes"
for n in 1 2 3 4; do
    ssh w$n "pkill -9 test_rdma_smoke" 2>/dev/null
done

echo "==> MS logs at wN:/tmp/ms_N.log  (ssh wN 'cat /tmp/ms_N.log')"
exit $CS_RC