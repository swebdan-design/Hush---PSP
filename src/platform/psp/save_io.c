/* HUSH — save file I/O on the Memory Stick.
 *
 * The save lives next to EBOOT.PBP (ms0:/PSP/GAME/HUSH/hush.sav). Writes go to
 * hush.tmp first; the previous save is kept as hush.bak; the temp file is then
 * renamed into place, so a power loss mid-write never destroys the last good
 * save. Loading falls back to hush.bak if hush.sav fails validation. */
#include "save_io.h"
#include <stdio.h>
#include <string.h>

static int read_file(const char *path, uint8_t *buf, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

int save_io_load(Progress *p, char *msg, int msg_len) {
    static uint8_t buf[SAVE_MAX_BYTES + 16];
    const char *err = NULL;
    int n = read_file(SAVE_PATH, buf, sizeof(buf));
    if (n > 0 && progress_deserialize(p, buf, n, &err)) { snprintf(msg, (size_t)msg_len, "SAVE LOADED"); return 1; }
    const char *first = n < 0 ? "no save found" : (err ? err : "unreadable");
    n = read_file(SAVE_BAK, buf, sizeof(buf));
    if (n > 0 && progress_deserialize(p, buf, n, &err)) { snprintf(msg, (size_t)msg_len, "BACKUP SAVE LOADED (%s)", first); return 1; }
    progress_defaults(p);
    snprintf(msg, (size_t)msg_len, "%s", first);
    return 0;
}

int save_io_store(const Progress *p, char *msg, int msg_len) {
    static uint8_t buf[SAVE_MAX_BYTES + 16];
    int n = progress_serialize(p, buf, sizeof(buf));
    if (n <= 0) { snprintf(msg, (size_t)msg_len, "SAVE FAILED (serialise)"); return 0; }
    FILE *f = fopen(SAVE_TMP, "wb");
    if (!f) { snprintf(msg, (size_t)msg_len, "SAVE FAILED (cannot open)"); return 0; }
    int w = (int)fwrite(buf, 1, (size_t)n, f);
    int ok = fclose(f) == 0 && w == n;
    if (!ok) { remove(SAVE_TMP); snprintf(msg, (size_t)msg_len, "SAVE FAILED (write)"); return 0; }
    remove(SAVE_BAK);
    rename(SAVE_PATH, SAVE_BAK);
    if (rename(SAVE_TMP, SAVE_PATH) != 0) { snprintf(msg, (size_t)msg_len, "SAVE FAILED (rename)"); return 0; }
    snprintf(msg, (size_t)msg_len, "PROGRESS SAVED");
    return 1;
}
