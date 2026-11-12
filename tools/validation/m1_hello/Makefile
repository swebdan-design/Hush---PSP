TARGET = hush_m1
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspgum -lpspgu -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = HUSH M1 Probe
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
