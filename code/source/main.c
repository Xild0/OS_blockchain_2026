// test run:
// gcc source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/log.c source/sha256.c -I include -o blockchain -lrt -lpthread
// ./blockchain 2 2 3 1 12
// 1 -> tx_frequency, 12 -> difficulty

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>
#include <inttypes.h>
#include "../include/blockchain.h"
#include "../include/miner.h"
#include "../include/client.h"
#include "../include/errors.h"
#include "../include/log.h"

int node_main(int id, int n_nodes, int shm_fd);

static pid_t node_pids[MAX_NODES];
static pid_t miner_pids[MAX_MINERS];
static pid_t client_pids[MAX_CLIENTS];

static int num_nodes = 0;
static int num_miners = 0;
static int num_clients = 0;

static int msqid = -1;
static int shm_fd = -1;
static SharedMemory *shm = NULL;

// parses a strictly positive integer; returns SUCCESS or INVALID_ARGUMENT
static int parse_positive_int(const char *s, int *out){
    char *end = NULL;
    long value;

    if (s == NULL || *s == '\0') {
        return INVALID_ARGUMENT;
    }

    errno = 0;
    value = strtol(s, &end, 10);

    if (errno != 0 || *end != '\0' || value <= 0 || value > 1000000) {
        return INVALID_ARGUMENT;
    }

    *out = (int)value;
    return SUCCESS;
}

// parses a non-negative uint64_t used by the CLI request commands.
// returns 0 on success, -1 on invalid input
static int parse_uint64(const char *s, uint64_t *out){
    char *end = NULL;
    unsigned long long v;

    if (s == NULL || *s == '\0' || *s == '-' || *s == '+') {
        return -1;
    }

    errno = 0;
    v = strtoull(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }

    *out = (uint64_t)v;
    return 0;
}

// checks whether a CLI hash argument is exactly 64 hexadecimal characters
static int is_hex_64(const char *s){
    if (s == NULL) {
        return 0;
    }

    if (strlen(s) != HASH_LENGTH) {
        return 0;
    }

    for (int i = 0; i < HASH_LENGTH; i++){
        if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }

    return 1;
}

// sends a signal to a child process.
// if the process is already gone, marks its PID as -1 instead of failing hard.
static void signal_process(pid_t *pid, int sig){
    if (pid == NULL || *pid <= 0) {
        return;
    }

    if (kill(*pid, sig) == -1) {
        if (errno == ESRCH) {
            *pid = -1;   // process already terminated
        } else {
            perror("kill");
        }
    }
}

// waits for one child process, retrying if interrupted by a signal.
// if the process was already reaped or does not exist, it simply returns.
static void wait_process(pid_t pid){
    if (pid <= 0) {
        return;
    }

    while (waitpid(pid, NULL, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }

        if (errno == ECHILD) {
            break;
        }

        perror("waitpid");
        break;
    }
}

