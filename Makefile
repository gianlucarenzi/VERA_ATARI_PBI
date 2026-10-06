# Makefile — VeraX16 PBI ROM handler + relocatable VERA.SYS driver.
#
# This Makefile generates the PBI ROM (fixed at 80x60) and three different 
# versions of the relocatable VERA.SYS driver for various resolutions.

CA65     = ca65
LD65     = ld65
DIR2ATR  ?= dir2atr
PYTHON   ?= python3

ASFLAGS    = --cpu 6502 --feature labels_without_colons
# PBI ROM always starts in 80x60 mode (8x8 font, 1:1 scale) to provide
# a consistent boot experience before any driver is loaded.
ROM_ASFLAGS = $(ASFLAGS) -D FONT_8X8=1
SYSASFLAGS = $(ASFLAGS) -D __ATARIXL__ -D SHRAM_HANDLERS

# Screen/viewport selection for the RAM driver (passed via sub-make calls):
#   SCREEN=80x60 → 80×60 viewport (8-pixel tiles, 640×480)
#   SCREEN=80x30 → 80×30 viewport (16-pixel tiles, 640×480, default)
#   SCREEN=40x30 → 40×30 viewport (8-pixel tiles, 2× scale, 320×240)
SCREEN  ?= 80x30
ifeq ($(SCREEN),80x60)
  SYSASFLAGS += -D FONT_8X8=1
endif
ifeq ($(SCREEN),40x30)
  SYSASFLAGS += -D MODE_40X30=1 -D FONT_8X8=1
endif

# --- Output filenames for multi-resolution drivers ---

SYS4030 = VERA4030.SYS
SYS8030 = VERA8030.SYS
SYS8060 = VERA8060.SYS

# --- PBI ROM (unchanged) -----------------------------------------------------

TARGET   = vera_pbi_handler.rom
OBJ      = vera_pbi_handler.o
SRC      = vera_pbi_handler.s
CFG      = vera_pbi.cfg
MAPFILE_ROM = vera_pbi.map

# --- Relocatable VERA.SYS body ----------------------------------------------

SYS         = VERA.SYS
SYSCFG      = vera_sys.cfg
BODY_BASE_A = 0xA000
BODY_BASE_B = 0xA100

BODY_SRC = vera_stub.s vera_driver.s vera_sys_vbi.s vera_sys_es_hook.s \
           vera_sys_font.s vera_sys_dosini.s
BODY_OBJ = $(BODY_SRC:.s=.o)

BODY_BIN_A = body_A000.bin
BODY_BIN_B = body_A100.bin
BODY_LBL_A = body_A000.lbl
BODY_LBL_B = body_A100.lbl
BODY_MAP_A = body_A000.map
BODY_MAP_B = body_A100.map

FIXUPS_BIN = fixups.bin

# --- One-shot bootstrap loader ----------------------------------------------

LOADER_CFG = vera_loader.cfg
LOADER_SRC = vera_sys_loader.s
LOADER_OBJ = vera_sys_loader.o
LOADER_BIN = loader.bin
LOADER_LBL = loader.lbl
LOADER_MAP = loader.map

# --- Output / packaging -----------------------------------------------------

ATR      = vera_pbi.atr
ATRBUILD = .atrbuild
ROMDIR   = ../roms
LABELS   = vera.lbl

GEN_FIXUPS  = gen_fixups.py
ASSEMBLE    = assemble_autorun.py
BUNDLE_VERA = bundle_vera.py

.PHONY: all clean cleanall install atr labels drivers clean_objs

# The default target builds the PBI ROM and all three driver versions.
all: $(TARGET) $(SYS) drivers disk1-runcpm.atr disk2-veratests-40x30.atr disk2-veratests-80x30.atr disk2-veratests-80x60.atr disk3-standalone.atr disk4-rmtio.atr

# Rule to generate the three resolution-specific drivers.
# Each build requires a clean objects pass to ensure correct defines are applied.
drivers: $(SYS4030) $(SYS8030) $(SYS8060) $(SYS4030)

$(SYS4030):
	$(MAKE) clean_objs
	$(MAKE) SCREEN=40x30 $(SYS)
	cp $(SYS) $(SYS4030)

$(SYS8030): $(BODY_SRC) $(LOADER_SRC) vera_common.inc
	$(MAKE) clean_objs
	$(MAKE) SCREEN=80x30 $(SYS)
	cp $(SYS) $(SYS8030)

