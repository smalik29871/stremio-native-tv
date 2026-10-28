/* Cue Party's plan against a simulated player. Its numbers are the devices': frames every
 * 21 ms on the LG (48 Hz measured) and 17 ms on a phone; a seek stands still for about a
 * second and a half before it plays on (Diagnostics > Speed > Seek recovery: the LG 0.9 s,
 * about 2 s for a long jump; a phone 1.4 s on average, 1.8 s at most). */
#include "party.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct {
    double pos, rate;
    int playing;
    double seek_cost;      /* how long a seek stands still here (s) */
    double stalled_until;  /* a seek is landing until then (ms) */
    int obeys_rate;        /* the player really changes speed */
    unsigned seeks, plays, pauses;
} Sim;

static void sim_apply(Sim *p, const PartyCmd *c, double now)
{
    if (c->pause) { p->playing = 0; p->pauses++; }
    if (c->play) { p->playing = 1; p->plays++; }
    if (c->seek >= 0) { p->pos = c->seek; p->stalled_until = now + p->seek_cost * 1000; p->seeks++; }
    if (c->rate > 0) p->rate = c->rate;
}

static void sim_step(Sim *p, double now, double dt_ms)
{
    if (p->playing && now >= p->stalled_until) p->pos += dt_ms / 1000 * (p->obeys_rate ? p->rate : 1);
}

/* Runs the plan each frame from `from` for `ms`; the room as given. */
static void run(const PartyPlay *room, Sim *p, PartySync *s, double from, double ms, double frame)
{
    for (double now = from; now < from + ms; now += frame) {
        PartyLocal l = { .loaded = 1, .playing = p->playing, .buffering = now < p->stalled_until, .pos = p->pos };
        PartyCmd c;
        party_plan(room, now, &l, s, &c);
        sim_apply(p, &c, now);
        sim_step(p, now, frame);
    }
}

static double target(const PartyPlay *r, double now) { return r->playing && now > r->at ? r->pos + (now - r->at) / 1000 : r->pos; }

