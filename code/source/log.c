#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include "../include/log.h"


/** TO-DO 
 * includere log.h e errors.h nei file .c dei processi
 * sostituire la creazione manuale dei file con log_init("miner", miner_id);
 * sostituire tutte le chiamate miner_log("...") con log_msg("...");
 * aggiungere log_close() prima del return 0 finale 
 */

static FILE *logfile_ptr = NULL; 
static char proc_type_global[32];
static int proc_id_global = -1;

int log_init(const char *process_type, int id){
    char filename[128];
    proc_id_global = id;
    snprintf(proc_type_global, sizeof(proc_type_global), "%s", process_type);

    // nome: process_type_ID-PID.log (es: client_0-7777.log)
    snprintf(filename, sizeof(filename), "%s_%d-%d.log", process_type, id, (int)getpid());

    logfile_ptr = fopen(filename, "w");
    if (logfile_ptr == NULL){
        perror("Errore apertura file di log globale");
        return -1;
    }

    log_write("%s avviato correttamente", process_type);
    return 0;
}

void log_write(const char *format, ...){
    if(logfile_ptr == NULL) return; 

    // scrittura prefisso con timestamp, nome processo e ID
    fprintf(logfile_ptr, "[%ld] %s ID [%d]:", (long)time(NULL), proc_type_global, proc_id_global);

    // gestione argomenti variabili tipo printf
    va_list args;
    va_start(args, format);
    vfprintf(logfile_ptr, format, args);
    va_end(args);

    fprintf(logfile_ptr, "\n");                 // va a capo
    fflush(logfile_ptr);                        // forza lo svuotamento del buffer
}

void log_close(void){
    if(logfile_ptr != NULL){
        log_write("%s chiuso", proc_type_global);
        fclose(logfile_ptr);
        logfile_ptr = NULL;
    }
}