$(SYS8060):
	$(MAKE) clean_objs
	$(MAKE) SCREEN=80x60 $(SYS)
	cp $(SYS) $(SYS8060)

# Deletes only the object files and intermediate binaries to allow switching
# between different resolution builds without deleting the final renamed drivers.
clean_objs:
	rm -rf $(OBJ) $(BODY_OBJ) $(LOADER_OBJ) $(BODY_BIN_A) $(BODY_BIN_B) $(FIXUPS_BIN) $(LOADER_BIN) $(SYS)

# === PBI ROM ================================================================

$(OBJ): $(SRC)
	$(CA65) $(ROM_ASFLAGS) -o $(OBJ) $(SRC)

$(TARGET): $(OBJ) $(CFG)
	$(LD65) -C $(CFG) --mapfile $(MAPFILE_ROM) -vm -o $(TARGET) $(OBJ)
	@echo "ROM size: $$(wc -c < $(TARGET)) bytes (max 2048)"

# === VERA.SYS body ===========================================================

$(BODY_OBJ): %.o: %.s
	$(CA65) $(SYSASFLAGS) -o $@ $<

# Two builds at adjacent base addresses (delta $100). gen_fixups.py diffs
# them to discover every internal absolute pointer.
$(BODY_BIN_A) $(BODY_LBL_A) $(BODY_MAP_A): $(BODY_OBJ) $(SYSCFG)
	$(LD65) -C $(SYSCFG) -S $(BODY_BASE_A) \
		--mapfile $(BODY_MAP_A) -Ln $(BODY_LBL_A) \
		-vm -o $(BODY_BIN_A) $(BODY_OBJ)

$(BODY_BIN_B) $(BODY_LBL_B) $(BODY_MAP_B): $(BODY_OBJ) $(SYSCFG)
	$(LD65) -C $(SYSCFG) -S $(BODY_BASE_B) \
		--mapfile $(BODY_MAP_B) -Ln $(BODY_LBL_B) \
		-vm -o $(BODY_BIN_B) $(BODY_OBJ)

$(FIXUPS_BIN): $(BODY_BIN_A) $(BODY_BIN_B) $(GEN_FIXUPS)
	$(PYTHON) $(GEN_FIXUPS) $(BODY_BIN_A) $(BODY_BIN_B) $(FIXUPS_BIN)

# === Loader ==================================================================

$(LOADER_OBJ): $(LOADER_SRC)
	$(CA65) $(SYSASFLAGS) -o $(LOADER_OBJ) $(LOADER_SRC)

$(LOADER_BIN) $(LOADER_LBL) $(LOADER_MAP): $(LOADER_OBJ) $(LOADER_CFG)
	$(LD65) -C $(LOADER_CFG) \
		--mapfile $(LOADER_MAP) -Ln $(LOADER_LBL) \
		-vm -o $(LOADER_BIN) $(LOADER_OBJ)

# === AUTORUN.SYS assembly ===================================================

$(SYS): $(BODY_BIN_A) $(BODY_LBL_A) $(FIXUPS_BIN) \
        $(LOADER_BIN) $(LOADER_LBL) $(ASSEMBLE)
	$(PYTHON) $(ASSEMBLE) \
		$(BODY_BIN_A) $(BODY_LBL_A) $(FIXUPS_BIN) \
		$(LOADER_BIN) $(LOADER_LBL) $(SYS)

labels: $(TARGET) $(SYS)
	$(PYTHON) make_labels.py $(MAPFILE_ROM) $(BODY_MAP_A) > $(LABELS)
	@echo "Labels written to $(LABELS) ($$(wc -l < $(LABELS)) entries)"

# === ATR image ==============================================================

TEST_SRC      = vera-tests/test_font.c
TEST_GS_SRC   = vera-tests/test_gradient_scroll.c
TEST_MAZE_SRC = vera-tests/test_maze.c
TEST_FX_SRC   = vera-tests/test_fx.c
TEST_FX_EXE   = TESTFX.COM
TEST_IRQ_SRC  = vera-tests/test_irq.c
TEST_IRQ_EXE  = TESTIRQ.COM
VERA_IRQ_SRC  = vera-tests/vera_irq.s
VERA_IRQ_OBJ  = vera-tests/vera_irq.o
TEST_MTX_SRC  = vera-tests/test_matrix.c

