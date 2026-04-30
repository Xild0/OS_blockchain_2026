#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_TRANSACTION 5000							// arbitrary number max transaction divided by "::"
#define HASH_LENGTH 64 									// 64 hax hit equal to 256 bit

typedef strudct block
{
	uint64_t index;										// index = prev_index + 1
	uint64_t timestamp;				
	char prev_hash[HASH_LENGTH + 1];					// 64 bytes of length + 1 (for the \o character) 
	char merkle_root[HASH_LENGTH + 1];					// merkle_root = hash valor of every transaction 
	uint64_t nonce;										// just a number for miners
	char transactions[MAX_TRANSACTION];					// array for transaction string
}block;

void int_to_hex(uint64_t value, char *out);				// converte uint64_t in stringa hex

uint64_t hex_to_int(const char *value);					// converte stringa hex a uint64_t




#endif