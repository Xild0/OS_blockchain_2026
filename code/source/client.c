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

// Names used to generate random transactions
static const char *names[NUM_NAMES] = {
    "Alice",
    "Bob",
    "Giovanna",
    "Ilaria",
    "Francesco",
    "Giacomo"
};
    
// SIGTERM handler
// The client does not stop immediately: it only sets the flag and exits the loop safely
static void handler_sigterm(int sig){
    (void)sig;
    got_stop = 1;
}

// Generates a random transaction that respects the regex: "Alice pays Bob 10 coins"
static void generate_transaction(char *transaction){
    int sender;
    int receiver;
    int amount;

    // Random sender
    sender = rand() % NUM_NAMES;

    // Random receiver different from sender
    do{
        receiver = rand() % NUM_NAMES;
    }while(receiver == sender);

    // Random amount between 1 and 99
    amount = (rand() % 99) + 1;

    // Builds the final transaction string
    snprintf(transaction,
             MAX_TX_LEN,
             "%s pays %s %d coins",
             names[sender],
             names[receiver],
             amount);
}

// Main function of the client process
int client_main(int id, int transaction_frequency){

    key_t key;
    int msqid;

    // Inizializza sistema di log
    if(log_init("client", id) != 0){
        return 1;
    }

    // Protect against invalid frequencies
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
        log_write("ERROR: ftok failed con codice %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    // Connect to the existing message queue
    msqid = msgget(key, 0666);

    if(msqid == -1){
        log_write("ERROR: msgget failed con codice %s", error_to_string(BC_ERR_FILE_OPEN));
        return 1;
    }

    log_write("Client started");

    // Generate and send transactions until SIGTERM arrives
    while(!got_stop){

        TxMessage msg;
        memset(&msg, 0, sizeof(TxMessage));                     // azzero tutto l'array

        // Message type used by miners
        msg.mtype = MSG_TYPE_TRANSACTION;

        // Fill the message with a valid transaction
        generate_transaction(msg.content);

        // Send the message without blocking
        if(msgsnd(msqid,
                  &msg,
                  sizeof(msg.content),
                  IPC_NOWAIT) == -1){

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


