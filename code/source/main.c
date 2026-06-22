// test run: 
//      $gcc source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/log.c source/sha256.c -I include -o blockchain -lrt -lpthread
//      $ ./blockchain 2 2 3 1 12 
//      1->tx_frequency, 12->difficulty

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>
#include "../include/blockchain.h"
#include "../include/miner.h"
#include "../include/client.h"
#include "../include/errors.h"
#include "../include/log.h"

int node_main(int id, int n_nodes, int shm_fd);
int miner_main(int id, int difficulty);
int client_main(int id, int tx_frequency);

// int node(int id, int num, int shm_fd);                   // funzione morta, si può togliere

static pid_t node_pids[MAX_NODES];  
static pid_t miner_pids[MAX_MINERS];
static pid_t client_pids[MAX_CLIENTS];

static int num_nodes   = 0; 
static int num_miners  = 0;
static int num_clients = 0;
static int msqid       = -1;
static int shm_fd      = -1;
static SharedMemory *shm = NULL;

static void cleanup(){ //deallocates resoruces of sem, shm, msgqueue, fifo after stop call 
    sem_unlink("/sem_blockchain");
    sem_unlink("/sem_block");

    if(shm_fd >= 0){
        close(shm_fd);
    }
    shm_unlink("/blockchain_shm");

    if(msqid >=0){
        msgctl(msqid, IPC_RMID, NULL);
    }

    for (int i=0; i<num_nodes; i++){
        char fifo[64];
        snprintf(fifo, sizeof(fifo), "node_%d_block.fifo", i);
        unlink(fifo);
        snprintf(fifo, sizeof(fifo), "node_%d_cmd.fifo", i);
        unlink(fifo);
    }
    
    for(int i =0; i<num_miners; i++){
        char fifo[64];
        snprintf(fifo, sizeof(fifo), "miner_%d_block.fifo", i);
        unlink(fifo);
    }

    unlink("parent.fifo");
    unlink(MSGQUEUE_PATH);
}

static void stop_all(void){ //terminates all child processes and wait for them to finish
    for (int i = 0; i < num_nodes; ++i)
    {
        if (node_pids[i] > 0){
            kill(node_pids[i], SIGCONT);
        }
    }

    for (int i = 0; i < num_miners; ++i)
    {
        if (miner_pids[i] > 0){
            kill(miner_pids[i], SIGCONT);
        }
    }

    for (int i = 0; i < num_clients; ++i)
    {
        if (client_pids[i] > 0){
            kill(client_pids[i], SIGCONT);
        }
    }

    for(int i=0; i<num_nodes; i++){
        if(node_pids[i] > 0){
            kill(node_pids[i], SIGTERM);
        }
    }

    for(int i=0; i<num_miners; i++){
        if(miner_pids[i] > 0){
            kill(miner_pids[i], SIGTERM);
        }
    }
    
    for(int i=0; i<num_clients; i++){
        if(client_pids[i] > 0){
            kill(client_pids[i], SIGTERM);
        }
    }

    int total_processes = num_nodes + num_miners + num_clients;
    for (int  i = 0; i < total_processes; i++)
    {
        wait(NULL);
    }
}

static void pause_all(void){ //sends SIGSTOP to all child processes to make them pause
    for(int i=0; i<num_nodes; i++){
        if(node_pids[i] > 0){
            kill(node_pids[i], SIGSTOP);
        }
    }

    for(int i=0; i<num_miners; i++){
        if(miner_pids[i] > 0){
            kill(miner_pids[i], SIGSTOP);
        }
    }
    
    for(int i=0; i<num_clients; i++){
        if(client_pids[i] > 0){
            kill(client_pids[i], SIGSTOP);
        }
    }

    printf("The system is paused\n");
}

