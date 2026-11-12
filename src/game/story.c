/* HUSH — prologue, operator's log and overheard chatter. All text original. */
#include "story.h"

const StoryCard story_prologue[PROLOGUE_CARDS] = {
    { "VESPER, 2146", "A privately administered city of forty-one million records. Fewer people. Nobody is sure how many fewer." },
    { "THE MUNICIPAL SIGNAL AUTHORITY", "Every broadcast in Vesper is licensed, metered and owned. Rain is a product. Silence is a subscription. The Authority keeps the air clean." },
    { "OPERATOR SEVEN", "You fly the night rota in a Moth: four fans, a scanner, a radio. You listen for things that should not be there." },
    { "61.7 MHz", "For eleven nights, on a frequency nobody owns, someone has been playing the city back to itself." },
    { "", "No transmitter has been found. No licence has been filed. The rain continues." },
    { "", "Nobody has complained. That is what worries the Authority." },
    { "DISPATCH", "Moth-7, you're cleared for the Civic Centre corridor. Routine work. Try not to listen too closely." },
};

const char *const story_log[12] = {
    "Logged the carrier as weather bleed. It wasn't. Weather doesn't pause between drops like someone breathing.",
    "The recorder had no owner. I keep thinking about who sits on a roof all night to keep one night of rain.",
    "Things go into the Archive. Nothing comes out. Except, tonight, a name: Ines Vale.",
    "Three silent flats. They weren't empty. People were listening so hard they forgot to make any noise.",
    "The tower doesn't send. It receives. Everyone here is receiving something and calling it their own.",
    "Her note said: I was never only the record. Hale read it twice and filed it under equipment fault.",
    "The grid hums the tune. You can't arrest a hum. You can only decide whether to hear it.",
    "A timetable for trains that will never come, still keeping time. I recognised myself in that more than I'd like.",
    "A drone learned her commute from her file and flew it for nine years. Is that remembering? Is that her?",
    "The lowest place in the city rings like a bell. I hovered there longer than procedure allows.",
    "Four fragments. None of them is her. All of them are hers. I don't know how to log that.",
    "Report filed: SOURCE NOT FOUND. Dispatch asked if I was all right. I said I'd never heard the city so clearly.",
};

static const char *const everyday[] = {
    "My silence allowance ran out at four. I just sat there humming.",
    "They've licensed the smell of rain now. Breathe through your mouth.",
    "WeatherHold says tonight's storm is a premium tier.",
    "Did your memories renew? Mine lapsed in March.",
    "I registered the tune I whistle. Cheaper than the fine.",
    "The wellbeing broadcast told me I was calm. I believed it for a minute.",
    "Don't look up. The Authority counts faces.",
    "Two noodles. No, the cheap broth. The licensed broth.",
    "My mother's file still pays rent. She doesn't.",
    "Three jobs and a capsule. Living the forecast.",
    "They predicted I'd be sad tonight, so I got the discount.",
    "Is that a Moth? Smile, you're being archived.",
};

static const char *const acts[5][4] = {
    { "There's a station between the stations. Sixty-one point seven. Just rain.",
      "Somebody's playing the rain back to us. Like we'd forgotten it.",
      "My radio picked up a storm that isn't on the forecast.",
      "Turn it down? I can't find the dial it's on." },
    { "Vale? There was a Vale on the ninth floor. Or there's a record of one.",
      "My neighbour stopped paying for silence. Now she just listens.",
      "The old choir tower hums at night. My kid says it's singing.",
      "They're taking the rooftop aerials down. They'll only build more." },
    { "Did you hear? Somebody knocked an Archive drone out of the sky.",
      "The grid's humming that tune. You can hear it in the kettle.",
      "Station Nine still announces trains. Nobody's on them. Everybody listens.",
      "My file says I was at work all night. I was here, listening." },
    { "They say the signal comes from under the water.",
      "The Authority's own pilots are tuning in. I saw one hovering, just listening.",
      "Hale's gone. Took leave. Nobody takes leave.",
      "If it's not from anywhere, who do we bill?" },
    { "They looked for where it came from. Turns out it comes from wherever it's heard.",
      "I turned the radio off and could still hear it. That's not a complaint.",
      "The silence tariff's still running. I've stopped minding it.",
      "Funny. The city sounds different when you stop owning it." },
};

int story_act(uint32_t done) {
    int n = 0;
    for (int i = 0; i < 12; i++) n += (done >> i) & 1;
    if (n >= 12) return 4;
    if (n >= 9) return 3;
    if (n >= 6) return 2;
    if (n >= 3) return 1;
    return 0;
}

const char *story_chatter(int act, uint32_t pick) {
    if (act < 0) act = 0;
    if (act > 4) act = 4;
    const int ne = (int)(sizeof(everyday) / sizeof(everyday[0]));
    /* roughly half of what you overhear is about the signal, the rest is ordinary life in Vesper */
    if (pick & 1) return acts[act][(pick >> 1) % 4];
    return everyday[(pick >> 1) % (uint32_t)ne];
}