# --- VTM PSG music player test (standalone, no VERA.SYS needed) ------------

TEST_PLAYER_SRC = vera-tests/test_player.c
VTM_LOADER_SRC  = vera-tests/vtm_loader.c
VTM_PLAYER_SRC  = vera-tests/vtm_player.s
VTM_PLAYER_OBJ  = vera-tests/vtm_player.o
VU_PM_SRC       = vera-tests/vu_pm.s
VU_PM_OBJ       = vera-tests/vu_pm.o
VBM_DISPLAY_SRC = vera-tests/vbm_display.s
VBM_DISPLAY_OBJ = vera-tests/vbm_display.o
VBM_LOADER_SRC  = vera-tests/vbm_loader.c
TEST_PLAYER_EXE = TESTPLR.COM
VTM_COMPILE     = vera-tests/tools/vtm_compile.py
DEMO_SONG_SRC   = vera-tests/songs/axelf.vtms
DEMO_SONG_BIN   = vera-tests/songs/DEMO.VTM

$(VTM_PLAYER_OBJ): $(VTM_PLAYER_SRC) vera-tests/vtm_notes.inc vera_common.inc
	$(CA65) -I . -I vera-tests -o $@ $<

$(VU_PM_OBJ): $(VU_PM_SRC)
	$(CA65) -I . -I vera-tests -o $@ $<

$(VBM_DISPLAY_OBJ): $(VBM_DISPLAY_SRC) vera_common.inc
	$(CA65) -I . -I vera-tests -o $@ $<

$(TEST_PLAYER_EXE): $(TEST_PLAYER_SRC) $(VTM_LOADER_SRC) $(VTM_PLAYER_OBJ) $(VU_PM_OBJ) $(VBM_DISPLAY_OBJ) $(VBM_LOADER_SRC) vera-tests/vtm.h vera-tests/vu_pm.h vera-tests/vbm.h vera-tests/vera_detect.h vera-tests/atari_nosyschk.cfg
	cl65 -t atari -C vera-tests/atari_nosyschk.cfg -I vera-tests --start-addr 0x2500 -o $(TEST_PLAYER_EXE) $(TEST_PLAYER_SRC) $(VTM_LOADER_SRC) $(VTM_PLAYER_OBJ) $(VU_PM_OBJ) $(VBM_DISPLAY_OBJ) $(VBM_LOADER_SRC)

$(DEMO_SONG_BIN): $(DEMO_SONG_SRC) $(VTM_COMPILE)
	$(PYTHON) $(VTM_COMPILE) $(DEMO_SONG_SRC) $(DEMO_SONG_BIN)

# Optional artwork to go with $(DEMO_SONG_BIN) (see vera-tests/vbm.h and
# workflow/01-vera-asset-format.md's VBM1 format) — not built by default
# (DEMO_IMAGE_SRC is empty), pick a source image explicitly:
#   make DEMO_IMAGE_SRC=path/to/cover.png vera-tests/songs/DEMO.VBM
IMG2VBM         = vera-tests/tools/img2vbm.py
DEMO_IMAGE_SRC  =
DEMO_IMAGE_BIN  = vera-tests/songs/DEMO.VBM

$(DEMO_IMAGE_BIN): $(DEMO_IMAGE_SRC) $(IMG2VBM)
	$(PYTHON) $(IMG2VBM) $(DEMO_IMAGE_SRC) $(DEMO_IMAGE_BIN)

# Resolution-specific bundled variants (suffix: 4=40x30, 8=80x30, 6=80x60)
TEST_EXES     = TEST4.COM TEST8.COM TEST6.COM
TESTGS_EXES   = TESTGS4.COM TESTGS8.COM TESTGS6.COM
TESTMAZE_EXES = TESTMAZ4.COM TESTMAZ8.COM TESTMAZ6.COM
TESTMTX_EXES  = TESTMTX4.COM TESTMTX8.COM TESTMTX6.COM
ALL_TEST_EXES = $(TEST_EXES) $(TESTGS_EXES) $(TESTMAZE_EXES) $(TESTMTX_EXES) $(TEST_FX_EXE) $(TEST_IRQ_EXE) $(TEST_RMT_EXE) $(TEST_RIO_EXE) $(TEST_PLAYER_EXE) $(RUNCPM_EXE)

