#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>
#include "../include/node.h"
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/log.h"
#include "../include/errors.h"


static sig_atomic_t got_new_block = 0;
static sig_atomic_t got_stop = 0;

static SharedMemory *shm = NULL;
static sem_t *sem_blockchain = NULL;
static sem_t *sem_block = NULL;
static Blockchain local_chain; //local blockchain for node copy

static int node_id = -1;
static int num_nodes = 0;

// wraps sem_wait and retries if interrupted by a signal (EINTR)
static int sem_lock(sem_t *sem) {
    while (sem_wait(sem) == -1) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static void close_if_valid(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

// sets flag when a miner signals a new block is ready
static void handler_sigusr1(int sig) {
    (void)sig;
    got_new_block = 1;
}

// sets flag to trigger graceful shutdown
static void handler_sigterm(int sig) {
    (void)sig;
    got_stop = 1;
}

// checks merkle_root, index continuity, prev_hash and tx format against the current chain
static int validate_block(const Block *b, const Blockchain *blockchain) {
    char computed_merkle[SHA256_BUFFER_LEN];
    char tx_copy[MAX_TX_LEN];
    strncpy(tx_copy, b->transactions, MAX_TX_LEN - 1);
    tx_copy[MAX_TX_LEN - 1] = '\0';

    int rc = compute_merkle_root(tx_copy, computed_merkle);
    if (rc != BC_OK) {
        log_write("ERROR: merkle computation failed. Error: %s", error_to_string(rc));
        return rc;
    }

    if (strcmp(b->merkle_root, computed_merkle) != 0) {
        log_write("ERROR: merkle_root mismatch. Error: %s", error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }

    if (blockchain->length == 0) {
        if (b->index != 0) {
            log_write("ERROR: first block must have index 0. Error: %s", error_to_string(BC_ERR_INVALID_BLOCK));
            return BC_ERR_INVALID_BLOCK;
        }
        return BC_OK;
    }

    const Block *prev = &blockchain->blocks[blockchain->length - 1];

    if (b->index != prev->index + 1) {
        log_write("Wrong index! Expected %lu, received %lu. Error: %s",
                  prev->index + 1, b->index, error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }

    char computed_hash[SHA256_BUFFER_LEN];
    rc = compute_block_hash(prev, computed_hash);
    if (rc != BC_OK) {
        log_write("ERROR: block hash computation failed. Error: %s", error_to_string(rc));
        return rc;
    }

    if (strcmp(b->prev_hash, computed_hash) != 0) {
        log_write("ERROR: prev_hash mismatch. Error: %s", error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }


    if (b->index != 0) {
        if (!check_tx_format(b->transactions)) {
            log_write("ERROR: invalid transaction format. Error: %s",
                    error_to_string(BC_ERR_INVALID_TRANSACTION));
            return BC_ERR_INVALID_TRANSACTION;
        }
    }

    return BC_OK;
}
static void handle_new_block(const Block *b){
    if (local_chain.length > 0 && local_chain.blocks[local_chain.length - 1].index >= b->index ){
        log_write("Block already in local chain, rejected");
        return;
    }

    if(sem_lock(sem_blockchain) != 0){
        log_write("Error: sem_lock(sem_blockchain) failed");
        return;
    }

    Blockchain *shared = &shm->blockchain;

    if(shared->length > 0 && shared->blocks[shared->length - 1].index >= b->index){
        while(local_chain.length<shared->length){
            local_chain.blocks[local_chain.length]=shared->blocks[local_chain.length];
            local_chain.length++;
        }
        log_write("Local chain aligned shared memory");
        sem_post(sem_blockchain);
        return;
    }


    if(validate_block(b, &local_chain) != BC_OK){
        sem_post(sem_blockchain);
        return; 
    }
    if(shared->length >= MAX_BLOCKS){
        log_write("Error: blockchain full");
        sem_post(sem_blockchain);
        return;
    }
    shared->blocks[shared->length] = *b;
    shared->length++;
    local_chain.blocks[local_chain.length] = *b;
    local_chain.length++;
    log_write("Block added to local chain and shred memory");

    sem_post(sem_blockchain);


    if(sem_lock(sem_block) != 0){
        log_write("ERROR: sem_lock(sem_block) failed");
        return;
    }
    shm->confirmed = *b;
    sem_post(sem_block);

    for (int i = 0; i < shm->num_miners; ++i)
    {
        if(shm->miner_pids[i] > 0){
            kill(shm->miner_pids[i], SIGUSR2);
        }
    }

    log_write("Signal SIGUSR2 sent to miners");
    log_write("Starting peer propagation for block %lu", b->index);

    for (int i = 0; i < num_nodes; i++) {
        if (i == node_id) continue;

        char fifo_path[64];
        snprintf(fifo_path, sizeof(fifo_path), "node_%d_block.fifo", i);
        int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
        
        if (fd < 0) {
            log_write("Cannot open peer FIFO %s: %s", fifo_path, strerror(errno));
            continue;
        }
        ssize_t written = write(fd, b, sizeof(Block));

        if (written != (ssize_t)sizeof(Block)){
            log_write("ERROR: failed to send full block to peer");
        } else {
            log_write("Block sent to peer node %d", i);
        }
        close_if_valid(fd);
    }
}

int node_main(int id, int n_nodes, int shm_fd) {
    if (log_init("node", id) != 0) {
        return 1;
    }

    node_id = id;
    num_nodes = n_nodes;

    shm = (SharedMemory *)mmap(NULL, sizeof(SharedMemory),
                               PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        close_if_valid(shm_fd);
        log_close();
        return 1;
    }
    close_if_valid(shm_fd);

    local_chain = shm->blockchain; //init local blockchain

    sem_blockchain = sem_open("/sem_blockchain", 0);
    sem_block = sem_open("/sem_block", 0);
    if (sem_blockchain == SEM_FAILED || sem_block == SEM_FAILED) {
        perror("sem_open");
        if (sem_blockchain != SEM_FAILED) {
            sem_close(sem_blockchain);
        }
        if (sem_block != SEM_FAILED) {
            sem_close(sem_block);
        }
        munmap(shm, sizeof(SharedMemory));
        log_close();
        return 1;
    }

    signal(SIGUSR1, handler_sigusr1);
    signal(SIGTERM, handler_sigterm);

    char peer_fifo[64];
    for (int i = 0; i < n_nodes; i++) {
        char fifo_path[64];
        snprintf(fifo_path, sizeof(fifo_path), "node_%d_block.fifo", i);

        if (mkfifo(fifo_path, 0666) < 0 && errno != EEXIST) {
            log_write("ERROR: mkfifo failed for %s: %s", fifo_path, strerror(errno));
        }
    }

    snprintf(peer_fifo, sizeof(peer_fifo), "node_%d_block.fifo", node_id);

    int peer_fd = open(peer_fifo, O_RDWR | O_NONBLOCK);
    if (peer_fd < 0) {
        log_write("ERROR: open own FIFO failed for %s: %s", peer_fifo, strerror(errno));
    } else {
        log_write("Own FIFO opened correctly: %s", peer_fifo);
    }

    int peer_wr = -1;

    log_write("Node started");

    while (!got_stop) {
        
        if (peer_fd < 0) {
            peer_fd = open(peer_fifo, O_RDONLY | O_NONBLOCK);
            if (peer_fd >= 0 && peer_wr < 0) {
                peer_wr = open(peer_fifo, O_WRONLY | O_NONBLOCK);
            }
        }

        if (got_new_block) {
            got_new_block = 0;

            if (sem_lock(sem_block) != 0) {
                log_write("ERROR: sem_lock(sem_block) failed while reading mined block");
                continue;
            }

            Block b = shm->mined_last;
            shm->block_pending = 0;
            sem_post(sem_block);

            handle_new_block(&b);  
        }

        if (peer_fd >= 0) {
            Block peer_block;
            ssize_t n = read(peer_fd, &peer_block, sizeof(Block));
            if (n == sizeof(Block)) {
                log_write("Block received from peer");
                handle_new_block(&peer_block);
            }
        }
        usleep(50000);
    }

    log_write("Node shutting down");
    close_if_valid(peer_fd);
    close_if_valid(peer_wr);
    munmap(shm, sizeof(SharedMemory));
    sem_close(sem_blockchain);
    sem_close(sem_block);
    log_close();
    return 0;
}