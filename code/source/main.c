// test run: 
//      $gcc source/main.c source/blockchain.c source/client.c source/miner.c source/node.c source/ipc.c source/log.c source/sha256.c -I include -o blockchain -lrt -lpthread
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
#include "../include/blockchain.h"
#include "../include/miner.h"
#include "../include/client.h"

// dichiarazioni
int node_main(int id, int n_nodes, int shm_fd);
int miner_main(int id, int n_nodes, int difficulty);
int client_main(int id, int tx_frequency);

int node(int id, int num, int shm_fd);

static pid_t node_pids[MAX_NODES];  
static pid_t miner_pids[MAX_MINERS];
static pid_t client_pids[16];

static int num_nodes   = 0; 
static int num_miners  = 0;
static int num_clients = 0;
static int msqid       = -1;
static int shm_fd      = -1;
static SharedMemory *shm = NULL;

static void cleanup(){ //deallocates resoruces of sem, shm, msgqueue, fifo after stop call 
        sem_unlink("/sem_blockchain");
        sem_unlink("/sem_block");
        if(shm !=NULL){
            shm_unlink("/blockchain_shm");
        }
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
    TxMessage msg;
    int l = MAX_TX_LEN-1;
    msg.mtype = MSG_TYPE_TRANSACTION;
    strncpy(msg.content, tx, l);
    msg.content[l] = '\0';
    if(msgsnd(msqid, &msg, sizeof(msg.content), IPC_NOWAIT)==-1){
        perror("Failed transaction submission\n");
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
        printf("  Block %lu:\n", shm->blockchain.blocks[i].index);
        printf("    prev_hash:    %s\n", shm->blockchain.blocks[i].prev_hash);
        printf("    merkle_root:  %s\n", shm->blockchain.blocks[i].merkle_root);
        printf("    transactions: %s\n", shm->blockchain.blocks[i].transactions);
    }
    sem_post(sem);
    sem_close(sem);
   
   /* int fd = open("node_0_cmd.fifo", O_WRONLY | O_NONBLOCK); //first node receive req for blockchain
    if(fd<0){
        perror("Failed to open node\n");
        return;
    }
    char cmd[] = "request blockchain";
    write(fd, cmd, sizeof(cmd));
    close(fd);

    mkfifo("parent.fifo", 0666); //parent fifo creation
    int pfd = open("parent.fifo", O_RDONLY | O_NONBLOCK); //read response
    if(pfd<0){
        perror("Failed to open parent fifo\n");
        return;
    }
    usleep(200000); // 200ms for response

    Blockchain bc;
    ssize_t bytesR = read(pfd, &bc, sizeof(Blockchain));
    close(pfd);

    if(bytesR == sizeof(Blockchain)){
        printf("Blockchain lenght: %d\n", bc.length);
        for(int i=0; i<bc.length; i++){
            printf("  Block index:%lu \n prev_hash: %s\n", bc.blocks[i].index, bc.blocks[i].prev_hash);
        }
    }else{
        perror("No response received\n");
    }*/
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
        printf("Block %lu:\n", b->index);
        printf("  prev_hash:    %s\n", b->prev_hash);
        printf("  merkle_root:  %s\n", b->merkle_root);
        printf("  transactions: %s\n", b->transactions);
    } else {
        printf("Block %lu not found (blockchain length: %d)\n", index, shm->blockchain.length);
    }
    sem_post(sem);
    sem_close(sem);
    /*int fd = open("node_0_cmd.fifo", O_WRONLY | O_NONBLOCK); //first node receive req for block by index
    if(fd<0){
        perror("Error: failed to open node\n");
        return;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "request blockchain %lu", index);
    write(fd, cmd, strlen(cmd));
    close(fd);

    mkfifo("parent.fifo", 0666); //parent fifo creation
    int pfd = open("parent.fifo", O_RDONLY | O_NONBLOCK); //read response
    if(pfd<0){
        perror("Error: failed to open parent fifo\n");
        return;
    }
    usleep(200000); // 200ms for response

    Block b;
    ssize_t bytesR = read(pfd, &b, sizeof(Block));
    close(pfd);

    if(bytesR == sizeof(Block)){
        printf("Block %lu:\n", b.index);
        printf("prev_hash:%s - ", b.prev_hash);
        printf("merkle_root:%s - ", b.merkle_root);
        printf("transactions:%s - ", b.transactions);
    }else{
        perror("Block not found\n");
    }*/
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

static void run_cli(void){
    char line[512];
    printf("System CLI, available commands:\n");
    printf("submit <transaction>\n");
    printf("request blockchain\n");
    printf("request block <index>\n");
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
        }else if(strcmp(line, "pause")==0){pause_all();
        } else if(strcmp(line, "resume")==0){resume_all();
        } else if (strcmp(line, "request blockchain")==0){request_blockchain();
        } else if (strncmp(line, "request block", 14)==0){
            uint64_t idx = (uint64_t)atoll(line + 14); 
            request_block_by_Index(idx);
        } else if(strncmp(line, "save blockchain ", 16) == 0) {
            save_blockchain(line + 16);
        }
        else if (strncmp(line, "submit ", 7) == 0) {
    submit_transaction(line + 7);
    }else{
        printf("Unknown command\n");
    }
}
}

int main(int argc, char *argv[]){
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

    if (num_nodes > MAX_NODES || num_miners > MAX_MINERS) {
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
    if(file) {fclose(file);} //crea file se non esiste

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
            exit(miner_main(i, num_nodes, difficulty));
        }
        miner_pids[i] = pid;
        shm->miner_pids[i] = pid;
        printf("Miner %d started with PID: %d\n", i, pid);
    }

    //for for CLIENTS
     for (int i = 0; i < num_clients; i++) {
        pid_t pid = fork();
        if (pid < 0) { 
            perror("fork client"); 
            stop_all(); 
            cleanup(); 
            return 1; 
        }
        if (pid == 0) {
            exit(client_main(i, tx_frequency));
        }
        client_pids[i] = pid;
        printf("Client %d started with PID: %d\n", i, pid);
    }

    usleep(500000); // aspetta 500ms che i figli si inizializzino
    
    run_cli(); 

    return 0;

}






