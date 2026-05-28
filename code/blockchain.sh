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
    if [ "$#" -lt 2 ]; then
        echo "Errore: nessuna stringa da convertire."
        exit $INVALID_ARGUMENT
    elif [ -z "$2" ]; then 
        hash_result=$(echo -n "" | sha256sum | awk '{print $1}')
        echo $hash_result
        exit 0
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

    if [ "$#" -lt 2 ]; then
        echo "Errore: nessuna stringa da convertire."
        exit $INVALID_ARGUMENT

    # caso stringa vuota
    elif [ -z "$2" ]; then
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

    # controllo dell'argomento
    if [ -z "$csv_file" ]; then
        echo "Errore: specificare file CSV."
        exit $INVALID_ARGUMENT
    fi

    # controllo che il file esista
    if [ ! -f "$csv_file" ]; then
        echo "Errore: file CSV non trovato."
        exit $FILE_NOT_FOUND
    fi

    # controllo blockchain
    block_count=$(awk '!/^index/' "$csv_file" | wc -l)
    if [ "$block_count" -eq 0 ]; then
        echo "Errore: la blockchain è vuota."
        exit $EMPTY_BLOCKCHAIN
    fi

    prev_index_dec=-1
    prev_hash=""
    is_valid=1

    while IFS=',' read -r idx ts p_hash merkle nonce txs; do

        # salto riga di intestazione
        if [[ "$idx" == "index"* ]]; then
            continue
        fi

        # converto indice da hex a decimale
        idx_dec=$(printf '%d' "0x$idx")

        # il primo blocco deve avere indice 0
        if [ "$prev_index_dec" -eq -1 ] && [ "$idx_dec" -ne 0 ]; then
            echo "Errore BLOCK_NOT_FOUND: il blocco genesi manca."
            is_valid=0
            break
        fi

        # dal secondo blocco in poi controllo indice e prev_hash
        if [ "$prev_index_dec" -ne -1 ]; then
            expected=$((prev_index_dec + 1))
            if [ "$idx_dec" -ne "$expected" ]; then
                echo "Errore INVALID_BLOCK: indice non valido (atteso $expected, trovato $idx_dec)"
                is_valid=0
                break
            fi
            if [ "$p_hash" != "$prev_hash" ]; then
                echo "Errore CHAIN_MISMATCH: prev_hash non corretto al blocco $idx_dec"
                is_valid=0
                break
            fi
        fi

        txs_clean="${txs%\"}"
        txs_clean="${txs_clean#\"}"
        if [ -z "$txs_clean" ]; then
            echo "Errore INVALID_TRANSACTION: nessuna transazione al blocco $idx_dec"
            is_valid=0
            break
        fi

        # verifico merkle root
        computed_merkle=$(./blockchain.sh --merkle "$txs_clean")
        if [ "$computed_merkle" != "$merkle" ]; then
            echo "Errore INVALID_BLOCK: merkle root non valida al blocco $idx_dec"
            is_valid=0
            break
        fi

        # calcolo l'hash del blocco. Questo hash diventerà il prev_hash per il successivo blocco
        block_data="${idx}${ts}${p_hash}${merkle}${nonce}${txs_clean}"
        prev_hash=$(./blockchain.sh --hash "$block_data")
        prev_index_dec="$idx_dec"

    done < "$csv_file"

    if [ "$is_valid" -eq 1 ]; then
        echo "Integrità della Blockchain verificata con successo."
        exit $SUCCESS
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