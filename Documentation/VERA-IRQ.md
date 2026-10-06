# Interrupt della VERA su Atari (gancio `VIMIRQ`)

Come attivare, gestire e disattivare gli interrupt della VERA dai programmi Atari, con la
libreria `vera-tests/vera_irq.s` (interfaccia C `vera-tests/vera_irq.h`) e il test
`TESTIRQ.COM`.

---

## 1. Perché un gancio su `VIMIRQ` e non il meccanismo PBI

Il PBI prevede che un device identifichi il proprio IRQ pilotando il suo bit di `$D1FF` in lettura
(la VERA: D7) e che l'OS chiami la ROM del device tramite `PDIMSK` e il vettore `$D808`. Sulla
scheda reale questo non si può fare: il transceiver del bus dati (74LVC4245) abilita tutti e otto i
bit o nessuno, quindi non può pilotare solo D7 (dettagli in
`Vera-Module-RBL-XE/VERA-ATARI-HW-REQUIREMENTS.md` §2.4).

I registri della VERA però sono sempre leggibili. La libreria aggancia il vettore IRQ immediato
dell'OS, `VIMIRQ` (`$0216`):

```
IRQ 6502 ──► OS: JMP (VIMIRQ) ──► irq_handler di vera_irq.s
                                     │
                    ISR & IEN ≠ 0 ?  ├── sì ─► conferma la sorgente, conta, callback, RTI
                                     └── no ──► JMP (vecchio VIMIRQ)  → POKEY, PIA, ...
```

`PDIMSK` (`$0249`) resta a 0 e il vettore IRQ della ROM PBI non viene usato.

**Requisito hardware.** L'uscita IRQ della VERA è push-pull: sulla scheda serve lo stadio
open-drain (`74LVC1G07`) verso la linea IRQ dell'Atari (documento dei requisiti, §2.3). Senza di
esso non abilitare nessun IRQ della VERA sulla scheda reale. L'emulatore non ha questo vincolo.

---

## 2. Le sorgenti

| Costante | Bit di `IEN`/`ISR` | Quando scatta | Come si conferma |
|---|---|---|---|
| `VERA_IRQ_VSYNC`  | 0 | inizio del vertical blank della VERA (riga 480), **59,94 Hz** | scrittura di 1 in `ISR` (fatto dal gancio) |
| `VERA_IRQ_LINE`   | 1 | quando il raster della VERA raggiunge la riga impostata (0-511) | come sopra |
| `VERA_IRQ_SPRCOL` | 2 | collisione fra sprite nel frame (bit 7:4 di `ISR` = gruppi in collisione) | come sopra |
| `VERA_IRQ_AFLOW`  | 3 | FIFO audio PCM sotto 1/4 (meno di 1024 byte) | **non si conferma**: il gancio la maschera in `IEN` |

La VERA va a 59,94 Hz fissi, **non** sincronizzata con il VBI dell'Atari (50 Hz in PAL, 59,92 Hz
in NTSC). VSYNC e LINE servono a sincronizzarsi con lo schermo della VERA, non con quello ANTIC.

L'IRQ della VERA è **a livello**: resta attivo finché `ISR & IEN` è diverso da zero. Per questo
ogni sorgente servita va confermata (o mascherata, per AFLOW), altrimenti l'IRQ rientra
all'infinito.

---

## 3. Compilazione

`vera_irq.s` si assembla con `ca65` e si collega al programma C:

```sh
ca65 -I . -I vera-tests -o vera-tests/vera_irq.o vera-tests/vera_irq.s
cl65 -t atari --start-addr 0x5000 -I vera-tests -o PROG.COM prog.c vera-tests/vera_irq.o
```

