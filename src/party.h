/* Cue Party: friends in different homes watch the same thing at the same moment, each on their
 * own TV and from their own sources. The party is a room on the key relay (relay/src/party.js):
 * who is in, what is on, and where playback is; never a link, the video or an account.
 *
 * A thread of its own waits on the room (a request the relay holds until something changes,
 * up to 25 s), so the six connections addons, artwork and playback share are never held. What
 * this TV does (play, pause, seek, buffering, ready) goes to the room as it happens; what the
 * room says comes back as a plan for this TV's player (party_plan), set by the room's clock so
 * every TV acts at the same moment. See docs/CUE-PARTY.md. */
#ifndef SNTV_PARTY_H
#define SNTV_PARTY_H

#define PARTY_MEMBERS 8

typedef enum { PARTY_OFF, PARTY_CONNECTING, PARTY_IN, PARTY_ENDED } PartyState;

typedef struct { char id[12], name[28], device[44]; int ready, buffering; } PartyMember;
/* Where playback is: playing from pos (seconds) since at (the room's clock, ms), or stopped at
 * pos. A play or seek is set a little ahead (lead_ms), so at can be later than now. */
typedef struct { int playing; double pos, at; } PartyPlay;

typedef struct {
    PartyState state;
    char code[8], me[12];      /* the party's code, this TV's member id */
    unsigned v;                /* the room's version: changes with everything below */
    PartyMember members[PARTY_MEMBERS]; int n_members;
    int has_title; char meta[44], video[84], title[124], type[20];   /* what is on: a title's id, the episode's video id */
    int has_play; PartyPlay play;
    int lead_ms;
    struct { unsigned n; char who[28], by[12], what[16], title[124], emoji[12]; } notice;   /* the last thing someone did (by: their member id); n changes with each */
    char error[160];           /* why it ended, or "" */
    int reconnecting;          /* the room has not answered for a while */
} PartyView;

/* Start a party, or join one by its code ("7CKPVV", any case). The name is shown to the others;
 * device is the kind of device ("LG TV", "Phone"). Answers come in party_view. */
void party_start(const char *name, const char *device);
void party_join(const char *code, const char *name, const char *device);
void party_leave(void);
void party_shutdown(void);   /* at exit: leave, and stop the thread */

int  party_active(void);     /* connecting or in */
unsigned party_session(void);   /* changes with each party started or joined */
void party_view(PartyView *out);
const char *party_code(void);   /* "" when none */

/* What this TV did, for the room. pos in seconds. */
void party_load(const char *meta, const char *video, const char *title, const char *type, double pos);
void party_play(double pos);
void party_pause(double pos);
void party_seek(double pos);
void party_ready(int ready);
void party_buffering(int on);
void party_react(const char *emoji);

/* The room's clock now (ms since 1970), as this TV best knows it. */
double party_now_ms(void);

/* --- keeping this TV's player with the room ------------------------------------------- */

typedef struct {
    int loaded;      /* the room's video is playing or paused here (its first frame shown) */
    int playing;     /* playing, not paused */
    int buffering;
    double pos;      /* seconds */
} PartyLocal;

/* What the player should do now: pause, play, seek to (>= 0), and the speed (0: leave it). */
typedef struct { int pause, play; double seek, rate; } PartyCmd;

/* The plan's memory between frames: zero it when a new video starts. */
typedef struct {
    double hold_until;     /* the last command settles until then (room ms) */
    double rate;           /* the speed set (0 or 1: normal) */
    int rate_ok;           /* 0 not known yet, 1 the speed changes, -1 it does not: seeks only */
    double nudge_at, nudge_drift;   /* when a speed nudge began, and the difference then */
    double seek_at, seek_to;        /* a seek under way: sent then, to there (room ms, s) */
    double seek_cost;      /* seconds a seek here costs (stands still), learned; 0 unknown */
    unsigned seeks, nudges;
} PartySync;

#define PARTY_SEEK_DRIFT 2.0   /* off by more than this: a seek, not a nudge (s) */
#define PARTY_NUDGE_DRIFT 0.25 /* off by more than this: played away at a slightly other speed (s) */
#define PARTY_NUDGE_DONE 0.08
#define PARTY_NUDGE_SLOW 0.95
#define PARTY_NUDGE_FAST 1.05

/* One step: the room's playback at now (room ms) and this TV's; fills out. Pure: no I/O. */
void party_plan(const PartyPlay *room, double now, const PartyLocal *l, PartySync *s, PartyCmd *out);

#endif