int main(void)
{
    const double T0 = 1.7e12, LG = 21, PHONE = 17;

    /* a start set 2.5 s ahead: nothing moves before it, then everyone plays from there */
    {
        PartyPlay room = { .playing = 1, .pos = 0, .at = T0 + 2500 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1 };
        PartySync s = { 0 };
        run(&room, &tv, &s, T0, 2490, LG);
        CHECK(!tv.playing && tv.pos == 0 && tv.seeks == 0);
        run(&room, &tv, &s, T0 + 2490, 3000, LG);
        CHECK(tv.playing && tv.plays == 1 && tv.seeks == 0);
        CHECK(fabs(tv.pos - target(&room, T0 + 5490)) < 0.05);
    }

    /* a TV paused in the wrong place before the start is moved there, still paused */
    {
        PartyPlay room = { .playing = 1, .pos = 600, .at = T0 + 2500 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1, .pos = 12 };
        PartySync s = { 0 };
        run(&room, &tv, &s, T0, 2000, LG);
        CHECK(!tv.playing && tv.pos == 600 && tv.seeks == 1);
    }

    /* joining a party an hour in: one seek to where playback will be once the seek is done,
       then within a third of a second; the seek's real cost is learned */
    {
        PartyPlay room = { .playing = 1, .pos = 3600, .at = T0 - 10000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1 };
        PartySync s = { 0 };
        run(&room, &phone, &s, T0, 4000, PHONE);
        CHECK(phone.seeks == 1 && phone.playing);
        CHECK(fabs(phone.pos - target(&room, T0 + 4000)) < 0.3);
        CHECK(s.seek_cost > 1.2 && s.seek_cost < 1.5);
        run(&room, &phone, &s, T0 + 4000, 20000, PHONE);   /* the rest is played away */
        CHECK(fabs(phone.pos - target(&room, T0 + 24000)) <= PARTY_NUDGE_DRIFT);
        CHECK(phone.seeks == 1);
    }

    /* 0.6 s ahead: played away at 0.95x, no seek, and the player's speed is trusted after */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1, .playing = 1, .pos = 101.6 };
        PartySync s = { 0 };
        run(&room, &phone, &s, T0, 15000, PHONE);
        CHECK(phone.seeks == 0 && s.nudges == 1 && s.rate_ok == 1);
        CHECK(fabs(phone.pos - target(&room, T0 + 15000)) < PARTY_NUDGE_DONE + 0.02);
        CHECK(phone.rate == 1);
    }

    /* behind by 0.4 s: 1.05x */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1, .playing = 1, .pos = 100.6 };
        PartySync s = { 0 };
        PartyLocal l = { .loaded = 1, .playing = 1, .pos = 100.6 };
        PartyCmd c;
        party_plan(&room, T0, &l, &s, &c);
        CHECK(c.rate == PARTY_NUDGE_FAST && c.seek < 0);
        sim_apply(&phone, &c, T0);
        run(&room, &phone, &s, T0, 12000, PHONE);
        CHECK(fabs(phone.pos - target(&room, T0 + 12000)) < PARTY_NUDGE_DONE + 0.02 && phone.seeks == 0);
    }

    /* a player that ignores the speed (the LG's, until it is known): found out within ~3 s,
       then small differences stay and a larger one is a seek */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 0, .playing = 1, .pos = 101.6 };
        PartySync s = { 0 };
        run(&room, &tv, &s, T0, 4000, LG);
        CHECK(s.rate_ok == -1 && tv.rate == 1 && tv.seeks == 0);
        run(&room, &tv, &s, T0 + 4000, 5000, LG);
        CHECK(tv.seeks == 0);   /* 0.6 s off stays */
        tv.pos += 0.6;          /* it slips further: 1.2 s */
        run(&room, &tv, &s, T0 + 9000, 4000, LG);
        /* the first seek takes 1.2 s for its cost (this TV's 0.9 s is learned from it): 0.3 s
           early, which a player without speeds keeps */
        CHECK(tv.seeks == 1 && fabs(tv.pos - target(&room, T0 + 13000)) < 0.35);
        CHECK(fabs(s.seek_cost - 0.9) < 0.05);
    }

    /* 3 s off is a seek straight away */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 2.0, .obeys_rate = 1, .playing = 1, .pos = 98 };
        PartySync s = { 0 };
        run(&room, &tv, &s, T0, 6000, LG);
        CHECK(tv.seeks == 1);   /* landing 0.8 s late (a 2 s seek, taken for 1.2 s), then played away */
        CHECK(fabs(tv.pos - target(&room, T0 + 6000)) < 1.0 && tv.rate == PARTY_NUDGE_FAST);
        run(&room, &tv, &s, T0 + 6000, 20000, LG);
        CHECK(tv.seeks == 1 && fabs(tv.pos - target(&room, T0 + 26000)) < PARTY_NUDGE_DONE + 0.02);
        CHECK(fabs(s.seek_cost - 2.0) < 0.05);
    }

    /* a pause in the room stops this TV where the room says, even mid-nudge */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1, .playing = 1, .pos = 101.5 };
        PartySync s = { 0 };
        run(&room, &tv, &s, T0, 1000, LG);
        CHECK(tv.rate == PARTY_NUDGE_SLOW);
        PartyPlay paused = { .playing = 0, .pos = 104.2, .at = T0 + 1000 };
        run(&paused, &tv, &s, T0 + 1000, 2000, LG);
        CHECK(!tv.playing && tv.pos == 104.2 && tv.rate == 1 && tv.pauses == 1);
        run(&paused, &tv, &s, T0 + 3000, 5000, LG);
        CHECK(tv.pauses == 1 && tv.seeks == 1);   /* and leaves it there */
    }

    /* nothing is done before this TV has the video */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        PartyLocal l = { .loaded = 0, .playing = 0, .pos = 0 };
        PartySync s = { 0 };
        PartyCmd c;
        party_plan(&room, T0, &l, &s, &c);
        CHECK(!c.play && !c.pause && c.seek < 0 && c.rate == 0);
    }

    /* a TV stalled for data is left alone (the room pauses the others for it) */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        PartyLocal l = { .loaded = 1, .playing = 0, .buffering = 1, .pos = 99 };
        PartySync s = { 0 };
        PartyCmd c;
        party_plan(&room, T0, &l, &s, &c);
        CHECK(!c.play && c.seek < 0);
    }

    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("party: all checks passed\n");
    return 0;
}
