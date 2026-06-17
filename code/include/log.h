#ifndef LOG_H
#define LOG_H

int log_init(const char *process_type, int id);		// init log file : "node", "client" or "miner" and "id" of the process

void log_write(const char *format, ...);              //printf wrapper - write a message in the log file with a timestamp 

void log_close(void);                              

void log_cleanup(void); 							

#endif