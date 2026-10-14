# RMT player su POKEY + VERA PSG

Suona i moduli RMT (Raster Music Tracker) sul POKEY, sul PSG della VERA, o su entrambi insieme:

- **RMT4** (mono, 4 canali): voci PSG 0-3;
- **RMT8** (stereo, pensati per due POKEY): voci PSG 0-7. I canali 1-4 suonano anche sul POKEY
  reale; i canali 5-8, che su un Atari con un solo POKEY andrebbero persi, suonano sulla VERA.
- **SAP / XEX esportati da RITMO** (o da RMT 1.31+): i registri POKEY registrati da RMT, compressi
  LZSS, suonati da `lzsplayr.s` come li suona VU-Player (vedi "SAP e XEX esportati da RITMO").

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

## SAP e XEX esportati da RITMO: `lzsplayr.s`

Un `.sap` o `.xex` esportato da RITMO (o da Raster Music Tracker 1.31 e successivi) non contiene il
modulo RMT: contiene i registri POKEY che RMT ha registrato frame per frame, con il driver scelto
nelle opzioni (di default Patch16, intonato diversamente dal `rmtplayr.s` di RMT 1.28), compressi
LZSS (LZ16 di DMSC) e suonati da VU-Player v2. Con un file così come `RMT_SONG`, il Makefile usa
`lzsplayr.s` al posto di `rmtplayr.s`: stessa interfaccia (`rmt_init`, `rmt_play`, `rmt_silence`,
`rmt_ioactive`, `trackn_audf`/`trackn_audc`/`v_audctl` per `psgrmt.s`, `rmt_pokey_mute`...),
stesse build `RMT_VERA` e `RMT_TRACKS`. POKEY e VERA suonano quello che suona il SAP / XEX.

```sh
make RMTPLAY.COM RMT_SONG=vera-tests/rmt/music/gemx.sap            # mono
make RMTPLAY.COM RMT_SONG=vera-tests/rmt/music/Final_Fantasy.sap   # stereo: canali 5-8 sulla VERA
make TESTRMT.COM RMT_SONG=file.xex RMT_SUBSONG=1                   # un altro subtune
./rmtplay.sh vera-tests/rmt/music/gemx.sap [antic|vera|both]
```

- `tools/sap2ca65.py` estrae un subtune dalla memoria di VU-Player del file (indice dei brani a
  `$1F40`, instrument speed, flag stereo) e scrive le sezioni intro e loop come sorgente
  rilocabile; decodifica ogni sezione una volta per controllarla. `--tracks` dà 4 o 8 al Makefile
  (`RMT_TRACKS`), `--info` scrive `songinfo.h` dai tag `NAME` / `AUTHOR` / `DATE` del SAP o dal
  testo a video dello XEX.
- `lzsplayr.s` decodifica un frame per `rmt_play` (9 flussi di byte con una finestra di 256 byte
  ciascuno: 2,3 KB di BSS, 4 byte di pagina zero): l'intro una volta, poi il loop all'infinito.
  Come `rmtplayr.s`, `rmt_play` scrive prima il POKEY e poi decodifica il frame successivo. Uno
  stereo decodifica due frame per chiamata: il sinistro nei canali 1-4 (POKEY), il destro nei
  canali 5-8 e in `v_audctl2` (solo VERA).
- Instrument speed N: lo stream ha N frame per frame video, `rmtvbi.s` chiama il player N volte
  per frame come per un `.rmt`.
