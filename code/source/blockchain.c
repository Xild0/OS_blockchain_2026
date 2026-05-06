#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../include/blockchain.h"


void int_to_hex(uint64_t value, char *hex){
	snprintf(hex, 17, "%016lx", value); 							//%016 aggiunge zeri iniziali per avere sempre 16 caratteri
}


uint64_t hex_to_int(const char *hex){
	return (uint64_t)strtoull(hex, NULL, 16);
}