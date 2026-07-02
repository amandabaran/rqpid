#!/bin/bash

# Kill any existing memcached
if [ -f /tmp/memcached.pid ]; then
    cat /tmp/memcached.pid | xargs -r kill 2>/dev/null
fi
pkill -x memcached 2>/dev/null
sleep 2

# Launch memcached on memory node's internal IP
memcached -u "$USER" -l 10.10.1.1 -p 18888 -c 10000 -d -P /tmp/memcached.pid
sleep 2

# Initialize coordinator keys
echo -e "set serverNum 0 0 1\r\n0\r\nquit\r" | nc 10.10.1.1 18888
sleep 1
echo -e "set clientNum 0 0 1\r\n0\r\nquit\r" | nc 10.10.1.1 18888
sleep 1
