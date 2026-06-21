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

is_valid_transaction() {
    [[ "$1" =~ ^[A-Za-z0-9]+\ pays\ [A-Za-z0-9]+\ [1-9][0-9]*\ coins$ ]]
}

validate_tx_list() {
    local resto="$1"
    local parte

    while [[ "$resto" == *::* ]]; do
        parte="${resto%%::*}"
        is_valid_transaction "$parte" || return 1
        resto="${resto#*::}"
    done

    is_valid_transaction "$resto" || return 1
    return 0
}

ascii_to_hex() {
    printf '%s' "$1" | od -An -v -tx1 | tr -d ' \n'
}

hex_to_ascii() {
    local hex
    hex=$(printf '%s' "$1" | tr 'A-F' 'a-f')

    [ -z "$hex" ] && return 0

    printf '%b' "$(printf '%s' "$hex" | sed 's/../\\x&/g')"
}

if [ "$comando" == "--hash" ]; then
    if [ "$#" -lt 2 ] || [ -z "$2" ]; then
        echo "Error INVALID_ARGUMENT: missing block hex string"
        exit $INVALID_ARGUMENT
    fi

    block_hex="$parametro"

    if ! [[ "$block_hex" =~ ^[0-9a-fA-F]+$ ]]; then
        echo "Error INVALID_ARGUMENT: block must be a hexadecimal string"
        exit $INVALID_ARGUMENT
    fi

    if [ $(( ${#block_hex} % 2 )) -ne 0 ]; then
        echo "Error INVALID_ARGUMENT: block hex length must be even"
        exit $INVALID_ARGUMENT
    fi

    if [ "${#block_hex}" -lt 176 ]; then
        echo "Error INVALID_ARGUMENT: block hex too short"
        exit $INVALID_ARGUMENT
    fi

    idx="${block_hex:0:16}"
    ts="${block_hex:16:16}"
    prev="${block_hex:32:64}"
    merkle="${block_hex:96:64}"
    nonce="${block_hex:160:16}"
    tx_hex="${block_hex:176}"

    tx_ascii=$(hex_to_ascii "$tx_hex")

    preimage="${idx}${ts}${prev}${merkle}${nonce}${tx_ascii}"
    printf '%s' "$preimage" | sha256sum | awk '{print $1}'
    exit $SUCCESS

elif [ "$comando" == "--merkle" ]; then
    tx_string="$parametro"

    if [ "$#" -lt 2 ]; then
        echo "Error INVALID_ARGUMENT: missing transaction list"
        exit $INVALID_ARGUMENT
    elif [ -z "$2" ]; then
        echo -n "" | sha256sum | awk '{print $1}'
        exit $SUCCESS
    fi

    declare -a tx_array=()
    resto="$tx_string"

    while [[ "$resto" == *::* ]]; do
        parte="${resto%%::*}"
        tx_array+=("$parte")
        resto="${resto#*::}"
    done

    tx_array+=("$resto")

    declare -a hashes=()

    for tx in "${tx_array[@]}"; do
        h=$(echo -n "$tx" | sha256sum | awk '{print $1}')
        hashes+=("$h")
    done

    count=${#hashes[@]}

    if [ "$count" -eq 1 ]; then
        hash_vuoto="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        concatenata="${hashes[0]}${hash_vuoto}"
        risultato=$(echo -n "$concatenata" | sha256sum | awk '{print $1}')
        echo "$risultato"
        exit $SUCCESS
    fi

    while [ ${#hashes[@]} -gt 1 ]; do
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
    exit $SUCCESS

elif [ "$comando" == "--verify" ]; then
    csv_file="$parametro"

    if [ -z "$csv_file" ]; then
        echo "Error INVALID_ARGUMENT: missing CSV file"
        exit $INVALID_ARGUMENT
    fi

    if [ ! -f "$csv_file" ]; then
        echo "Error FILE_NOT_FOUND: CSV file not found"
        exit $FILE_NOT_FOUND
    fi

    block_count=$(awk 'NF && !/^index/' "$csv_file" | wc -l)

    if [ "$block_count" -eq 0 ]; then
        echo "Error EMPTY_BLOCKCHAIN: blockchain is empty"
        exit $EMPTY_BLOCKCHAIN
    fi

    prev_index_dec=-1
    prev_hash=""
    is_valid=1
    exit_code=$SUCCESS

    while IFS=',' read -r idx ts p_hash merkle nonce txs || [ -n "$idx$ts$p_hash$merkle$nonce$txs" ]; do
        idx="${idx%$'\r'}"
        ts="${ts%$'\r'}"
        p_hash="${p_hash%$'\r'}"
        merkle="${merkle%$'\r'}"
        nonce="${nonce%$'\r'}"
        txs="${txs%$'\r'}"

        if [[ "$idx" == "index"* ]]; then
            continue
        fi

        if [ -z "$idx" ]; then
            continue
        fi

        if ! [[ "$idx" =~ ^[0-9a-fA-F]{16}$ ]]; then
            echo "Error INVALID_BLOCK: invalid index encoding"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        if ! [[ "$ts" =~ ^[0-9a-fA-F]{16}$ ]]; then
            echo "Error INVALID_BLOCK: invalid timestamp encoding (block $idx)"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        if ! [[ "$p_hash" =~ ^[0-9a-fA-F]{64}$ ]]; then
            echo "Error INVALID_BLOCK: invalid prev_hash encoding (block $idx)"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        if ! [[ "$merkle" =~ ^[0-9a-fA-F]{64}$ ]]; then
            echo "Error INVALID_BLOCK: invalid merkle_root encoding (block $idx)"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        if ! [[ "$nonce" =~ ^[0-9a-fA-F]{16}$ ]]; then
            echo "Error INVALID_BLOCK: invalid nonce encoding (block $idx)"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        idx_dec=$(printf '%d' "0x$idx")

        if [ "$prev_index_dec" -eq -1 ] && [ "$idx_dec" -ne 0 ]; then
            echo "Error BLOCK_NOT_FOUND: missing genesis block"
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
                echo "Error CHAIN_MISMATCH: incorrect prev_hash (block $idx_dec)"
                is_valid=0
                exit_code=$CHAIN_MISMATCH
                break
            fi
        fi

        txs_clean="${txs%\"}"
        txs_clean="${txs_clean#\"}"

        if [ -z "$txs_clean" ]; then
            echo "Error INVALID_TRANSACTION: no transactions (block $idx_dec)"
            is_valid=0
            exit_code=$INVALID_TRANSACTION
            break
        fi

        if [ "$idx_dec" -ne 0 ]; then
            if ! validate_tx_list "$txs_clean"; then
                echo "Error INVALID_TRANSACTION: invalid transaction format (block $idx_dec)"
                is_valid=0
                exit_code=$INVALID_TRANSACTION
                break
            fi
        fi

        computed_merkle=$(bash "$0" --merkle "$txs_clean")

        if [ "$computed_merkle" != "$merkle" ]; then
            echo "Error INVALID_BLOCK: invalid Merkle root (block $idx_dec)"
            is_valid=0
            exit_code=$INVALID_BLOCK
            break
        fi

        tx_hex=$(ascii_to_hex "$txs_clean")
        block_hex="${idx}${ts}${p_hash}${merkle}${nonce}${tx_hex}"
        prev_hash=$(bash "$0" --hash "$block_hex")
        prev_index_dec="$idx_dec"

    done < "$csv_file"

    if [ "$is_valid" -eq 1 ]; then
        echo "Blockchain verified"
        exit $SUCCESS
    else
        exit $exit_code
    fi

else
    echo "Usage:"
    echo "  ./blockchain.sh --verify <state.csv>"
    echo "  ./blockchain.sh --hash <block_hex>"
    echo "  ./blockchain.sh --merkle <tx1::tx2::...>"
    exit $INVALID_ARGUMENT
fi
