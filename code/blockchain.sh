#!/bin/bash

# ==============================================================================
# OS Course - Laboratory Project 2026: Blockchain Script
# Error Codes consistent with specifications
# ==============================================================================
SUCCESS=0
INVALID_BLOCK=1
CHAIN_MISMATCH=2
INVALID_TRANSACTION=3
BLOCK_NOT_FOUND=4
INVALID_ARGUMENT=5
FILE_NOT_FOUND=6
EMPTY_BLOCKCHAIN=7

# Controlla la presenza delle utility richieste
for cmd in sha256sum xxd awk; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "Error: Required system tool '$cmd' is not installed." >&2
        exit $INVALID_ARGUMENT
    fi
done

# --- Funzioni di Supporto ---

# Calcola lo SHA256 di una stringa di testo piana
sha256_string() {
    echo -n "$1" | sha256sum | awk '{print $1}'
}

# Decodifica una stringa esadecimale in binario e ne calcola lo SHA256
hash_block() {
    local hex_str="$1"
    # Uniforma in minuscolo per consistenza
    hex_str=$(echo "$hex_str" | tr '[:upper:]' '[:lower:]')
    echo -n "$hex_str" | xxd -r -p | sha256sum | awk '{print $1}'
}

# Calcola la radice di Merkle a partire da transazioni separate da ::
compute_merkle_root() {
    local transactions="$1"
    
    if [[ -z "$transactions" ]]; then
        # Se non ci sono transazioni, la radice è l'hash della stringa vuota
        sha256_string ""
        return 0
    fi

    # Splitta le transazioni usando sed per sostituire '::' con newlines
    local tx_list=()
    while IFS= read -r line; do
        tx_list+=("$line")
    done < <(echo -n "$transactions" | sed 's/::/\n/g')

    # Step 1: Calcola l'hash iniziale di ciascuna transazione
    local hashes=()
    for tx in "${tx_list[@]}"; do
        hashes+=($(sha256_string "$tx"))
    done

    # Step 2-4: Riduci ad albero fino a un unico hash (la radice)
    while [ ${#hashes[@]} -gt 1 ]; do
        local next_hashes=()
        
        # Se il numero di elementi è dispari, duplica l'ultimo elemento 
        # accodando l'hash di una stringa vuota come da specifiche
        if [ $(( ${#hashes[@]} % 2 )) -ne 0 ]; then
            hashes+=($(sha256_string ""))
        fi

        # Accoppia gli hash a due a due e calcola l'hash della concatenazione
        for ((i=0; i<${#hashes[@]}; i+=2)); do
            local h1="${hashes[i]}"
            local h2="${hashes[i+1]}"
            local pair_hash=$(sha256_string "${h1}${h2}")
            next_hashes+=("$pair_hash")
        done
        hashes=("${next_hashes[@]}")
    done

    echo "${hashes[0]}"
}

# Verifica l'integrità della blockchain memorizzata nel file CSV
verify_blockchain() {
    local csv_file="$1"

    if [ ! -f "$csv_file" ]; then
        echo "Error: File '$csv_file' not found." >&2
        exit $FILE_NOT_FOUND
    fi

    # Conta le righe non vuote
    local total_lines=$(grep -vc '^$' "$csv_file")
    if [ "$total_lines" -eq 0 ]; then
        echo "Error: Blockchain CSV is completely empty." >&2
        exit $EMPTY_BLOCKCHAIN
    fi

    # Legge ed valida l'intestazione (header)
    local first_line=$(head -n 1 "$csv_file" | tr -d '\r')
    if [ "$first_line" != "index,timestamp,prev_hash,merkle_root,nonce,transactions" ]; then
        echo "Error: Invalid CSV header format." >&2
        exit $INVALID_BLOCK
    fi

    if [ "$total_lines" -eq 1 ]; then
        echo "Error: Blockchain contains only the header." >&2
        exit $EMPTY_BLOCKCHAIN
    fi

    local prev_idx=-1
    local prev_computed_hash=""
    local line_num=0

    # Usiamo la process substitution per mantenere le variabili nel contesto corrente
    while IFS=, read -r r_index r_timestamp r_prev_hash r_merkle_root r_nonce r_transactions || [ -n "$r_index" ]; do
        [ -z "$r_index" ] && continue # Salta righe vuote

        # Pulizia dei ritorni a capo (\r\n) tipici di file modificati su piattaforme miste
        r_index=$(echo "$r_index" | tr -d '\r\n')
        r_timestamp=$(echo "$r_timestamp" | tr -d '\r\n')
        r_prev_hash=$(echo "$r_prev_hash" | tr -d '\r\n' | tr '[:upper:]' '[:lower:]')
        r_merkle_root=$(echo "$r_merkle_root" | tr -d '\r\n' | tr '[:upper:]' '[:lower:]')
        r_nonce=$(echo "$r_nonce" | tr -d '\r\n')
        r_transactions=$(echo "$r_transactions" | tr -d '\r\n')

        line_num=$((line_num + 1))

        # 1. Verifica dell'Indice Progressivo
        local idx_dec
        if ! idx_dec=$(printf "%d" "0x$r_index" 2>/dev/null); then
            echo "Block at line $line_num: Invalid index hex format '$r_index'." >&2
            exit $INVALID_BLOCK
        fi

        if [ "$prev_idx" -eq -1 ]; then
            # Validazione del blocco Genesis
            if [ "$idx_dec" -ne 0 ]; then
                echo "Block at line $line_num: Genesis block index must be 0, found $idx_dec." >&2
                exit $INVALID_BLOCK
            fi
        else
            local expected_idx=$((prev_idx + 1))
            if [ "$idx_dec" -ne "$expected_idx" ]; then
                echo "Block at line $line_num: Wrong index sequence. Expected $expected_idx, found $idx_dec." >&2
                exit $INVALID_BLOCK
            fi
        fi

        # 2. Verifica del collegamento (prev_hash)
        if [ "$prev_idx" -ne -1 ]; then
            if [ "$r_prev_hash" != "$prev_computed_hash" ]; then
                echo "Block at line $line_num: Broken chain link (CHAIN_MISMATCH). Expected prev_hash '$prev_computed_hash', found '$r_prev_hash'." >&2
                exit $CHAIN_MISMATCH
            fi
        fi

        # 3. Verifica della Merkle Root
        local computed_merkle=$(compute_merkle_root "$r_transactions" | tr '[:upper:]' '[:lower:]')
        if [ "$r_merkle_root" != "$computed_merkle" ]; then
            echo "Block at line $line_num: Invalid Merkle root. Expected '$computed_merkle', found '$r_merkle_root'." >&2
            exit $INVALID_BLOCK
        fi

        # 4. Calcola l'hash di questo blocco per la validazione del successivo
        # Convertiamo la stringa di transazioni in esadecimale ASCII
        local tx_hex=$(echo -n "$r_transactions" | xxd -p | tr -d '\n')
        local block_hex_input=$(echo "${r_index}${r_timestamp}${r_prev_hash}${r_merkle_root}${r_nonce}${tx_hex}" | tr '[:upper:]' '[:lower:]')
        
        prev_computed_hash=$(hash_block "$block_hex_input")
        prev_idx=$idx_dec

    done < <(tail -n +2 "$csv_file") # Salta l'intestazione

    echo "Blockchain verification successful! All blocks are valid and properly linked."
    exit $SUCCESS
}

# --- Gestione dei parametri da riga di comando (CLI) ---

show_help() {
    echo "Usage: $0 [OPTION] [ARGUMENT]"
    echo "Options:"
    echo "  --verify <state.csv>      Verifies the integrity of the blockchain CSV file."
    echo "  --hash <block_hex>        Decodes the hexadecimal block and prints its SHA256."
    echo "  --merkle <transactions>   Computes the Merkle root of the given transactions (separated by '::')."
    echo "  --help                    Displays this help message."
}

if [ $# -lt 1 ]; then
    show_help
    exit $INVALID_ARGUMENT
fi

case "$1" in
    --verify)
        if [ -z "$2" ]; then
            echo "Error: Missing CSV file path." >&2
            exit $INVALID_ARGUMENT
        fi
        verify_blockchain "$2"
        ;;
    --hash)
        if [ -z "$2" ]; then
            echo "Error: Missing hexadecimal block string." >&2
            exit $INVALID_ARGUMENT
        fi
        hash_block "$2"
        ;;
    --merkle)
        if [ -z "$2" ]; then
            echo "Error: Missing transactions string." >&2
            exit $INVALID_ARGUMENT
        fi
        compute_merkle_root "$2"
        ;;
    --help)
        show_help
        exit $SUCCESS
        ;;
    *)
        echo "Error: Unknown option '$1'." >&2
        show_help
        exit $INVALID_ARGUMENT
        ;;
esac