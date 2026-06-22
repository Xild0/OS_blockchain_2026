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
#include "../include/log.h"
#include "../include/errors.h"

#define NUM_NAMES 6

// Flag used to stop the client correctly
static sig_atomic_t got_stop = 0;


static const char *names[NUM_NAMES] = {
    "Alice",
    "Bob",
    "Giovanna",
    "Ilaria",
    "Francesco",
    "Giacomo"
};


static void handler_sigterm(int sig){
    (void)sig;
    got_stop = 1;
}

// Generates a random transaction ("Alice pays Bob 10 coins")
static void generate_transaction(char *transaction){
    int sender;
    int receiver;
    int amount;


    sender = rand() % NUM_NAMES;

    // Random receiver different from sender
    do{
        receiver = rand() % NUM_NAMES;
    }while(receiver == sender);

    amount = (rand() % 99) + 1;

   
    snprintf(transaction, MAX_TX_LEN, "%s pays %s %d coins", names[sender], names[receiver], amount);
}

int client_main(int id, int transaction_frequency){
    key_t key;
    int msqid;

    if(log_init("client", id) != 0){
        return 1;
    }

    if(transaction_frequency <= 0){
        transaction_frequency = 1;
    }

    // Different random seed for each client
    srand(time(NULL) ^ getpid());

    // Register SIGTERM handler
    signal(SIGTERM, handler_sigterm);

    // Generate System V IPC key
    key = ftok(MSGQUEUE_PATH, MSGQUEUE_PROJ_ID);
    if(key == -1){
        log_write("ERROR: ftok failed with code %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    
    msqid = msgget(key, 0666);
    if(msqid == -1){
        log_write("ERROR: msgget failed with code %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    log_write("Client started");

    // Generate and send transactions until SIGTERM arrives
    while(!got_stop){
        TxMessage msg;
        memset(&msg, 0, sizeof(TxMessage));

        // Message type used by miners
        msg.mtype = MSG_TYPE_TRANSACTION;
        generate_transaction(msg.content);

        // Send the message without blocking
        if(msgsnd(msqid, &msg, sizeof(msg.content), IPC_NOWAIT) == -1){
            log_write("ERROR: transaction not sent");
        }
        else{
            log_write("%s", msg.content);
        }

        // Wait before generating the next transaction
        sleep(transaction_frequency);
    }

    log_write("Client shutting down");
    log_close();
    return 0;
}