#!/bin/bash

SUCCESS=0
INVALID_BLOCK=1
CHAIN_MISMATCH=2
INVALID_TRANSACTION=3
BLOCK_NOT_FOUND=4
INVALID_ARGUMENT=5
FILE_NOT_FOUND=6
EMPTY_BLOCKCHAIN=7

comando="$1"
parametro="$2"
#echo "Script avviato, comando=$comando, parametro=$parametro"



# HASH
if [ "$comando" == "--hash" ]; then
    if [ -z "$parametro" ]; then
        echo "Errore: no stringa da convertire."
        exit $INVALID_ARGUMENT
    fi
    # calcolo hash
    hash_result=$(echo -n "$parametro" | sha256sum | awk '{print $1}')
    echo "$hash_result"
    exit 0



# MERKLE
elif [ "$comando" == "--merkle" ]; then
    tx_string="$parametro"
    # echo "DEBUG: parametro='$parametro'" >&2
    # echo "DEBUG: string='$tx_string'" >&2

    # caso stringa vuota
    if [ -z "$tx_string" ]; then
        echo -n "" | sha256sum | awk '{print $1}'
        exit 0
    fi

    declare -a tx_array=()
    resto="$tx_string"
    while [[ "$resto" == *::* ]]; do
        parte="${resto%%::*}"       # prendo tutto quello che sta prima del primo "::"
        tx_array+=("$parte")
        resto="${resto#*::}"        # rimuovo la parte appena presa + i due punti
    done
    tx_array+=("$resto")           # aggiungo l'ultima transazione rimasta

    # hash di ogni transazione singola
    declare -a hashes=()
    for tx in "${tx_array[@]}"; do
        h=$(echo -n "$tx" | sha256sum | awk '{print $1}')
        hashes+=("$h")
    done

    count=${#hashes[@]}

    # caso singola transazione
    if [ "$count" -eq 1 ]; then
        hash_vuoto="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        concatenata="${hashes[0]}${hash_vuoto}"
        risultato=$(echo -n "$concatenata" | sha256sum | awk '{print $1}')
        echo "$risultato"
        exit 0
    fi

    # calcolo merkle root livello per livello
    while [ ${#hashes[@]} -gt 1 ]; do

        # se il numero di hash è dispari, aggiungo sha256("") come padding
        if [ $(( ${#hashes[@]} % 2 )) -ne 0 ]; then
            hash_vuoto=$(echo -n "" | sha256sum | awk '{print $1}')
            hashes+=("$hash_vuoto")
        fi

        declare -a new_hashes=()
        for (( i=0; i<${#hashes[@]}; i+=2 )); do
            h1="${hashes[$i]}"
            h2="${hashes[$i+1]}"
            concatenata=$(echo -n "${h1}${h2}" | sha256sum | awk '{print $1}')
            new_hashes+=("$concatenata")
        done

        hashes=("${new_hashes[@]}")
    done

    echo "${hashes[0]}"
    exit 0



# VERIFY 
elif [ "$comando" == "--verify" ]; then
    
    csv_file="$parametro"

    if [ ! -f "$csv_file" ]; then
        echo "Errore: file CSV mancante o inaccessibile."
        exit $INVALID_ARGUMENT
    fi

    prev_index="-1"
    prev_hash=""
    is_valid=1

    cat "$csv_file" | while IFS=',' read -r index timestamp p_hash merkle nonce txs; do
        
        echo "DEBUG: leggo riga CSV -> index=$index"
        
        if [[ "$index" == "index" || "$index" == "index"* ]]; then
            # echo "DEBUG: salto l'intestazione"
            continue
        fi

        if [ "$prev_index" != "-1" ]; then
            
            # echo "DEBUG: prev_index=$prev_index"
            expected_index=$((prev_index + 1))
            # echo "DEBUG: expected=$expected_index, index_letto=$index"
            
            if [ "$index" != "$expected_index" ]; then
                # echo "SONO QUI - Trovato errore nell'indice!"
                echo "Errore INVALID_BLOCK: Indice non valido trovato a $index."
                is_valid=0
                # echo "DEBUG: is_valid settato a $is_valid, esco dal while"
                break
            fi

            if [ "$p_hash" != "$prev_hash" ]; then
                echo "Errore CHAIN_MISMATCH: Hash non concatenato correttamente all'indice $index."
                is_valid=0
                break
            fi
        fi

        prev_index="$index"
        prev_hash="fake_calculated_hash" # Da sostituire con la chiamata reale ad hash
    done

    # echo "DEBUG: fuori dal while"
    # echo "DEBUG: is_valid vale $is_valid"

    # =================================================================================
    # TODO: why is_valid stampa sempre 1 anche se entra nel break???
    # =================================================================================


    if [ $is_valid -eq 1 ]; then
        echo "Integrità della Blockchain verificata con successo."
        exit 0
    else
        exit $INVALID_BLOCK
    fi

else
    echo "Comandi disponibili: ./Blockchain.sh {--hash|--merkle|--verify} <argomento>"
    echo "./blockchain.sh --hash <string>"
    echo "./blockchain.sh --merkle <list of::various::transactions:Alice pays Bob 10 coins"
    echo "./blockchain.sh --verify <file.csv>"
    exit 1
fi