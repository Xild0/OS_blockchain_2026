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
#define MAX_TX_LEN 4096     							 // Lunghezza massima della stringa delle transazioni
#define LINE_MAX_LEN 2048   							 // Lunghezza massima di una riga nel CSV 
#define SHA256_HEX_LEN 64
#define SHA256_BUFFER_LEN 65
#define CONC_LEN 128
#define CONC_BUFFER_LEN 130
#define CSV_HEADER "index, timestamp, prev_hash, merkle_root, nonce, transactions\n"

#define MSGQUEUE_PATH "/tmp/blockchain_queue"
#define MSGQUEUE_PROJ_ID 'B'
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

void int_to_hex(uint64_t value, char *out);				// converte uint64_t in stringa hex

uint64_t hex_to_int(const char *value);					// converte stringa hex a uint64_t

void calcola_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root);

//int read_line(int fd, char *buf, int maxlen);   //legge una riga dal fd, restituisce lunghezza o -1 a EndOfLine 

//int line_to_block(char *line, Block *b);        // converte una riga csv in un blocco, restituisce BC_OK o codice errore

int blockchain_load(Blockchain *bc, const char *filename);   // Carica la blockchain dal file CSV specificato. Restituisce BC_OK in caso di successo, codice di errore altrimenti. 
int blockchain_save(const Blockchain *bc, const char *filename); // Salva l'intera blockchain sul file CSV, sovrascrivendo il contenuto esistente. Restituisce BC_OK in caso di successo, codice di errore altrimenti.
int blockchain_append(const Block *b, const char *filename); //Aggiunge un singolo blocco in fondo al file CSV. Crea il file con header se non esiste. Restituisce BC_OK in caso di successo, codice di errore altrimenti. */


#endif