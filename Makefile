# PokeRush : roguelike / boss rush Pokémon pour la NumWorks N0120 (EADK).
#
#   make            build/pokerush.nwa
#   make check      lie l'app comme la calculatrice (build/pokerush.bin), vérifie flash et RAM
#   make run        installe sur la calculatrice branchée en USB (nwlink)
#   make assets     régénère build/gen/ depuis assets/ et data/ (Python 3 + Pillow)
#   make top        les 30 plus gros symboles (où part la flash)
#   make sim        version PC (build/sim/pokerush) : la fenêtre, la manette et le son (voir sim/), ou
#                   des captures d'écran avec SIM_SCRIPT ; la version à distribuer : tools/build_pc.py
#   make clean
#   make DEBUG=1    -Og -g, sans LTO (pour lire le .map et les symboles)
#
# Il faut un shell POSIX (MSYS2, Git Bash ou WSL sous Windows), arm-none-eabi-gcc avec
# newlib-nano, Node.js pour nwlink, Python 3 et Pillow.

Q ?= @
APP     = pokerush
CC      = arm-none-eabi-gcc
STRIP   = arm-none-eabi-strip
SIZE    = arm-none-eabi-size
NM      = arm-none-eabi-nm
NWLINK ?= npx --yes -- nwlink@1.0.0
PY     ?= python3

BUILD = build
GEN   = $(BUILD)/gen
OBJD  = $(BUILD)/obj

# Flash : espace des apps d'Epsilon <= 26 (0x90200000..0x903F0000). Epsilon 26.4 sur N0120
# en donne plus (2,44 Mo), mais on vise le plus petit pour s'installer partout.
FLASH_BUDGET = 2031616
# RAM : Epsilon 23.2 donne 148 928 octets aux apps (pile à part). On garde ~17 Ko de marge.
RAM_BUDGET = 131072

# ---------------------------------------------------------------------------
# Sources : src/ à la main, build/gen/ produit par tools/ (étapes 3 et 6)

SRC = $(wildcard src/*.c src/*/*.c)