- RMTPLAY mostra `SECT` (sezione dello stream: 0 intro, 1 loop, oppure 0 se il brano torna
  all'inizio) e `FRAME` (frame nella sezione / ultimo frame) al posto di riga, row e speed.

Verificato: su un emulatore 6502 `lzsplayr.s` dà i registri POKEY di VU-Player v2 byte per byte
(22 export SAP di 11 brani con i driver Patch16 e Unpatched, più stereo e instrument speed 2). In
atari800 con `-verax16` e `-pokeyrec` RMTPLAY suona `gemx.sap` per 7,4 minuti e
`Final_Fantasy.sap` (stereo, POKEY = canali di sinistra) per 8 minuti, e un brano a speed 2 per 6
minuti, con ogni frame uguale a quello decodificato dal file.

In un SAP esportato da RITMO prima della 2.6, VU-Player stesso dopo il secondo giro del loop va
avanti diversamente: il suo `INIT` azzerava il contatore dei loop che l'export mette a `$FF`
(infinito), quindi alla seconda fine del loop parte una dissolvenza che si ferma dopo un passo: dal
terzo giro suona un gradino di volume più basso e un frame prima. `lzsplayr.s` continua il loop a
volume pieno, come voleva l'export e come suona un SAP di RITMO 2.6.

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
verifica che player e modulo coincidano. I dischi contengono la versione di default. Un `.sap` o
`.xex` passa da `tools/sap2ca65.py` e `lzsplayr.s` (vedi sopra).

## RMTPLAY: tempi del visualizzatore

Il ciclo principale di `rmtplay.c` deve stare in un frame accanto al player sul VBI: barre a ogni
frame, registri e campi di testo a frame alterni, solo le celle che cambiano, niente
moltiplicazioni né divisioni lunghe (come RITMO PLAYER, l'XEX con il player RMT di RITMO, e il
player C64 di `AT2019/ATARI-Driver/rmt_cbm64`). Con un brano a instrument speed 4 e i due schermi
un giro durava da 3 a 5 frame: lo schermo si aggiornava circa 10 volte al secondo. Le cause:
tutte le 8 righe di ogni barra che si muoveva riscritte su ANTIC e VERA, `frames / fps` (divisione
a 32 bit) a ogni frame per il tempo, la riga lunga di NAME/AUTHOR ricostruita un carattere alla
volta e il suo scorrimento calcolato con uno shift a 32 bit e un modulo, `y * 40` e
`(y + 3) * 128` a ogni scrittura, i campi esadecimali passati per `screen_code()` e `memcpy()`,
le variabili locali sullo stack di cc65 (ora `#pragma static-locals`, e `-Osir` nel Makefile).

Misurato in atari800 con `-verax16` (contatori in una build di prova, 3000 giri del ciclo,
aggiornamenti dello schermo al secondo):

| Brano | ANTIC + VERA prima | dopo | solo ANTIC | solo VERA |
|---|---|---|---|---|
| `claude_3ch_mono_speed_4.rmt` (RMT4, speed 4) | ~10 | 50 | | |
| `claude_3ch_stereo_speed_4.rmt` (RMT8, speed 4) | ~10 | ~41 | ~10 → ~47 | ~12 → 50 |
| `stereo_reference_song.rmt` di RITMO (RMT8, speed 4) | ~12 | ~45 | | |
| `gemx.sap` | | 50 | | |

In stereo con i due schermi il resto del frame va al player a 8 tracce (N chiamate) e a
`psg_update`. Lo schermo ANTIC dopo 600 frame coincide con quello di prima (scorrimento, tempo,
posizione); la VERA riceve gli stessi codici agli stessi indirizzi, presi da una tabella.

## Limiti

- **Instrument speed > 1**: le N chiamate per frame sono consecutive, non distribuite nel frame
  come nel player originale (che usa i timer POKEY): gli effetti hanno la velocità giusta ma una
  risoluzione di un frame. Il costo in CPU cresce di N volte.
- Modo "solo volume" (campioni) e filtri passa-alto del POKEY non riprodotti sulla VERA.

Brani inclusi: `gemx.rmt`, `PROJECT-X_THESMOPHORIA_pokey.rmt` (RMT4, da `AT2019/ATARI-Driver`),
`turrican2_rev2s.rmt` (RMT8, di Raster, dagli esempi di Raster Music Tracker); `gemx.sap` (gemx
esportato da RITMO, driver Patch16) e `Final_Fantasy.sap` (RMT8 di kjmann dai brani di RITMO,
esportato stereo).
