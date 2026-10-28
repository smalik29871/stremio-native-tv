/* Cue Party in the app (party.h is the room): the player tells the party what the viewer does
 * and keeps itself with the room, and the app follows when someone puts something else on. */
#ifndef SNTV_PARTY_UI_H
#define SNTV_PARTY_UI_H
#include "../party.h"
#include "../addon/addon.h"
#include "../video/video.h"

/* A player's part in the party (one per player, zeroed with it). */
typedef struct {
    int trailer;
    unsigned session;          /* the party it was opened in (0: none) */
    int joined;                /* it has seen the room since */
    int ready_sent, last_pop, buf_on;
    float buf_since, own_seek_at, played_at, logged_at;
    PartySync sync;
} PartyPlayer;

/* The player opened this video (from start_s). */
void party_player_open(PartyPlayer *pp, const char *video_id, int trailer);
/* Each frame: started (its first frame shown), seek: the player's own seek. */
void party_player_tick(PartyPlayer *pp, const MetaPreview *meta, const char *video_id, const char *title,
                       const VideoInfo *vi, int started, void (*seek)(double));
/* The viewer moved the position (the player's seek): the others go there too. */
void party_player_seeked(PartyPlayer *pp, double pos);
void party_player_close(PartyPlayer *pp);

/* Each frame, from the main loop: the party's notices, and following what it puts on. */
void party_app_tick(void);

/* "Alex paused", "Sam put on The Bear S2 E3" for a notice; 0 when it is not worth one. */
int party_notice_text(const PartyView *v, char *out, size_t n);

#endif
