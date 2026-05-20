#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/miner.h"
#include "../include/log.h"
#include "../include/errors.h"

// Flag used to stop the miner correctly
static sig_atomic_t got_stop = 0;

// Miner information
static int num_nodes = 0;
static int difficulty = 1;

// Pool of pending transactions separated by "::"
static char tx_pool[MAX_TX_LEN] = "";

// Number of transactions inside the pool
static int tx_count = 0;

// Current blockchain state known by the miner
static char prev_hash[HASH_LENGTH + 1];
static uint64_t next_index = 0;
static uint64_t current_nonce = 0;

// Used to avoid mining again before confirmation
static int waiting_confirmation = 0;

// SIGTERM handler
static void handler_sigterm(int sig){
    (void)sig;
    got_stop = 1;
}

// Computes the hash of an entire block. The fields are concatenated in hexadecimal form
static void compute_block_hash(const Block *b, char *hash_out){

    char buffer[8192];

    char idx_hex[17];
    char ts_hex[17];
    char nonce_hex[17];

    int_to_hex(b->index, idx_hex);
    int_to_hex(b->timestamp, ts_hex);
    int_to_hex(b->nonce, nonce_hex);

    sprintf(buffer,
            "%s%s%s%s%s%s",
            idx_hex,
            ts_hex,
            b->prev_hash,
            b->merkle_root,
            nonce_hex,
            b->transactions);

    sha256_hex(buffer, strlen(buffer), hash_out);

    hash_out[SHA256_HEX_LEN] = '\0';
}

// Adds a transaction inside the local pool
static void add_transaction_to_pool(const char *tx){

    int new_len;

    // Compute future length to avoid overflow
    if(tx_count == 0){
        new_len = strlen(tx);
    }
    else{
        new_len = strlen(tx_pool) + 2 + strlen(tx);
    }

    if(new_len >= MAX_TX_LEN){
        log_write("WARNING: transaction pool full");
        return;
    }

    // Add separator if needed
    if(tx_count > 0){
        strcat(tx_pool, "::");
    }

    strcat(tx_pool, tx);

    tx_count++;
}

// Checks if a transaction is already inside a block
static int is_transaction_in_block(const char *tx,
                                   const char *block_txs){

    char copy[MAX_TX_LEN];

    char *start;
    char *sep;

    strcpy(copy, block_txs);

    start = copy;

    // Split transactions using "::"
    while((sep = strstr(start, "::")) != NULL){

        *sep = '\0';

        if(strcmp(start, tx) == 0){
            return 1;
        }

        start = sep + 2;
    }

    // Check the last transaction
    if(strcmp(start, tx) == 0){
        return 1;
    }

    return 0;
}

// Removes already mined transactions from the pool
static void remove_mined_transactions(const Block *confirmed){

    char new_pool[MAX_TX_LEN] = "";

    char copy[MAX_TX_LEN];

    char *start;
    char *sep;

    int new_count = 0;

    strcpy(copy, tx_pool);

    start = copy;

    while((sep = strstr(start, "::")) != NULL){

        *sep = '\0';

        // Keep only transactions not already confirmed
        if(!is_transaction_in_block(start,
                                    confirmed->transactions)){

            if(new_count > 0){
                strcat(new_pool, "::");
            }

            strcat(new_pool, start);

            new_count++;
        }

        start = sep + 2;
    }

    // Check the last transaction
    if(strlen(start) > 0 &&
       !is_transaction_in_block(start,
                                confirmed->transactions)){

        if(new_count > 0){
            strcat(new_pool, "::");
        }

        strcat(new_pool, start);

        new_count++;
    }

    // Replace old pool
    strcpy(tx_pool, new_pool);

    tx_count = new_count;
}

// Reads all pending transactions from the message queue
static void drain_message_queue(int msqid){

    TxMessage msg;

    while(msgrcv(msqid,
                 &msg,
                 sizeof(msg.content),
                 MSG_TYPE_TRANSACTION,
                 IPC_NOWAIT) != -1){

        add_transaction_to_pool(msg.content);

        log_write("%s", msg.content);
    }
}

// Handles a confirmed block received from a node
static void handle_confirmed_block(const Block *b){

    char new_prev_hash[SHA256_BUFFER_LEN];

    log_write("Confirmed block received from node");

    // Update local blockchain state
    compute_block_hash(b, new_prev_hash);

    strcpy(prev_hash, new_prev_hash);

    next_index = b->index + 1;

    // Remove already mined transactions
    remove_mined_transactions(b);

    // Reset nonce and confirmation state
    current_nonce = 0;

    waiting_confirmation = 0;
}

