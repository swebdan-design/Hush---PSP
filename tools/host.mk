# HUSH — host (desktop) build: audio renderer and tests.
# Included from the top-level Makefile.

HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-fast-math
HOST_BUILD := build/host

AUDIO_SRC := src/audio/dsp.c src/audio/synth.c src/audio/fx.c src/audio/theory.c \
             src/audio/profiles.c src/audio/composer.c src/audio/music_engine.c
GAME_SHARED_SRC := $(wildcard src/game/*.c)

HOST_AUDIO_OBJ := $(patsubst src/%.c,$(HOST_BUILD)/%.o,$(AUDIO_SRC))
HOST_GAME_OBJ := $(patsubst src/%.c,$(HOST_BUILD)/%.o,$(GAME_SHARED_SRC))

$(HOST_BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DHUSH_HOST -MMD -MP -c $< -o $@

$(HOST_BUILD)/hush-render: tools/audio-renderer/hush_render.c $(HOST_AUDIO_OBJ)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DHUSH_HOST $^ -o $@ -lm

$(HOST_BUILD)/hush-tests: $(wildcard tests/*.c) $(HOST_AUDIO_OBJ) $(HOST_GAME_OBJ)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DHUSH_HOST $(filter %.c,$^) $(HOST_AUDIO_OBJ) $(HOST_GAME_OBJ) -o $@ -lm

-include $(HOST_AUDIO_OBJ:.o=.d) $(HOST_GAME_OBJ:.o=.d)