# Template: compile once to an intermediate binary, then bundle three times.
# $(1) = output base name (7 chars max, no digit suffix)
# $(2) = intermediate raw binary (e.g. _TEST.COM)
# $(3) = C source file
TEST_HEADERS = vera-tests/vera_detect.h vera-tests/vera_keys.h

define make_test_variants
$(2): $(3) $(TEST_HEADERS)
	cl65 -t atari --start-addr 0x5000 -o $$@ $$<

$(1)4.COM: $(2) $(SYS4030) $(BUNDLE_VERA)
	$$(PYTHON) $$(BUNDLE_VERA) $$(SYS4030) $$< $$@

$(1)8.COM: $(2) $(SYS8030) $(BUNDLE_VERA)
	$$(PYTHON) $$(BUNDLE_VERA) $$(SYS8030) $$< $$@

$(1)6.COM: $(2) $(SYS8060) $(BUNDLE_VERA)
	$$(PYTHON) $$(BUNDLE_VERA) $$(SYS8060) $$< $$@
endef

$(eval $(call make_test_variants,TEST,_TEST.COM,$(TEST_SRC)))
$(eval $(call make_test_variants,TESTGS,_TESTGS.COM,$(TEST_GS_SRC)))
$(eval $(call make_test_variants,TESTMAZ,_TESTMAZE.COM,$(TEST_MAZE_SRC)))
$(eval $(call make_test_variants,TESTMTX,_TESTMTX.COM,$(TEST_MTX_SRC)))

# RUNCPM.COM specifically for 80x30
RUNCPM_SRC = vera-tests/runcpm.c
RUNCPM_EXE = RUNCPM.COM
RUNCPM80_EXE = RUNCPM80.COM

$(RUNCPM_EXE): $(RUNCPM_SRC) vera-tests/serterm_handler.o vera_sys_font.o vera-tests/atari_nosyschk.cfg
	# Load RUNCPM low enough to fit even if resident drivers (VERA/FujiNet/etc.) lower MEMTOP.
	# Use a cc65 cfg without the SYSCHK chunk at $2E00.
	cl65 -t atari -C vera-tests/atari_nosyschk.cfg --start-addr 0x3000 -o $(RUNCPM_EXE) $(RUNCPM_SRC) vera-tests/serterm_handler.o vera_sys_font.o

$(RUNCPM80_EXE): $(RUNCPM_EXE) $(SYS8030) $(BUNDLE_VERA)
	$(PYTHON) $(BUNDLE_VERA) $(SYS8030) $(RUNCPM_EXE) $(RUNCPM80_EXE)

vera-tests/serterm_handler.o: vera-tests/serterm_handler.s
	$(CA65) -I . -o $@ $<

$(TEST_FX_EXE): $(TEST_FX_SRC) vera-tests/vera_detect.h
	cl65 -t atari --start-addr 0x5000 -o $(TEST_FX_EXE) $(TEST_FX_SRC)

# VERA IRQ hook on VIMIRQ (standalone, no VERA.SYS needed)
$(VERA_IRQ_OBJ): $(VERA_IRQ_SRC) vera_common.inc
	$(CA65) -I . -I vera-tests -o $@ $<

$(TEST_IRQ_EXE): $(TEST_IRQ_SRC) $(VERA_IRQ_OBJ) vera-tests/vera_irq.h vera-tests/vera_detect.h
	cl65 -t atari --start-addr 0x5000 -I vera-tests -o $(TEST_IRQ_EXE) $(TEST_IRQ_SRC) $(VERA_IRQ_OBJ)

# --- RMT player on POKEY + VERA PSG (standalone, no VERA.SYS needed) --------
#   make TESTRMT.COM RMT_SONG=vera-tests/rmt/music/other.rmt

RMT_DIR      = vera-tests/rmt
RMT_GEN      = $(RMT_DIR)/gen
RMT_SONG    ?= $(RMT_DIR)/music/gemx.rmt
RMT_SONGNAME = $(basename $(notdir $(RMT_SONG)))
# 4 for RMT4 (mono) modules, 8 for RMT8 (stereo): player, translator and C
# side are built for the module's track count (checked again at link time)
RMT_TRACKS  := $(shell $(PYTHON) -c "d=open('$(RMT_SONG)','rb').read(10); print(8 if d[6:10]==b'RMT8' else 4)")
TEST_RMT_EXE = TESTRMT.COM
TEST_RIO_EXE = TESTRIO.COM
RMT_ASM      = $(RMT_DIR)/rmtplayr.s $(RMT_DIR)/rmtvbi.s $(RMT_DIR)/psgrmt.s

