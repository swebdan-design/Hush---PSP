/* HUSH — story content outside the missions: prologue, operator's log,
 * overheard street chatter that changes as the investigation unfolds. */
#ifndef HUSH_STORY_H
#define HUSH_STORY_H

#include <stdint.h>

typedef struct { const char *heading; const char *text; } StoryCard;

#define PROLOGUE_CARDS 7
extern const StoryCard story_prologue[PROLOGUE_CARDS];
/* one entry per mission, written by Operator Seven after it */
extern const char *const story_log[12];

/* act 0..4 from the number of completed missions */
int story_act(uint32_t missions_done);
/* a line a pedestrian might say, chosen from the act's pool plus the city's everyday satire */
const char *story_chatter(int act, uint32_t pick);

#endif
