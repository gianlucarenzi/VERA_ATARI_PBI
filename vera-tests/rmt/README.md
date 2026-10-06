# RMT player su POKEY + VERA PSG

Suona i moduli RMT (Raster Music Tracker) sul POKEY, sul PSG della VERA, o su entrambi insieme:

- **RMT4** (mono, 4 canali): voci PSG 0-3;
- **RMT8** (stereo, pensati per due POKEY): voci PSG 0-7. I canali 1-4 suonano anche sul POKEY
  reale; i canali 5-8, che su un Atari con un solo POKEY andrebbero persi, suonano sulla VERA.

La VERA ha 16 voci PSG, ma il formato RMT non va oltre gli 8 canali: le voci 8-15 restano libere.

## Come funziona

- `rmtplayr.s` è il player Atari di Raster nel port ca65 di `AT2019/ATARI-Driver/RmtSkeleton`
  (stesso sorgente del player per C64 `rmt_cbm64`). Con `-D RMT_VERA` continua a scrivere il POKEY,
  ma esporta lo stato dei canali (`trackn_audf`, `trackn_audc`, `v_audctl`) e può tenere il POKEY
  muto (`rmt_pokey_mute`). Con `-D RMT_TRACKS=8` usa la modalità stereo dell'originale
  (`STEREOMODE 1`: L1 L2 L3 L4 R1 R2 R3 R4). Il POKEY di destra **non viene mai scritto**: su un
  Atari standard `$D210` è un'immagine di `$D200`; i canali 5-8 esistono solo in
  `trackn_audf/audc+4` e `v_audctl2`.
- `rmtvbi.s` (da `RmtSkeleton`) fa girare il player nel VBI; con `RMT_VERA` chiama `psg_update`
  dopo ogni passo del player.
  - Normalmente nel **VBI differito** (`VVBLKD`), cioè dopo lo stadio 2 dell'OS che copia i registri
    ombra, compreso il puntatore della display list. Nel VBI immediato il player (30-60 righe con
    POKEY + VERA) ritardava lo stadio 2: in NTSC il vertical blank è di sole ~20 righe, `DLISTL/H`
    veniva riscritto mentre ANTIC stava già disegnando e l'immagine "ballava".
  - Durante l'I/O da disco (SIO mette `CRITIC` ≠ 0, l'OS salta stadio 2 e VBI differito) nel
    **VBI immediato**, così la musica continua anche durante il caricamento.
  - Se il VBI interrompe un gestore IRQ il passo viene rimandato alla fine del successivo IRQ
    seriale o al frame dopo (contatori `Deferred` / `dropped` a video).
  - **Instrument speed > 1**: il player viene chiamato N volte di seguito per frame. Il brano
    avanza una volta ogni N chiamate (contatore interno del player), quindi tempo e velocità degli
    strumenti sono giusti; il suono però cambia una volta per frame, non N volte.
- `psgrmt.s` traduce i canali POKEY nelle voci PSG (0-3 o 0-7), come `sidrmt.s` fa per il SID:

| POKEY | VERA PSG |
|---|---|
| frequenza da `AUDF` + `AUDCTL` (64/15 kHz, 1,79 MHz, canali uniti a 16 bit) | parola = K / n, n = divisore in cicli macchina; 64 kHz 8 bit da tabella (`tools/mkpsgtab.py`), gli altri modi con una divisione, solo quando cambiano `AUDF`, `AUDCTL` o la distorsione |
| tono puro | impulso 50% |
| distorsione C (poly4), toni poly5 | impulso 25% |
| rumore poly17 / poly5 | rumore della VERA |
| volume lineare 0-15 | livello logaritmico VERA con la stessa ampiezza (`vol_log`) |
| canale basso di una coppia a 16 bit | voce muta, come sul POKEY |

  Ogni canale usa l'`AUDCTL` del proprio POKEY. Panoramica: L+R, oppure con `psg_stereo` i canali
  1/3 a sinistra e 2/4 a destra (RMT4) o 1-4 a sinistra e 5-8 a destra (RMT8). Non riprodotti: modo
  "solo volume" (campioni) e filtri passa-alto del POKEY; il rumore è quello della VERA alla stessa
  frequenza, non lo schema poly17/poly5.

- Le scritture nella VERA avvengono dentro l'interrupt: `CTRL`, `FX_CTRL` e l'indirizzo della porta 0
  vengono salvati e ripristinati, quindi il programma interrotto può essere a metà di un accesso alla
  VRAM.

## Musica durante l'I/O da disco

Durante un trasferimento SIO i canali POKEY 3+4 e `AUDCTL` sono il generatore del baud rate della
porta seriale (16 bit uniti, 1,79 MHz). Il player non scrive `AUDF3/AUDF4/AUDCTL` e tiene
`AUDC3/AUDC4` a volume 0: **il POKEY suona solo i canali 1+2**, senza note appese né rumore SIO.
**La VERA non ha questo limite**: il PSG continua a suonare tutti i canali (legge lo stato calcolato
dal player, non i registri del POKEY).

Il player considera "in I/O" il sistema quando:

- `CRITIC` ≠ 0: lo mette il SIO dell'OS, quindi anche ogni caricamento da DOS. Automatico, il
  programma non deve fare niente;
- `rmt_io_begin()` … `rmt_io_end()`: per i driver SIO propri che lasciano `CRITIC` a 0 (come
  `sio.s`).

Il gestore VBI riabilita gli IRQ (CLI) mentre il player e `psg_update` girano: un byte seriale
arriva ogni ~930 cicli, e il player con le scritture VERA dura molto di più. Con IRQ mascherati i
byte andrebbero persi (errore 140, overrun).

