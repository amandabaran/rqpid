#!/bin/bash
trap 'kill $(jobs -p)' SIGINT

# All cluster nodes are remote from the laptop
remote_nodes="1 2 3 4 5 6 7"
target_user="adb321"                       # cluster username (not laptop user)
mem_threshold_kb=$((1024 * 512))           # 512MB
log_file="drop_cache_kill.log"

echo "========== Starting memory cleanup ==========" | tee $log_file

for n in $remote_nodes; do
    echo "========== [w$n] ==========" | tee -a $log_file

    ssh w$n "bash -s" <<EOF | tee -a $log_file
echo "--- Before cleanup ---"
ps -u $target_user -eo pid,rss,comm --sort=-rss 2>/dev/null | \
    awk '{ printf "%s\t%.2f MB\t%s\n", \$1, \$2/1024, \$3 }' | head -n 10

for pid in \$(ps -u $target_user -o pid= 2>/dev/null); do
    rss=\$(awk '/VmRSS/ {print \$2}' /proc/\$pid/status 2>/dev/null)
    if [ -n "\$rss" ] && [ "\$rss" -gt $mem_threshold_kb ]; then
        echo "Killing PID \$pid (VmRSS=\$rss kB > $mem_threshold_kb kB)"
        sudo kill -9 \$pid 2>/dev/null
    fi
done

sudo sync
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

echo "--- After cleanup ---"
ps -u $target_user -eo pid,rss,comm --sort=-rss 2>/dev/null | \
    awk '{ printf "%s\t%.2f MB\t%s\n", \$1, \$2/1024, \$3 }' | head -n 10
EOF

done

echo "========== Cleanup complete ==========" | tee -a $log_file