# HUSH — PSP build via the PSPSDK build.mak. Invoked from the top-level Makefile.
BUILD ?= build/psp
TARGET = $(BUILD)/hush
SRC := $(wildcard src/audio/*.c) $(wildcard src/game/*.c) $(wildcard src/render/*.c) \
       $(wildcard src/ui/*.c) $(wildcard src/platform/psp/*.c)
OBJS := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC))

CFLAGS = -O2 -G0 -Wall -Wno-unused-function -Wno-format -fsingle-precision-constant -ffast-math -std=gnu11 -DHUSH_PSP $(EXTRA_DEFS) -MMD -MP
LIBS = -lpspfpu -lpspgum -lpspgu -lpsppower -lpsprtc -lpspaudio -lm
EXTRA_TARGETS = $(BUILD)/EBOOT.PBP
PSP_EBOOT_TITLE = HUSH
PSP_EBOOT_ICON = artwork/psp/ICON0.PNG
PSP_EBOOT_PIC1 = artwork/psp/PIC1.PNG
PSP_EBOOT = $(BUILD)/EBOOT.PBP
PSP_EBOOT_SFO = $(BUILD)/PARAM.SFO
PSP_FW_VERSION = 500

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# The composer must produce identical decisions on PSP and host: strict IEEE
# single-precision arithmetic (no fast-math reassociation, no FP contraction).
STRICT_FP = -fno-fast-math -ffp-contract=off
$(BUILD)/obj/audio/composer.o $(BUILD)/obj/audio/theory.o $(BUILD)/obj/audio/profiles.o $(BUILD)/obj/audio/music_engine.o $(BUILD)/obj/game/world.o $(BUILD)/obj/game/traffic.o: CFLAGS += $(STRICT_FP)

# the per-sample DSP loops: -O3 unswitches the per-voice waveform/filter branches out of the sample loop
$(BUILD)/obj/audio/synth.o $(BUILD)/obj/audio/fx.o $(BUILD)/obj/audio/dsp.o: CFLAGS += -O3

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
