#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <semaphore.h>
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

static int difficulty = 1;

static SharedMemory *shm = NULL;
static sem_t *sem_block = NULL;
static sem_t *sem_blockchain = NULL;

// Pool of pending transactions separated by "::"
static char tx_pool[MAX_TX_LEN] = "";
static int tx_count = 0; //num transaction inside pool

// Current blockchain state known by the miner
static char prev_hash[HASH_LENGTH + 1];
static uint64_t next_index = 0;
static uint64_t current_nonce = 0;
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

// Adds a transaction inside the local pool
static void add_transaction_to_pool(const char *tx){
    size_t new_len;
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

    if(tx_count > 0){
        strcat(tx_pool, "::");
    }

    strcat(tx_pool, tx);
    tx_count++;
}

// Checks if a transaction is already inside a block
static int is_transaction_in_block(const char *tx, const char *block_txs){
    char copy[MAX_TX_LEN];
    char *start;
    char *sep;

    strcpy(copy, block_txs);
    start = copy;

    while((sep = strstr(start, "::")) != NULL){
        *sep = '\0';
        if(strcmp(start, tx) == 0){
            return 1;
        }
        start = sep + 2;
    }

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
        if(!is_transaction_in_block(start, confirmed->transactions)){
            if(new_count > 0){
                strcat(new_pool, "::");
            }
            strcat(new_pool, start);
            new_count++;
        }
        start = sep + 2;
    }

    if(strlen(start) > 0 && !is_transaction_in_block(start, confirmed->transactions)){
        if(new_count > 0){
            strcat(new_pool, "::");
        }
        strcat(new_pool, start);
        new_count++;
    }

    strcpy(tx_pool, new_pool);
    tx_count = new_count;
}


// true if tx already appears in any block of the shared chain
static int tx_in_chain(const char *tx){
    for(int i = 0; i < shm->blockchain.length; i++){
        if(is_transaction_in_block(tx, shm->blockchain.blocks[i].transactions)){
            return 1;
        }
    }
    return 0;
}


static void prune_pool_against_chain(void){ // removes from the local pool any transaction already included in the shared blockchain
    if(tx_count == 0){
        return;
    }

    char new_pool[MAX_TX_LEN] = "";
    char copy[MAX_TX_LEN];
    char *start;
    char *sep;
    int new_count = 0;

    strcpy(copy, tx_pool);
    start = copy;

   while (sem_wait(sem_blockchain) == -1){
    if (errno == EINTR) continue;
    log_write("ERROR: sem_wait failed, skipping prune");
    return;
   }

    while((sep = strstr(start, "::")) != NULL){
        *sep = '\0';
        if(!tx_in_chain(start)){
            if(new_count > 0){ strcat(new_pool, "::"); }
            strcat(new_pool, start);
            new_count++;
        }
        start = sep + 2;
    }
    if(strlen(start) > 0 && !tx_in_chain(start)){
        if(new_count > 0){ strcat(new_pool, "::"); }
        strcat(new_pool, start);
        new_count++;
    }
    sem_post(sem_blockchain);

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
            if(errno == ENOMSG){
                break;
            }
    
            if(errno == EINTR){
                continue;
            }
            log_write("Fatal error in msgrcv");
            break;
        }

        if(!is_valid_transaction(msg.content)){
            log_write("Invalid transaction rejected: %s", msg.content);
            continue;
        }

        add_transaction_to_pool(msg.content);
        log_write("%s", msg.content);
    }
}

// Handles a confirmed block received from a node
static void handle_confirmed_block(const Block *b){
    char new_prev_hash[SHA256_BUFFER_LEN];

    log_write("Confirmed block received from node");

    compute_block_hash(b, new_prev_hash);
    strcpy(prev_hash, new_prev_hash);
    next_index = b->index + 1;

    remove_mined_transactions(b);

    current_nonce = 0;
    waiting_confirmation = 0;
}

// Sends the mined block to all nodes via shared memory + SIGUSR1
static void broadcast_block(const Block *b){
    while(!got_stop){
        if(sem_wait(sem_block) == -1){
            if(errno == EINTR){
                continue;
            }
            log_write("ERROR: sem_wait sem_block failed");
            return;
        }

        if(!shm->block_pending){
            shm->mined_last = *b;
            shm->block_pending = 1;
            sem_post(sem_block);

            for(int i = 0; i < shm->num_nodes; i++){
                if(shm->node_pids[i] > 0){
                    if (kill(shm->node_pids[i], SIGUSR1) == -1 && errno == ESRCH) {
                        log_write("WARNING: node %d appears to have crashed", i);
                    }
                }
            }

            log_write("Block written to shm and SIGUSR1 sent to nodes");
            return;
        }

        sem_post(sem_block);
        usleep(10000);
    }
}

