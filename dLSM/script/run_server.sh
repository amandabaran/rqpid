#!/bin/bash

# Define the dLSM directory as a variable
dm_tree_dir="$HOME/skipvecdm/dLSM"

# Navigate to the build directory under dLSM
cd $dm_tree_dir/build

# Start the server in the background and log its process ID
nohup numactl --interleave=all ./Server 19843 150 0 >> log.out 2>&1 & echo $! > /tmp/server_log.out
echo "server started"