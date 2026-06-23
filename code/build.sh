#!/bin/bash
# Usage:
#   ./build.sh build
#   ./build.sh run <num_nodes> <num_miners> <num_clients> [tx_frequency] [difficulty] [initial_state.csv]
#   ./build.sh clean

comando="$1"

if [ "$comando" == "build" ]; then
    echo "[build] Compile blockchain"
    gcc source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/log.c source/sha256.c -I include -o blockchain -lrt -lpthread
 
    if [ $? -eq 0 ]; then
        echo "Compiled blockchain"
    else
        echo "Error during compilation"
        exit 1
    fi
 
elif [ "$comando" == "clean" ]; then
    echo "[clean] Removing exec and log"
    rm -f "blockchain"
    rm -f "source/blockchain.o"
    rm -f logs/*.log
 
    echo "[clean] Removing FIFO"
    rm -f node_*.fifo
    rm -f parent.fifo
 
    echo "[clean] Removing shared memory and semaphores"
    rm -f "/dev/shm/blockchain_shm"
    rm -f "/dev/shm/sem.sem_blockchain" 
    rm -f "/dev/shm/sem.sem_block"
 
    echo "[clean] Removing remaining user message queue SysV"
    for id in $(ipcs -q | awk -v u="$(whoami)" '$3==u {print $2}'); do
        ipcrm -q "$id" 2>/dev/null
    done
 
    echo "[clean] Removing MSGQUEUE"
    rm -f "/tmp/blockchain_queue"
 
    echo "[clean] System clean"
 
elif [ "$comando" == "run" ]; then          # if no values are added, we use fault values
    num_nodi="${2:-2}"                      # default: 2
    num_miner="${3:-2}"                     # default: 2
    num_client="${4:-3}"                    # default: 3
    tx_frequency="${5:-1}"                  # default: 1
    difficulty="${6:-12}"                   # default: 12
    initial_state="${7:-}"                  # optional: initial state of blockchain
 
    if [[ "$num_nodi" -lt 1 || "$num_nodi" -gt 16 ]]; then
        echo "Invalid node number (min 1, max 16)"
        exit 1
    fi
    if [[ "$num_miner" -lt 1 || "$num_miner" -gt 16 ]]; then
        echo "Invalid miner number (min 1, max 16)"
        exit 1
    fi
    if [[ "$num_client" -lt 1 || "$num_client" -gt 16 ]]; then
        echo "Invalid client number (min 1, max 16)"
        exit 1
    fi
    if [[ "$tx_frequency" -lt 1 ]]; then
        echo "Invalid transaction frequency value: min. 1"
        exit 1
    fi
    if [[ "$difficulty" -lt 1 ]]; then
        echo "Invalid difficulty value: min. 1"
        exit 1
    fi
 
    echo "[run] Running blockchain $num_nodi $num_miner $num_client $tx_frequency $difficulty $initial_state"
    ./blockchain "$num_nodi" "$num_miner" "$num_client" "$tx_frequency" "$difficulty" $initial_state
 
else
    echo "Available commands: ./build.sh {build | clean | run}"
    echo "./build.sh build"
    echo "./build.sh clean"
    echo "./build.sh run <num_nodi> <num_miner> <num_client> <tx_frequency> <difficulty> [initial_state.csv]"
    exit 1
fi
 
