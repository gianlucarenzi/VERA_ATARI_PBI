---
title: "VERA X16 per Atari — Eseguire i test nell'emulatore"
subtitle: "Fork di atari800 con la scheda VeraX16 PBI"
lang: it
---

# Panoramica

I programmi di test in `vera-tests/` girano su un fork dell'emulatore
**atari800** (versione 5.2.0) che emula la scheda VeraX16 sul bus PBI
dell'Atari: registri VERA a `$D100-$D11F`, ROM handler da 2 KB a
`$D800-$DFFF` (selezionata dal latch PBI `$D1FF`).

L'emulatore si trova accanto a questo repository:

```
~/Progetti/ATARI/
├── atari800/                 emulatore (src/atari800 è l'eseguibile)
│   └── vera_pbi_rom -> ../VERA_ATARI_PBI
└── VERA_ATARI_PBI/           questo repository (ROM, driver, test)
```

Tutti i comandi seguenti si eseguono **dalla radice di questo repository**
(`VERA_ATARI_PBI`), quindi l'emulatore è `../atari800/src/atari800`.
Per brevità:

```sh
EMU=../atari800/src/atari800
```

# Prerequisiti

## Compilare l'emulatore

```sh
cd ../atari800
./autogen.sh
./configure --enable-pbi-verax16
make
cd -
```

L'emulazione della VERA è in `src/pbi_verax16.c` (registri, FX, audio, SPI)
e `src/vera_video.c` (rendering). Senza `--enable-pbi-verax16` le opzioni
`-verax16` non esistono.

## ROM del sistema operativo Atari

Se atari800 è compilato con libcurl (`configure` attiva da solo la funzione
di download quando la trova) e non è configurato nessun file ROM del
sistema operativo, all'avvio **scarica le ROM da Internet**: preleva
`http://www.emulators.com/freefile/pcxf380.zip` (PC Xformer 3.8), estrae i
file `.rom` in `../atari800/src/rom/`, li usa (OS XL e BASIC originali
Atari) e ne scrive i percorsi in `../atari800/src/.atari800.cfg`. Da quel
momento non li scarica più.

atari800 legge `.atari800.cfg` dalla cartella dell'eseguibile prima di
`~/.atari800.cfg`: una volta creato `../atari800/src/.atari800.cfg`,
lanciando `../atari800/src/atari800` il file `~/.atari800.cfg` non viene
più letto.

Per usare AltirraOS e Altirra BASIC integrati, senza download, compilare
con:

```sh
./configure --enable-pbi-verax16 --disable-download
```

e lasciare vuoti i percorsi `ROM_OS_*` nel file di configurazione.

## Compilare ROM, driver e dischi di test

Strumenti richiesti: `cc65` (`ca65`, `ld65`, `cl65`), `dir2atr`, `python3`.

```sh
make            # ROM, driver VERA*.SYS, programmi di test, immagini disco
```

Prodotti principali:

| File | Contenuto |
|---|---|
| `vera_pbi_handler.rom` | ROM handler PBI, 2 KB (avvio in 80×60) |
| `VERA4030.SYS`, `VERA8030.SYS`, `VERA8060.SYS` | driver in RAM per 40×30, 80×30, 80×60 |
| `disk1-runcpm.atr` | DOS 2.0S, `RUNCPM.COM`, `VERA8030.SYS` |
| `disk2-veratests-40x30.atr` | `TEST4`, `TESTGS4`, `TESTMAZ4`, `TESTMTX4`, `TESTRMT`, `VERA4030.SYS` |
| `disk2-veratests-80x30.atr` | `TEST8`, `TESTGS8`, `TESTMAZ8`, `TESTMTX8`, `TESTRMT`, `VERA8030.SYS` |
| `disk2-veratests-80x60.atr` | `TEST6`, `TESTGS6`, `TESTMAZ6`, `TESTMTX6`, `TESTRMT`, `VERA8060.SYS` |
| `disk3-standalone.atr` | `TESTFX`, `TESTIRQ`, `TESTPLR` + `DEMO.VTM` (test senza driver) |
| `disk4-rmtio.atr` | `TESTRIO` con autorun MyPicoDos e i file di prova |

I programmi `TESTn`/`TESTGSn`/`TESTMAZn`/`TESTMTXn` dei dischi `disk2`
contengono già il driver della loro risoluzione (`n` = 4: 40×30,
8: 80×30, 6: 80×60).

# Il comando di base

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

- `-verax16` inserisce la scheda; `-verax16-rom` indica la ROM handler.
- `-xl` (800XL) o `-xe` (130XE): il driver richiede una macchina XL/XE.
- `-pal` o `-ntsc`: i test devono passare in entrambi.
- `-nopatch`: nessuna patch SIO ad alta velocità, l'I/O su disco va alla
  velocità seriale reale (il dispositivo H: resta attivo). Per renderlo
  permanente impostare `ENABLE_SIO_PATCH=0` in `~/.atari800.cfg`.
- L'ultimo argomento è il disco in `D1:`; altri `.atr` vanno in `D2:`, `D3:`...

L'emulatore mostra due uscite: lo schermo ANTIC/GTIA normale e lo schermo
della VERA (VGA 640×480).

## Avviare un programma dal DOS 2.0S

I dischi avviano il DOS 2.0S. Dal menu del DUP:

1. premere `L` (BINARY LOAD);
2. scrivere il nome del file, per esempio `TESTFX.COM`, e premere Return.

## Avviare un programma senza disco

