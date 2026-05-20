#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "../include/blockchain.h"
#include "../include/sha256.h"
#include "../include/errors.h"



void int_to_hex(uint64_t value, char *hex){
	snprintf(hex, 17, "%016lx", value); 							//%016 aggiunge zeri iniziali per avere sempre 16 caratteri
}


uint64_t hex_to_int(const char *hex){
	return (uint64_t)strtoull(hex, NULL, 16);
}

void calcola_merkle_root(char transactions[MAX_TX_LEN], char *merkle_root){

    uint16_t count = 0;                                                          // conta quante stringhe ci sono nell'array
    uint16_t tx_count = 0;                                                       // contatore per selezionare il puntatore alle stringhe
    char *ptr = transactions;                                                   // puntatore utile per scorrere la stringa concatenate
    char *start = ptr;                                                          // punta all'inizio della stringa concatenata
    
    while(*ptr != '\0'){                                                        // scorro per tutta la stringa concatenata
        if(ptr[0] == ':' && ptr[1] == ':'){                                     // quando trovo '::' incremento il contatore di stringhe
            count++;
        }
        ptr++;
    }
    //printf("Contatore stringhe: %i\n", count);                                  // stampo il contatore di stringhe
    uint16_t c_count = count;

    if(count == 0){
        sha256_hex("", 0, merkle_root);
        return;
    }

    char **array_transactions = malloc(count * sizeof(char*));                                            // creo un array di puntatori a stringhe
    ptr = transactions;                                                         // resetto il puntatore per scorrere

    while(*ptr != '\0'){
        if(ptr[0] == ':' && ptr[1] == ':'){                                     // quando trovo '::'
            uint64_t diff = ptr - start;                                        // calcolo la differenza dei puntatori
            array_transactions[tx_count] = malloc(diff + 1);                    // creo nello heap una stringa della dimensione corretta
            strncpy(array_transactions[tx_count], start, diff);                 // copio la parte della stringa dentro la stringa puntata dall'array
            array_transactions[tx_count][diff] = '\0';                          // aggiungo il carattere di terminazione
            tx_count++;                                                         
            ptr += 2;                                                           // incremento e resetto i counter/puntatori, ptr lo alzo di due per i due punti ('::') da saltare
            start = ptr; 
        }
        else{
            ptr++;
        }
    }

    /*
    for (int i = 0; i < count; ++i){                                            // stampo le stringhe separatamente
        printf("Stringa %i: %s\n", i+1, array_transactions[i]);
    }*/
    

    char **array_transactions_sha256 = malloc(count * sizeof(char*));                               // creo un array di puntatori lungo count che puntano a stringhe lunghe 65 caratteri    

    for (int i = 0; i < count; ++i)
    {
        array_transactions_sha256[i] = malloc(SHA256_BUFFER_LEN);               
        sha256_hex(array_transactions[i], strlen(array_transactions[i]), array_transactions_sha256[i]);        // eseguo hashing sha256
        array_transactions_sha256[i][SHA256_HEX_LEN] = '\0';                                                   // aggiungo terminatore di stringa
       // printf("Stampa hash %i: %s\n\n", i+1, array_transactions_sha256[i]);
    }

    for (int i = 0; i < count; ++i)
    {
        free(array_transactions[i]);
    }
    free(array_transactions);
 
    /*
    printf("%s\n", *array_transactions_sha256[1]);
    printf("489cdba1288d6741c7b929eacbc97b43a60039d6a21fc08a9d641716ba851778\n");
    printf("%s\n", *array_transactions_sha256[2]);
    printf("753e2988a3bf4fab50896d6dd8090bad50bf0eba46dbef380f1e7b20764b4689\n");
    */


    if (count == 1){
        char tmp[CONC_BUFFER_LEN];
        char tmp_sha256[SHA256_BUFFER_LEN];
        strcpy(tmp, array_transactions_sha256[0]);
        strcat(tmp, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\0");
        sha256_hex(tmp, strlen(tmp), tmp_sha256);
        //printf("%li\n", strlen(tmp));
        tmp_sha256[SHA256_HEX_LEN]= '\0';
        strcpy(merkle_root, tmp_sha256);
        free(array_transactions_sha256[0]);
        free(array_transactions_sha256);
        return;
    }

    while(count > 1){

        uint8_t new_count = 0;
        char **stringhe_concatenate = malloc(count * sizeof(char*));
        char **stringhe_concatenate_sha256 = malloc((((count+1)/2)) * sizeof(char*));

        for (int i = 0; i < count-1; i+=2)
        {
            stringhe_concatenate[new_count] = malloc(CONC_BUFFER_LEN);
            stringhe_concatenate_sha256[new_count] = malloc(SHA256_BUFFER_LEN);
            strcpy(stringhe_concatenate[new_count], array_transactions_sha256[i]);
            //printf("Prima meta' stringa concatenata: %s\n", stringhe_concatenate[new_count]);
            strcat(stringhe_concatenate[new_count], array_transactions_sha256[i+1]);
            //printf("Seconda meta' stringa concatenata: %s\n\n", stringhe_concatenate[new_count]);
            stringhe_concatenate[new_count][CONC_LEN] = '\0';
            sha256_hex(stringhe_concatenate[new_count], strlen(stringhe_concatenate[new_count]), stringhe_concatenate_sha256[new_count]);
            stringhe_concatenate_sha256[new_count][SHA256_HEX_LEN] = '\0';
            ++new_count;
        }

        if((count%2) != 0){
            stringhe_concatenate[new_count] = malloc(CONC_BUFFER_LEN);
            stringhe_concatenate_sha256[new_count] = malloc(SHA256_BUFFER_LEN);
            strcpy(stringhe_concatenate[new_count], array_transactions_sha256[count-1]);
            //printf("Prima meta' stringa concatenata: %s\n", stringhe_concatenate[new_count]);
            char tmp[SHA256_BUFFER_LEN];
            sha256_hex("", 0, tmp);
            tmp[SHA256_HEX_LEN] = '\0';
            strcat(stringhe_concatenate[new_count], tmp);
            //printf("Seconda meta' stringa concatenata: %s\n\n", stringhe_concatenate[new_count]);
            stringhe_concatenate[new_count][CONC_LEN]= '\0';
            sha256_hex(stringhe_concatenate[new_count], strlen(stringhe_concatenate[new_count]), stringhe_concatenate_sha256[new_count]);
            stringhe_concatenate_sha256[new_count][SHA256_HEX_LEN] = '\0';
            ++new_count;
        }

        if((count%2) != 0){
            count = (count +1)/2;
        }else{
            count = count/2;
        }

        for (int i = 0; i < new_count; ++i)
        {
            strcpy(array_transactions_sha256[i], stringhe_concatenate_sha256[i]);
            free(stringhe_concatenate[i]);
            free(stringhe_concatenate_sha256[i]);
        }
        free(stringhe_concatenate);
        free(stringhe_concatenate_sha256);

        /*
        printf("\n\n");
        printf("Stringa concatenata totale: %s\n", stringhe_concatenate[0]);
        printf("Stringa concatenata hash: %s\n\n", array_transactions_sha256[0]);
        */
    }
    //printf("merkle_root: %s", array_transactions_sha256[0]);
    strcpy(merkle_root, array_transactions_sha256[0]);

    for (int i = 0; i < c_count; ++i)
    {
        free(array_transactions_sha256[i]);
    }
    free(array_transactions_sha256);

    return;
}

// legge una riga dal fd, restituisce lunghezza o -1 a EndOfLine 
static int read_line(int fd, char *buf, int maxlen) { //filedescriptor, buffer , lunghezza max buffer
    int buffer_index = 0; 
    char single_char;
    while (buffer_index < maxlen - 1) { //legge finchè il buffer non è pieno
        size_t n = read(fd, &single_char, 1); //legge 1 byte alla volta (file, char letto, numero bytes)

        if (n <= 0) { //gestione errori 
            if (buffer_index == 0) {
                return -1;  //file finito 
            } else {
                return buffer_index;   // file end senza /n
            }
        }
        if (single_char == '\n') { //fine riga
            break;
        }

        buf[buffer_index] = single_char; //salva sul buffer il char letto
        buffer_index++;
    }

    buf[buffer_index] = '\0'; //null terminator 
    return buffer_index;
}

// converte una riga csv in un blocco, restituisce BC_OK o codice errore
static int line_to_block(char *line, Block *b) { //arg: riga csv, puntatore al blocco da riempire
    char idx[17], ts[17], nonce[17]; //buffer temporanei per campi numerici (16 + null)

    //separa i campi della riga csv usando un puntatore alla virgola
    char *fields[6];
    char *ptr = line;
    for (int i = 0; i < 5; i++) {
        fields[i] = ptr;
        ptr = strchr(ptr, ','); //ptr alla virgola
        if (!ptr){ return -1;}
        *ptr = '\0';
        ptr++;
    }
    fields[5] = ptr; //utilimo campo

    //conversione
	b->index     = hex_to_int(fields[0]);
    b->timestamp = hex_to_int(fields[1]);
    b->nonce     = hex_to_int(fields[4]);

    strncpy(b->prev_hash,   fields[2], HASH_LENGTH); //no conversioni , stringhe salvate nel buffer
    b->prev_hash[HASH_LENGTH];
	strncpy(b->merkle_root, fields[3], HASH_LENGTH);
	b->merkle_root[HASH_LENGTH] = '\0';


    //transazioni: rimuove le virgolette se presenti
    char *tx = fields[5];
    if (tx[0] == '"') tx++;
    size_t len = strlen(tx);
    if (len > 0 && tx[len-1] == '"') tx[len-1] = '\0';

    // Copia i dati della transazione
    strncpy(b->transactions, tx, MAX_TX_LEN - 1);
    b->transactions[MAX_TX_LEN - 1] = '\0'; // Sicurezza
    
    // Imposta la lunghezza della stringa salvata
    b->transactions_len = (uint32_t)strlen(b->transactions);

    return BC_OK;

}

int blockchain_load(Blockchain *bc, const char *filename) { //arg: puntatore struct blockchain, file csv
    if (!bc || !filename) return -1;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) return BC_ERR_FILE_OPEN;

    char line[LINE_MAX_LEN];

    //salta intestazione
    if (read_line(fd, line, sizeof(line)) < 0) {
        close(fd);
        return BC_ERR_FILE_READ;
    }

    bc->length = 0; //init contatore blocchi
    while (read_line(fd, line, sizeof(line)) >= 0) { //fichè il file non finisce
        if (bc->length >= MAX_BLOCKS) { //finchè num blocchi < max
            close(fd);
            return BC_ERR_FULL;
        }

        int rc = line_to_block(line, &bc->blocks[bc->length]); 
        if (rc != BC_OK) {
            close(fd);
            return rc;
        }
        bc->length++;
    }

    close(fd);
    return BC_OK;
}