static void resume_all(void){ //sends SIGCONT to all child processes to make them resume
    for(int i=0; i<num_nodes; i++){
        if(node_pids[i] > 0){
            kill(node_pids[i], SIGCONT);
        }
    }

    for(int i=0; i<num_miners; i++){
        if(miner_pids[i] > 0){
            kill(miner_pids[i], SIGCONT);
        }
    }
    
    for(int i=0; i<num_clients; i++){
        if(client_pids[i] > 0){
            kill(client_pids[i], SIGCONT);
        }
    }

    printf("The system is resumed");
}

static void submit_transaction(const char *tx){
    if(!is_valid_transaction(tx)){
        printf("Error INVALID_TRANSACTION: invalid transaction format\n");
        return;
    }

    TxMessage msg;
    memset(&msg, 0, sizeof(TxMessage));
    msg.mtype=MSG_TYPE_TRANSACTION;
    snprintf(msg.content, sizeof(msg.content), "%s", tx);

    if(msgsnd(msqid, &msg, sizeof(msg.content), IPC_NOWAIT)==-1){
        perror("Transaction send failed\n");
    }else{
        printf("Transaction send successfully\n");
    }
}

static void request_blockchain(void){
   sem_t *sem = sem_open("/sem_blockchain", 0);
    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore\n");
        return;
    }
 
    sem_wait(sem);
    int length = shm->blockchain.length;
    printf("Blockchain length: %d\n", length);
    for(int i = 0; i < length; i++){
        printf(" Block %lu:\n", shm->blockchain.blocks[i].index);
        printf(" prev_hash:    %s\n", shm->blockchain.blocks[i].prev_hash);
        printf(" merkle_root:  %s\n", shm->blockchain.blocks[i].merkle_root);
        printf(" transactions: %s\n", shm->blockchain.blocks[i].transactions);
    }
    sem_post(sem);
    sem_close(sem);
   
}

static void request_blockchain_by_Index(uint64_t index){
    printf("REQUEST_BLOCKCHAIN_BY_INDEX\n");
    sem_t *sem = sem_open("/sem_blockchain", 0);
    if(sem == SEM_FAILED){
        perror("Error: failed to open sempahore\n");
        return;
    }

    sem_wait(sem); 
    if(index >= (uint64_t)shm->blockchain.length){
        printf("La blockchain non è ancora arrivata a quel blocco (lunghezza attuale: %d)\n", shm->blockchain.length);
        sem_post(sem);
        sem_close(sem);
        return;
    }
    else if((shm->blockchain.length - index) == 0){
        signed long int diff = shm->blockchain.length - index;
        printf("Blockchain length from index: %ld\n", diff);
        printf("  Block %lu:\n", shm->blockchain.blocks[index].index);
        printf("    prev_hash:    %s\n", shm->blockchain.blocks[index].prev_hash);
        printf("    merkle_root:  %s\n", shm->blockchain.blocks[index].merkle_root);
        printf("    transactions: %s\n", shm->blockchain.blocks[index].transactions);
        sem_post(sem);
        sem_close(sem);
        return;
    } else {
        signed long int diff = shm->blockchain.length - index;
        printf("Blockchain lenght from index: %ld\n", diff);
        for(int i = index; i < shm->blockchain.length; i++){
            printf("  Block %lu:\n", shm->blockchain.blocks[i].index);
            printf("    prev_hash:    %s\n", shm->blockchain.blocks[i].prev_hash);
            printf("    merkle_root:  %s\n", shm->blockchain.blocks[i].merkle_root);
            printf("    transactions: %s\n", shm->blockchain.blocks[i].transactions);
        }
        sem_post(sem);
        sem_close(sem);

    }        
}

static void request_blockchain_by_Hash(char *hash){
    printf("REQUEST_BLOCKCHAIN_BY_HASH\n");
    sem_t *sem = sem_open("/sem_blockchain", 0);
    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore\n");
        return;
    }

    sem_wait(sem);
    char hash_found[SHA256_BUFFER_LEN];
    bool found = false;
    uint64_t index_start = 0;
    for (int i = 0; i < shm->blockchain.length; ++i)
    {
        compute_block_hash(&shm->blockchain.blocks[i], hash_found);
        if(strcmp(hash_found, hash) ==0){
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
    } else {
        sem_post(sem);
        sem_close(sem);
        request_blockchain_by_Index(index_start);
    }
}

