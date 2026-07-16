#!/bin/bash

# Define the SkipVectorDM directory as a variable
dm_tree_dir="$HOME/rqpid/SkipVectorDM"

# Navigate to the build directory under SkipVectorDM
cd $dm_tree_dir/build

# Start the server in the background and log its process ID
sleep 10
nohup numactl --interleave=all ./server >> log.out 2>&1 & echo $! > /tmp/server_log.out
echo "server started"

# Sleep for 100 seconds
sleep 100