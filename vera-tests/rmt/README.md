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
- `rmtvbi.s` (da `RmtSkeleton`) fa girare il player nel **VBI immediato**, così la musica continua
  anche durante l'I/O da disco; con `RMT_VERA` chiama `psg_update` dopo ogni passo del player.
- `psgrmt.s` traduce i canali POKEY nelle voci PSG (0-3 o 0-7), come `sidrmt.s` fa per il SID:

| POKEY | VERA PSG |
|---|---|
| frequenza da `AUDF` + `AUDCTL` (64/15 kHz, 1,79 MHz, canali uniti a 16 bit) | parola = K / n, n = divisore in cicli macchina; 64 kHz 8 bit da tabella (`tools/mkpsgtab.py`), gli altri modi con una divisione, solo quando il divisore cambia |
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
(per RMT8 la seconda riga, "R", è il POKEY di destra che non esiste in hardware). Tempo nel VBI in
emulatore: circa 38 righe con RMT4, circa 60 con RMT8, su 312 (PAL).

## Compilazione

```sh
make TESTRMT.COM                                                   # default: music/gemx.rmt (RMT4)
make TESTRMT.COM RMT_SONG=vera-tests/rmt/music/turrican2_rev2s.rmt  # RMT8, stereo
```

Il brano viene collegato nell'eseguibile (`tools/rmt2ca65.py` lo rende rilocabile ed esporta
`rmt_song_tracks`). Il Makefile ricava `RMT_TRACKS` (4 o 8) dall'intestazione del modulo e il linker
verifica che player e modulo coincidano. I dischi contengono la versione di default.

## Limiti

- **Instrument speed > 1**: alcuni moduli chiedono di chiamare la routine degli strumenti più volte
  per frame; il VBI la chiama una volta, quindi vibrati ed effetti degli strumenti vanno più lenti
  (il tempo del brano resta giusto). `rmt2ca65.py` lo segnala con un avviso.
- Modo "solo volume" (campioni) e filtri passa-alto del POKEY non riprodotti sulla VERA.

Brani inclusi: `gemx.rmt`, `PROJECT-X_THESMOPHORIA_pokey.rmt` (RMT4, da `AT2019/ATARI-Driver`),
`turrican2_rev2s.rmt` (RMT8, di Raster, dagli esempi di Raster Music Tracker).