// Builds a new block and broadcasts it
static void build_and_broadcast_block(void){
    Block new_block;
    memset(&new_block, 0, sizeof(Block)); 

    new_block.index = next_index;
    new_block.timestamp = (uint64_t)time(NULL);
    strcpy(new_block.prev_hash, prev_hash);
    strcpy(new_block.transactions, tx_pool);
    new_block.transactions_len = (uint32_t)strlen(new_block.transactions);

    if (compute_merkle_root(new_block.transactions, new_block.merkle_root) != BC_OK){
        log_write("ERROR: merkle computation failed, block not mined");
        return;
    }
    new_block.merkle_root[HASH_LENGTH] = '\0';
    new_block.nonce = current_nonce;

    log_write("Mining successful");
    broadcast_block(&new_block);
    waiting_confirmation = 1;
}

int miner_main(int id, int diff){
    key_t key;
    int msqid;
    int shm_fd;

    if(log_init("miner", id) != 0){
        return 1;
    }

    if(diff <= 0){
        difficulty = 1;
    }
    else{
        difficulty = diff;
    }

    shm_fd = shm_open("/blockchain_shm", O_RDWR, 0);
    if(shm_fd < 0){
        log_write("ERROR: shm_open failed");
        log_close();
        return 1;
    }

    shm = (SharedMemory *)mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
        log_write("ERROR: mmap failed");
        close(shm_fd);
        log_close();
        return 1;
    }

    sem_block = sem_open("/sem_block", 0);
    if(sem_block == SEM_FAILED){
        log_write("ERROR: sem_open sem_block failed");
        munmap(shm, sizeof(SharedMemory));
        close(shm_fd);
        log_close();
        return 1;
    }

    sem_blockchain = sem_open("/sem_blockchain", 0);
    if(sem_blockchain == SEM_FAILED){
        log_write("ERROR: sem_open sem_blockchain failed");
        sem_close(sem_block);
        munmap(shm, sizeof(SharedMemory));
        close(shm_fd);
        log_close();
        return 1;
    }

    if(shm->blockchain.length > 0){
        Block last_block = shm->blockchain.blocks[shm->blockchain.length - 1];
        next_index = last_block.index + 1;
        compute_block_hash(&last_block, prev_hash);
    }
    else{
        memset(prev_hash, '0', HASH_LENGTH);
        prev_hash[HASH_LENGTH] = '\0';
        next_index = 0;
    }

    srand(time(NULL) ^ getpid());
    signal(SIGTERM, handler_sigterm);
    signal(SIGUSR2, handler_sigusr2);

    key = ftok(MSGQUEUE_PATH, MSGQUEUE_PROJ_ID);
    if(key == -1){
        log_write("ERROR: ftok failed with code %s", error_to_string(BC_ERR_FILE_OPEN));
        sem_close(sem_block);
        sem_close(sem_blockchain);
        munmap(shm, sizeof(SharedMemory));
        close(shm_fd);
        log_close();
        return 1;
    }

    msqid = msgget(key, 0666);
    if(msqid == -1){
        log_write("ERROR: msgget failed with code %s", error_to_string(BC_ERR_FILE_OPEN));
        sem_close(sem_block);
        sem_close(sem_blockchain);
        munmap(shm, sizeof(SharedMemory));
        close(shm_fd);
        log_close();
        return 1;
    }

    log_write("Miner started");

    while(!got_stop){
        if(got_confirmed){
            got_confirmed = 0;
            Block confirmed = shm->confirmed;
            handle_confirmed_block(&confirmed);
        }

        int sleep_seconds;

        drain_message_queue(msqid);
        sleep_seconds = 1 + (rand() % 5);
        sleep(sleep_seconds);

        if(got_stop){
            break;
        }

        if(got_confirmed){
            got_confirmed = 0;
            Block confirmed = shm->confirmed;
            handle_confirmed_block(&confirmed);
        }

        drain_message_queue(msqid);

        current_nonce++;

        if(waiting_confirmation == 0){
            prune_pool_against_chain();

            if((rand() % difficulty) == 0){
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
    sem_close(sem_block);
    sem_close(sem_blockchain);
    munmap(shm, sizeof(SharedMemory));
    close(shm_fd);
    log_close();
    return 0;
}