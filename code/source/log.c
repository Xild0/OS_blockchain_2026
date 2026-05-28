#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>  
#include <dirent.h>
#include "../include/log.h"

static FILE *logfile_ptr = NULL; 
static char proc_type_global[32];
static int proc_id_global = -1;

int log_init(const char *process_type, int id){
    proc_id_global = id;
    snprintf(proc_type_global, sizeof(proc_type_global), "%s", process_type);

    // creazione cartella "logs" se non esiste. 0777 indica i permessi POSIX standard
    mkdir("logs", 0777);                        

    // meglio esagerare sulla dimensione per evitare troncamenti
    char filename[256];         

    // nome: process_type_ID-PID.log (es: client_0-7777.log)
    snprintf(filename, sizeof(filename), "logs/%s_%d-%d.log", process_type, id, (int)getpid());

    // apertura dei file di log
    logfile_ptr = fopen(filename, "w");
    if (logfile_ptr == NULL){
        perror("Errore apertura file di log globale");
        return -1;
    }

    log_write("%s avviato correttamente", process_type);
    return 0;
}

void log_write(const char *format, ...){
    // se init fallito o non chiamato
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

void log_cleanup(void){
    DIR *dir = opendir("logs");
    if (dir == NULL) return;

    struct dirent *entry;
    char filepath[512];

    while ((entry = readdir(dir)) != NULL){
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, ".") == 0){
            continue;
        }
        snprintf(filepath, sizeof(filepath), "logs/%s", entry->d_name);
        remove(filepath);  
    }
    closedir(dir);
}