static void request_block_by_Index(uint64_t index){
    sem_t *sem = sem_open("/sem_blockchain", 0);
    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore\n");
        return;
    }
 
    sem_wait(sem);
    if(index < (uint64_t)shm->blockchain.length){
        Block *b = &shm->blockchain.blocks[index];
        printf(" Block %lu:\n", b->index);
        printf(" prev_hash:    %s\n", b->prev_hash);
        printf(" merkle_root:  %s\n", b->merkle_root);
        printf(" transactions: %s\n", b->transactions);
    } else {
        printf("Block %lu not found (blockchain length: %d)\n", index, shm->blockchain.length);
    }
    sem_post(sem);
    sem_close(sem);
   
}

static void request_block_by_Hash(char *hash){
    sem_t *sem = sem_open("/sem_blockchain", 0);
    if(sem == SEM_FAILED){
        perror("Error: failed to open semaphore\n");
        return;
    }

    sem_wait(sem);
    bool found = false;
    char hash_found[SHA256_BUFFER_LEN];
    for (int i = 0; i < shm->blockchain.length; ++i)
    {
        compute_block_hash(&shm->blockchain.blocks[i], hash_found);
        if (strcmp(hash_found, hash)==0){
            printf("Block %lu:\n", shm->blockchain.blocks[i].index);
            printf("  prev_hash:    %s\n", shm->blockchain.blocks[i].prev_hash);
            printf("  merkle_root:  %s\n", shm->blockchain.blocks[i].merkle_root);
            printf("  transactions: %s\n", shm->blockchain.blocks[i].transactions);
            found = true;
            break;
        }
    }
    if(!found){
        printf("Block %s not found (blockchain length: %d)\n", hash, shm->blockchain.length);
    }

    sem_post(sem);
    sem_close(sem);
    return;
}

static void save_blockchain(const char *filename){ //read from shm and save on file
    sem_t* sem = sem_open("/sem_blockchain", 0);
    if(sem== SEM_FAILED){
        perror("Error: Failed to open semaphore\n");
        return;
    }
    sem_wait(sem);
    int rc = blockchain_save(&shm->blockchain, filename);
    sem_post(sem);
    sem_close(sem);

    if(rc == BC_OK){
        printf("Blockchain saved successfully to %s\n", filename);

    }else{
        printf("Error: failed to save blockchain to %s\n", filename);
    }
}

static void run_cli(void){ //interactive command loop: dispatches user input to system control functions
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
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        if(strcmp(line, "stop")==0){
            stop_all();
            cleanup();
            break;
        } else if(strcmp(line, "pause")==0){pause_all();
        } else if(strcmp(line, "resume")==0){resume_all();
        } else if(strcmp(line, "request blockchain")==0){request_blockchain();
        } else if(strncmp(line, "request blockchain --index ", 27) == 0){
            uint64_t idx = (uint64_t)atoll(line + 27);
            request_blockchain_by_Index(idx);
        } else if(strncmp(line, "request blockchain --hash ", 26) == 0 ){
            char hex[65];
            strncpy(hex, line + 26, 64);
            hex[64] = '\0';
            request_blockchain_by_Hash(hex);
        } else if (strncmp(line, "request block --index ", 22)==0){
            uint64_t idx = (uint64_t)atoll(line + 22); 
            request_block_by_Index(idx);
        } else if (strncmp(line, "request block --hash ", 21) == 0){
            char hex[65];
            strncpy(hex, line + 21, 64);
            hex[64]='\0';
            request_block_by_Hash(hex);
        } else if(strncmp(line, "save blockchain ", 16) == 0) {
            save_blockchain(line + 16);
        } else if (strncmp(line, "submit ", 7) == 0) {
            submit_transaction(line + 7);
        }else {
        printf("Unknown command\n");
        }
    }
}

