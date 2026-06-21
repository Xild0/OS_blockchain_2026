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
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/log.h"
#include "../include/errors.h"

static sig_atomic_t got_new_block = 0;
static sig_atomic_t got_stop = 0;

static SharedMemory *shm = NULL;
static sem_t *sem_blockchain = NULL;
static sem_t *sem_block = NULL;

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

static void sem_unlock(sem_t *sem) {
    sem_post(sem);
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

// check that all transactions separated by "::" match the regex
static int check_transactions_format(const char *transactions) {
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

    // last transaction, or only one transaction
    if (!is_valid_transaction(start)) {
        return 0;
    }

    return 1;
}

// checks merkle_root, index continuity, prev_hash and tx format against the current chain
static int validate_block(const Block *b, const Blockchain *blockchain) {
    char msg[512];

    // merkle_root is always checked, including the first block received live
    // (when the chain is still empty), so a corrupted genesis cannot slip in
    char computed_merkle[SHA256_BUFFER_LEN];
    char tx_copy[MAX_TX_LEN];
    strncpy(tx_copy, b->transactions, MAX_TX_LEN - 1);
    tx_copy[MAX_TX_LEN - 1] = '\0';
    int rc = compute_merkle_root(tx_copy, computed_merkle);
    if (rc != BC_OK) {
        snprintf(msg, sizeof(msg), "ERROR: merkle computation failed. Error: %s",
                error_to_string(rc));
        log_write(msg);
        return rc;
    }

    if (strcmp(b->merkle_root, computed_merkle) != 0) {
        snprintf(msg, sizeof(msg), "ERROR: merkle_root mismatch. Error: %s",
                 error_to_string(BC_ERR_INVALID_BLOCK));
        log_write(msg);
        return BC_ERR_INVALID_BLOCK;
    }

    // first block (empty chain): must be a genesis with index 0 and free-form payload
    if (blockchain->length == 0) {
        if (b->index != 0) {
            snprintf(msg, sizeof(msg), "ERROR: first block must have index 0. Error: %s",
                     error_to_string(BC_ERR_INVALID_BLOCK));
            log_write(msg);
            return BC_ERR_INVALID_BLOCK;
        }
        return BC_OK;
    }

    const Block *prev = &blockchain->blocks[blockchain->length - 1];

    if (b->index != prev->index + 1) {
        snprintf(msg, sizeof(msg), "Wrong index! Expected %lu, received %lu. Error: %s",
                 prev->index + 1, b->index, error_to_string(BC_ERR_INVALID_BLOCK));
        log_write(msg);
        return BC_ERR_INVALID_BLOCK;
    }

    // prev_hash must equal the hash of the previous block (same routine as the miner)
    char computed_hash[SHA256_BUFFER_LEN];
    rc = compute_block_hash(prev, computed_hash);
    if (rc != BC_OK) {
        snprintf(msg, sizeof(msg), "ERROR: block hash computation failed. Error: %s",
                error_to_string(rc));
        log_write(msg);
        return rc;
    }

    if (strcmp(b->prev_hash, computed_hash) != 0) {
        snprintf(msg, sizeof(msg), "ERROR: prev_hash mismatch. Error: %s",
                 error_to_string(BC_ERR_INVALID_BLOCK));
        log_write(msg);
        return BC_ERR_INVALID_BLOCK;
    }

    // the genesis block can have a free-form payload; the others cannot
    if (b->index != 0) {
        if (!check_transactions_format(b->transactions)) {
            snprintf(msg, sizeof(msg), "ERROR: invalid transaction format. Error: %s",
                     error_to_string(BC_ERR_INVALID_BLOCK));
            log_write(msg);
            return BC_ERR_INVALID_BLOCK;
        }
    }

    return BC_OK;
}

// validates and appends a block, then notifies miners and peer nodes
static void handle_new_block(const Block *b) {
    if (sem_lock(sem_blockchain) != 0) {
        log_write("ERROR: sem_lock(sem_blockchain) failed");
        return;
    }

    Blockchain *linked_list = &shm->blockchain;

    if (linked_list->length > 0 &&
        linked_list->blocks[linked_list->length - 1].index >= b->index) {
        log_write("Block is already in the blockchain, rejected");
        sem_unlock(sem_blockchain);
        return;
    }

    if (validate_block(b, linked_list) != BC_OK) {
        sem_unlock(sem_blockchain);
        return;
    }

    // guard against overflowing the fixed-size block array
    if (linked_list->length >= MAX_BLOCKS) {
        log_write("ERROR: blockchain is full");
        sem_unlock(sem_blockchain);
        return;
    }

    linked_list->blocks[linked_list->length] = *b;
    linked_list->length++;
    log_write("Block added to the blockchain");

    sem_unlock(sem_blockchain);

    if (sem_lock(sem_block) != 0) {
        log_write("ERROR: sem_lock(sem_block) failed");
        return;
    }
    shm->confirmed = *b;
    sem_unlock(sem_block);

    for (int i = 0; i < shm->num_miners; i++) {
        if (shm->miner_pids[i] > 0) {
            kill(shm->miner_pids[i], SIGUSR2);
        }
    }
    log_write("Signal SIGUSR2 sent to miners");

    for (int i = 0; i < num_nodes; i++) {
        if (i == node_id) {
            continue;
        }

        char fifo_path[64];
        snprintf(fifo_path, sizeof(fifo_path), "node_%d_block.fifo", i);

        int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            log_write("Cannot open peer FIFO");
            continue;
        }

        ssize_t written = write(fd, b, sizeof(Block));
        if (written != (ssize_t)sizeof(Block)) {
            log_write("ERROR: failed to send full block to peer");
        } else {
            log_write("Block sent to peer");
        }
        close_if_valid(fd);
    }
}

// entry point for a node process: maps shared memory, opens FIFOs and runs the event loop
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

    // create the FIFO used to propagate blocks to peer nodes
    char peer_fifo[64];
    snprintf(peer_fifo, sizeof(peer_fifo), "node_%d_block.fifo", node_id);

    mkfifo(peer_fifo, 0666);

    // open the FIFO non-blocking; keep a write-end open to prevent EOF on the read-end.
    int peer_fd = open(peer_fifo, O_RDONLY | O_NONBLOCK);
    int peer_wr = -1;
    if (peer_fd >= 0) {
        peer_wr = open(peer_fifo, O_WRONLY | O_NONBLOCK);
    }

    if (peer_fd < 0) {
        log_write("peer FIFO read endpoint not ready yet");
    }

    log_write("Node started");

    while (!got_stop) {
        // Retry opening the FIFO endpoint if it was not ready at startup.
        if (peer_fd < 0) {
            peer_fd = open(peer_fifo, O_RDONLY | O_NONBLOCK);
            if (peer_fd >= 0 && peer_wr < 0) {
                peer_wr = open(peer_fifo, O_WRONLY | O_NONBLOCK);
            }
        }

        // 1. block mined locally
        if (got_new_block) {
            got_new_block = 0;

            if (sem_lock(sem_block) != 0) {
                log_write("ERROR: sem_lock(sem_block) failed while reading mined block");
                continue;
            }

            Block b = shm->mined_last;
            shm->block_pending = 0;
            sem_unlock(sem_block);

            handle_new_block(&b);  
        }

        // 2. block received from a peer via FIFO
        if (peer_fd >= 0) {
            Block peer_block;
            ssize_t n = read(peer_fd, &peer_block, sizeof(Block));
            if (n == sizeof(Block)) {
                log_write("Block received from peer");
                handle_new_block(&peer_block);
            }
        }

        // 50ms to avoid busy-waiting
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