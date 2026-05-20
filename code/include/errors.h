#ifndef ERRORS_H
#define ERRORS_H

// errori coerenti con blockchain.sh
#define SUCCESS                 0
#define INVALID_BLOCK           1
#define CHAIN_MISMATCH          2
#define INVALID_TRANSACTION     3
#define BLOCK_NOT_FOUND         4
#define INVALID_ARGUMENT        5
#define FILE_NOT_FOUND          6
#define EMPTY_BLOCKCHAIN        7

// errori interni per operazioni di lettura e scrittura sui file
#define BC_OK                  0
#define BC_ERR_FILE_OPEN      -1
#define BC_ERR_FILE_WRITE     -2
#define BC_ERR_FILE_READ      -3
#define BC_ERR_INVALID_FORMAT -4
#define BC_ERR_FULL           -5
#define BC_ERR_NULL_ARG       -6
#define BC_ERR_INVALID_BLOCK  -7

static inline const char *error_to_string(int error_code){
	switch (error_code){
		case BC_OK: 				return "Success (BC_OK)";
		case BC_ERR_FILE_OPEN:		return "Errore: impossibile aprire il file (BC_ERR_FILE_OPEN)";
		case BC_ERR_FILE_WRITE:		return "Errore: Scrittura su file fallita (BC_ERR_FILE_WRITE)";
		case BC_ERR_FILE_READ: 		return "Errore: Lettura da file fallita (BC_ERR_FILE_READ)";
		case BC_ERR_INVALID_FORMAT: return "Errore: Formato non valido (BC_ERR_INVALID_FORMAT)";
        case BC_ERR_FULL:           return "Errore: Blockchain o struttura piena (BC_ERR_FULL)";
        case BC_ERR_NULL_ARG:       return "Errore: Argomento puntatore NULL (BC_ERR_NULL_ARG)";
        case BC_ERR_INVALID_BLOCK:  return "Errore: Blocco non valido / validazione fallita (BC_ERR_INVALID_BLOCK)";
        default:                    return "Errore: Codice errore sconosciuto";
	}
}

#endif