int blockchain_save(const Blockchain *bc, const char *filename) { // lettura no modifica (blockchain + file)
    if (!bc || !filename) return BC_ERR_NULL_ARG; //verifico blockchain e nome file esistenti

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); //solo scrittura + se non esiste crea altrimenti svuota
    if (fd < 0) return BC_ERR_FILE_OPEN;

    //
    if (write(fd, CSV_HEADER, strlen(CSV_HEADER)) < 0) { // scrive sul file, distinguendo per parametri e per byte
        close(fd); //prima del return , altrimenti il file rimane aperto per sempre
        return BC_ERR_FILE_WRITE;
    }

    for (int i = 0; i < bc->length; i++) { //
        char idx[17], ts[17], nonce[17]; //array per conversione
        char line[LINE_MAX_LEN]; //buffer per ricostruire la linea CSV completa

        int_to_hex(bc->blocks[i].index,     idx);
        int_to_hex(bc->blocks[i].timestamp, ts);
        int_to_hex(bc->blocks[i].nonce,    nonce);

        int len = snprintf(line, sizeof(line), //costruzione riga csv
        "%s,%s,%s,%s,%s,\"%s\"\n", idx, ts, bc->blocks[i].prev_hash, bc->blocks[i].merkle_root, nonce, bc->blocks[i].transactions);

        if (write(fd, line, len) < 0) { //scrive nel buffer line la linea len
            close(fd);
            return BC_ERR_FILE_WRITE;
        }
    }

    close(fd);
    return BC_OK;
}


int blockchain_append(const Block *b, const char *filename) { //block solo letto (const) 
    if (!b || !filename) return BC_ERR_NULL_ARG;

    // controlla se il file esiste già 
    int new_file = (access(filename, F_OK) != 0); //se non esiste crea un file (newfile=1)

    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644); //flag uguali a prima + APPEND(va in fondo)
    if (fd < 0) return BC_ERR_FILE_OPEN;

    if (new_file) {
        if (write(fd, CSV_HEADER, strlen(CSV_HEADER)) < 0) { //scrivo intestazione csv se file nuovo
            close(fd);
            return BC_ERR_FILE_WRITE;
        }
    }

    char idx[17], ts[17], nonce[17];
    char line[LINE_MAX_LEN];

    int_to_hex(b->index,     idx);
    int_to_hex(b->timestamp, ts);
    int_to_hex(b->nonce,     nonce);

    int len = snprintf(line, sizeof(line),
        "%s,%s,%s,%s,%s,\"%s\"\n", idx, ts, b->prev_hash, b->merkle_root, nonce, b->transactions);

    if (write(fd, line, len) < 0) {
        close(fd);
        return BC_ERR_FILE_WRITE;
    }

    close(fd);
    return BC_OK;
}
