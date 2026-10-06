# RMT player su POKEY + VERA PSG

Suona i moduli RMT (Raster Music Tracker, mono a 4 canali) sul POKEY, sul PSG della VERA, o su
entrambi insieme.

## Come funziona

- `rmtplayr.s` è il player Atari di Raster nel port ca65 di `AT2019/ATARI-Driver/RmtSkeleton`
  (stesso sorgente del player per C64 `rmt_cbm64`). Con `-D RMT_VERA` continua a scrivere il POKEY,
  ma esporta lo stato dei canali (`trackn_audf`, `trackn_audc`, `v_audctl`) e può tenere il POKEY
  muto (`rmt_pokey_mute`).
- `rmtvbi.s` (da `RmtSkeleton`) fa girare il player nel **VBI immediato**, così la musica continua
  anche durante l'I/O da disco; con `RMT_VERA` chiama `psg_update` dopo ogni passo del player.
- `psgrmt.s` traduce i 4 canali POKEY nelle voci PSG 0-3, come `sidrmt.s` fa per il SID:

| POKEY | VERA PSG |
|---|---|
| frequenza da `AUDF` + `AUDCTL` (64/15 kHz, 1,79 MHz, canali uniti a 16 bit) | parola = K / n, n = divisore in cicli macchina; 64 kHz 8 bit da tabella (`tools/mkpsgtab.py`), gli altri modi con una divisione, solo quando il divisore cambia |
| tono puro | impulso 50% |
| distorsione C (poly4), toni poly5 | impulso 25% |
| rumore poly17 / poly5 | rumore della VERA |
| volume lineare 0-15 | livello logaritmico VERA con la stessa ampiezza (`vol_log`) |
| canale basso di una coppia a 16 bit | voce muta, come sul POKEY |

  Panoramica: L+R, oppure canali 1/3 a sinistra e 2/4 a destra (`psg_stereo`). Non riprodotti: modo
  "solo volume" (campioni) e filtri passa-alto del POKEY; il rumore è quello della VERA alla stessa
  frequenza, non lo schema poly17/poly5.

- Le scritture nella VERA avvengono dentro l'interrupt: `CTRL`, `FX_CTRL` e l'indirizzo della porta 0
  vengono salvati e ripristinati, quindi il programma interrotto può essere a metà di un accesso alla
  VRAM. Il traduttore usa le voci PSG 0-3.

## Programma di prova: `TESTRMT.COM`

Sui dischi `disk2-veratests-*.atr`; non richiede `VERA.SYS`.

| Tasto | Uscita |
|---|---|
| `1` | solo POKEY |
| `2` | solo VERA |
| `3` | entrambi (default) |
| `S` | stereo VERA on/off |
| `ESC` | ferma e esce |

Mostra frame, tempo del player nel VBI (righe raster) e i volumi POKEY / livelli VERA dei 4 canali.

## Compilazione

```sh
make TESTRMT.COM                                   # brano di default: music/gemx.rmt
make TESTRMT.COM RMT_SONG=vera-tests/rmt/music/PROJECT-X_THESMOPHORIA_pokey.rmt
```

Il brano viene collegato nell'eseguibile (`tools/rmt2ca65.py` lo rende rilocabile). Per cambiarlo
rimuovere prima `vera-tests/rmt/gen/song.s`, che il Makefile rigenera solo se cambia il file `.rmt`.
