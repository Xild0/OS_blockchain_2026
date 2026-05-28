#ifndef LOG_H
#define LOG_H

int log_init(const char *process_type, int id);		// inizializza file di log: "node", "client" o "miner" e "id" del processo

void log_write(const char *format, ...);              // si usa come printf, scrive un messaggio nel file di log includendo il timestamp

void log_close(void);                               // chiude il file di log

void log_cleanup(void); 							// pulisce tutti i log della scorsa run

#endif