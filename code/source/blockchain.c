#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/errors.h"



void int_to_hex(uint64_t value, char *hex){
	snprintf(hex, 17, "%016lx", value); //%016lx : padding with zeros to 16 char
}


uint64_t hex_to_int(const char *hex){
	return (uint64_t)strtoull(hex, NULL, 16);
}

void calcola_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root){

    uint16_t count = 0;
    uint16_t tx_count = 0;

    if (*transactions == '\0'){
        sha256_hex("", 0, merkle_root);
        return;
    }

    char *ptr = transactions;
    char *start = ptr;

    // count transactions separated by '::'
    while(*ptr != '\0'){
        if(ptr[0] == ':' && ptr[1] == ':'){
            count++;
            ptr+=2;
        }else{
            ptr++;
        }
    }
    ++count;
    uint16_t c_count = count;

    char **array_transactions = malloc(count * sizeof(char*));
    ptr = transactions;

    // split the concatenated string on '::' and store each token
    while(*ptr != '\0'){
        if(ptr[0] == ':' && ptr[1] == ':'){
            uint64_t diff = ptr - start;          // length of current token
            array_transactions[tx_count] = malloc(diff + 1);
            strncpy(array_transactions[tx_count], start, diff);
            array_transactions[tx_count][diff] = '\0';
            tx_count++;
            ptr += 2;    // skip '::' separator
            start = ptr;
        }
        else{
            ptr++;
        }
    }
    // copy the last token (no trailing '::')
    uint64_t diff = ptr - start;
    array_transactions[tx_count] = malloc(diff+1);
    strncpy(array_transactions[tx_count], start, diff);
    array_transactions[tx_count][diff] = '\0';
    tx_count++;

    // hash each transaction
    char **array_transactions_sha256 = malloc(count * sizeof(char*));
    for (int i = 0; i < count; ++i){
        array_transactions_sha256[i] = malloc(SHA256_BUFFER_LEN);
        sha256_hex(array_transactions[i], strlen(array_transactions[i]), array_transactions_sha256[i]);
        array_transactions_sha256[i][SHA256_HEX_LEN] = '\0';
    }

    for (int i = 0; i < count; ++i) free(array_transactions[i]);
    free(array_transactions);

    // single tx: pair with empty hash
    if (count == 1){
        char tmp[CONC_BUFFER_LEN];
        char tmp_sha256[SHA256_BUFFER_LEN];
        strcpy(tmp, array_transactions_sha256[0]);
        strcat(tmp, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\0");
        sha256_hex(tmp, strlen(tmp), tmp_sha256);
        tmp_sha256[SHA256_HEX_LEN]= '\0';
        strcpy(merkle_root, tmp_sha256);
        free(array_transactions_sha256[0]);
        free(array_transactions_sha256);
        return;
    }

    // iteratively pair and hash until one root remains
    // pair adjacent hashes and re-hash them, if count is odd, pad the last one with sha256()
    while(count > 1){
        uint8_t new_count = 0;
        char **stringhe_concatenate = malloc(count * sizeof(char*));
        char **stringhe_concatenate_sha256 = malloc((((count+1)/2)) * sizeof(char*));

        for (int i = 0; i < count-1; i+=2){
            stringhe_concatenate[new_count] = malloc(CONC_BUFFER_LEN);
            stringhe_concatenate_sha256[new_count] = malloc(SHA256_BUFFER_LEN);
           
            strcpy(stringhe_concatenate[new_count], array_transactions_sha256[i]);
            strcat(stringhe_concatenate[new_count], array_transactions_sha256[i+1]);
           
            stringhe_concatenate[new_count][CONC_LEN] = '\0';
            sha256_hex(stringhe_concatenate[new_count], strlen(stringhe_concatenate[new_count]), stringhe_concatenate_sha256[new_count]);
            stringhe_concatenate_sha256[new_count][SHA256_HEX_LEN] = '\0';
            ++new_count;
        }

        if((count%2) != 0){
            stringhe_concatenate[new_count] = malloc(CONC_BUFFER_LEN);
            stringhe_concatenate_sha256[new_count] = malloc(SHA256_BUFFER_LEN);
            
            strcpy(stringhe_concatenate[new_count], array_transactions_sha256[count-1]);
            char tmp[SHA256_BUFFER_LEN];
            sha256_hex("", 0, tmp);
            tmp[SHA256_HEX_LEN] = '\0';
            strcat(stringhe_concatenate[new_count], tmp);
            stringhe_concatenate[new_count][CONC_LEN]= '\0';
            sha256_hex(stringhe_concatenate[new_count], strlen(stringhe_concatenate[new_count]), stringhe_concatenate_sha256[new_count]);
            stringhe_concatenate_sha256[new_count][SHA256_HEX_LEN] = '\0';
            ++new_count;
        }

        count = (count % 2 != 0) ? (count + 1) / 2 : count / 2;

        for (int i = 0; i < new_count; ++i){
            strcpy(array_transactions_sha256[i], stringhe_concatenate_sha256[i]);
            free(stringhe_concatenate[i]);
            free(stringhe_concatenate_sha256[i]);
        }
        free(stringhe_concatenate);
        free(stringhe_concatenate_sha256);
    }

    strcpy(merkle_root, array_transactions_sha256[0]);
    for (int i = 0; i < c_count; ++i) free(array_transactions_sha256[i]);
    free(array_transactions_sha256);
    return;
}

// reads one line from fd into buf and returns length or -1 on EOF
static int read_line(int fd, char *buf, int maxlen) {
    int buffer_index = 0;
    char single_char;
    while (buffer_index < maxlen - 1) {
        size_t n = read(fd, &single_char, 1);
        if (n <= 0) return (buffer_index == 0) ? -1 : buffer_index;
        if (single_char == '\n') break;
        buf[buffer_index++] = single_char;
    }
    buf[buffer_index] = '\0';
    return buffer_index;
}

// parses a CSV line into a Block and returns BC_OK or error code
static int line_to_block(char *line, Block *b) {
    char *fields[6];
    char *ptr = line;
    for (int i = 0; i < 5; i++) {
        fields[i] = ptr;
        ptr = strchr(ptr, ',');
        if (!ptr) return BC_ERR_INVALID_FORMAT;
        *ptr = '\0';
        ptr++;
    }
    fields[5] = ptr;

    b->index     = hex_to_int(fields[0]);
    b->timestamp = hex_to_int(fields[1]);
    b->nonce     = hex_to_int(fields[4]);

    strncpy(b->prev_hash,   fields[2], HASH_LENGTH);
    b->prev_hash[HASH_LENGTH] = '\0';
    strncpy(b->merkle_root, fields[3], HASH_LENGTH);
    b->merkle_root[HASH_LENGTH] = '\0';

    // strip surrounding quotes from transactions field
    char *tx = fields[5];
    if (tx[0] == '"') tx++;
    size_t len = strlen(tx);
    if (len > 0 && tx[len-1] == '"') tx[len-1] = '\0';

    strncpy(b->transactions, tx, MAX_TX_LEN - 1);
    b->transactions[MAX_TX_LEN - 1] = '\0';
    b->transactions_len = (uint32_t)strlen(b->transactions);

    return BC_OK;
}
// loads all blocks from a CSV file into bc skipping header and empty lines
int blockchain_load(Blockchain *bc, const char *filename) {
    if (!bc || !filename) return -1;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    char line[LINE_MAX_LEN];

    // skip CSV header
    if (read_line(fd, line, sizeof(line)) < 0) {
        close(fd);
        return BC_ERR_FILE_READ;
    }

    bc->length = 0;
    while (read_line(fd, line, sizeof(line)) >= 0) {
        if (line[0] == '\0') continue;
        if (bc->length >= MAX_BLOCKS) { close(fd); return BC_ERR_FULL; }

        int rc = line_to_block(line, &bc->blocks[bc->length]);
        if (rc != BC_OK) { close(fd); return rc; }
        bc->length++;
    }

    close(fd);
    return BC_OK;
}


int blockchain_save(const Blockchain *bc, const char *filename) {
    if (!bc || !filename) return BC_ERR_NULL_ARG;

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    if (write(fd, CSV_HEADER, strlen(CSV_HEADER)) < 0) {
        close(fd);
        return BC_ERR_FILE_WRITE;
    }

    for (int i = 0; i < bc->length; i++) {
        char idx[17], ts[17], nonce[17];
        char line[LINE_MAX_LEN];

        int_to_hex(bc->blocks[i].index,     idx);
        int_to_hex(bc->blocks[i].timestamp, ts);
        int_to_hex(bc->blocks[i].nonce,     nonce);

        int len = snprintf(line, sizeof(line),
            "%s,%s,%s,%s,%s,\"%s\"\n", idx, ts, bc->blocks[i].prev_hash, bc->blocks[i].merkle_root, nonce, bc->blocks[i].transactions);

        if (write(fd, line, len) < 0) { close(fd); return BC_ERR_FILE_WRITE; }
    }

    close(fd);
    return BC_OK;
}


int blockchain_append(const Block *b, const char *filename) {
    if (!b || !filename) return BC_ERR_NULL_ARG;

    // write header only if file didn't exist yet
    int new_file = (access(filename, F_OK) != 0);

    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    if (new_file){
        if (write(fd, CSV_HEADER, strlen(CSV_HEADER)) < 0) { close(fd); return BC_ERR_FILE_WRITE; }
    }

    char idx[17], ts[17], nonce[17];
    char line[LINE_MAX_LEN];

    int_to_hex(b->index,     idx);
    int_to_hex(b->timestamp, ts);
    int_to_hex(b->nonce,     nonce);

    int len = snprintf(line, sizeof(line),
        "%s,%s,%s,%s,%s,\"%s\"\n", idx, ts, b->prev_hash, b->merkle_root, nonce, b->transactions);

    if (write(fd, line, len) < 0) { close(fd); return BC_ERR_FILE_WRITE; }

    close(fd);
    return BC_OK;
}
