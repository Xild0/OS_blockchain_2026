#!/bin/bash
#
# test.sh - script di test del progetto blockchain
#
# Esegue una serie di test su:
#   - compilazione (normale e con warning)
#   - blockchain.sh (--merkle, --hash, --verify)
#   - edge case richiesti dalla specifica (2.2.7)
#   - sistema completo (run, submit, save, stop)
#   - convergenza dei nodi (niente fork / indici duplicati)
#
# Uso:
#   ./test.sh
#
# Va eseguito dalla cartella "code", dove ci sono build.sh e blockchain.sh

# codici di uscita attesi da blockchain.sh
SUCCESS=0
INVALID_BLOCK=1
CHAIN_MISMATCH=2
INVALID_TRANSACTION=3
BLOCK_NOT_FOUND=4
INVALID_ARGUMENT=5
FILE_NOT_FOUND=6
EMPTY_BLOCKCHAIN=7

passati=0
falliti=0

# confronta exit code ottenuto con quello atteso
# uso: check "nome" <atteso> <ottenuto>
check() {
    local nome="$1"
    local atteso="$2"
    local ottenuto="$3"
    if [ "$atteso" -eq "$ottenuto" ]; then
        echo "  [OK]   $nome (exit $ottenuto)"
        passati=$((passati + 1))
    else
        echo "  [FAIL] $nome (atteso $atteso, ottenuto $ottenuto)"
        falliti=$((falliti + 1))
    fi
}

# confronta due stringhe
# uso: check_eq "nome" "atteso" "ottenuto"
check_eq() {
    local nome="$1"
    local atteso="$2"
    local ottenuto="$3"
    if [ "$atteso" == "$ottenuto" ]; then
        echo "  [OK]   $nome"
        passati=$((passati + 1))
    else
        echo "  [FAIL] $nome"
        echo "         atteso:   $atteso"
        echo "         ottenuto: $ottenuto"
        falliti=$((falliti + 1))
    fi
}


echo "================================================="
echo " TEST PROGETTO BLOCKCHAIN"
echo "================================================="


# -------------------------------------------------
echo ""
echo "[1] COMPILAZIONE"
# -------------------------------------------------

./build.sh clean > /dev/null 2>&1
./build.sh build > /tmp/build_out.txt 2>&1
if [ -x ./blockchain ]; then
    check "compilazione (build.sh build)" 0 0
else
    check "compilazione (build.sh build)" 0 1
    echo "  ERRORE: compilazione fallita, controlla /tmp/build_out.txt"
    exit 1
fi

