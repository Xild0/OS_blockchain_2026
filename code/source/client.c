#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "../include/blockchain.h"
#include "../include/client.h"

#define NUM_NAMES 6

static sig_atomic_t got_stop = 0;

static int client_id = -1;
static FILE *logfile = NULL;

static const char *names[NUM_NAMES] = {
    "Alice",
    "Bob",
    "Gio",
    "Tollo",
    "Ilaria",
    "Francesco"
};

static void handler_sigterm(int sig){
    (void)sig;
    got_stop = 1;
}

static void client_log(const char *msg){
    if(logfile == NULL){
        return;
    }

    fprintf(logfile, "CLIENT ID: [%d], timestamp: [%ld], message: %s\n",
            client_id, (long)time(NULL), msg);
    fflush(logfile);
}

static void generate_transaction(char *transaction){
    int sender;
    int receiver;
    int amount;

    sender = rand() % NUM_NAMES;

    do{
        receiver = rand() % NUM_NAMES;
    }while(receiver == sender);

    amount = (rand() % 99) + 1;

    snprintf(transaction, MAX_TX_LEN, "%s pays %s %d coins",
             names[sender], names[receiver], amount);
}

int client_main(int id, int transaction_frequency){
    client_id = id;

    char logname[64];
    snprintf(logname, sizeof(logname), "client_%d_%d.log", client_id, (int)getpid());

    logfile = fopen(logname, "w");
    if(logfile == NULL){
        perror("fopen");
        return 1;
    }

    if(transaction_frequency <= 0){
        transaction_frequency = 1;
    }

    srand(time(NULL) ^ getpid());

    signal(SIGTERM, handler_sigterm);

    key_t key = ftok(MSGQUEUE_PATH, MSGQUEUE_PROJ_ID);
    if(key == -1){
        client_log("ERROR: ftok failed");
        fclose(logfile);
        return 1;
    }

    int msqid = msgget(key, 0666);
    if(msqid == -1){
        client_log("ERROR: msgget failed");
        fclose(logfile);
        return 1;
    }

    client_log("Client avviato");

    while(!got_stop){
        TxMessage msg;

        msg.mtype = MSG_TYPE_TRANSACTION;
        generate_transaction(msg.content);

        if(msgsnd(msqid, &msg, sizeof(msg.content), IPC_NOWAIT) == -1){
            client_log("ERROR: transaction not sent");
        }
        else{
            client_log(msg.content);
        }

        sleep(transaction_frequency);
    }

    client_log("Client in chiusura");

    fclose(logfile);
    return 0;
}

int main(){
    return client_main(0, 1);
}