// removes a single pair of surrounding double quotes, if present
static void strip_surrounding_quotes(char *s){
    size_t len = strlen(s);

    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static void cleanup(){
    // deallocates resources of sem, shm, msgqueue and fifo after the stop call
    sem_unlink("/sem_blockchain");
    sem_unlink("/sem_block");

    if(shm_fd >= 0){
        close(shm_fd);
        shm_fd = -1;
    }

    shm_unlink("/blockchain_shm");

    if(msqid >= 0){
        msgctl(msqid, IPC_RMID, NULL);
        msqid = -1;
    }

    for (int i = 0; i < num_nodes; i++){
        char fifo[64];
        snprintf(fifo, sizeof(fifo), "node_%d_block.fifo", i);
        unlink(fifo);
    }

    unlink(MSGQUEUE_PATH);
}

static void stop_all(void){
    // If the system is paused, SIGTERM would not be handled until resumed.
    // Therefore, first resume every child, then terminate it.
    for (int i = 0; i < num_nodes; ++i) {
        signal_process(&node_pids[i], SIGCONT);
    }
    for (int i = 0; i < num_miners; ++i) {
        signal_process(&miner_pids[i], SIGCONT);
    }
    for (int i = 0; i < num_clients; ++i) {
        signal_process(&client_pids[i], SIGCONT);
    }

    for(int i = 0; i < num_nodes; i++){
        signal_process(&node_pids[i], SIGTERM);
    }
    for(int i = 0; i < num_miners; i++){
        signal_process(&miner_pids[i], SIGTERM);
    }
    for(int i = 0; i < num_clients; i++){
        signal_process(&client_pids[i], SIGTERM);
    }

    for(int i = 0; i < num_nodes; i++){
        wait_process(node_pids[i]);
    }
    for(int i = 0; i < num_miners; i++){
        wait_process(miner_pids[i]);
    }
    for(int i = 0; i < num_clients; i++){
        wait_process(client_pids[i]);
    }
}

static void pause_all(void){
    // sends SIGSTOP to all child processes to pause them
    for(int i = 0; i < num_nodes; i++){
        signal_process(&node_pids[i], SIGSTOP);
    }
    for(int i = 0; i < num_miners; i++){
        signal_process(&miner_pids[i], SIGSTOP);
    }
    for(int i = 0; i < num_clients; i++){
        signal_process(&client_pids[i], SIGSTOP);
    }

    printf("The system is paused\n");
}

static void resume_all(void){
    // sends SIGCONT to all child processes to resume them
    for(int i = 0; i < num_nodes; i++){
        signal_process(&node_pids[i], SIGCONT);
    }
    for(int i = 0; i < num_miners; i++){
        signal_process(&miner_pids[i], SIGCONT);
    }
    for(int i = 0; i < num_clients; i++){
        signal_process(&client_pids[i], SIGCONT);
    }

    printf("The system is resumed\n");
}

static void submit_transaction(const char *tx){
    if(!is_valid_transaction(tx)){
        printf("Error INVALID_TRANSACTION: invalid transaction format\n");
        return;
    }

    TxMessage msg;
    memset(&msg, 0, sizeof(TxMessage));

    msg.mtype = MSG_TYPE_TRANSACTION;
    snprintf(msg.content, sizeof(msg.content), "%s", tx);

    if(msgsnd(msqid, &msg, sizeof(msg.content), IPC_NOWAIT) == -1){
        perror("Failed transaction submission");
    } else {
        printf("Transaction sent successfully\n");
    }
}

// prints a complete block representation for CLI requests
static void print_block_full(const Block *b){
    if (b == NULL) {
        return;
    }

    char ts[17];
    char nonce[17];
    char hash[SHA256_BUFFER_LEN];

    int_to_hex(b->timestamp, ts);
    int_to_hex(b->nonce, nonce);

    printf(" Block %" PRIu64 ":\n", b->index);
    printf("   timestamp: %s\n", ts);
    printf("   prev_hash: %s\n", b->prev_hash);
    printf("   merkle_root: %s\n", b->merkle_root);
    printf("   nonce: %s\n", nonce);
    printf("   transactions: %s\n", b->transactions);

    if (compute_block_hash(b, hash) == BC_OK) {
        printf("   hash: %s\n", hash);
    } else {
        printf("   hash: <error computing hash>\n");
    }
}

static void request_blockchain(void){
    sem_t *sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    int length = shm->blockchain.length;
    printf("Blockchain length: %d\n", length);

    for(int i = 0; i < length; i++){
        print_block_full(&shm->blockchain.blocks[i]);
    }

    sem_post(sem);
    sem_close(sem);
}

static void request_blockchain_by_Index(uint64_t index){
    printf("REQUEST_BLOCKCHAIN_BY_INDEX\n");

    sem_t *sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    if(index >= (uint64_t)shm->blockchain.length){
        printf("The blockchain has not reached that block yet (current length: %d)\n",
               shm->blockchain.length);
        sem_post(sem);
        sem_close(sem);
        return;
    }

    signed long int diff = shm->blockchain.length - (signed long int)index;
    printf("Blockchain length from index: %ld\n", diff);

    for(int i = (int)index; i < shm->blockchain.length; i++){
        print_block_full(&shm->blockchain.blocks[i]);
    }

    sem_post(sem);
    sem_close(sem);
}

static void request_blockchain_by_Hash(char *hash){
    printf("REQUEST_BLOCKCHAIN_BY_HASH\n");

    sem_t *sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    bool found = false;
    uint64_t index_start = 0;
    char computed[SHA256_BUFFER_LEN];

    for (int i = 0; i < shm->blockchain.length; ++i) {
        if (compute_block_hash(&shm->blockchain.blocks[i], computed) != BC_OK) {
            continue;
        }

        if(strcmp(computed, hash) == 0){
            found = true;
            index_start = shm->blockchain.blocks[i].index;
            break;
        }
    }

    if(!found){
        printf("Blockchain request failed:\n");
        printf("Block %s not found (blockchain length: %d)\n", hash, shm->blockchain.length);
        sem_post(sem);
        sem_close(sem);
        return;
    }

    sem_post(sem);
    sem_close(sem);

    request_blockchain_by_Index(index_start);
}

static void request_block_by_Index(uint64_t index){
    sem_t *sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    if(index < (uint64_t)shm->blockchain.length){
        Block *b = &shm->blockchain.blocks[index];
        print_block_full(b);
    } else {
        printf("Block %" PRIu64 " not found (blockchain length: %d)\n",
               index, shm->blockchain.length);
    }

    sem_post(sem);
    sem_close(sem);
}

static void request_block_by_Hash(char *hash){
    sem_t *sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    bool found = false;
    char computed[SHA256_BUFFER_LEN];

    for (int i = 0; i < shm->blockchain.length; ++i) {
        if (compute_block_hash(&shm->blockchain.blocks[i], computed) != BC_OK) {
            continue;
        }

        if(strcmp(computed, hash) == 0){
            print_block_full(&shm->blockchain.blocks[i]);
            found = true;
            break;
        }
    }

    if(!found){
        printf("Block %s not found (blockchain length: %d)\n", hash, shm->blockchain.length);
    }

    sem_post(sem);
    sem_close(sem);
}

static void save_blockchain(const char *filename){
    // read from shm and save on file
    sem_t* sem = sem_open("/sem_blockchain", 0);

    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore");
        return;
    }

    if (sem_wait(sem) == -1) {
        perror("Error: failed to lock semaphore");
        sem_close(sem);
        return;
    }

    int rc = blockchain_save(&shm->blockchain, filename);

    sem_post(sem);
    sem_close(sem);

    if(rc == BC_OK){
        printf("Blockchain saved successfully to %s\n", filename);
    } else {
        printf("Error: failed to save blockchain to %s\n", filename);
    }
}