echo "  --- compilazione con -Wall -Wextra (informativa) ---"
warn=$(gcc -Wall -Wextra source/*.c -I include -o /tmp/blockchain_w -lrt -lpthread 2>&1 | grep -c warning)
echo "  warning trovati: $warn"


# -------------------------------------------------
echo ""
echo "[2] BLOCKCHAIN.SH --merkle"
# -------------------------------------------------

m=$(./blockchain.sh --merkle "Alice pays Bob 10 coins")
if [[ "$m" =~ ^[0-9a-f]{64}$ ]]; then
    check_eq "merkle singola tx produce 64 hex" "ok" "ok"
else
    check_eq "merkle singola tx produce 64 hex" "ok" "NO ($m)"
fi

m1=$(./blockchain.sh --merkle "Alice pays Bob 10 coins::Bob pays Alice 5 coins")
m2=$(./blockchain.sh --merkle "Alice pays Bob 10 coins::Bob pays Alice 5 coins")
check_eq "merkle deterministico (stesso input, stesso output)" "$m1" "$m2"


# -------------------------------------------------
echo ""
echo "[3] BLOCKCHAIN.SH --verify (casi VALIDI)"
# -------------------------------------------------

cat > /tmp/genesis.csv << 'CSV'
index,timestamp,prev_hash,merkle_root,nonce,transactions
0000000000000000,0000000067890001,0000000000000000000000000000000000000000000000000000000000000000,b815a93dd7f59058a27e63558ba5aa6445d851f316070ec13db673d5ab38e0cc,0000000000000000,"Genesis block"
CSV

./blockchain.sh --verify /tmp/genesis.csv > /dev/null 2>&1
check "verify su blockchain col solo genesis" $SUCCESS $?


# -------------------------------------------------
echo ""
echo "[4] BLOCKCHAIN.SH --verify (EDGE CASE - spec 2.2.7)"
# -------------------------------------------------

# 4a) file inesistente -> FILE_NOT_FOUND
./blockchain.sh --verify /tmp/non_esiste_12345.csv > /dev/null 2>&1
check "file inesistente -> FILE_NOT_FOUND" $FILE_NOT_FOUND $?

# 4b) solo header -> EMPTY_BLOCKCHAIN
printf "index,timestamp,prev_hash,merkle_root,nonce,transactions\n" > /tmp/vuoto.csv
./blockchain.sh --verify /tmp/vuoto.csv > /dev/null 2>&1
check "blockchain vuota (solo header) -> EMPTY_BLOCKCHAIN" $EMPTY_BLOCKCHAIN $?

# 4c) genesis mancante (index parte da 1) -> BLOCK_NOT_FOUND
cat > /tmp/no_genesis.csv << 'CSV'
index,timestamp,prev_hash,merkle_root,nonce,transactions
0000000000000001,0000000067890001,0000000000000000000000000000000000000000000000000000000000000000,b815a93dd7f59058a27e63558ba5aa6445d851f316070ec13db673d5ab38e0cc,0000000000000000,"Genesis block"
CSV
./blockchain.sh --verify /tmp/no_genesis.csv > /dev/null 2>&1
check "genesis mancante (index parte da 1) -> BLOCK_NOT_FOUND" $BLOCK_NOT_FOUND $?

# preparo un genesis coerente e calcolo il suo hash: serve a costruire blocchi 1 ben legati
gen_idx="0000000000000000"; gen_ts="0000000067890001"
gen_prev="0000000000000000000000000000000000000000000000000000000000000000"
gen_merkle="b815a93dd7f59058a27e63558ba5aa6445d851f316070ec13db673d5ab38e0cc"
gen_nonce="0000000000000000"; gen_tx="Genesis block"
gen_txhex=$(printf '%s' "$gen_tx" | od -An -v -tx1 | tr -d ' \n')
gen_blockhex="${gen_idx}${gen_ts}${gen_prev}${gen_merkle}${gen_nonce}${gen_txhex}"
h0=$(./blockchain.sh --hash "$gen_blockhex")

# 4d) prev_hash sbagliato -> CHAIN_MISMATCH
# blocco 1 con prev_hash tutto zeri (errato: dovrebbe essere h0)
b1_merkle=$(./blockchain.sh --merkle "Alice pays Bob 10 coins")
cat > /tmp/chain_rotta.csv << CSV
index,timestamp,prev_hash,merkle_root,nonce,transactions
${gen_idx},${gen_ts},${gen_prev},${gen_merkle},${gen_nonce},"${gen_tx}"
0000000000000001,000000006a3932ba,0000000000000000000000000000000000000000000000000000000000000000,${b1_merkle},0000000000000002,"Alice pays Bob 10 coins"
CSV
./blockchain.sh --verify /tmp/chain_rotta.csv > /dev/null 2>&1
check "prev_hash errato -> CHAIN_MISMATCH" $CHAIN_MISMATCH $?

# 4e) transazione formato errato con CATENA CORRETTA -> INVALID_TRANSACTION
# il blocco 1 ha prev_hash = h0 (giusto) e merkle coerente con la tx invalida,
# cosi' il verify non fallisce prima e arriva al controllo della regex
bad_merkle=$(./blockchain.sh --merkle "Alice gives Bob 10 coins")
cat > /tmp/tx_invalida.csv << CSV
index,timestamp,prev_hash,merkle_root,nonce,transactions
${gen_idx},${gen_ts},${gen_prev},${gen_merkle},${gen_nonce},"${gen_tx}"
0000000000000001,000000006a3932ba,${h0},${bad_merkle},0000000000000002,"Alice gives Bob 10 coins"
CSV
./blockchain.sh --verify /tmp/tx_invalida.csv > /dev/null 2>&1
check "transazione formato errato (merkle ok) -> INVALID_TRANSACTION" $INVALID_TRANSACTION $?

# 4f) argomento mancante -> INVALID_ARGUMENT
./blockchain.sh --verify > /dev/null 2>&1
check "verify senza file -> INVALID_ARGUMENT" $INVALID_ARGUMENT $?


# -------------------------------------------------
echo ""
echo "[5] SISTEMA COMPLETO (run + CLI + save + verify finale)"
# -------------------------------------------------

rm -f /tmp/sistema_out.csv
(
    sleep 8
    echo "request blockchain"
    sleep 1
    echo "submit Alice pays Bob 10 coins"
    sleep 1
    echo "submit questa non e valida"
    sleep 1
    echo "request block --index 0"
    sleep 1
    echo "save blockchain /tmp/sistema_out.csv"
    sleep 1
    echo "stop"
) | ./blockchain 15 15 15 1 1 /tmp/genesis.csv > /tmp/run_out.txt 2>&1

# 5a) transazione invalida rifiutata?
if grep -qi "invalid transaction format" /tmp/run_out.txt; then
    check_eq "submit invalido rifiutato dalla CLI" "ok" "ok"
else
    check_eq "submit invalido rifiutato dalla CLI" "ok" "NO"
fi

# 5b) file salvato?
if [ -f /tmp/sistema_out.csv ]; then
    check_eq "save blockchain ha creato il file" "ok" "ok"
else
    check_eq "save blockchain ha creato il file" "ok" "NO"
fi

# 5c) il CSV salvato dal C passa il verify? (coerenza C <-> Bash)
if [ -f /tmp/sistema_out.csv ]; then
    ./blockchain.sh --verify /tmp/sistema_out.csv > /dev/null 2>&1
    check "verify sul CSV salvato dal sistema" $SUCCESS $?
fi

# 5d) il sistema NON parte con un initial_state invalido?
( sleep 2; echo "stop" ) | ./blockchain 2 2 2 1 2 /tmp/chain_rotta.csv > /tmp/run_bad.txt 2>&1
rc_bad=$?
if [ "$rc_bad" -ne 0 ]; then
    check_eq "il sistema NON parte con initial_state invalido" "ok" "ok"
else
    check_eq "il sistema NON parte con initial_state invalido" "ok" "NO (e' partito)"
fi


# -------------------------------------------------
echo ""
echo "[6] CONVERGENZA DEI NODI (niente fork)"
# -------------------------------------------------

# Avvio piu' nodi e miner con difficolta' bassa (mining veloce) e lascio crescere
# la catena. Poi controllo che il CSV salvato NON abbia indici duplicati: due blocchi
# con lo stesso indice significherebbero un fork (vietato dalla spec 2.2.4.1).
rm -f /tmp/conv.csv
( sleep 18; echo "save blockchain /tmp/conv.csv"; sleep 1; echo "stop" ) | ./blockchain 15 15 15 1 1 > /tmp/conv_run.txt 2>&1

if [ -f /tmp/conv.csv ]; then
    tot=$(awk 'NF && !/^index/' /tmp/conv.csv | wc -l)
    uniq=$(awk 'NF && !/^index/ {print $1}' /tmp/conv.csv | cut -d, -f1 | sort -u | wc -l)
    echo "  (blocchi totali: $tot, indici unici: $uniq)"
    if [ "$tot" -eq "$uniq" ]; then
        check_eq "nessun indice duplicato (catena senza fork)" "ok" "ok"
    else
        check_eq "nessun indice duplicato (catena senza fork)" "ok" "NO (fork rilevato)"
    fi

    # la catena prodotta da 4 nodi deve comunque passare il verify
    ./blockchain.sh --verify /tmp/conv.csv > /dev/null 2>&1
    check "verify sulla catena prodotta da 4 nodi" $SUCCESS $?
else
    check_eq "convergenza: CSV creato" "ok" "NO"
fi


# -------------------------------------------------
echo ""
echo "================================================="
echo " RISULTATO: $passati passati, $falliti falliti"
echo "================================================="

./build.sh clean > /dev/null 2>&1

# pulizia file temporanei
rm -f /tmp/genesis.csv /tmp/vuoto.csv /tmp/no_genesis.csv /tmp/chain_rotta.csv
rm -f /tmp/tx_invalida.csv /tmp/sistema_out.csv /tmp/run_out.txt /tmp/run_bad.txt
rm -f /tmp/build_out.txt /tmp/blockchain_w /tmp/conv.csv /tmp/conv_run.txt

if [ "$falliti" -eq 0 ]; then
    exit 0
else
    exit 1
fi