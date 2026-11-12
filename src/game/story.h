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
/* what people say when the Moth hovers over them */
const char *story_reaction(uint32_t pick);
/* the 32 hidden transmissions (signal hunting), indexed by global signal id */
extern const char *const story_intercept[32];
/* radio lines from municipal patrol craft that notice you */
const char *story_patrol(uint32_t pick);

#endif