ASSET_PNG = $(wildcard assets/*.png assets/*/*.png assets/*/*/*.png assets/*/*.gif)
DATA_SRC  = $(wildcard data/*.csv data/*.json)
GEN_SRC   =
GEN_STAMP =
ifneq ($(wildcard tools/convert_assets.py),)
GEN_SRC   += $(GEN)/assets.c
GEN_STAMP += $(GEN)/.assets
endif
ifneq ($(wildcard tools/gen_data.py),)
GEN_SRC   += $(GEN)/data.c
GEN_STAMP += $(GEN)/.data
endif

OBJ = $(patsubst %.c,$(OBJD)/%.o,$(SRC)) $(patsubst $(GEN)/%.c,$(OBJD)/gen/%.o,$(GEN_SRC))

# ---------------------------------------------------------------------------
# Options

EADK_CFLAGS := $(shell $(NWLINK) eadk-cflags-device)

CFLAGS  = -std=c11 $(EADK_CFLAGS) -Isrc -I$(GEN)
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wshadow -Wstack-usage=2048
# pas de flottants dans le moteur ; s'il en reste, en simple précision et sans errno
CFLAGS += -fno-math-errno -fsingle-precision-constant
# une section par fonction / donnée, pour que --gc-sections jette tout ce qui ne sert pas
CFLAGS += -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
# GCC ne remplace pas nos boucles par des appels memcpy/memset (src/platform/libc.c les fournit)
CFLAGS += -fno-tree-loop-distribute-patterns

ifeq ($(DEBUG),1)
CFLAGS += -Og -g -DDEBUG=1
else
CFLAGS += -Os -DNDEBUG
# tout le programme optimisé d'un bloc : inlining entre fichiers, tables inutilisées supprimées
CFLAGS += -flto -flto-partition=one -fno-fat-lto-objects -fwhole-program -fvisibility=internal
endif

# Une .nwa est un objet relogeable : le chargeur d'Epsilon fait l'édition de liens finale.
LDFLAGS  = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel -Wl,-Map=$(BUILD)/$(APP).map

# ---------------------------------------------------------------------------

.PHONY: all build check run assets top clean
all build: $(BUILD)/$(APP).nwa

$(BUILD)/$(APP).elf: $(OBJ) $(BUILD)/icon.o
	@echo "LD      $@"
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD)/$(APP).nwa: $(BUILD)/$(APP).elf
	$(Q)$(STRIP) --strip-unneeded $< -o $@
	$(Q)$(SIZE) $@

$(OBJD)/%.o: %.c | $(GEN_STAMP)
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJD)/gen/%.o: $(GEN)/%.c
	@mkdir -p $(@D)
	@echo "CC      $<"
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/icon.o: assets/icon.png
	@mkdir -p $(@D)
	@echo "ICON    $<"
	$(Q)$(NWLINK) png-icon-o $< $@

# Fichiers générés : un tampon par outil, les .h/.c qu'il produit en dépendent.
$(GEN)/assets.c: $(GEN)/.assets ;
$(GEN)/.assets: tools/convert_assets.py tools/expansion.py assets/manifest.json data/roster.csv $(ASSET_PNG)
	@mkdir -p $(@D)
	@echo "ASSETS  $(GEN)"
	$(Q)$(PY) tools/convert_assets.py assets $(GEN)
	@touch $@

$(GEN)/data.c: $(GEN)/.data ;
$(GEN)/.data: tools/gen_data.py tools/expansion.py $(DATA_SRC)
	@mkdir -p $(@D)
	@echo "DATA    $(GEN)"
	$(Q)$(PY) tools/gen_data.py data $(GEN)
	@touch $@

assets:
ifneq ($(strip $(GEN_STAMP)),)
	$(Q)rm -f $(GEN_STAMP)
	$(Q)$(MAKE) --no-print-directory $(GEN_STAMP)
else
	@echo "Rien à générer : tools/convert_assets.py et tools/gen_data.py n'existent pas encore."
endif

check: $(BUILD)/$(APP).nwa
	$(Q)$(NWLINK) nwa-bin $< $(BUILD)/$(APP).bin
	$(Q)n=$$(wc -c < $(BUILD)/$(APP).bin); \
	  echo "FLASH   $$n / $(FLASH_BUDGET) octets"; \
	  test $$n -le $(FLASH_BUDGET) || { echo "ERREUR : budget flash dépassé"; exit 1; }
	$(Q)$(SIZE) $< | awk 'NR == 2 { ram = $$2 + $$3; \
	  printf "RAM     %d / %d octets (data + bss)\n", ram, $(RAM_BUDGET); \
	  if (ram > $(RAM_BUDGET)) { print "ERREUR : budget RAM dépassé"; exit 1 } }'

run: $(BUILD)/$(APP).nwa
	$(Q)$(NWLINK) install-nwa $<

top: $(BUILD)/$(APP).elf
	$(Q)$(NM) --size-sort -S -C $< | tail -n 30

# Version PC : sim/ remplace l'EADK (écran en mémoire, clavier scripté, captures PPM).
HOSTCC ?= cc
SIM_CFLAGS = -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter -Isim -Isrc -I$(GEN)
SIM_LIBS =
ifeq ($(OS),Windows_NT)
# les champs de bits doivent être rangés comme sur la calculatrice (disposition GCC, pas MS)
SIM_CFLAGS += -mno-ms-bitfields
SIM_LIBS += -luser32 -lgdi32
endif
.PHONY: sim
sim: $(BUILD)/sim/$(APP)
$(BUILD)/sim/$(APP): $(SRC) $(GEN_SRC) $(wildcard sim/*.c sim/*.h) $(wildcard src/*/*.h) | $(GEN_STAMP)
	@mkdir -p $(@D)
	@echo "SIM     $@"
	$(Q)$(HOSTCC) $(SIM_CFLAGS) -DPOKERUSH_PC $(SRC) $(GEN_SRC) $(wildcard sim/*.c) $(SIM_LIBS) -lwinmm -o $@

# Tests sur PC : la sauvegarde (Base64, fichiers d'Epsilon, pokemon.py) et les données générées.
.PHONY: test
test: $(BUILD)/sim/test_save $(BUILD)/sim/test_data $(BUILD)/sim/test_battle $(BUILD)/sim/test_trainer
	$(Q)mkdir -p $(BUILD)/sim/test && SIM_OUT=$(BUILD)/sim/test SIM_SCRIPT=x $(BUILD)/sim/test_save
	$(Q)$(BUILD)/sim/test_data
	$(Q)$(BUILD)/sim/test_battle
	$(Q)$(BUILD)/sim/test_trainer
$(BUILD)/sim/test_trainer: tests/test_trainer.c src/game/trainer.c src/game/ai.c src/game/battle.c $(GEN)/data.c $(GEN)/assets.c | $(GEN_STAMP)
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) $(SIM_CFLAGS) -O2 $^ -o $@
$(BUILD)/sim/test_battle: tests/test_battle.c src/game/battle.c $(GEN)/data.c | $(GEN_STAMP)
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) $(SIM_CFLAGS) -O2 $^ -o $@
$(BUILD)/sim/test_save: tests/test_save.c src/core/base64.c src/core/save.c src/platform/storage.c $(GEN)/data.c sim/eadk_host.c | $(GEN_STAMP)
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) $(SIM_CFLAGS) $^ $(SIM_LIBS) -o $@
$(BUILD)/sim/test_data: tests/test_data.c src/game/battle.c $(GEN)/data.c | $(GEN_STAMP)
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) $(SIM_CFLAGS) $^ -o $@

clean:
	$(Q)rm -rf $(BUILD)

-include $(OBJ:.o=.d)
