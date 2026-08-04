#!/bin/bash
set -u
# NOTE: single-quoted so $HOME resolves on the *remote* host.
REPO_DIR='$HOME/skipvecdm/SkipVectorDM'
BUILD_DIR="$REPO_DIR/build"
BIN=test_rdma_smoke

trap 'echo "Interrupt — killing MSes"; for n in 1 2 3 4; do ssh w$n "pkill -9 -f $BIN" 2>/dev/null; done; exit 1' SIGINT

echo "==> Restarting memcached + seeding coordinator keys"
if ! ssh w1 "bash $REPO_DIR/script/restart_memc.sh" > /tmp/memc_init.log 2>&1; then
    echo "ERROR: memcached restart failed — see /tmp/memc_init.log"
    cat /tmp/memc_init.log
    exit 1
fi

echo "==> Killing any lingering processes"
for n in 1 2 3 4; do ssh w$n "pkill -9 -f $BIN" 2>/dev/null; done
sleep 1

echo "==> Launching 3 memory servers"
for n in 1 2 3; do
    ssh w$n "cd $BUILD_DIR && stdbuf -oL -eL ./$BIN > /tmp/ms_$n.log 2>&1" &
    sleep 0.2
done
sleep 2

echo "==> Launching compute server on w4 (foreground)"
ssh w4 "cd $BUILD_DIR && ./$BIN" | tee /tmp/cs.log
CS_RC=${PIPESTATUS[0]}

echo "==> CS exited (rc=$CS_RC); stopping MSes"
for n in 1 2 3 4; do ssh w$n "pkill -9 -f $BIN" 2>/dev/null; done

echo "==> MS logs at wN:/tmp/ms_N.log  (ssh wN 'cat /tmp/ms_N.log')"
exit $CS_RC
