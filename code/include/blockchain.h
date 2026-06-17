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

#define MAX_BLOCKS 5000									// arbitrary number max transaction divided by "::"
#define HASH_LENGTH 64 									// 64 hex char equal to 256 bit
#define MAX_TX_LEN 1024         //4096     				// max lenght of transaction string
#define LINE_MAX_LEN 2048   							 // max length of a line in the CSV 
#define SHA256_HEX_LEN 64
#define SHA256_BUFFER_LEN 65
#define CONC_LEN 128
#define CONC_BUFFER_LEN 130
#define CSV_HEADER "index, timestamp, prev_hash, merkle_root, nonce, transactions\n"

#define MSGQUEUE_PATH "/tmp/blockchain_queue"
#define MSGQUEUE_PROJ_ID 'B'                     // project identifier - unique key for the message queue
#define MSG_TYPE_TRANSACTION 1

#define MAX_MINERS 16
#define MAX_NODES 16


typedef struct Block
{
	uint64_t index;										// index = prev_index + 1
	uint64_t timestamp;				
	char prev_hash[HASH_LENGTH + 1];					// 64 bytes of length + 1 (for the \0 character) 
	char merkle_root[HASH_LENGTH + 1];					// merkle_root = hash valor of every transaction 
	uint64_t nonce;										// just a number for miners
	uint32_t transactions_len;							// length of the string
	char transactions[MAX_TX_LEN];						// fixed array
}Block;

typedef struct Blockchain{
	Block blocks[MAX_BLOCKS];
	int length;
}Blockchain;

typedef struct SharedMemory{
	Blockchain blockchain;
	Block mined_last;
	Block confirmed;
	int ready_block;
	pid_t miner_pids[MAX_MINERS];
	int num_miners;
	pid_t node_pids[MAX_NODES];
	int num_nodes;
}SharedMemory;

typedef struct TxMessage{
    long mtype;
    char content[MAX_TX_LEN];
}TxMessage;

void int_to_hex(uint64_t value, char *out);				// converts uint64_t to hex string

uint64_t hex_to_int(const char *value);					// converts hex string to uint64_t

void calcola_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root);

int blockchain_load(Blockchain *bc, const char *filename);   // loads the blockchain from the CSV file. Returns BC_OK on success, error code otherwise.
int blockchain_save(const Blockchain *bc, const char *filename); // saves the entire blockchain to the CSV file, overwriting existing content. Returns BC_OK on success, error code otherwise.
int blockchain_append(const Block *b, const char *filename); // adds a single block to the end of the CSV file. Creates the file with header if it doesn't exist. Returns BC_OK on success, error code otherwise.


#endif