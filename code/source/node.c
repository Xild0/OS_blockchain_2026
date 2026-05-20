#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
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


static void sem_lock(sem_t *sem){
    sem_wait(sem);
}

static void sem_unlock(sem_t *sem){
    sem_post(sem);
}

static void handler_sigusr1(int sig){
    (void)sig;
    got_new_block = 1;
}

static void handler_sigterm(int sig) {
    (void)sig;
    got_stop = 1;
}

static int validate_block(const Block* b, const Blockchain* blockchain){
    if(blockchain->length == 0){
        return BC_OK;
    }

    const Block *prev = &blockchain->blocks[blockchain->length-1];

    if(b->index != prev->index+1){
        log_write("Wrong index! Atteso %lu, ricevuto %lu. Interruzione con codice: %s", prev->index + 1, b->index, error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }

    char prev_hex[4500];
    char idx_hex[17];
    char ts_hex[17];
    char nonce_hex[17];
    int_to_hex(prev->index,     idx_hex);
    int_to_hex(prev->timestamp, ts_hex);
    int_to_hex(prev->nonce,     nonce_hex);

    snprintf(prev_hex, sizeof(prev_hex), "%s%s%s%s%s%s", idx_hex, ts_hex,prev->prev_hash, prev->merkle_root, nonce_hex, prev->transactions);

    char computed_hash[SHA256_BUFFER_LEN];
    sha256_hex(prev_hex, strlen(prev_hex), computed_hash);
    computed_hash[SHA256_HEX_LEN] = '\0';
    if(strcmp(b->prev_hash, computed_hash)!= 0){
        log_write("ERROR: prev hash do not correspond! Interruzione con codice: %s", error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }

    char computed_merkel[SHA256_BUFFER_LEN];
    char tx_copy[MAX_TX_LEN];
    strncpy(tx_copy, b->transactions, MAX_TX_LEN -1);
    tx_copy[MAX_TX_LEN-1]='\0';
    calcola_merkle_root(tx_copy, computed_merkel);

    if(strcmp(b->merkle_root, computed_merkel)!=0){
        log_write("ERROR: merkel root do not correspond! Interruzione con codice: %s", error_to_string(BC_ERR_INVALID_BLOCK));
        return BC_ERR_INVALID_BLOCK;
    }
    return BC_OK;

}

static void handle_new_block(const Block* b){
    sem_lock(sem_blockchain);

    Blockchain *linked_list = &shm->blockchain;

    if(linked_list->length >0 && linked_list->blocks[linked_list->length-1].index >= b->index){
        log_write("Block is already in the blockchain, rejected");
        sem_unlock(sem_blockchain);
        return;
    }

    if(validate_block(b, linked_list)!= BC_OK){
        sem_unlock(sem_blockchain);
        return;
    }

    linked_list->blocks[linked_list->length]=*b;
    linked_list->length++;
    log_write("Block add to blockchain");
    sem_unlock(sem_blockchain);

    sem_lock(sem_block);
    shm->confirmed = *b;
    shm->ready_block = 0;
    sem_unlock(sem_block);
    for (int i = 0; i < shm->num_miners; i++)
    {
        if(shm->miner_pids[i]>0){
            kill(shm->miner_pids[i], SIGUSR2);
        }
    }
    log_write("signal: SIGUSR2 send to miner");

    for (int i = 0; i < num_nodes; i++)
    {
        if(i == node_id) continue;
        char fifo_path[64];
        snprintf(fifo_path, sizeof(fifo_path), "node_%d_block.fifo", i);

        int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
        if(fd<0){
            log_write("cannot open fifo peer");
            continue;
        }

        write(fd, b, sizeof(Block));
        close(fd);
        log_write("Block send to peer");
    }
    
    
}

static void handle_parent_command(int command_fifo){
    char command[256];
    ssize_t s = read(command_fifo,command, sizeof(command)-1);
    if(s<=0){return;}
    command[s] = '\0';

    log_write("%s", command);

    int pfd = open("parent.fifo", O_WRONLY | O_NONBLOCK);
    if(pfd<0){return;}

     // manda l'intera blockchain
    if(strcmp(command, "request blockchain") == 0){
        sem_lock(sem_blockchain);
        write(pfd, &shm->blockchain, sizeof(Blockchain));
        sem_unlock(sem_blockchain);

        //manda un singolo blocco per index
    }else if(strncmp(command, "request blockchain", 19)==0){
        uint64_t index = (uint64_t)atoll(command + 19);
        sem_lock(sem_blockchain);
        if(index <(uint64_t)shm->blockchain.length){
            write(pfd, &shm->blockchain.blocks[index], sizeof(Block));

        }
        sem_unlock(sem_blockchain);
    }
    close(pfd);

}

int node_main(int id, int n_nodes, int shm_fd){
    if(log_init("node", id) != 0){
        return 1;
    }

    node_id = id;
    num_nodes = n_nodes;

    shm = (SharedMemory *)mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
        perror("mmap");
        return 1;
    }

    sem_blockchain = sem_open("/sem_blockchain", 0);
    sem_block = sem_open("/sem_block", 0);
    if(sem_blockchain == SEM_FAILED || sem_block == SEM_FAILED){
        perror("sem_open");
        return 1;
    }

    signal(SIGUSR1, handler_sigusr1);
    signal(SIGTERM, handler_sigterm);

    // crea le FIFO
    char peer_fifo[64], command_fifo[64];
    snprintf(peer_fifo, sizeof(peer_fifo), "node_%d_block.fifo", node_id);
    snprintf(command_fifo,  sizeof(command_fifo),  "node_%d_cmd.fifo",  node_id);
    mkfifo(peer_fifo, 0666);
    mkfifo(command_fifo,  0666);

    // apre le FIFO in lettura non bloccante + trick write per evitare EOF
    int peer_fd = open(peer_fifo, O_RDONLY | O_NONBLOCK);
    int peer_wr = open(peer_fifo, O_WRONLY | O_NONBLOCK);
    int cmd_fd  = open(command_fifo,  O_RDONLY | O_NONBLOCK);
    int cmd_wr  = open(command_fifo,  O_WRONLY | O_NONBLOCK);

    log_write("Nodo avviato");

    // Main loop 
    while (!got_stop) {

     
        if (got_new_block) {
            got_new_block = 0;

            sem_lock(sem_block);
            Block b = shm->mined_last;
            sem_unlock(sem_block);

            handle_new_block(&b);
        }

        // 2. blocco da un peer via FIFO
        Block peer_block;
        ssize_t n = read(peer_fd, &peer_block, sizeof(Block));
        if (n == sizeof(Block)) {
            log_write("Blocco ricevuto da peer");
            handle_new_block(&peer_block);
        }

        // 3. comando dal parent via FIFO
        handle_parent_command(cmd_fd);

        usleep(50000); // 50ms per non consumare CPU
    }

    log_write("Nodo in chiusura");
    close(peer_fd);
    close(peer_wr);
    close(cmd_fd);
    close(cmd_wr);
    munmap(shm, sizeof(SharedMemory));
    sem_close(sem_blockchain);
    sem_close(sem_block);
    log_close();
    return 0;


}

