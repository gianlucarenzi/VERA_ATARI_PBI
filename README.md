# VERA X16 for Atari (PBI Implementation)

Questo progetto porta la potenza del sottosistema video **VERA (Video Enhanced Retro Adapter)** — celebre per il suo uso nel computer Commander X16 — sulla famiglia di computer **Atari 8-bit** (400/800, XL, XE) tramite l'interfaccia **PBI (Parallel Bus Interface)**.

## Cos'è la scheda video VERA X16?
La VERA è un chip video moderno progettato per il retro-computing che offre:
*   **Risoluzione nativa VGA:** Output 640x480 @ 60Hz.
*   **Memoria Video Dedicata:** 128 KB di VRAM indipendente dalla RAM principale dell'Atari.
*   **Multiple Layer:** Supporto per tilemap e sprite ad alta risoluzione e profondità di colore (fino a 256 colori).
*   **Coprocessore FX:** Un modulo hardware dedicato per accelerare operazioni grafiche come scrolling, riempimenti di poligoni, trasformazioni affini e calcoli matematici a 16-bit.

## VERA in versione ATARI
A differenza delle schede video originali dell'Atari (ANTIC/GTIA), la VERA viene collegata tramite il bus **PBI** o lo slot **ECI**. Questo significa:
*   **Bypass dei limiti originali:** Non è soggetta alle limitazioni di memoria o di DMA dell'Atari originale.
*   **Integrazione Trasparente:** Viene vista dal sistema operativo come una periferica intelligente (Device ID $80+).
*   **Dual Display:** Può coesistere con l'uscita video originale dell'Atari, permettendo configurazioni a doppio monitor o l'uso della VERA come display primario ad alta risoluzione.

## Driver Open Source e Integrazione OS
Il software di supporto è completamente open-source e si divide in due componenti critici:

### 1. PBI Handler (ROM)
Un firmware da 2 KB caricato nella ROM della periferica che:
*   Inizializza l'hardware VERA al momento dell'accensione (Cold Start).
*   Configura una modalità testo di base per permettere il boot del sistema.
*   Gestisce il rilevamento della scheda da parte del sistema operativo Atari.

### 2. Driver di Sistema (VERA.SYS / RAM)
Un driver rilocabile che si installa nella RAM alta (RAMTOP) e "prende il controllo" del sistema:
*   **CIO Hooks:** Sostituisce gli handler dei dispositivi `E:` (Editor) e `S:` (Screen). Comandi come `PRINT`, `LIST` o `GRAPHICS 0` vengono rediretti automaticamente alla VERA.
*   **VBI Hooks:** Utilizza il Vertical Blank Interrupt per gestire il lampeggio del cursore e i task di sistema in modo fluido e non bloccante.
*   **Hardware Acceleration:** Sfrutta il modulo FX per uno scrolling e una pulizia dello schermo 4 volte più veloci rispetto ai metodi software tradizionali, minimizzando il traffico sul bus PBI.

## Strumenti di Test e Diagnostica
Il progetto include una suite di test avanzata per validare l'implementazione hardware e misurare le prestazioni:
*   **`TESTFX.COM`:** Un software diagnostico che verifica ogni registro del coprocessore FX e include benchmark per misurare il throughput reale (es. Copy e Fill della VRAM).
*   **`RUNCPM.COM`:** Un terminale ANSI completo che permette di usare l'Atari con la scheda VERA per accedere a sistemi CP/M remoti via FujiNet.
*   **Test Funzionali:** Programmi per verificare il caricamento dei font, il rendering di gradienti e la stabilità dello scrolling hardware.

---
*Questo progetto è un ponte tra l'era d'oro degli 8-bit Atari e le possibilità offerte dall'hardware moderno, espandendo i confini di ciò che queste macchine leggendarie possono visualizzare.*