// Sends the mined block to all nodes
static void broadcast_block(const Block *b){

    int i;

    for(i = 0; i < num_nodes; i++){

        char fifo_path[64];

        int fd;

        sprintf(fifo_path,
                "node_%d_block.fifo",
                i);

        fd = open(fifo_path,
                  O_WRONLY | O_NONBLOCK);

        if(fd < 0){
            log_write("WARNING: cannot open node fifo");
            continue;
        }

        if(write(fd, b, sizeof(Block)) < 0){
            log_write("WARNING: write to node fifo failed");
        }

        close(fd);
    }

    log_write("Block sent to nodes");
}

// Builds a new block and broadcasts it
static void build_and_broadcast_block(void){

    Block new_block;

    new_block.index = next_index;

    new_block.timestamp = (uint64_t)time(NULL);

    strcpy(new_block.prev_hash, prev_hash);

    strcpy(new_block.transactions, tx_pool);

    new_block.transactions_len =
            (uint32_t)strlen(new_block.transactions);

    // Compute Merkle root
    calcola_merkle_root(new_block.transactions,
                        new_block.merkle_root);

    new_block.merkle_root[HASH_LENGTH] = '\0';

    new_block.nonce = current_nonce;

    log_write("Mining successful");

    broadcast_block(&new_block);

    // Clear local pool
    tx_pool[0] = '\0';

    tx_count = 0;

    // Wait for confirmation before mining again
    waiting_confirmation = 1;
}

// Main function of the miner process
int miner_main(int id,
               int n_nodes,
               int diff){
    log_init("miner", id);
    char logname[64];

    key_t key;

    int msqid;

    char my_fifo[64];

    int fifo_rd;
    int fifo_wr;

    num_nodes = n_nodes;

    // Protect against invalid difficulty
    if(diff <= 0){
        difficulty = 1;
    }
    else{
        difficulty = diff;
    }

    // Initial blockchain state
    memset(prev_hash, '0', HASH_LENGTH);

    prev_hash[HASH_LENGTH] = '\0';

    next_index = 0;

    current_nonce = 0;

    // Different random seed for each miner
    srand(time(NULL) ^ getpid());

    // Register SIGTERM handler
    signal(SIGTERM, handler_sigterm);

    // Generate System V IPC key
    key = ftok(MSGQUEUE_PATH,
               MSGQUEUE_PROJ_ID);

    if(key == -1){
        log_write("ERROR: ftok failed con codice %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    // Connect to the existing message queue
    msqid = msgget(key, 0666);

    if(msqid == -1){
        log_write("ERROR: msgget failed con codice %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    // FIFO used to receive confirmed blocks from nodes
    sprintf(my_fifo,
            "miner_%d_block.fifo",
            id);

    mkfifo(my_fifo, 0666);

    // Double open trick: avoids EOF when nobody is writing
    fifo_rd = open(my_fifo,
                   O_RDONLY | O_NONBLOCK);

    fifo_wr = open(my_fifo,
                   O_WRONLY | O_NONBLOCK);

    if(fifo_rd < 0 || fifo_wr < 0){

        log_write("ERROR: cannot open miner fifo");

        if(fifo_rd >= 0){
            close(fifo_rd);
        }

        if(fifo_wr >= 0){
            close(fifo_wr);
        }

        return 1;
    }

    log_write("Miner started");

    // Main loop
    while(!got_stop){

        Block confirmed;

        ssize_t n;

        int sleep_seconds;

        // Read all pending transactions
        drain_message_queue(msqid);

        // Check if a confirmed block arrived
        n = read(fifo_rd,
                 &confirmed,
                 sizeof(Block));

        if(n == sizeof(Block)){
            handle_confirmed_block(&confirmed);
        }

        // Simulate mining work
        sleep_seconds = 1 + (rand() % 5);

        sleep(sleep_seconds);

        if(got_stop){
            break;
        }

        // Read new transactions again after sleeping
        drain_message_queue(msqid);

        // Check confirmations again
        n = read(fifo_rd,
                 &confirmed,
                 sizeof(Block));

        if(n == sizeof(Block)){
            handle_confirmed_block(&confirmed);
        }

        current_nonce++;

        // Mine only if waiting_confirmation is false
        if(waiting_confirmation == 0){

            // Probability = 1 / difficulty
            if((rand() % difficulty) == 0){

                // Do not mine empty blocks
                if(tx_count > 0){

                    build_and_broadcast_block();
                }
                else{

                    log_write("Lottery won but no transactions to mine");
                }
            }
        }
    }

    log_write("Miner shutting down");

    close(fifo_rd);

    close(fifo_wr);

    unlink(my_fifo);

    log_close();

    return 0;
}