#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <sys/mman.h>
#include "errors.h"

#define MAX_BLOCKS 5000              // maximum number of blocks stored in memory
#define HASH_LENGTH 64                   // 64 hex chars equal to 256 bit
#define MAX_TX_LEN 1024                  // max length of transaction string
#define LINE_MAX_LEN 2048               // max length of a line in the CSV
#define SHA256_HEX_LEN 64
#define SHA256_BUFFER_LEN 65
#define CONC_LEN 128
#define CONC_BUFFER_LEN 130

#define CSV_HEADER "index, timestamp, prev_hash, merkle_root, nonce, transactions\n"

#define MSGQUEUE_PATH "/tmp/blockchain_queue"
#define MSGQUEUE_PROJ_ID 'B'            // project identifier - unique key for the message queue
#define MSG_TYPE_TRANSACTION 1

#define MAX_MINERS 16
#define MAX_NODES 16
#define MAX_CLIENTS 16

typedef struct Block
{
    uint64_t index;                     // index = prev_index + 1
    uint64_t timestamp;
    char prev_hash[HASH_LENGTH + 1];    // 64 chars + 1 (for the '\0' character)
    char merkle_root[HASH_LENGTH + 1];  // Merkle root of the block transactions
    uint64_t nonce;                     
    uint32_t transactions_len;          // length of the transaction string
    char transactions[MAX_TX_LEN];      
}Block;

typedef struct Blockchain{
    Block blocks[MAX_BLOCKS];
    int length;
}Blockchain;

typedef struct SharedMemory{
    Blockchain blockchain;
    Block mined_last;
    Block confirmed;
    int block_pending;
    pid_t miner_pids[MAX_MINERS];
    int num_miners;
    pid_t node_pids[MAX_NODES];
    int num_nodes;
}SharedMemory;

typedef struct TxMessage{
    long mtype;
    char content[MAX_TX_LEN];
}TxMessage;

void int_to_hex(uint64_t value, char *out);   // converts uint64_t to hex string
uint64_t hex_to_int(const char *value);        // converts hex string to uint64_t

int compute_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root); // computes the Merkle root of a transaction list separated by "::"
int compute_block_hash(const Block *b, char *hash_out); // computes SHA-256 hash of a block
int is_valid_transaction(const char *tx);      // checks the regex format of transactions

int blockchain_load(Blockchain *bc, const char *filename);
int blockchain_save(const Blockchain *bc, const char *filename);
int blockchain_append(const Block *b, const char *filename);
int blockchain_validate(const Blockchain *bc);

int check_tx_format(const char *transactions); 
#endif