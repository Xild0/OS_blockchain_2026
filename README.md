# Progetto Operating Systems - Blockchain 2026

Repository progetto di laboratorio di Sistemi Operativi: simulazione di un sistema blockchain basato su processi separati comunicanti tramite IPC[cite: 1].

---

## Struttura della Repository

La struttura dei file è stata organizzata per separare chiaramente gli header dai file sorgente e dagli script:

- `build.sh`: Lo script Bash fondamentale per la gestione del progetto. Dovrà supportare i comandi `./build.sh build` (per compilare), `./build.sh clean` (per pulire i file oggetto e ripristinare il sistema) e `./build.sh run` (per eseguire lo scenario)[cite: 1].
- `README.md`: Questo file, che spiega l'organizzazione del lavoro.
- `/code/`: Cartella contenente tutto il codice sorgente del progetto, rigorosamente in C e Bash[cite: 1].
  - `blockchain.sh`: Script Bash richiesto dalle specifiche che espone i comandi utility come `-verify`, `-hash` e `-merkle`[cite: 1].
  - `/include/`: Cartella per i file header.
    - `blockchain.h`: Conterrà le definizioni della struttura dei blocchi, le costanti e i codici d'errore condivisi.
  - `/source/`: Cartella per i file sorgente C.
    - `blockchain.c`: Il programma di bootstrapping che avvierà i processi e fornirà la CLI[cite: 1].
    - `node.c`: La logica dei Nodi (mantenimento e validazione della blockchain)[cite: 1].
    - `miner.c`: La logica dei Minatori (creazione di nuovi blocchi e Proof of Work simulata)[cite: 1].
    - `client.c`: La logica dei Client (generazione e invio di transazioni)[cite: 1].

---

## Regole codice esame

1. **Linguaggi:** Il codice deve essere scritto *esclusivamente* in C e Bash[cite: 1]. Soluzioni in altri linguaggi (come C++) non saranno accettate[cite: 1].
2. **Plagio:** La repository è privata per un motivo. Il codice verrà verificato con software anti-plagio[cite: 1].
3. **Appelli:** Possiamo consegnare il progetto a Giugno (24 Giugno, preferenziale per il momento), Luglio, Settembre (2026), oppure a Gennaio o Febbraio (2027)[cite: 1].
4. **Formato di Consegna:** Il progetto non si consegna tramite GitHub, ma dovremo creare un archivio denominato `${cognome1}_${cognome2}_${cognome3}.tar.gz` (l'ordine dei cognomi non importa)[cite: 1].
5. **Relazione:** Insieme alla cartella `code/`, dovremo includere nell'archivio un file `report.pdf` di massimo 5 pagine contenente le nostre scelte progettuali[cite: 1].

---

## Guida Rapida a Git per il Gruppo

Per collaborare in modo pulito ed evitare di sovrascrivere il lavoro degli altri, seguiremo questo flusso di lavoro standard.

### 1. Scaricare il progetto la prima volta (Clone)
Se non hai ancora la cartella del progetto sul tuo computer, apri il terminale, spostati nella directory dove vuoi salvare il progetto e digita:

`git clone [https://github.com/Xild0/OS_blockchain_2026.git](https://github.com/TUO_USERNAME/OS_blockchain_2026.git)`

`cd OS_blockchain_2026`

## 2.  Aggiornare il codice locale (Pull)
Verificare di avere sempre l'ultima versione del progeto scaricando le modifiche fatte dagli altri: 

`git pull`

## 3. Salvare il proprio lavoro (Add & Commit)
Una volta finito di modificare o scrivere parti del codice, salvare tutto su github per dare acesso a tutto il gruppo: 

`git add .`

`git commit /m "Descrizione chiara, semplice e concisa delle modifiche"`

## 4. Inviare le modifiche a GitHub (Push)

`git push`
