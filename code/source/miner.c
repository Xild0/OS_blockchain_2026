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
#include <errno.h>
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/miner.h"
#include "../include/log.h"
#include "../include/errors.h"

// Flag used to stop the miner correctly
static sig_atomic_t got_stop = 0;

// Flag set by SIGUSR2 when a block is confirmed by a node
static sig_atomic_t got_confirmed = 0;

// Miner information
static int num_nodes = 0;
static int difficulty = 1;

// Shared memory pointer (read-only for miner)
static SharedMemory *shm = NULL;

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


// SIGUSR2 handler: node confirmed a block
static void handler_sigusr2(int sig){
    (void)sig;
    got_confirmed = 1;
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
    if(strlen(start) > 0 && !is_transaction_in_block(start, confirmed->transactions)){

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
    ssize_t res;
    while(1){
        res = msgrcv(msqid, &msg, sizeof(msg.content), MSG_TYPE_TRANSACTION, IPC_NOWAIT);
        if(res == -1){
            // if there are no messages we exit the loop
            if(errno == ENOMSG) break;

            // if it was interrupted by a signal, ignore and retry
            if(errno == EINTR) continue;

            // for other errors, log and break
            log_write("Fatal error in msgrcv");
            break;
        }
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
    memset(&new_block, 0, sizeof(Block)); // set to zero all parameters of the structure

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
    if(log_init("miner", id) != 0){
        return 1;
    }
    char logname[64];

    key_t key;

    int msqid;
    int shm_fd;
    num_nodes = n_nodes;

    if(diff <= 0){
        difficulty = 1;
    }
    else{
        difficulty = diff;
    }

    // Open shared memory (already created by parent)
    shm_fd = shm_open("/blockchain_shm", O_RDONLY, 0);
    if(shm_fd < 0){
        log_write("ERROR: shm_open failed");
        return 1;
    }
    shm = (SharedMemory *)mmap(NULL, sizeof(SharedMemory),
                               PROT_READ, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
        log_write("ERROR: mmap failed");
        return 1;
    }
    
    if (shm->blockchain.length > 0){
        Block last_block = shm->blockchain.blocks[shm->blockchain.length -1]; // last block of shm 
        next_index = last_block.index + 1;                          
        compute_block_hash(&last_block, prev_hash);  //calculate prev_hash from last valid block 
    } else {

        memset(prev_hash, '0', HASH_LENGTH);                                
        prev_hash[HASH_LENGTH] = '\0';
        next_index = 0;
    }

    // Different random seed for each miner
    srand(time(NULL) ^ getpid());

    // Register SIGTERM handler
    signal(SIGTERM, handler_sigterm);
    signal(SIGUSR2, handler_sigusr2);

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

    log_write("Miner started");

    // Main loop
    while(!got_stop){

        if(got_confirmed){
            got_confirmed = 0;
            // Read the confirmed block from shared memory
            Block confirmed = shm->confirmed;
            handle_confirmed_block(&confirmed);
        }

        
        Block confirmed;
        ssize_t n;
        int sleep_seconds;

        // Read all pending transactions
        drain_message_queue(msqid);

        // Simulate mining work
        sleep_seconds = 1 + (rand() % 5);

        sleep(sleep_seconds);

        if(got_stop){
            break;
        }

        // Check confirmation again after sleep
        if(got_confirmed){
            got_confirmed = 0;
            Block confirmed = shm->confirmed;
            handle_confirmed_block(&confirmed);
        }

        // Read new transactions again after sleeping
        drain_message_queue(msqid);
        current_nonce++;

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


    munmap(shm, sizeof(SharedMemory));
    close(shm_fd);
    log_close();
    return 0;
}