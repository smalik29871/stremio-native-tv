/* See party_ui.h. */
#include "party_ui.h"
#include "../app.h"
#include "../log.h"
#include "input.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BUFFERING_TELL 4.0f   /* stalled this long while playing: the others wait (pausing them costs them too, so not for a short one) */
#define SETTLING 10.0f        /* after a seek or a start here, its own buffering is not a stall to wait for */
#define FOLLOWED 4

static char g_playing[84];                /* the player's video now ("" none) */
static char g_followed[FOLLOWED][84];     /* the room's videos this TV opened lately */
static int g_applying;                    /* the party is moving the player: not the viewer */
static unsigned g_notice_seen, g_session_seen;
static PartyState g_state_seen;

void player_party_tick(Screen *player);   /* screen_player.c */

static int followed(const char *video)
{
    for (int i = 0; i < FOLLOWED; i++) if (g_followed[i][0] && !strcmp(g_followed[i], video)) return 1;
    return 0;
}

static void note_followed(const char *video)
{
    if (followed(video)) return;
    memmove(g_followed[1], g_followed[0], sizeof g_followed[0] * (FOLLOWED - 1));
    snprintf(g_followed[0], sizeof g_followed[0], "%s", video);
}

void party_player_open(PartyPlayer *pp, const char *video_id, int trailer)
{
    memset(pp, 0, sizeof *pp);
    pp->trailer = trailer;
    pp->last_pop = -1;
    if (trailer) return;
    snprintf(g_playing, sizeof g_playing, "%s", video_id ? video_id : "");
    pp->session = party_active() ? party_session() : 0;
}

void party_player_close(PartyPlayer *pp)
{
    if (pp->trailer) return;
    if (pp->sync.rate && pp->sync.rate != 1) video_set_rate(1.0);
    g_playing[0] = 0;
}