$(RMT_GEN)/psgtab.s: $(RMT_DIR)/tools/mkpsgtab.py
	@mkdir -p $(RMT_GEN)
	$(PYTHON) $< $@

# one stamp per song name: switching RMT_SONG regenerates song.s even when
# the new .rmt file is older than the last song.s
RMT_SONG_STAMP = $(RMT_GEN)/song.$(RMT_SONGNAME).stamp

$(RMT_SONG_STAMP):
	@mkdir -p $(RMT_GEN)
	@rm -f $(RMT_GEN)/song.*.stamp
	@touch $@

$(RMT_GEN)/song.s: $(RMT_SONG) $(RMT_DIR)/tools/rmt2ca65.py $(RMT_SONG_STAMP)
	@mkdir -p $(RMT_GEN)
	$(PYTHON) $(RMT_DIR)/tools/rmt2ca65.py $(RMT_SONG) $@

$(TEST_RMT_EXE): $(RMT_DIR)/test_rmt.c $(RMT_DIR)/rmt.h $(RMT_ASM) $(RMT_DIR)/rmt_feat.inc $(RMT_DIR)/testrmt.cfg $(RMT_GEN)/psgtab.s $(RMT_GEN)/song.s vera_common.inc vera-tests/vera_detect.h
	cl65 -t atari -C $(RMT_DIR)/testrmt.cfg -I vera-tests -I $(RMT_DIR) \
	     --asm-include-dir . --asm-include-dir $(RMT_DIR) --asm-define RMT_VERA \
	     --asm-define RMT_TRACKS=$(RMT_TRACKS) -DRMT_TRACKS=$(RMT_TRACKS) \
	     -DRMT_SONG_NAME=\"$(RMT_SONGNAME)\" \
	     -o $@ $(RMT_DIR)/test_rmt.c $(RMT_ASM) $(RMT_GEN)/psgtab.s $(RMT_GEN)/song.s

# TESTRIO: same player while loading assets from disk through SIO (from
# AT2019/ATARI-Driver/PokeyATest). Own bootable disk, MyPicoDos autorun:
#   make disk4-rmtio.atr [RMT_SONG=...] [RIO_ASSET_SIZES="2048 16384"]
#   atari800 -nopatchall ... disk4-rmtio.atr   (real POKEY serial timing)
RIO_SRC         = $(RMT_DIR)/test_rio.c $(RMT_DIR)/dos2fs.c $(RMT_DIR)/sio.s
RIO_DISK        = .atrbuild/disk4
RIO_ASSET_SIZES ?= 2048 3500 5120 8000 12288 16384
RIO_BOOTDOS     ?= MyPicoDos406N

# asset sizes kept in a stamp file: changing them rebuilds header and disk
$(RMT_GEN)/assets.cfg: FORCE
	@mkdir -p $(RMT_GEN)
	@echo '$(RIO_ASSET_SIZES)' | cmp -s - $@ || echo '$(RIO_ASSET_SIZES)' > $@

$(RMT_GEN)/assets.h: $(RMT_GEN)/assets.cfg $(RMT_DIR)/tools/mkassets.py
	rm -rf $(RIO_DISK)
	mkdir -p $(RIO_DISK)
	$(PYTHON) $(RMT_DIR)/tools/mkassets.py --out $(RIO_DISK) --header $@ \
		--reserved $(TEST_RIO_EXE) --sizes $(RIO_ASSET_SIZES)

$(TEST_RIO_EXE): $(RIO_SRC) $(RMT_DIR)/dos2fs.h $(RMT_DIR)/rmt.h $(RMT_ASM) $(RMT_DIR)/rmt_feat.inc $(RMT_DIR)/testrmt.cfg $(RMT_GEN)/psgtab.s $(RMT_GEN)/song.s $(RMT_GEN)/assets.h vera_common.inc vera-tests/vera_detect.h
	cl65 -t atari -O -C $(RMT_DIR)/testrmt.cfg -I vera-tests -I $(RMT_DIR) -I $(RMT_GEN) \
	     --asm-include-dir . --asm-include-dir $(RMT_DIR) --asm-define RMT_VERA \
	     --asm-define RMT_TRACKS=$(RMT_TRACKS) -DRMT_TRACKS=$(RMT_TRACKS) \
	     -DRMT_SONG_NAME=\"$(RMT_SONGNAME)\" \
	     -o $@ $(RIO_SRC) $(RMT_ASM) $(RMT_GEN)/psgtab.s $(RMT_GEN)/song.s

