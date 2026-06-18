#!/bin/bash

comando="$1"
#echo "Script avviato"



if [ "$comando" == "build" ]; then
    echo "[build] Compilo blockchain"
    gcc source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/log.c source/sha256.c -I include -o blockchain -lrt -lpthread

    if [ $? -eq 0 ]; then
        echo "Compilato blockchain"
    else
        echo "Errore di compilazione"
        exit 1
    fi

elif [ "$comando" == "clean" ]; then
    echo "[clean] Rimuovo eseguibile e log"
    rm -f "blockchain"
    rm -f logs/*.log

    echo "[clean] Rimuovo FIFO residue"
    rm -f node_*.fifo
    rm -f parent.fifo

    echo "[clean] Rimuovo shared memory e semafori"
    rm -f "/dev/shm/blockchain_shm"
    rm -f "/dev/shm/sem.sem_blockchain" 
    rm -f "/dev/shm/sem.sem_block"

    echo "[clean] Rimuovo coda messaggi SysV residua (utente corrente)"
    for id in $(ipcs -q | awk -v u="$(whoami)" '$3==u {print $2}'); do
        ipcrm -q "$id" 2>/dev/null
    done

    echo "[clean] Rimuovo MSGQUEUE"
    rm -f "/tmp/blockchain_queue"

    echo "[clean] Sistema pulito"

elif [ "$comando" == "run" ]; then
    num_nodi="$2"
    num_miner="$3"
    num_client="$4"
    tx_frequency="$5"
    difficulty="$6"

    if [[ "$num_nodi" -lt 1 || "$num_nodi" -gt 16 ]]; then
        echo "Numero nodi non valido (min 1, max 16)"
        exit 1
    fi
    if [[ "$num_miner" -lt 1 || "$num_miner" -gt 16 ]]; then
        echo "Numero miner non valido (min 1, max 16)"
        exit 1
    fi
    if [[ "$num_client" -lt 1 || "$num_client" -gt 16 ]]; then
        echo "Numero client non valido (min 1, max 16)"
        exit 1
    fi
    if [[ "$tx_frequency" -lt 1 ]]; then
        echo "Frequenza transazioni: valore minimo 1"
        exit 1
    fi
    if [[ "$difficulty" -lt 1 ]]; then
        echo "Difficolta': valore minimo 1"
        exit 1
    fi

    echo "[run] Avvio blockchain $num_nodi $num_miner $num_client $tx_frequency $difficulty"
    ./blockchain "$num_nodi" "$num_miner" "$num_client" "$tx_frequency" "$difficulty"

else
    echo "Comandi disponibili: ./build.sh {build | clean | run}"
    echo "./build.sh build"
    echo "./build.sh clean"
    echo "./build.sh run <num_nodi> <num_miner> <num_client> <tx_frequency> <difficulty>"
    exit 1
fi

