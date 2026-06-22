#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include "../include/log.h"

static FILE *logfile_ptr = NULL; 
static char proc_type[32];
static int proc_id = -1;

int log_init(const char *process_type, int id){
    proc_id = id;
    snprintf(proc_type, sizeof(proc_type), "%s", process_type);

    if (mkdir("logs", 0777) == -1 && errno != EEXIST){
        perror("Error creating logs directory");
        return -1;
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "logs/%s-%d.log", process_type, (int)getpid());

    logfile_ptr = fopen(filename, "w");
    if (logfile_ptr == NULL){
        perror("Error opening log file");
        return -1;
    }

    log_write("%s started correctly", process_type);
    return 0;
}

void log_write(const char *format, ...){
    if(logfile_ptr == NULL) return;

    fprintf(logfile_ptr, "[%ld] %s [%d]:", (long)time(NULL), proc_type, proc_id);

    va_list args;
    va_start(args, format);
    vfprintf(logfile_ptr, format, args);
    va_end(args);

    fprintf(logfile_ptr, "\n"); 
    fflush(logfile_ptr); 
}

void log_close(void){
    if(logfile_ptr != NULL){
        log_write("%s closed", proc_type);
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
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        snprintf(filepath, sizeof(filepath), "logs/%s", entry->d_name);
        remove(filepath); 
    }

    closedir(dir);
}