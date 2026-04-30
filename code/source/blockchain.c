#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>												// libreria per l'uso di "sprintf()"
#include "blockchain.h"


void int_to_hex(uint64_t value, char *out){
	sprintf(out, "%016" PRIx64, value); 							//%016 aggiunge zeri iniziali per avere sempre 16 caratteri
}


uint64_t hex_to_int(const char *value){

}