# DOS 2.x sector links are 10 bit: the loader reads up to sector 1023
disk4-rmtio.atr: $(TEST_RIO_EXE) $(RMT_GEN)/assets.h $(RMT_DIR)/tools/atrcheck.py $(RMT_DIR)/tools/atrorder.py
	cp $(TEST_RIO_EXE) $(RIO_DISK)/
	rm -f $@
	$(DIR2ATR) -a -b $(RIO_BOOTDOS) $@ $(RIO_DISK)
	@$(PYTHON) $(RMT_DIR)/tools/atrcheck.py $@ 1023 || { rm -f $@; exit 1; }
	@# MyPicoDos autorun starts the first directory entry: the program
	@$(PYTHON) $(RMT_DIR)/tools/atrorder.py $@ $(TEST_RIO_EXE) || { rm -f $@; exit 1; }
	$(call copy_atr_to_fujinet,$@)

.PHONY: FORCE

# ATR image configuration
REQUIRED_TEST_EXES = TEST4.COM TEST6.COM TESTFX.COM

# FUJINET_SD_PATH can be set to the path of your Fujinet SD card
# Example: make atr FUJINET_SD_PATH=/media/user/FUJINET/
FUJINET_SD_PATH ?=

# DOS 2.0s master disk used as source to extract DOS.SYS/DUP.SYS.
# Note: DOS 2.0s itself doesn't support ED, but this still produces a bootable
# image; extra sectors are simply unused by DOS.
DOS20_ATR    = 810_Master_Disk_DOS_2.0s_1980_Atari.atr
DOS20_DIR    = .dos20
DOS20_EXTRACT = extract_dos20.py