static void run_cli(void){
    // interactive command loop: dispatches user input to system control functions
    char line[512];

    printf("System CLI, available commands:\n");
    printf("submit <transaction>\n");
    printf("request blockchain\n");
    printf("request blockchain --index <index>\n");
    printf("request blockchain --hash <block_hash>\n");
    printf("request block --index <index>\n");
    printf("request block --hash <block_hash>\n");
    printf("save blockchain <filename>\n");
    printf("pause\n");
    printf("resume\n");
    printf("stop\n");

    while(1){
        printf("> ");

        if(fgets(line, sizeof(line), stdin) == NULL){
            printf("Error reading input\n");
            stop_all();
            cleanup();
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        if(strcmp(line, "stop") == 0){
            stop_all();
            cleanup();
            break;
        }
        else if(strcmp(line, "pause") == 0){
            pause_all();
        }
        else if(strcmp(line, "resume") == 0){
            resume_all();
        }
        else if(strcmp(line, "request blockchain") == 0){
            request_blockchain();
        }
        else if(strncmp(line, "request blockchain --index ", 27) == 0){
            uint64_t idx;

            if (parse_uint64(line + 27, &idx) != 0){
                printf("Error INVALID_ARGUMENT: index must be a non-negative integer\n");
            } else {
                request_blockchain_by_Index(idx);
            }
        }
        else if(strncmp(line, "request blockchain --hash ", 26) == 0){
            const char *arg = line + 26;

            if (!is_hex_64(arg)){
                printf("Error INVALID_ARGUMENT: hash must be 64 hex chars\n");
            } else {
                char hex[HASH_LENGTH + 1];
                strncpy(hex, arg, HASH_LENGTH);
                hex[HASH_LENGTH] = '\0';
                request_blockchain_by_Hash(hex);
            }
        }
        else if (strncmp(line, "request block --index ", 22) == 0){
            uint64_t idx;

            if (parse_uint64(line + 22, &idx) != 0){
                printf("Error INVALID_ARGUMENT: index must be a non-negative integer\n");
            } else {
                request_block_by_Index(idx);
            }
        }
        else if (strncmp(line, "request block --hash ", 21) == 0){
            const char *arg = line + 21;

            if (!is_hex_64(arg)){
                printf("Error INVALID_ARGUMENT: hash must be 64 hex chars\n");
            } else {
                char hex[HASH_LENGTH + 1];
                strncpy(hex, arg, HASH_LENGTH);
                hex[HASH_LENGTH] = '\0';
                request_block_by_Hash(hex);
            }
        }
        else if(strncmp(line, "save blockchain ", 16) == 0) {
            save_blockchain(line + 16);
        }
        else if (strncmp(line, "submit ", 7) == 0) {
            char tx[MAX_TX_LEN];
            snprintf(tx, sizeof(tx), "%s", line + 7);
            strip_surrounding_quotes(tx);   // accept both submit X and submit "X"
            submit_transaction(tx);
        }
        else {
            printf("Unknown command\n");
        }
    }
}

int main(int argc, char *argv[]){
    // unbuffered stdout so child processes do not inherit and re-print buffered text after fork()
    setvbuf(stdout, NULL, _IONBF, 0);

    log_cleanup();

    if(argc < 4){
        fprintf(stderr, "Usage: %s <num_nodes> <num_miners> <num_clients> [tx_frequency] [difficulty] [initial_state.csv]\n", argv[0]);
        return INVALID_ARGUMENT;
    }

    if (parse_positive_int(argv[1], &num_nodes) != SUCCESS ||
        parse_positive_int(argv[2], &num_miners) != SUCCESS ||
        parse_positive_int(argv[3], &num_clients) != SUCCESS) {
        fprintf(stderr, "Error: num_nodes, num_miners and num_clients must be positive integers\n");
        return INVALID_ARGUMENT;
    }

    int tx_frequency = 1;
    if(argc > 4){
        if (parse_positive_int(argv[4], &tx_frequency) != SUCCESS) {
            fprintf(stderr, "Error: transaction_frequency must be a positive integer\n");
            return INVALID_ARGUMENT;
        }
    }

    int difficulty = 12;
    if(argc > 5){
        if (parse_positive_int(argv[5], &difficulty) != SUCCESS) {
            fprintf(stderr, "Error: difficulty must be a positive integer\n");
            return INVALID_ARGUMENT;
        }
    }

    char *init_csv = (argc > 6) ? argv[6] : NULL;

    if (num_nodes > MAX_NODES || num_miners > MAX_MINERS || num_clients > MAX_CLIENTS) {
        fprintf(stderr, "Error: too many processes (max %d nodes, %d miners, %d clients)\n",
                MAX_NODES, MAX_MINERS, MAX_CLIENTS);
        return INVALID_ARGUMENT;
    }

    // shm
    shm_unlink("/blockchain_shm"); // cleanup in case of previous run
    shm_fd = shm_open("/blockchain_shm", O_CREAT | O_RDWR, 0666);
    if(shm_fd < 0){
        perror("Error: failed to create shared memory");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(SharedMemory)) == -1) {
        perror("Error: failed to resize shared memory");
        cleanup();
        return 1;
    }

    shm = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
        perror("Error: failed to map shared memory");
        cleanup();
        return 1;
    }

    memset(shm, 0, sizeof(SharedMemory));

    // sem
    sem_unlink("/sem_blockchain");
    sem_unlink("/sem_block");

    sem_t* sem_blockchain = sem_open("/sem_blockchain", O_CREAT, 0666, 1);
    sem_t* sem_block = sem_open("/sem_block", O_CREAT, 0666, 1);

    if(sem_blockchain == SEM_FAILED || sem_block == SEM_FAILED){
        perror("Error: failed to create semaphores");
        cleanup();
        return 1;
    }

    sem_close(sem_blockchain);
    sem_close(sem_block);

    // msg queue
    FILE *file = fopen(MSGQUEUE_PATH, "w");
    if(file) {
        fclose(file);
    } // create file if it doesn't exist

    key_t key = ftok(MSGQUEUE_PATH, MSGQUEUE_PROJ_ID);
    if(key == -1){
        perror("ftok");
        cleanup();
        return 1;
    }

    msqid = msgget(key, IPC_CREAT | 0666);
    if(msqid == -1){
        perror("msgget");
        cleanup();
        return 1;
    }

    if(init_csv != NULL){
        int rc = blockchain_load(&shm->blockchain, init_csv);

        if (rc != BC_OK) {
            fprintf(stderr, "Error loading %s (rc=%d)\n", init_csv, rc);
            cleanup();
            return (rc == BC_ERR_FILE_OPEN) ? FILE_NOT_FOUND : INVALID_BLOCK;
        }

        if (shm->blockchain.length == 0) {
            fprintf(stderr, "Error: the provided initial blockchain is empty\n");
            cleanup();
            return EMPTY_BLOCKCHAIN;
        }

        rc = blockchain_validate(&shm->blockchain);
        if (rc != BC_OK) {
            fprintf(stderr, "Error: invalid initial blockchain state (rc=%d)\n", rc);
            cleanup();
            return INVALID_BLOCK;
        }

        printf("Blockchain created from %s with %d blocks\n", init_csv, shm->blockchain.length);
    }

    // fork for NODES
    shm->num_nodes = num_nodes;
    for (int i = 0; i < num_nodes; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork node");
            stop_all();
            cleanup();
            return 1;
        }

        if (pid == 0) {
            exit(node_main(i, num_nodes, shm_fd));
        }

        node_pids[i] = pid;
        shm->node_pids[i] = pid;
        printf("Node %d started with PID: %d\n", i, pid);
    }

    // fork for MINERS
    shm->num_miners = num_miners;
    for (int i = 0; i < num_miners; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork miner");
            stop_all();
            cleanup();
            return 1;
        }

        if (pid == 0) {
            close(shm_fd);
            exit(miner_main(i, num_nodes, difficulty));
        }

        miner_pids[i] = pid;
        shm->miner_pids[i] = pid;
        printf("Miner %d started with PID: %d\n", i, pid);
    }

    // fork for CLIENTS
    for (int i = 0; i < num_clients; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork client");
            stop_all();
            cleanup();
            return 1;
        }

        if (pid == 0) {
            close(shm_fd);
            exit(client_main(i, tx_frequency));
        }

        client_pids[i] = pid;
        printf("Client %d started with PID: %d\n", i, pid);
    }

    usleep(500000); // wait 500ms for children to initialize
    run_cli();

    return 0;
}