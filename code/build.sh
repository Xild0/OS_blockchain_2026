#!/bin/bash
#
# Build / run helper for the blockchain project.
#
# Usage:
#   ./build.sh build
#   ./build.sh run <num_nodes> <num_miners> <num_clients> [tx_frequency] [difficulty] [initial_state.csv]
#   ./build.sh clean

CC=gcc
CFLAGS="-Wall -Wextra -I include"
LDFLAGS="-lrt -lpthread"
SRC="source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/log.c source/sha256.c"
OUT="blockchain"

is_positive_int() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

case "$1" in
    build)
        echo "Compiling..."
        $CC $CFLAGS $SRC -o $OUT $LDFLAGS
        if [ $? -eq 0 ]; then
            echo "Build successful: ./$OUT"
        else
            echo "Build failed"
            exit 1
        fi
        ;;

    run)
        shift
        num_nodes="$1"
        num_miners="$2"
        num_clients="$3"
        tx_frequency="$4"
        difficulty="$5"
        initial_state="$6"

        if [ -z "$num_nodes" ] || [ -z "$num_miners" ] || [ -z "$num_clients" ]; then
            echo "Usage: ./build.sh run <num_nodes> <num_miners> <num_clients> [tx_frequency] [difficulty] [initial_state.csv]"
            exit 1
        fi

        for arg in "$num_nodes" "$num_miners" "$num_clients"; do
            if ! is_positive_int "$arg"; then
                echo "Error: num_nodes, num_miners and num_clients must be positive integers"
                exit 1
            fi
        done

        if [ -n "$tx_frequency" ] && ! is_positive_int "$tx_frequency"; then
            echo "Error: tx_frequency must be a positive integer"
            exit 1
        fi

        if [ -n "$difficulty" ] && ! is_positive_int "$difficulty"; then
            echo "Error: difficulty must be a positive integer"
            exit 1
        fi

        if [ -n "$initial_state" ] && [ ! -f "$initial_state" ]; then
            echo "Error: initial state file not found: $initial_state"
            exit 1
        fi

        if [ ! -x "./$OUT" ]; then
            echo "Executable not found, building first..."
            "$0" build || exit 1
        fi

        # Pass every provided argument through to the program.
        ./$OUT "$num_nodes" "$num_miners" "$num_clients" $tx_frequency $difficulty $initial_state
        ;;

    clean)
        echo "Cleaning up..."
        rm -f "$OUT"
        rm -f logs/*.log
        rm -f node_*_block.fifo node_*_cmd.fifo parent.fifo
        echo "Done"
        ;;

    *)
        echo "Usage: ./build.sh {build|run|clean}"
        echo "  build                                  compile the project"
        echo "  run <nodes> <miners> <clients> [freq] [difficulty] [initial_state.csv]"
        echo "  clean                                  remove binary, logs and FIFOs"
        exit 1
        ;;
esac