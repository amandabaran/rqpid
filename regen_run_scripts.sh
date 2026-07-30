#!/bin/bash
# Regenerate run_exp0.sh and run_exp14.sh for every baseline.
# Run from ~/skipvecdm/

set -e

# baseline dir name -> tag used in output filenames
declare -A TAGS=(
    [DMTree]=dmtree
    [Sherman]=sherman
    [SMART]=smart
    [ROLEX]=rolex
    [CHIME]=chime
    [FPTree]=fptree
    [dLSM]=dlsm
)

# baselines that DO use hugepages
HUGEPAGE_BASELINES=(DMTree Sherman SMART ROLEX CHIME FPTree)

is_hugepage_baseline() {
    local b=$1
    for x in "${HUGEPAGE_BASELINES[@]}"; do
        [[ "$x" == "$b" ]] && return 0
    done
    return 1
}

# baselines with a special pre/post scan-only hook (load_keys.data)
needs_load_keys_cleanup() {
    [[ "$1" == "ROLEX" ]]
}

gen_script() {
    local baseline=$1     # e.g., DMTree
    local exp=$2          # exp0 or exp14
    local tag=${TAGS[$baseline]}
    local outpath="$baseline/script/run_${exp}.sh"

    # Workloads differ between exp0 (micro) and exp14 (YCSB)
    local workloads
    if [ "$exp" = "exp0" ]; then
        workloads="ycsb-c insert-only update-only scan-only"
    else
        workloads="ycsb-a ycsb-b ycsb-c ycsb-d ycsb-e ycsb-f"
    fi

    # Hugepage block
    local hp_mem hp_cn hp_local
    if is_hugepage_baseline "$baseline"; then
        hp_mem=51200; hp_cn=12768; hp_local=12768
    else
        hp_mem=0; hp_cn=0; hp_local=0
    fi

    # scan-only cleanup snippet (ROLEX only, only for exp0 since exp14 has no scan-only)
    local cleanup_pre="" cleanup_post=""
    if needs_load_keys_cleanup "$baseline" && [ "$exp" = "exp0" ]; then
        cleanup_pre='            if [ "$file_name" = "scan-only" ]; then
                for n in $node; do ssh w$n "rm -f $base_dir/build/load_keys.data"; done
                rm -f "$base_dir/build/load_keys.data"
            fi'
        cleanup_post="$cleanup_pre"
    fi

    cat > "$outpath" <<EOF
#!/bin/bash
trap 'kill \$(jobs -p)' SIGINT

base_dir="\$HOME/skipvecdm/$baseline"
ae_data_dir="\$HOME/skipvecdm/AE/Data"

workloads="$workloads"
threads="72"
distribution="zipfian uniform"

# Cluster layout
node="2 3 4 5 6"
driver="7"
memory_nodes="1"

mkdir -p "\$base_dir/data"
for n in \$node \$memory_nodes; do
    ssh w\$n "mkdir -p \$base_dir/data"
done
mkdir -p "\$ae_data_dir"

echo "---------- Configuring hugepages ----------"
for n in \$memory_nodes; do
    ssh w\$n "sudo sysctl -w vm.nr_hugepages=$hp_mem"
done
for n in \$node; do
    ssh w\$n "sudo sysctl -w vm.nr_hugepages=$hp_cn"
done
sudo sysctl -w vm.nr_hugepages=$hp_local

for dis in \$distribution; do
    for thread in \$threads; do
        for file_name in \$workloads; do
            echo "============================="
            echo "Starting run for \$dis-\$file_name with thread \$thread"
$cleanup_pre
            for m in \$memory_nodes; do
                ssh w\$m "bash -c 'cd \$base_dir/script && bash restart_memc.sh'"
                ssh w\$m "bash -c 'cd \$base_dir/script && bash run_server.sh'" &
            done
            sleep 5

            for n in \$node; do
                sleep 2
                ssh w\$n "cd \$base_dir/build && nohup ./ycsbc \$thread 4 \$file_name \$dis > \$base_dir/data/node\$n-${exp}_${tag}_\$file_name-\$dis-thread\$thread-coro4.txt 2>&1" &
            done

            sleep 2
            cd \$base_dir/build && ./ycsbc \$thread 4 \$file_name \$dis \\
                > \$base_dir/data/node\$driver-${exp}_${tag}_\$file_name-\$dis-thread\$thread-coro4.txt 2>&1

            for m in \$memory_nodes; do
                ssh w\$m "bash -c 'cd \$base_dir/script && bash kill_server.sh'"
            done

            for n in \$node; do
                scp "w\$n:\$base_dir/data/node\$n-${exp}_${tag}_\$file_name-\$dis-thread\$thread-coro4.txt" "\$ae_data_dir/"
            done
            cp "\$base_dir/data/node\$driver-${exp}_${tag}_\$file_name-\$dis-thread\$thread-coro4.txt" "\$ae_data_dir/"
$cleanup_post
            wait
        done
    done
done

echo "All $baseline $exp tasks finished"
EOF
    chmod +x "$outpath"
    echo "Generated $outpath"
}

for baseline in "${!TAGS[@]}"; do
    [ -d "$baseline/script" ] || { echo "Skipping $baseline (no script/ dir)"; continue; }
    gen_script "$baseline" exp0
    gen_script "$baseline" exp14
done