static void say_seconds(double s, char *out, size_t n)
{
    int t = s > 0 ? (int)s : 0;
    if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

void party_player_tick(PartyPlayer *pp, const MetaPreview *meta, const char *video_id, const char *title,
                       const VideoInfo *vi, int started, void (*seek)(double))
{
    if (pp->trailer || !party_active()) return;
    PartyView v;
    party_view(&v);
    if (v.state != PARTY_IN) return;
    if (!pp->joined) {   /* first sight of the room from this player */
        pp->joined = 1;
        pp->last_pop = video_paused_on_purpose();
        /* this TV puts on what it plays when nothing is on yet, or when the viewer chose it during
           the party (not when it opened something the room had on earlier: then it follows) */
        int mine = !v.has_title || (pp->session == party_session() && strcmp(v.video, video_id) && !followed(video_id));
        if (mine) {
            party_load(meta->id, video_id, title, meta->type, vi->position > 1 ? vi->position : 0);
            note_followed(video_id);
            party_view(&v);
        }
    }
    if (!v.has_title || strcmp(v.video, video_id)) return;   /* the room has something else on: the app opens it */

    if (started && !pp->ready_sent) { pp->ready_sent = 1; party_ready(1); LOGI("party: this TV is ready at %.1f s", vi->position); }

    int pop = video_paused_on_purpose();   /* the viewer's pause and play (and the app's: a break, the background) */
    if (pop != pp->last_pop) {
        if (pp->last_pop >= 0 && started) {
            if (pop) party_pause(vi->position); else { party_play(vi->position); pp->played_at = g_app.time; }
            LOGI("party: this TV %s at %.1f s", pop ? "paused" : "pressed play", vi->position);
        }
        pp->last_pop = pop;
        party_view(&v);
    }

    float now = g_app.time;
    if (started && vi->buffering && v.has_play && v.play.playing && now - pp->own_seek_at > SETTLING && now - pp->played_at > SETTLING) {   /* a stall: the others wait for it */
        if (!pp->buf_since) pp->buf_since = now;
        if (!pp->buf_on && now - pp->buf_since > BUFFERING_TELL) { pp->buf_on = 1; party_buffering(1); LOGI("party: this TV is buffering; the others wait"); }
    } else {
        pp->buf_since = 0;
        if (pp->buf_on && !vi->buffering) { pp->buf_on = 0; party_buffering(0); LOGI("party: this TV has caught up"); }
    }

    if (!v.has_play) return;
    PartyLocal l = { .loaded = started, .playing = vi->state == VIDEO_PLAYING && !pop, .buffering = vi->buffering, .pos = vi->position };
#if defined(SNTV_PLATFORM_WEBOS) || defined(SNTV_PLATFORM_TIZEN)
    l.no_rate = 1;   /* a TV's player may buffer again at a speed change: small differences stay, larger ones are a seek */
#endif
    double rnow = party_now_ms();
    PartyCmd c;
    double was_cost = pp->sync.seek_cost;
    party_plan(&v.play, rnow, &l, &pp->sync, &c);
    if (pp->sync.seek_cost != was_cost && pp->sync.seek_cost > 0) LOGI("party: a seek here costs %.2f s", pp->sync.seek_cost);
    double target = v.play.playing && rnow > v.play.at ? v.play.pos + (rnow - v.play.at) / 1000 : v.play.pos;
    if (!c.pause && !c.play && c.seek < 0 && !c.rate) {
        if (now - pp->logged_at > 60 && l.playing && v.play.playing) { pp->logged_at = now; LOGI("party: %+.2f s from the room", vi->position - target); }
        return;
    }
    g_applying = 1;
    char at[16];
    if (c.pause) video_pause();
    if (c.seek >= 0) {
        say_seconds(c.seek, at, sizeof at);
        LOGI("party: %.2f s %s the room: to %s (%.1f s)", fabs(vi->position - target), vi->position < target ? "behind" : "ahead of", at, c.seek);
        seek(c.seek);
        pp->own_seek_at = now;
    }
    if (c.play) { video_play(); pp->played_at = now; }
    if (c.rate > 0) {
        video_set_rate(c.rate);
        if (c.rate != 1) LOGI("party: %.2f s %s the room: playing at %.2fx", fabs(vi->position - target), vi->position < target ? "behind" : "ahead of", c.rate);
        else LOGI("party: back at normal speed, %+.2f s from the room%s", vi->position - target, pp->sync.rate_ok < 0 ? " (this player keeps its speed: seeks only)" : "");
    }
    if (c.pause || c.play) LOGI("party: %s at %.1f s, as the room says", c.pause ? "paused" : "playing", vi->position);
    g_applying = 0;
    pp->last_pop = video_paused_on_purpose();   /* the party's own pause or play is not the viewer's */
}

void party_player_seeked(PartyPlayer *pp, double pos)
{
    if (g_applying || pp->trailer || !pp->joined || !party_active()) return;
    if (!party_code()[0]) return;
    party_seek(pos);
    pp->own_seek_at = g_app.time;   /* its buffering after it is no stall to wait for */
    LOGI("party: this TV moved to %.1f s", pos);
}

int party_notice_text(const PartyView *v, char *out, size_t n)
{
    const char *who = v->notice.who[0] ? v->notice.who : "Someone", *w = v->notice.what;
    char at[16];
    say_seconds(v->play.pos, at, sizeof at);
    if (!strcmp(w, "join")) snprintf(out, n, "%s joined the party", who);
    else if (!strcmp(w, "leave")) snprintf(out, n, "%s left the party", who);
    else if (!strcmp(w, "gone")) snprintf(out, n, "A TV dropped out of the party");
    else if (!strcmp(w, "pause")) snprintf(out, n, "%s paused", who);
    else if (!strcmp(w, "play")) snprintf(out, n, "%s pressed play", who);
    else if (!strcmp(w, "seek")) snprintf(out, n, "%s jumped to %s", who, at);
    else if (!strcmp(w, "buffering")) snprintf(out, n, "Waiting for %s to catch up", who);
    else if (!strcmp(w, "load")) snprintf(out, n, "%s put on %s", who, v->notice.title[0] ? v->notice.title : "something");
    else return 0;
    return 1;
}

void party_app_tick(void)
{
    int active = party_active();   /* (also sends a seek that has settled) */
    PartyView v;
    if (!active) {
        if (g_state_seen == PARTY_IN || g_state_seen == PARTY_CONNECTING) {
            party_view(&v);
            if (v.state == PARTY_ENDED && v.error[0]) app_toast("%s", v.error);
            g_state_seen = v.state;
        }
        return;
    }
    party_view(&v);
    if (v.state == PARTY_IN && g_session_seen != party_session()) {   /* in a new party: the room's title is new to us */
        g_session_seen = party_session();
        g_notice_seen = v.notice.n;
        memset(g_followed, 0, sizeof g_followed);
        if (v.has_title && g_playing[0] && !strcmp(g_playing, v.video)) note_followed(v.video);
    }
    static int reconnecting;
    if (v.state == PARTY_IN && v.reconnecting != reconnecting) {
        reconnecting = v.reconnecting;
        app_toast("%s", reconnecting ? "The party isn't answering; trying again" : "Back in the party");
    }
    g_state_seen = v.state;
    if (v.state != PARTY_IN) return;
    if (v.notice.n != g_notice_seen) {
        g_notice_seen = v.notice.n;
        g_app.last_input = g_app.time;   /* someone in the party is there and doing things: no screensaver here meanwhile */
        char text[200];
        if (strcmp(v.notice.by, v.me) && party_notice_text(&v, text, sizeof text)) app_toast("%s", text);
    }
    {   /* a start waits for a TV still loading or catching up: say which */
        static char waiting[28];
        static float since;
        static int told;
        const char *who = "";
        for (int i = 0; i < v.n_members && v.has_title && !(v.has_play && v.play.playing); i++)
            if (strcmp(v.members[i].id, v.me) && (!v.members[i].ready || v.members[i].buffering)) { who = v.members[i].name; break; }
        if (strcmp(who, waiting)) { snprintf(waiting, sizeof waiting, "%s", who); since = g_app.time; told = 0; }
        if (*who && !told && g_app.time - since > 1.5f) { told = 1; app_toast("Waiting for %s\xE2\x80\xA6", who); }   /* (not for the moment its answer is on its way) */
    }
    for (int i = g_app.depth - 2; i >= 0; i--)   /* a player under another screen (the screensaver, a menu) keeps in step all the same */
        if (g_app.stack[i] && g_app.stack[i]->name && !strcmp(g_app.stack[i]->name, "player")) { if (g_app.stack[i] != g_app.pip) player_party_tick(g_app.stack[i]); break; }
    {   /* playing again while this TV's screensaver is up (the party paused it a while): it goes, and the player under it plays on */
        Screen *top = app_top();
        if (top && top->name && !strcmp(top->name, "ambient") && top->key && v.has_play && v.play.playing && g_playing[0] && !strcmp(g_playing, v.video)) {
            LOGI("party: playing again; the screensaver goes");
            top->key(top, KEY_BACK);
        }
    }
    if (v.has_title && v.video[0] && !followed(v.video)) {   /* something else is on: this TV opens it too */
        note_followed(v.video);
        if (strcmp(g_playing, v.video)) {
            MetaPreview m = { .id = v.meta, .type = v.type[0] ? v.type : "movie", .name = v.title };
            LOGI("party: the room put on %s; opening it here", v.video);
            const Screen *top = app_top();
            if (top && top->name && (!strcmp(top->name, "player") || !strcmp(top->name, "streams"))) {   /* instead of what plays (or starts) here */
                if (!strcmp(top->name, "player")) video_stop();
                app_replace(screen_streams_create_play(&m, v.video, v.title));
            } else app_push(screen_streams_create_play(&m, v.video, v.title));
        }
    }
}