$(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS: $(DOS20_ATR) $(DOS20_EXTRACT)
	$(PYTHON) $(DOS20_EXTRACT) $(DOS20_ATR) $(DOS20_DIR)

# Helper to copy ATR to FujiNet SD
define copy_atr_to_fujinet
	@if [ ! -z "$(FUJINET_SD_PATH)" ]; then \
		cp $(1) $(FUJINET_SD_PATH)/; \
		echo "Copied $(1) to $(FUJINET_SD_PATH)"; \
	fi
endef

# dir2atr sometimes writes a wrong flags byte in the last directory entries of
# a large Enhanced Density image (the file is there but DOS does not list it)
# and leaves VTOC1 wrong ("0 FREE SECTORS").  fix_atr_vtoc.py repairs the
# image and fails if the files do not fit in the 944 sectors VTOC1 can track.
FIX_ATR = vera-tests/tools/fix_atr_vtoc.py

# Build a bootable DOS 2.0s ED ATR containing RUNCPM.COM and both
# VERA8030.SYS (80x30) and VERA8060.SYS (80x60) drivers.
atr: $(TARGET) $(RUNCPM_EXE) $(TEST_FX_EXE) $(TEST_PLAYER_EXE) $(DEMO_SONG_BIN) $(SYS8030) $(SYS8060) $(SYS4030) $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(FIX_ATR)
	rm -rf $(ATRBUILD)
	mkdir -p $(ATRBUILD)
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(RUNCPM_EXE) $(TEST_FX_EXE) $(TEST_PLAYER_EXE) $(DEMO_SONG_BIN) $(SYS8030) $(SYS8060) $(SYS4030) $(ATRBUILD)/
	$(DIR2ATR) -E -b Dos20 $(ATR) $(ATRBUILD)
	$(PYTHON) $(FIX_ATR) $(ATR)
	@echo "ATR written to $(ATR)"
	$(call copy_atr_to_fujinet,$(ATR))

disk1-runcpm.atr: $(RUNCPM_EXE) $(SYS8030) $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(FIX_ATR)
	rm -rf .atrbuild/disk1
	mkdir -p .atrbuild/disk1
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(RUNCPM_EXE) $(SYS8030) .atrbuild/disk1/
	$(DIR2ATR) -E -b Dos20 $@ .atrbuild/disk1
	$(PYTHON) $(FIX_ATR) $@
	$(call copy_atr_to_fujinet,$@)

disk2-veratests-40x30.atr: TEST4.COM TESTGS4.COM TESTMAZ4.COM TESTMTX4.COM $(TEST_RMT_EXE) $(SYS4030) $(DOS20_DIR)/DOS.SYS $(FIX_ATR) $(DOS20_DIR)/DUP.SYS
	rm -rf .atrbuild/disk2_4030
	mkdir -p .atrbuild/disk2_4030
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS TEST4.COM TESTGS4.COM TESTMAZ4.COM TESTMTX4.COM $(TEST_RMT_EXE) $(SYS4030) .atrbuild/disk2_4030/
	$(DIR2ATR) -E -b Dos20 $@ .atrbuild/disk2_4030
	$(PYTHON) $(FIX_ATR) $@
	$(call copy_atr_to_fujinet,$@)

disk2-veratests-80x30.atr: TEST8.COM TESTGS8.COM TESTMAZ8.COM TESTMTX8.COM $(TEST_RMT_EXE) $(SYS8030) $(DOS20_DIR)/DOS.SYS $(FIX_ATR) $(DOS20_DIR)/DUP.SYS
	rm -rf .atrbuild/disk2_8030
	mkdir -p .atrbuild/disk2_8030
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS TEST8.COM TESTGS8.COM TESTMAZ8.COM TESTMTX8.COM $(TEST_RMT_EXE) $(SYS8030) .atrbuild/disk2_8030/
	$(DIR2ATR) -E -b Dos20 $@ .atrbuild/disk2_8030
	$(PYTHON) $(FIX_ATR) $@
	$(call copy_atr_to_fujinet,$@)

disk2-veratests-80x60.atr: TEST6.COM TESTGS6.COM TESTMAZ6.COM TESTMTX6.COM $(TEST_RMT_EXE) $(SYS8060) $(DOS20_DIR)/DOS.SYS $(FIX_ATR) $(DOS20_DIR)/DUP.SYS
	rm -rf .atrbuild/disk2_8060
	mkdir -p .atrbuild/disk2_8060
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS TEST6.COM TESTGS6.COM TESTMAZ6.COM TESTMTX6.COM $(TEST_RMT_EXE) $(SYS8060) .atrbuild/disk2_8060/
	$(DIR2ATR) -E -b Dos20 $@ .atrbuild/disk2_8060
	$(PYTHON) $(FIX_ATR) $@
	$(call copy_atr_to_fujinet,$@)

# Tests that do not need VERA.SYS (same files for every screen mode)
disk3-standalone.atr: $(TEST_FX_EXE) $(TEST_IRQ_EXE) $(TEST_PLAYER_EXE) $(DEMO_SONG_BIN) $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(FIX_ATR)
	rm -rf .atrbuild/disk3
	mkdir -p .atrbuild/disk3
	cp $(DOS20_DIR)/DOS.SYS $(DOS20_DIR)/DUP.SYS $(TEST_FX_EXE) $(TEST_IRQ_EXE) $(TEST_PLAYER_EXE) $(DEMO_SONG_BIN) $(wildcard $(DEMO_IMAGE_BIN)) .atrbuild/disk3/
	$(DIR2ATR) -E -b Dos20 $@ .atrbuild/disk3
	$(PYTHON) $(FIX_ATR) $@
	$(call copy_atr_to_fujinet,$@)

# === Cleanup ================================================================

clean: clean_objs
	rm -rf $(TARGET) $(ATR) $(ATRBUILD) $(LABELS) \
		$(BODY_LBL_A) $(BODY_LBL_B) \
		$(LOADER_LBL) \
		$(ALL_TEST_EXES) \
		_TEST.COM _TESTGS.COM _TESTMAZE.COM _TESTMTX.COM \
		$(VTM_PLAYER_OBJ) $(VU_PM_OBJ) $(VBM_DISPLAY_OBJ) $(VERA_IRQ_OBJ) $(DEMO_SONG_BIN) $(RMT_GEN) \
		$(SYS4030) $(SYS8030) $(SYS8060) \
		.dos20

cleanall: clean
	rm -rf $(MAPFILE_ROM) $(BODY_MAP_A) $(BODY_MAP_B) $(LOADER_MAP) \
		$(SYS4030) $(SYS8030) $(SYS8060)

install: $(TARGET)
	@mkdir -p $(ROMDIR)
	cp $(TARGET) $(ROMDIR)/$(TARGET)
	@echo "Installed to $(ROMDIR)/$(TARGET)"
