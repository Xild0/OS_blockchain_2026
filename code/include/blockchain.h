#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_TRANSACTION 150								// arbitrary number max transaction (I supposed "hash_number pays hash_number XX coins")
#define HASH_LENGTH 64 									// 256 hex bit are equal to 64 bytes

typedef struct block
{
	uint64_t index;										// index = prev_index + 1
	uint64_t timestamp;				
	char prev_hash[HASH_LENGTH + 1];					// 64 bytes of length + 1 (for the \o character) equal to 256 bit converted in hex
	char merkle_root[HASH_LENGTH + 1];					// merkle_root = hash valor of every transaction 
	char transactions[MAX_TRANSACTION];					// array for transaction string
}block;

void int_to_hex(uint64_t value, char *out);				// converte uint64_t in stringa hex

uint64_t hex_to_int(const char *value);					// converte stringa hex a uint64_t




#endif