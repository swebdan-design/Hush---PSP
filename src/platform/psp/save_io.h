/* HUSH — Memory Stick persistence (see save_io.c for the safe-write procedure). */
#ifndef HUSH_SAVE_IO_H
#define HUSH_SAVE_IO_H

#include "../../game/progress.h"

#define SAVE_PATH "hush.sav"
#define SAVE_TMP "hush.tmp"
#define SAVE_BAK "hush.bak"

int save_io_load(Progress *p, char *msg, int msg_len);
int save_io_store(const Progress *p, char *msg, int msg_len);

#endif
