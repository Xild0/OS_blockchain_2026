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

// Variabili globali statiche (limitate a questo file).
// Poiché ogni processo avrà il proprio spazio di memoria, non ci sono conflitti.
static FILE *logfile = NULL;
static char current_process_type[32] = "unknown";
static int current_id = -1;

int log_init(const char *process_type, int id) {
    char filename[128];
    pid_t pid = getpid();
    
    // Salva i dati per formattare le righe di log
    snprintf(current_process_type, sizeof(current_process_type), "%s", process_type);
    current_id = id;

    // Crea il nome del file come da specifiche: nome_processo-PID.log
    snprintf(filename, sizeof(filename), "%s_%d_%d.log", process_type, id, (int)pid);
    
    logfile = fopen(filename, "w");
    if (!logfile) {
        perror("Errore apertura file log");
        return -1;
    }
    return 0;
}

void log_msg(const char *format, ...) {
    if (!logfile) return; // Sicurezza: se init non è stato chiamato o ha fallito

    time_t now = time(NULL);
    
    // Stampa l'intestazione standard (es. "NODE ID: [1], timestamp: [171423456], message: ")
    // Convertiamo in maiuscolo il tipo processo per consistenza
    fprintf(logfile, "%s ID: [%d], timestamp: [%ld], message: ", 
            current_process_type, current_id, (long)now);

    // Gestione degli argomenti variabili
    va_list args;
    va_start(args, format);
    vfprintf(logfile, format, args);
    va_end(args);

    fprintf(logfile, "\n"); // Ritorno a capo
    fflush(logfile);        // Forza la scrittura su disco per non perdere i log in caso di crash
}

void log_close(void) {
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
}