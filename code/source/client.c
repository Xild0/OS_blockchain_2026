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

// Client identifier and logfile pointer
static int client_id = -1;
static FILE *logfile = NULL;

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

// Writes a message inside the logfile
static void client_log(const char *msg){
    if(logfile == NULL){
        return;
    }

    fprintf(logfile,
            "CLIENT ID: [%d], timestamp: [%ld], message: %s\n",
            client_id,
            (long)time(NULL),
            msg);

    fflush(logfile);
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

    char logname[64];
    key_t key;
    int msqid;

    client_id = id;

    // Create logfile name with client id and PID
    snprintf(logname,
             sizeof(logname),
             "client_%d_%d.log",
             client_id,
             (int)getpid());

    logfile = fopen(logname, "w");

    if(logfile == NULL){
        perror("fopen");
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
        client_log("ERROR: ftok failed");
        fclose(logfile);
        return 1;
    }

    // Connect to the existing message queue
    msqid = msgget(key, 0666);

    if(msqid == -1){
        client_log("ERROR: msgget failed");
        fclose(logfile);
        return 1;
    }

    client_log("Client started");

    // Generate and send transactions until SIGTERM arrives
    while(!got_stop){

        TxMessage msg;

        // Message type used by miners
        msg.mtype = MSG_TYPE_TRANSACTION;

        // Fill the message with a valid transaction
        generate_transaction(msg.content);

        // Send the message without blocking
        if(msgsnd(msqid,
                  &msg,
                  sizeof(msg.content),
                  IPC_NOWAIT) == -1){

            client_log("ERROR: transaction not sent");
        }
        else{
            client_log(msg.content);
        }

        // Wait before generating the next transaction
        sleep(transaction_frequency);
    }

    client_log("Client shutting down");

    fclose(logfile);

<<<<<<< HEAD
    return 0;
}
=======
/*
int main(){
    return client_main(0, 1);
}*/
>>>>>>> 6f194f2 (main added)
