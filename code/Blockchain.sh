#!/bin/bash

INVALID_BLOCK=1
CHAIN_MISMATCH=2
INVALID_TRANSACTION=3
BLOCK_NOT_FOUND=4
GENERIC_ERROR=5

comando="$1"
parametro="$2"

# echo "DEBUG: script avviato, comando=$comando, parametro=$parametro"



# HASH
if [ "$comando" == "--hash" ] || [ "$comando" == "-hash" ]; then
    
    if [ -z "$parametro" ]; then
        echo "Errore: block_hex mancante."
        exit $GENERIC_ERROR
    fi

    # Decodifica stringa esadecimale e calcolo hash
    hash_result=$(echo -n "$parametro" | xxd -r -p | sha256sum | awk '{print $1}')
    # echo "DEBUG: hash_result parziale = $hash_result"
    
    echo "$hash_result"
    exit 0




# MERKLE
elif [ "$comando" == "--merkle" ] || [ "$comando" == "-merkle" ]; then
    
    tx_string="$parametro"
    # echo "DEBUG: stringa transazioni ricevuta: $tx_string"

    if [ -z "$tx_string" ]; then
        echo -n "" | sha256sum | awk '{print $1}'
        exit 0
    fi

    tx_string_single_colon="${tx_string//::/:}"
    IFS=':' read -r -a tx_array <<< "$tx_string_single_colon"
    
    # echo "DEBUG: separate ${#tx_array[@]} transazioni"

    declare -a hashes=()

    for tx in "${tx_array[@]}"; do
        h=$(echo -n "$tx" | sha256sum | awk '{print $1}')
        # echo "DEBUG: hash transazione singola: $h"
        hashes+=("$h")
    done

    while [ ${#hashes[@]} -gt 1 ]; do
        # echo "DEBUG: inizio nuovo livello albero, elementi attuali = ${#hashes[@]}"
        
        if [ $(( ${#hashes[@]} % 2 )) -ne 0 ]; then
            # echo "DEBUG: numero dispari, aggiungo padding vuoto"
            # ============================
            # TODO: padding numero dispari
            # ============================

            empty_hash=$(echo "" | sha256sum | awk '{print $1}')
            hashes+=("$empty_hash")
            # echo "DEBUG: hash stringa vuota aggiunto = $empty_hash"
        fi

        declare -a new_hashes=()
        
        for (( i=0; i<${#hashes[@]}; i+=2 )); do
            h1="${hashes[$i]}"
            h2="${hashes[$i+1]}"
            
            # echo "DEBUG: sto accoppiando indice $i e $((i+1))"
            combined=$(echo -n "${h1}${h2}" | sha256sum | awk '{print $1}')
            new_hashes+=("$combined")
        done
        
        hashes=("${new_hashes[@]}")
    done

    # echo "DEBUG: finito! stampo la root"
    echo "${hashes[0]}"
    exit 0




# VERIFY 
elif [ "$comando" == "--verify" ] || [ "$comando" == "-verify" ]; then
    
    csv_file="$parametro"

    if [ ! -f "$csv_file" ]; then
        echo "Errore: file CSV mancante o inaccessibile."
        exit $BLOCK_NOT_FOUND
    fi

    prev_index="-1"
    prev_hash=""
    is_valid=1

    cat "$csv_file" | while IFS=',' read -r index timestamp p_hash merkle nonce txs; do
        
        # echo "DEBUG: leggo riga CSV -> index=$index"
        
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
    echo "Uso: ./blockchain.sh {--hash|--merkle|--verify} <argomento>"
    exit 1
fi