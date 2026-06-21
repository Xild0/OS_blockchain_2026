#ifndef ERRORS_H
#define ERRORS_H

#define SUCCESS 0
#define INVALID_BLOCK 1
#define CHAIN_MISMATCH 2
#define INVALID_TRANSACTION 3
#define BLOCK_NOT_FOUND 4
#define INVALID_ARGUMENT 5
#define FILE_NOT_FOUND 6
#define EMPTY_BLOCKCHAIN 7

// internal error codes for read/write operations
#define BC_OK 0
#define BC_ERR_FILE_OPEN -1
#define BC_ERR_FILE_WRITE -2
#define BC_ERR_FILE_READ -3
#define BC_ERR_INVALID_FORMAT -4
#define BC_ERR_FULL -5
#define BC_ERR_NULL_ARG -6
#define BC_ERR_INVALID_BLOCK -7
#define BC_ERR_NOMEM -8

static inline const char *error_to_string(int error_code){
    switch (error_code){
        case BC_OK:                 return "Success (BC_OK)";
        case BC_ERR_FILE_OPEN:      return "Error: cannot open file (BC_ERR_FILE_OPEN)";
        case BC_ERR_FILE_WRITE:     return "Error: file write failed (BC_ERR_FILE_WRITE)";
        case BC_ERR_FILE_READ:      return "Error: file read failed (BC_ERR_FILE_READ)";
        case BC_ERR_INVALID_FORMAT: return "Error: invalid format (BC_ERR_INVALID_FORMAT)";
        case BC_ERR_FULL:           return "Error: blockchain or structure full (BC_ERR_FULL)";
        case BC_ERR_NULL_ARG:       return "Error: NULL pointer argument (BC_ERR_NULL_ARG)";
        case BC_ERR_INVALID_BLOCK:  return "Error: invalid block / validation failed (BC_ERR_INVALID_BLOCK)";
        case BC_ERR_NOMEM:          return "Error: memory allocation failed (BC_ERR_NOMEM)";
        default:                    return "Error: unknown error code";
    }
}

#endif