Nel Makefile del progetto la regola è già presente (`$(VERA_IRQ_OBJ)`, usata da `TESTIRQ.COM`).
La libreria non richiede il driver `VERA.SYS` e funziona anche insieme a lui (il driver non usa gli
IRQ della VERA; il suo gestore della tastiera è chiamato dall'OS attraverso la catena).

---

## 4. Uso da C

### 4.1 Attivare

```c
#include "vera_irq.h"

vera_irq_install();                 /* aggancia VIMIRQ, nessuna sorgente ancora attiva */
vera_irq_enable(VERA_IRQ_VSYNC);    /* da qui arrivano gli IRQ VSYNC */
```

`vera_irq_enable()` cancella prima lo stato vecchio di VSYNC, LINE e SPRCOL (un IRQ "vecchio" non
scatta appena abilitato) e conserva le sorgenti già attive. `vera_irq_install()` si può chiamare più
volte senza effetti.

### 4.2 Gestire

**Attendere il VSYNC della VERA:**

```c
static void wait_vera_vsync(void)
{
    vera_irq_take();                              /* scarta eventi precedenti */
    while (!(vera_irq_take() & VERA_IRQ_VSYNC))
        ;
}
```

`vera_irq_take()` restituisce le sorgenti servite dall'ultima chiamata e le azzera in modo
atomico. In alternativa si possono leggere i contatori per sorgente:

```c
unsigned char frames = vera_irq_count[0];         /* VSYNC; [1] LINE, [2] SPRCOL, [3] AFLOW */
```

I contatori sono a 8 bit e ripartono da 0 dopo 255: usare sempre differenze
(`(unsigned char)(vera_irq_count[0] - prima)`).

**IRQ di riga (raster):**

```c
vera_irq_set_line(240);             /* 0-511; il bit 8 va in IEN bit 7, gestito dalla libreria */
vera_irq_enable(VERA_IRQ_LINE);
```

**Audio PCM (AFLOW):** con la FIFO sotto 1/4 l'IRQ scatta, il gancio lo serve una volta e lo
**maschera** in `IEN` (non si può confermare). Il programma vede `VERA_IRQ_AFLOW` in
`vera_irq_take()`, riempie la FIFO (`AUDIO_DATA`, `$D11D`) e poi riattiva:

```c
if (vera_irq_take() & VERA_IRQ_AFLOW) {
    refill_pcm_fifo();                  /* scrive i campioni in $D11D */
    vera_irq_enable(VERA_IRQ_AFLOW);    /* riarma */
}
```

Dopo il reset la FIFO è vuota, quindi AFLOW è attivo subito: abilitarlo senza avere dati da
scrivere produce un solo IRQ e poi la maschera.

### 4.3 Disattivare

```c
vera_irq_disable(VERA_IRQ_LINE);    /* spegne solo alcune sorgenti */
vera_irq_remove();                  /* spegne tutto e ripristina VIMIRQ */
```

`vera_irq_remove()` ripristina il vettore solo se nessun altro programma ha agganciato `VIMIRQ` dopo
la libreria; in quel caso il gancio resta installato ma inerte (`IEN` a 0) e continua a passare gli
IRQ al gestore precedente. Un programma che termina deve sempre chiamare `vera_irq_remove()`: il
codice del gancio sta nella memoria del programma e verrebbe sovrascritto.

---

## 5. Callback in assembly

Per reagire subito all'interrupt (per esempio cambiare un registro a una certa riga) si può
registrare una routine assembly, eseguita dentro l'IRQ dopo la conferma:

```c
extern void my_irq_cb(void);        /* definita in assembly, vedi sotto */
vera_irq_set_callback(my_irq_cb);
...
vera_irq_set_callback(0);           /* toglie la callback */
```

Contratto della callback:
- riceve in **A** le sorgenti appena servite (`VERA_IRQ_*`);
- termina con `RTS`; X e Y sono salvati dal gancio, A no;
- gira con gli interrupt disabilitati: deve essere breve;
- **non** chiamare codice C compilato da cc65 (usa lo stack software e i registri in pagina zero del
  programma principale);
- se tocca registri multiplexati da `CTRL` (DCSEL/ADDRSEL), deve salvare e ripristinare `CTRL`;
  **non** deve usare `ADDR_*`/`DATA0`/`DATA1`, perché il programma principale potrebbe essere a metà
  di un accesso alla VRAM e il prefetch non si può ripristinare.

Esempio: cambiare il colore del bordo della VERA all'IRQ di riga e ripristinarlo al VSYNC.

```asm
        .include "vera_common.inc"
        .export _my_irq_cb

_my_irq_cb:
        tax                         ; X = sorgenti (X è salvato dal gancio)
        lda VERA_CTRL               ; salva ADDRSEL/DCSEL del programma principale
        pha
        lda #$00                    ; DCSEL = 0 (bit 7 = 0: mai riconfigurare l'FPGA)
        sta VERA_CTRL
        txa
        and #$02                    ; VERA_IRQ_LINE ?
        beq @vsync
        lda #$02                    ; bordo rosso sotto la riga
        sta VERA_DC_BORDER
        jmp @out
@vsync: lda #$06                    ; bordo blu sopra la riga
        sta VERA_DC_BORDER
@out:   pla
        sta VERA_CTRL               ; ripristina CTRL
        rts
```

---

## 6. Uso da assembly (senza C)

Le funzioni seguono la convenzione `__fastcall__` di cc65 e si possono chiamare direttamente:

| Chiamata | Ingresso | Uscita |
|---|---|---|
| `jsr _vera_irq_install` | — | — |
| `lda #mask` / `jsr _vera_irq_enable` | A = maschera | — |
| `lda #mask` / `jsr _vera_irq_disable` | A = maschera | — |
| `jsr _vera_irq_take` | — | A = sorgenti servite, X = 0 |
| `lda #<riga` / `ldx #>riga` / `jsr _vera_irq_set_line` | A/X = riga | — |
| `lda #<cb` / `ldx #>cb` / `jsr _vera_irq_set_callback` | A/X = indirizzo (0 = nessuna) | — |
| `jsr _vera_irq_remove` | — | — |

Contatori: `_vera_irq_count` (4 byte).

---

## 7. Regole e avvertenze

1. **`IEN` in sezione critica.** Il gancio modifica `IEN` dentro l'IRQ (per mascherare AFLOW). Il
   programma non deve scrivere `IEN` direttamente con sequenze leggi-modifica-scrivi: usare
   `vera_irq_enable()`/`vera_irq_disable()`/`vera_irq_set_line()`, che lavorano con `SEI` e `CRITIC`.
2. **Non scrivere `$80` in `CTRL`** (`$D105`): sulla VERA reale riconfigura l'intero FPGA.
3. **Ordine dei ganci.** Se un altro programma aggancia `VIMIRQ` dopo questa libreria, va rimosso
   prima lui; altrimenti `vera_irq_remove()` lascia il gancio inerte in memoria (§4.3).
4. **RESET.** Con il tasto RESET l'OS ripristina i propri vettori, compreso `VIMIRQ`, ma **la VERA
   non viene resettata** (non ha un pin di reset) e mantiene `IEN`. L'handler PBI azzera `IEN`
   nell'`INIT` chiamato dall'OS, così l'IRQ della VERA non resta attivo senza gestore. Il
   comportamento al warm start va verificato sulla scheda reale.
5. **Latenza.** Fra l'evento sulla VERA e la callback passano il tempo di risposta della 6502, il
   gestore IRQ dell'OS fino a `JMP (VIMIRQ)` e la conferma: qualche decina di µs, variabile con il
   DMA di ANTIC. Non adatto a effetti precisi al pixel.

---

## 8. Test: `TESTIRQ.COM`

Sul disco `disk3-standalone.atr` (test che non richiedono `VERA.SYS`). Stampa solo i controlli
falliti e il riepilogo.

| Gruppo | Cosa verifica |
|---|---|
| [1] | il gancio è installato su `VIMIRQ`, nessuna sorgente attiva |
| [2] VSYNC | circa 59,94 IRQ al secondo misurati con `RTCLOK` (tolleranza 5 %), flag e disattivazione |
| [3] LINE | un IRQ per frame della VERA alla riga 240 |
| [4] AFLOW | servito una sola volta e poi mascherato in `IEN`; il sistema non si blocca |
| [5] catena | `RTCLOK` continua ad avanzare: l'OS riceve i suoi IRQ |
| [6] rimozione | sorgenti spente e `VIMIRQ` ripristinato |

Risultato in emulatore (`atari800 -xl -verax16 ... -run TESTIRQ.COM`): **PASS 13, FAIL 0**, sia in
NTSC sia in PAL (in PAL arrivano circa 144 IRQ VSYNC in 120 frame Atari). Sulla scheda reale va
provato dopo il montaggio dello stadio open-drain.