int main(int argc, char *argv[]){

    log_cleanup();

    if(argc <4){
        fprintf(stderr, "Usage: %s <num_nodes> <num_miners> <num_clients> [tx_frequency] [difficulty] [initial_state.csv]\n", argv[0]);
        return 1;
    }

    num_nodes = atoi(argv[1]);
    num_miners = atoi(argv[2]);
    num_clients = atoi(argv[3]);

    
    int tx_frequency;
    if(argc > 4){
        tx_frequency = atoi(argv[4]);
    }else{
        tx_frequency = 1;
    }

    int difficulty;
    if(argc > 5){
        difficulty = atoi(argv[5]);
    }else{
        difficulty = 12;
    }

    char *init_csv;

    if(argc > 6){
        init_csv = argv[6];
    }else{
        init_csv = NULL;
    }

    if (num_nodes <= 0 || num_miners <= 0 || num_clients <= 0) {
        fprintf(stderr, "Error: num_nodes, num_miners, num_clients have to be grater than 0\n");
        return 1;
    }

    if (num_nodes > MAX_NODES || num_miners > MAX_MINERS || num_clients > MAX_CLIENTS) {
        fprintf(stderr, "Error: too many nodes or miners (max %d nodes, %d miners)\n", MAX_NODES, MAX_MINERS);
        return 1;
    }


    //shm
    shm_unlink("/blockchain_shm"); //cleanup in case of previous run
    shm_fd = shm_open("/blockchain_shm", O_CREAT | O_RDWR, 0666);
    if(shm_fd <0){
        perror("Error: failed to create shared memory\n");
        return 1;
    }
    ftruncate(shm_fd, sizeof(SharedMemory));
    shm = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
        perror("Error: failed to map shared memory\n");
        return 1;
    }
    memset(shm, 0, sizeof(SharedMemory));
    

    //sem
    sem_unlink("/sem_blockchain");
    sem_unlink("/sem_block");
    sem_t* sem_blockchain = sem_open("/sem_blockchain", O_CREAT, 0666, 1);
    sem_t* sem_block = sem_open("/sem_block", O_CREAT, 0666, 1);

    if(sem_blockchain == SEM_FAILED || sem_block == SEM_FAILED){
        perror("Error: failed to create semaphores\n");
        return 1;
    }
    sem_close(sem_blockchain);
    sem_close(sem_block);

    //msg queue
    FILE *file = fopen(MSGQUEUE_PATH, "w");
    if(file) {fclose(file);} // create file if it doesn't exist

    key_t key = ftok(MSGQUEUE_PATH, MSGQUEUE_PROJ_ID);
    if(key == -1){
        perror("ftok");
        return 1;
    }

    msqid = msgget(key, IPC_CREAT | 0666);
    if(msqid == -1){
        perror("msgget");
        return 1;
    }

    if(init_csv != NULL){ 
         int rc = blockchain_load(&shm->blockchain, init_csv); //return code 
        if (rc != BC_OK) {
            fprintf(stderr, "Error loading: %s (rc=%d)\n", init_csv, rc);
            cleanup();
            return 1;
    }
     printf("Blockchain created from %s with: %d blocks\n", init_csv, shm->blockchain.length);
    }

    //parent FIFO
    mkfifo("parent.fifo", 0666);

    //fork for NODES
    shm->num_nodes = num_nodes;
    for (int i = 0; i < num_nodes; i++)
    {
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

    //fork for MINERS
    shm->num_miners = num_miners;
    for (int i = 0; i < num_miners; i++)
    {
        pid_t pid = fork();
        if (pid < 0) { 
            perror("fork miner"); 
            stop_all(); 
            cleanup(); 
            return 1; 
        }
        if (pid == 0) {
            close(shm_fd);
            exit(miner_main(i, difficulty));
        }
        miner_pids[i] = pid;
        shm->miner_pids[i] = pid;
        printf("Miner %d started with PID: %d\n", i, pid);
    }

    //fork for CLIENTS
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

