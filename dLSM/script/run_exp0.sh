#!/bin/bash
trap 'kill $(jobs -p)' SIGINT

base_dir="$HOME/skipvecdm/dLSM"
ae_data_dir="$HOME/skipvecdm/AE/Data"

workloads="ycsb-c insert-only update-only scan-only"
threads="72"
distribution="zipfian uniform"

# Cluster layout
node="2 3 4 5 6"
driver="7"
memory_nodes="1"

mkdir -p "$base_dir/data"
for n in $node $memory_nodes; do
    ssh w$n "mkdir -p $base_dir/data"
done
mkdir -p "$ae_data_dir"

echo "---------- Configuring hugepages ----------"
for n in $memory_nodes; do
    ssh w$n "sudo sysctl -w vm.nr_hugepages=0"
done
for n in $node; do
    ssh w$n "sudo sysctl -w vm.nr_hugepages=0"
done
sudo sysctl -w vm.nr_hugepages=0

for dis in $distribution; do
    for thread in $threads; do
        for file_name in $workloads; do
            echo "============================="
            echo "Starting run for $dis-$file_name with thread $thread"

            for m in $memory_nodes; do
                ssh w$m "bash -c 'cd $base_dir/script && bash restart_memc.sh'"
                ssh w$m "bash -c 'cd $base_dir/script && bash run_server.sh'" &
            done
            sleep 5

            for n in $node; do
                sleep 2
                ssh w$n "cd $base_dir/build && nohup numactl --interleave=all ./ycsbc $thread 4 $file_name $dis > $base_dir/data/node$n-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt 2>&1" &
            done

            sleep 2
            cd $base_dir/build && numactl --interleave=all ./ycsbc $thread 4 $file_name $dis \
                > $base_dir/data/node$driver-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt 2>&1

            for m in $memory_nodes; do
                ssh w$m "bash -c 'cd $base_dir/script && bash kill_server.sh'"
            done

            for n in $node; do
                scp "w$n:$base_dir/data/node$n-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt" "$ae_data_dir/"
            done
            cp "$base_dir/data/node$driver-exp0_dlsm_$file_name-$dis-thread$thread-coro4.txt" "$ae_data_dir/"

            wait
        done
    done
done

echo "All dLSM exp0 tasks finished"
