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


# ASCII string conversion to hex representation
ascii_to_hex() {
    printf '%s' "$1" | od -An -v -tx1 | tr -d ' \n'
}

# hex string conversion to ASCII representation
hex_to_ascii() {
    local hex="$1"
    [ -z "$hex" ] && return 0
    printf '%b' "$(printf '%s' "$hex" | sed 's/../\\x&/g')"
}


# HASH
if [ "$comando" == "--hash" ]; then
    block_hex="$parametro"

    if [ "$#" -lt 2 ] || [ -z "$block_hex" ]; then
        echo "Errore INVALID_ARGUMENT: no block to convert"
        exit $INVALID_ARGUMENT
    fi

    # block arrive in hex format
    # index(16) timestamp(16) prev_hash(64) merkle(64) nonce(16) transactions(everything else)
    idx="${block_hex:0:16}"
    ts="${block_hex:16:16}"
    prev="${block_hex:32:64}"
    merkle="${block_hex:96:64}"
    nonce="${block_hex:160:16}"
    tx_hex="${block_hex:176}"

    tx_ascii=$(hex_to_ascii "$tx_hex")
    calc="${idx}${ts}${prev}${merkle}${nonce}${tx_ascii}"
    echo -n "$calc" | sha256sum | awk '{print $1}'
    exit 0



# MERKLE
elif [ "$comando" == "--merkle" ]; then
    tx_string="$parametro"
    # echo "DEBUG: parametro='$parametro'" >&2
    # echo "DEBUG: string='$tx_string'" >&2

    if [ "$#" -lt 2 ]; then
        echo "Error INVALID_ARGUMENT: no string to convert"
        exit $INVALID_ARGUMENT

    # caso stringa vuota
    elif [ -z "$2" ]; then
        echo -n "" | sha256sum | awk '{print $1}'
        exit 0
    fi

    declare -a tx_array=()
    resto="$tx_string"
    while [[ "$resto" == *::* ]]; do
        parte="${resto%%::*}"       # take everything before "::"
        tx_array+=("$parte")
        resto="${resto#*::}" 
    done
    tx_array+=("$resto")           # add last transaction

    # single transaction hash
    declare -a hashes=()
    for tx in "${tx_array[@]}"; do
        h=$(echo -n "$tx" | sha256sum | awk '{print $1}')
        hashes+=("$h")
    done

    count=${#hashes[@]}

    # single transaction
    if [ "$count" -eq 1 ]; then
        hash_vuoto="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        concatenata="${hashes[0]}${hash_vuoto}"
        res=$(echo -n "$concatenata" | sha256sum | awk '{print $1}')
        echo "$res"
        exit 0
    fi

    # compute merkle root
    while [ ${#hashes[@]} -gt 1 ]; do

        # padding odd number
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

    if [ -z "$csv_file" ]; then
        echo "Error INVALID_ARGUMENT: add csv file as parameter"
        exit $INVALID_ARGUMENT
    fi

    if [ ! -f "$csv_file" ]; then
        echo "Error FILE_NOT_FOUND: CSV file not found"
        exit $FILE_NOT_FOUND
    fi

    block_count=$(awk 'NF && !/^index/' "$csv_file" | wc -l)
    if [ "$block_count" -eq 0 ]; then
        echo "Error EMPTY_BLOCKCHAIN"
        exit $EMPTY_BLOCKCHAIN
    fi

    prev_index_dec=-1
    prev_hash=""
    is_valid=1
    exit_code=$SUCCESS

    while IFS=',' read -r idx ts p_hash merkle nonce txs; do

        # exclude header line
        if [[ "$idx" == "index"* ]]; then
            continue
        fi

        idx="${idx// /}"                        # remove spaces
        idx_dec=$(printf '%d' "0x$idx")         # convert index from hex to decimal

        if [ "$prev_index_dec" -eq -1 ] && [ "$idx_dec" -ne 0 ]; then
            echo "Error BLOCK_NOT_FOUND: genesis block not found"
            is_valid=0
            exit_code=$BLOCK_NOT_FOUND
            break
        fi

        if [ "$prev_index_dec" -ne -1 ]; then
            expected=$((prev_index_dec + 1))
            if [ "$idx_dec" -ne "$expected" ]; then
                echo "Error INVALID_BLOCK: invalid index (expected $expected, found $idx_dec)"
                is_valid=0
                exit_code=$INVALID_BLOCK
                break
            fi
            if [ "$p_hash" != "$prev_hash" ]; then
                echo "Error CHAIN_MISMATCH: invalid prev_hash to block $idx_dec"
                is_valid=0
                exit_code=$CHAIN_MISMATCH
                break
            fi
        fi

        txs_clean="${txs%\"}"
        txs_clean="${txs_clean#\"}"
        if [ -z "$txs_clean" ]; then
            echo "Errore INVALID_TRANSACTION: no transactions to block $idx_dec"
            is_valid=0
            exit_code=$INVALID_TRANSACTION
            break
        fi

        # verify merkle root with transactions in clean (txs_clean)
        computed_merkle=$(./blockchain.sh --merkle "$txs_clean")
        if [ "$computed_merkle" != "$merkle" ]; then
            echo "Errore INVALID_BLOCK: invalid merkle_root to block $idx_dec"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        # Reassemble hex block and compute hash 
        tx_hex=$(ascii_to_hex "$txs_clean")
        block_data="${idx}${ts}${p_hash}${merkle}${nonce}${tx_hex}"
        prev_hash=$(./blockchain.sh --hash "$block_data")
        prev_index_dec="$idx_dec"

    done < "$csv_file"

    if [ "$is_valid" -eq 1 ]; then
        echo "Verified blockchain"
        exit $SUCCESS
    else
        exit $exit_code
    fi

else
    echo "Available commands: ./Blockchain.sh {--hash|--merkle|--verify} <argomento>"
    echo "./blockchain.sh --hash <block_hex>"
    echo "./blockchain.sh --merkle <list of::various::transactions:Alice pays Bob 10 coins"
    echo "./blockchain.sh --verify <file.csv>"
    exit 1
fi