**Attenzione con il SIO dell'OS** (`SIOV`, DOS): dopo il byte "Complete" del drive l'OS riabilita la
ricezione seriale dal codice principale; se in quel momento parte il VBI con il player, il primo byte
del settore va perso e il settore arriva spostato di un byte, a volte con stato 1 (OK). È un limite
dell'OS con un VBI pesante, già documentato in PokeyATest. Il loader IRQ di `sio.s` riceve tutta la
risposta dentro il gestore VSERIN e non ha il problema.

## Programma di prova: `TESTRIO.COM` (musica + caricamento da disco)

Da `AT2019/ATARI-Driver/PokeyATest`, adattato a POKEY + VERA. Disco proprio `disk4-rmtio.atr`
(MyPicoDos; il programma è il primo del menu, si avvia con RETURN) con file di prova da 2 a 16 KB:
li carica, verifica dimensione e checksum, e ricomincia, all'infinito, mentre la musica suona.

| Tasto | Funzione |
|---|---|
| `L` | loader: RBL IRQ SIO (`sio.s`, default) / OS SIOV (`CRITIC`, rilevamento automatico) |
| `1` `2` `3` `4` | uscita: POKEY / VERA / entrambi / ibrido (RMT8) |
| `S` | stereo VERA on/off |
| `N` | rumore SIO dell'OS (`SOUNDR`) on/off |
| `ESC` | ferma e esce |

A video: righe VU del POKEY ("P") e della VERA ("V"), stato "Disk: POKEY CH1+2" durante il
caricamento, tempo del player, tick rimandati (`late`, recuperati) e persi (`lost`, deve restare 0),
passate, file OK/errori/retry e velocità di caricamento.

```sh
make disk4-rmtio.atr                                     # gemx, 6 file di prova
make disk4-rmtio.atr RIO_ASSET_SIZES="2048 8000 16384"
atari800 -xl -pal -nopatchall -nobasic -verax16 ... disk4-rmtio.atr
```

`-nopatchall` è necessario: senza, l'emulatore intercetta il SIO e non c'è temporizzazione seriale
reale. In atari800 (PAL, 19200 baud) con il loader RBL: ~720 byte/s, 0 errori, 0 tick persi. Con
OS SIOV ci sono errori BADLNK/checksum, come previsto (vedi sopra).

## Programma di prova: `TESTRMT.COM`

Sui dischi `disk2-veratests-*.atr`; non richiede `VERA.SYS`.

| Tasto | Uscita |
|---|---|
| `1` | solo POKEY |
| `2` | solo VERA |
| `3` | entrambi (default) |
| `4` | ibrido, solo RMT8: POKEY canali 1-4, VERA canali 5-8 |
| `S` | stereo VERA on/off |
| `ESC` | ferma e esce |

Mostra frame, tempo del player nel VBI (righe raster) e i volumi POKEY / livelli VERA dei canali
(per RMT8 la seconda riga, "R", è il POKEY di destra che non esiste in hardware).

Tempo del player nel VBI in emulatore (`rmt_lines`, righe raster): massimo su 500 frame, media tra
parentesi, PAL. In NTSC i valori sono gli stessi, con il massimo fino a 2 righe in più.

| Brano | POKEY | VERA | entrambi |
|---|---|---|---|
| `gemx` (RMT4, speed 1) | 22 (10,1) | 34 (16,9) | 34 (19,0) |
| `PROJECT-X_THESMOPHORIA_pokey` (RMT4, speed 1) | 26 (12,8) | 34 (18,4) | 36 (18,9) |
| `turrican2_rev2s` (RMT8, speed 1) | 32 (14,7) | 36 (21,4) | 36 (22,3) |

La parte VERA (`psg_update`) costa 600-800 cicli per frame: un blocco srotolato per canale, i byte
scritti direttamente su `DATA0`. Prima di questa versione l'uscita "entrambi" arrivava a 44 / 44 / 56
righe in PAL e 50 / 50 / 68 in NTSC (medie 28,3 / 28,3 / 40,9). Con instrument speed N il player
(non `psg_update`) viene chiamato N volte per frame: un modulo RMT8 a speed 4 lascia poca CPU al
programma principale.

## Compilazione

```sh
make TESTRMT.COM                                                   # default: music/gemx.rmt (RMT4)
make TESTRMT.COM RMT_SONG=vera-tests/rmt/music/turrican2_rev2s.rmt  # RMT8, stereo
```

Il brano viene collegato nell'eseguibile (`tools/rmt2ca65.py` lo rende rilocabile ed esporta
`rmt_song_tracks`). Il Makefile ricava `RMT_TRACKS` (4 o 8) dall'intestazione del modulo e il linker
verifica che player e modulo coincidano. I dischi contengono la versione di default.

## Limiti

- **Instrument speed > 1**: le N chiamate per frame sono consecutive, non distribuite nel frame
  come nel player originale (che usa i timer POKEY): gli effetti hanno la velocità giusta ma una
  risoluzione di un frame. Il costo in CPU cresce di N volte.
- Modo "solo volume" (campioni) e filtri passa-alto del POKEY non riprodotti sulla VERA.

Brani inclusi: `gemx.rmt`, `PROJECT-X_THESMOPHORIA_pokey.rmt` (RMT4, da `AT2019/ATARI-Driver`),
`turrican2_rev2s.rmt` (RMT8, di Raster, dagli esempi di Raster Music Tracker).
