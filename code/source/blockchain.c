#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <errno.h>
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/errors.h"

void int_to_hex(uint64_t value, char *hex){
    snprintf(hex, 17, "%016lx", value); //%016lx : padding with zeros to 16 chars
}

uint64_t hex_to_int(const char *hex){
    return (uint64_t)strtoull(hex, NULL, 16);
}

// returns 1 if s is exactly len lowercase/uppercase hex chars, 0 otherwise
static int is_hex_string(const char *s, int len){
    if (s == NULL) return 0;
    if ((int)strlen(s) != len) return 0;

    for (int i = 0; i < len; i++){
        if (!isxdigit((unsigned char)s[i])) return 0;
    }

    return 1;
}

int compute_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root){
    uint16_t count = 0;
    uint16_t tx_count = 0;

    if (transactions == NULL || merkle_root == NULL) return BC_ERR_NULL_ARG;

    if (*transactions == '\0'){
        sha256_hex("", 0, merkle_root);
        return BC_OK;
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
    if (array_transactions == NULL) return BC_ERR_NOMEM;
    ptr = transactions;

    // split the concatenated string on '::' and store each token
    while(*ptr != '\0'){
        if(ptr[0] == ':' && ptr[1] == ':'){
            uint64_t diff = ptr - start;   // length of current token
            array_transactions[tx_count] = malloc(diff + 1);
            if (array_transactions[tx_count] == NULL) return BC_ERR_NOMEM;
            strncpy(array_transactions[tx_count], start, diff);
            array_transactions[tx_count][diff] = '\0';
            tx_count++;
            ptr += 2;                      // skip '::' separator
            start = ptr;
        } else{
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
    if (array_transactions_sha256 == NULL) return BC_ERR_NOMEM;
    for (int i = 0; i < count; ++i){
        array_transactions_sha256[i] = malloc(SHA256_BUFFER_LEN);
        if (array_transactions_sha256[i] == NULL) return BC_ERR_NOMEM;
        sha256_hex(array_transactions[i], strlen(array_transactions[i]), array_transactions_sha256[i]);
        array_transactions_sha256[i][SHA256_HEX_LEN] = '\0';
    }

    for (int i = 0; i < count; ++i) free(array_transactions[i]);
    free(array_transactions);

    // single tx: pair with the empty-string hash
    if (count == 1){
        char tmp[CONC_BUFFER_LEN];
        char tmp_sha256[SHA256_BUFFER_LEN];

        strcpy(tmp, array_transactions_sha256[0]);
        strcat(tmp, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

        sha256_hex(tmp, strlen(tmp), tmp_sha256);
        tmp_sha256[SHA256_HEX_LEN]= '\0';

        strcpy(merkle_root, tmp_sha256);

        free(array_transactions_sha256[0]);
        free(array_transactions_sha256);
        return BC_OK;
    }

    // iteratively pair and hash until one root remains.
    // pair adjacent hashes and re-hash them; if count is odd, pad the last one with sha256("")
    while(count > 1){
        int new_count = 0;
        char **stringhe_concatenate = malloc(count * sizeof(char*));
        if (stringhe_concatenate == NULL) return BC_ERR_NOMEM;

        char **stringhe_concatenate_sha256 = malloc((((count+1)/2)) * sizeof(char*));
        if (stringhe_concatenate_sha256 == NULL) return BC_ERR_NOMEM;
        
        for (int i = 0; i < count-1; i+=2){
            stringhe_concatenate[new_count] = malloc(CONC_BUFFER_LEN);
            if (stringhe_concatenate[new_count] == NULL) return BC_ERR_NOMEM;

            stringhe_concatenate_sha256[new_count] = malloc(SHA256_BUFFER_LEN);
            if (stringhe_concatenate_sha256[new_count] == NULL) return BC_ERR_NOMEM;

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
    return BC_OK;
}

int compute_block_hash(const Block *b, char *hash_out){
    if (b == NULL || hash_out == NULL) return BC_ERR_NULL_ARG;

    char buffer[8192];
    char idx_hex[17];
    char ts_hex[17];
    char nonce_hex[17];

    int_to_hex(b->index, idx_hex);
    int_to_hex(b->timestamp, ts_hex);
    int_to_hex(b->nonce, nonce_hex);

    int len = snprintf(buffer, sizeof(buffer), "%s%s%s%s%s%s",
                       idx_hex, ts_hex, b->prev_hash, b->merkle_root,
                       nonce_hex, b->transactions);

    if (len < 0 || len >= (int)sizeof(buffer)) {
        return BC_ERR_INVALID_FORMAT;
    }

    sha256_hex(buffer, strlen(buffer), hash_out);
    hash_out[SHA256_HEX_LEN] = '\0';
    return BC_OK;
}

// checks that a transaction follows the format:
// ^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$
int is_valid_transaction(const char *tx){
    if (tx == NULL || tx[0] == '\0'){
        return 0;
    }

    regex_t regex;
    const char *pattern = "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$";

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0){
        return 0;
    }

    int res = regexec(&regex, tx, 0, NULL, 0);
    regfree(&regex);

    return (res == 0) ? 1 : 0; // 1 = valid, 0 = invalid
}

// checks that every transaction in a "::"-separated list matches the regex
static int check_tx_format(const char *transactions){
    char copy[MAX_TX_LEN];
    strncpy(copy, transactions, MAX_TX_LEN - 1);
    copy[MAX_TX_LEN - 1] = '\0';

    char *start = copy;
    char *sep;
    while ((sep = strstr(start, "::")) != NULL) {
        *sep = '\0';
        if (!is_valid_transaction(start)) {
            return 0;
        }
        start = sep + 2;
    }
    if (!is_valid_transaction(start)) {
        return 0;
    }
    return 1;
}

// validates a loaded chain: genesis index 0, index continuity, prev_hash linkage,
// merkle root of every block, and transaction format of every non-genesis block.
int blockchain_validate(const Blockchain *bc){
    if (!bc) return BC_ERR_NULL_ARG;
    if (bc->length <= 0) return BC_ERR_INVALID_FORMAT;       // empty chain
    if (bc->blocks[0].index != 0) return BC_ERR_INVALID_BLOCK; // genesis must have index 0

    char computed_merkle[SHA256_BUFFER_LEN];
    char tx_copy[MAX_TX_LEN];
    char prev_hash_calc[SHA256_BUFFER_LEN];

    for (int i = 0; i < bc->length; i++) {
        const Block *b = &bc->blocks[i];

        // merkle root check for every block (including genesis)
        strncpy(tx_copy, b->transactions, MAX_TX_LEN - 1);
        tx_copy[MAX_TX_LEN - 1] = '\0';
        int rc = compute_merkle_root(tx_copy, computed_merkle);
        if (rc != BC_OK) {
            return rc;
        }

        if (strcmp(b->merkle_root, computed_merkle) != 0) {
            return BC_ERR_INVALID_BLOCK;
        }

        if (i == 0) {
            // genesis: free-form payload, no index/prev/tx-format checks beyond index 0
            continue;
        }

        const Block *prev = &bc->blocks[i - 1];

        // index continuity
        if (b->index != prev->index + 1) {
            return BC_ERR_INVALID_BLOCK;
        }

        // prev_hash must equal the hash of the previous block
        rc = compute_block_hash(prev, prev_hash_calc);
        if (rc != BC_OK) {
            return rc;
        }

        if (strcmp(b->prev_hash, prev_hash_calc) != 0) {
            return BC_ERR_INVALID_BLOCK;
        }

        // transaction format for non-genesis blocks
        if (!check_tx_format(b->transactions)) {
            return BC_ERR_INVALID_BLOCK;
        }
    }

    return BC_OK;
}

#define READ_LINE_EOF   -1
#define READ_LINE_ERROR -2

// reads one line from fd into buf and returns length or -1 on EOF
static int read_line(int fd, char *buf, int maxlen) {
    int buffer_index = 0;
    char single_char;

    while (buffer_index < maxlen - 1) {
        ssize_t n = read(fd, &single_char, 1);

        if (n < 0) {
            return READ_LINE_ERROR;
        }

        if (n == 0) {
            return (buffer_index == 0) ? READ_LINE_EOF : buffer_index;
        }

        if (single_char == '\n') {
            break;
        }

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
    if (!is_hex_string(fields[0], 16) ||  // index
        !is_hex_string(fields[1], 16) ||  // timestamp
        !is_hex_string(fields[2], 64) ||  // prev_hash
        !is_hex_string(fields[3], 64) ||  // merkle_root
        !is_hex_string(fields[4], 16)) {  // nonce
        return BC_ERR_INVALID_FORMAT;
    }

    b->index = hex_to_int(fields[0]);
    b->timestamp = hex_to_int(fields[1]);
    b->nonce = hex_to_int(fields[4]);

    strncpy(b->prev_hash, fields[2], HASH_LENGTH);
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
    if (!bc || !filename) return BC_ERR_NULL_ARG;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    char line[LINE_MAX_LEN];

    // skip CSV header
    int hr = read_line(fd, line, sizeof(line));
    if (hr == READ_LINE_ERROR || hr == READ_LINE_EOF) {
        close(fd);
        return BC_ERR_FILE_READ;
    }

    bc->length = 0;

    int n;
    while ((n = read_line(fd, line, sizeof(line))) != READ_LINE_EOF) {
        if (n == READ_LINE_ERROR) {
            close(fd);
            return BC_ERR_FILE_READ;
        }

        if (line[0] == '\0') {
            continue;
        }

        if (bc->length >= MAX_BLOCKS) {
            close(fd);
            return BC_ERR_FULL;
        }

        int rc = line_to_block(line, &bc->blocks[bc->length]);
        if (rc != BC_OK) {
            close(fd);
            return rc;
        }

        bc->length++;
    }

    close(fd);
    return BC_OK;
}

// writes all n bytes, retrying on partial writes / EINTR; 0 on success, -1 on error
static int write_all(int fd, const char *buf, size_t n){
    size_t written = 0;

    while (written < n){
        ssize_t w = write(fd, buf + written, n - written);

        if (w < 0){
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (w == 0) {
            return -1;
        }

        written += (size_t)w;
    }

    return 0;
}

int blockchain_save(const Blockchain *bc, const char *filename) {
    if (!bc || !filename) return BC_ERR_NULL_ARG;

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    if (write_all(fd, CSV_HEADER, strlen(CSV_HEADER)) != 0) {
        close(fd);
        return BC_ERR_FILE_WRITE;
    }

    for (int i = 0; i < bc->length; i++) {
        char idx[17], ts[17], nonce[17];
        char line[LINE_MAX_LEN];

        int_to_hex(bc->blocks[i].index, idx);
        int_to_hex(bc->blocks[i].timestamp, ts);
        int_to_hex(bc->blocks[i].nonce, nonce);

        int len = snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,\"%s\"\n",
                   idx, ts, bc->blocks[i].prev_hash, bc->blocks[i].merkle_root,
                   nonce, bc->blocks[i].transactions);

        if (len < 0 || len >= (int)sizeof(line)) {
            close(fd);
            return BC_ERR_FILE_WRITE;
        }

        if (write_all(fd, line, (size_t)len) != 0) {
            close(fd);
            return BC_ERR_FILE_WRITE;
        }
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
        if (write_all(fd, CSV_HEADER, strlen(CSV_HEADER)) != 0) {
            close(fd);
            return BC_ERR_FILE_WRITE;
        }
    }

    char idx[17], ts[17], nonce[17];
    char line[LINE_MAX_LEN];

    int_to_hex(b->index, idx);
    int_to_hex(b->timestamp, ts);
    int_to_hex(b->nonce, nonce);

    int len = snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,\"%s\"\n",
                   idx, ts, b->prev_hash, b->merkle_root, nonce, b->transactions);

    if (len < 0 || len >= (int)sizeof(line)) {
        close(fd);
        return BC_ERR_FILE_WRITE;
    }

    if (write_all(fd, line, (size_t)len) != 0) {
        close(fd);
        return BC_ERR_FILE_WRITE;
    }

    close(fd);
    return BC_OK;
}