`-run` carica un eseguibile direttamente dal file system dell'host:

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
```

# Opzioni

## Opzioni della scheda VeraX16

| Opzione | Significato |
|---|---|
| `-verax16` | attiva la scheda (alias `--use-verax16`) |
| `-verax16-rom F` | ROM handler, 2 KB a `$D800-$DFFF` |
| `-verax16-pbi-id N` | bit del dispositivo PBI 0-7 (default 7, maschera `$80`) |
| `-verax16-config-ms N` | tempo di configurazione dell'FPGA dopo l'accensione, bus non pilotato nel frattempo (default 100); `0` = la scheda tiene il RESET dell'Atari fino a CONFIG_DONE |
| `-verax16-debuglevel N` | livello di log (default 0) |
| `-verax16-psg-volume N` | livello del PSG VERA in percento, 0-400 (default 100 = una voce forte come un canale POKEY a volume 15) |
| `-verax16-sdcard F` | immagine SD grezza (per esempio da `dd`) esposta tramite la SPI della VERA |

## Opzioni utili di atari800

| Opzione | Uso |
|---|---|
| `-xl`, `-xe` | macchina 800XL / 130XE |
| `-pal`, `-ntsc` | standard video |
| `-basic`, `-nobasic` | ROM BASIC attiva/disattiva |
| `-run F` | esegue un file COM/EXE/XEX/BAS |
| `-nopatch` | nessuna patch SIO ad alta velocità: temporizzazione seriale reale, H: funziona |
| `-nopatchall` | nessuna patch all'OS: temporizzazione seriale SIO reale (H: non funziona) |
| `-volume N` | volume di uscita 0-100 |
| `-stereo` | due POKEY |
| `-turbo` | massima velocità |
| `-netsio [porta]` | NetSIO per FujiNet-PC (default UDP 9997) |
| `-config F` | file di configurazione alternativo |

# Eseguire i singoli test

## TESTFX — coprocessore FX

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

Dal DUP: `L`, `TESTFX.COM`. Verifica ogni registro FX e misura il throughput
di copia/riempimento della VRAM. Risultato atteso: **PASS 36, FAIL 0**.

## TESTIRQ — interrupt della VERA

```sh
$EMU -xl -pal  -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
$EMU -xl -ntsc -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
```

Verifica il gancio su `VIMIRQ`: VSYNC a 59,94 Hz, IRQ di riga, mascheramento
di AFLOW, catena verso l'OS e rimozione. Risultato atteso: **PASS 13,
FAIL 0**, sia in PAL sia in NTSC. Vedi `Documentation/VERA-IRQ.md`.

## TESTPLR — player VTM

```sh
$EMU -xl -pal -nopatch -volume 100 -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

Dal DUP: `L`, `TESTPLR.COM`. Usare `-pal` o `-ntsc` secondo la frequenza per
cui è stato convertito il brano. `-volume 100` serve: il PSG della VERA è
basso al livello di mixer predefinito (oppure alzare `-verax16-psg-volume`).

## TEST / TESTGS / TESTMAZ / TESTMTX — driver e modi video

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk2-veratests-80x30.atr
```

Usare il disco della risoluzione da provare (`40x30`, `80x30`, `80x60`) e
avviare dal DUP `TEST8.COM`, `TESTGS8.COM`, `TESTMAZ8.COM`, `TESTMTX8.COM`
(cifra 4 o 6 sugli altri dischi): caricamento font, gradiente, scrolling,
demo labirinto e matrice.

## TESTRMT — player RMT su POKEY + VERA

Su tutti i dischi `disk2-veratests-*.atr`, non richiede `VERA.SYS`. Dal
DUP: `L`, `TESTRMT.COM`.

| Tasto | Uscita |
|---|---|
| `1` | solo POKEY |
| `2` | solo VERA |
| `3` | entrambi (default) |
| `4` | ibrido, solo RMT8: POKEY canali 1-4, VERA canali 5-8 |
| `S` | stereo VERA on/off |
| `ESC` | ferma ed esce |

## TESTRIO — musica durante l'I/O su disco

```sh
make disk4-rmtio.atr
$EMU -xl -pal -nopatchall -nobasic \
     -verax16 -verax16-rom vera_pbi_handler.rom disk4-rmtio.atr
```

Il disco avvia da solo `TESTRIO.COM` (autorun MyPicoDos). `-nopatchall` è
obbligatorio: senza, l'emulatore intercetta il SIO e non c'è temporizzazione
seriale reale. Atteso (PAL, loader RBL): ~720 byte/s, 0 errori, 0 tick persi.

## RUNCPM — terminale ANSI tramite FujiNet

Terminale 1, FujiNet-PC:

```sh
cd FujiNet/fujinet-pc-ATARI
./run-fujinet -c fnconfig.ini -s SD/
```

Attendere `### NetSIO stopped ###` nel log, poi nel terminale 2:

```sh
$EMU -xl -pal -nopatch -netsio -verax16 -verax16-rom vera_pbi_handler.rom
```

Dettagli in `README-fujinet.md`.

# Risoluzione dei problemi

| Sintomo | Causa / rimedio |
|---|---|
| `ERROR: VeraX16 PBI card not found ($D100 not responding).` | manca `-verax16`: ogni test chiama `vera_require()` all'avvio |
| Tre beep dall'altoparlante della console all'avvio | la ROM non ha visto la VERA entro ~0,7 s; controllare `-verax16-config-ms` |
| Opzione `-verax16` sconosciuta | emulatore compilato senza `--enable-pbi-verax16` |
| VERA muta o molto bassa | aggiungere `-volume 100` o alzare `-verax16-psg-volume` |
| Un file è sul disco ma il DOS non lo elenca, o "0 FREE SECTORS" | bug di `dir2atr`; ricompilare con `make`, che esegue `vera-tests/tools/fix_atr_vtoc.py` |
| Errori SIO o velocità sbagliata in `TESTRIO` | manca `-nopatchall` |
