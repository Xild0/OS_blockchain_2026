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
    
    string="$parametro"
    # echo "Stringa transazioni ricevuta: $tx_string"

    if [ -z "$string" ]; then
        hash_result=$(echo -n "" | sha256sum | awk '{print $1}')
        exit 0
    fi

    string_singola="${string//::/:}"
    IFS=':' read -r -a tx_array <<< "$string_singola"
    
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
    echo "./Blockchain.sh --hash <string>"
    echo "./Blockchain.sh --merkle <list of::various::transactions:Alice pays Bob 10 coins"
    echo "./Blockchain.sh --verify <file.csv>"
    exit 1
fi