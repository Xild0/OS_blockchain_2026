# Progetto Operating Systems - Blockchain 2026

Repository progetto di laboratorio di Sistemi Operativi: simulazione di un sistema blockchain basato su processi separati comunicanti tramite IPC.

---

## Struttura della Repository
### Build & Scripts
La struttura dei file è stata organizzata per separare chiaramente gli header dai file sorgente e dagli script:

- `build.sh`: script Bash fondamentale per la gestione del progetto. Dovrà supportare i comandi: 
    - `./build.sh build` per compilare tutti i file e produrre gli eseguibili
    - `./build.sh clean` per rimuovere tutti i file compilati, gli eseguibili e ripristinare il sistema
    - `./build.sh run` per eseguire lo scenario del progetto
- `README.md`: questo file, che spiega l'organizzazione del lavoro
- `/code/`: cartella contenente tutto il codice sorgente del progetto
  - `blockchain.sh`: Script Bash richiesto dalle specifiche che espone i comandi utility:
    - `./blockchain.sh --verify <state.csv>` verifica l'integrità del file CSV
    - `./blockchain.sh --hash <block_hex>` calcola l'hash SHA256 di un blocco fornito in stringa esadecimale
    - `./blockchain.sh --merkle <tx1::tx2::...>` calcola la Merkle root di una lista di transazioni separate da "::"
    
---
    
### Headers (`code/include/`)
- `/include/`: cartella per i file header
- `blockchain.h`: definisce la struttura del blocco della blockchain e dichiara tutte le funzioni relative alle gestione dei blocchi (conversione int_hax e viceversa, hashing SHA256, calcolo della Merkle tree e le operazioni sul file CSV. 
- `errors.h`: definisce tutti gli errori usati nei file C. Gli stessi valori numerici vengono riportati come variabili bash nel file `blockchain.h`
- `ipc.h`: dichiara l'Inter-Process Communication interface usata per lo scambio di messaggi tra processi
- `log.h`: dichiara l'interfaccia di registrazione. Ogni processo registra tutti gli eventi rilevanti in un file dedicato denominato `nome_processo-PID.log`

---

### Sources (`code/source/`)

- `/source/`: Cartella per i file sorgente C.
- `blockchain.c`: implementa tutte le funzioni dichiarate in `blockchain.h`. Contiene il `main()` del programma di bootstraping
- `client.c`: implementa il processo dei client
- `miner.c`: implementa il processo dei miner
- `node.c`: implementa il processo dei node
- `ipc.c`: implementa la comunicazione IPC
- `log.c`: implementa le funzioni dichiarate in `log.h`

---

## Regole codice esame

1. **Formato di Consegna:** Il progetto non si consegna tramite GitHub, ma dovremo creare un archivio denominato `${cognome1}_${cognome2}_${cognome3}.tar.gz` (l'ordine dei cognomi non importa).
2. **Relazione:** Insieme alla cartella `code/`, dovremo includere nell'archivio un file `report.pdf` di massimo 5 pagine contenente le nostre scelte progettuali.

---

## Guida Rapida a Git per il Gruppo

Per collaborare in modo pulito ed evitare di sovrascrivere il lavoro degli altri, seguiremo questo flusso di lavoro standard.

### 1. Scaricare il progetto la prima volta (Clone)
Se non hai ancora la cartella del progetto sul tuo computer, apri il terminale, spostati nella directory dove vuoi salvare il progetto e digita:

`git clone [https://github.com/Xild0/OS_blockchain_2026.git](https://github.com/TUO_USERNAME/OS_blockchain_2026.git)`

`cd OS_blockchain_2026`

### 2.  Aggiornare il codice locale (Pull)
Verificare di avere sempre l'ultima versione del progeto scaricando le modifiche fatte dagli altri: 

`git pull`

### 3. Salvare il proprio lavoro (Add & Commit)
Una volta finito di modificare o scrivere parti del codice, salvare tutto su github per dare acesso a tutto il gruppo: 

`git add .`

`git commit /m "Descrizione chiara, semplice e concisa delle modifiche"`

### 4. Inviare le modifiche a GitHub (Push)

`git push`
