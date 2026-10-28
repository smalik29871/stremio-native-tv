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

    /* 1.6 s ahead: played away at 0.95x, no seek (a seek would make it buffer), and the
       player's speed is trusted after */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1, .playing = 1, .pos = 102.6 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        run(&room, &phone, &s, T0, 40000, PHONE);
        CHECK(phone.seeks == 0 && s.nudges == 1 && s.rate_ok == 1);
        CHECK(fabs(phone.pos - target(&room, T0 + 40000)) < PARTY_NUDGE_DONE + 0.02);
        CHECK(phone.rate == 1);
    }

    /* under a second off is left alone: nobody notices it, and every correction costs */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1, .playing = 1, .pos = 101.7 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        run(&room, &phone, &s, T0, 20000, PHONE);
        CHECK(phone.seeks == 0 && s.nudges == 0 && phone.rate == 1);
    }

    /* behind by 1.4 s: 1.05x */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim phone = { .rate = 1, .seek_cost = 1.4, .obeys_rate = 1, .playing = 1, .pos = 99.6 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        PartyLocal l = { .loaded = 1, .playing = 1, .pos = 99.6 };
        PartyCmd c;
        party_plan(&room, T0, &l, &s, &c);
        CHECK(c.rate == PARTY_NUDGE_FAST && c.seek < 0);
        sim_apply(&phone, &c, T0);
        run(&room, &phone, &s, T0, 40000, PHONE);
        CHECK(fabs(phone.pos - target(&room, T0 + 40000)) < PARTY_NUDGE_DONE + 0.02 && phone.seeks == 0);
    }

    /* an LG (its speed is never changed: it may buffer again at one): 3 s off stays; past the
       limit one seek, and the next only 20 s later at the earliest */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1, .playing = 1, .pos = 98 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        PartyLocal probe = { .loaded = 1, .playing = 1, .no_rate = 1 };
        for (double now = T0; now < T0 + 10000; now += LG) {   /* 3 s behind, ten seconds */
            probe.pos = tv.pos; PartyCmd c; party_plan(&room, now, &probe, &s, &c); sim_apply(&tv, &c, now); sim_step(&tv, now, LG);
        }
        CHECK(tv.seeks == 0 && tv.rate == 1 && s.nudges == 0);
        tv.pos -= 3;   /* it slips further: 6 s behind */
        for (double now = T0 + 10000; now < T0 + 14000; now += LG) {
            probe.pos = tv.pos; probe.buffering = now < tv.stalled_until; PartyCmd c; party_plan(&room, now, &probe, &s, &c); sim_apply(&tv, &c, now); sim_step(&tv, now, LG);
        }
        CHECK(tv.seeks == 1 && fabs(tv.pos - target(&room, T0 + 14000)) < 0.4);
        tv.pos -= 6;   /* and again, at once: no second seek within 20 s */
        for (double now = T0 + 14000; now < T0 + 20000; now += LG) {
            probe.pos = tv.pos; probe.buffering = now < tv.stalled_until; PartyCmd c; party_plan(&room, now, &probe, &s, &c); sim_apply(&tv, &c, now); sim_step(&tv, now, LG);
        }
        CHECK(tv.seeks == 1);
        for (double now = T0 + 20000; now < T0 + 40000; now += LG) {
            probe.pos = tv.pos; probe.buffering = now < tv.stalled_until; PartyCmd c; party_plan(&room, now, &probe, &s, &c); sim_apply(&tv, &c, now); sim_step(&tv, now, LG);
        }
        CHECK(tv.seeks == 2 && s.backoff > 1);   /* then one, and it waits for a larger slip next time */
    }

    /* a player that ignores the speed (found out within ~4 s): then only seeks, past the limits */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 0, .playing = 1, .pos = 102.6 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        run(&room, &tv, &s, T0, 5000, LG);
        CHECK(s.rate_ok == -1 && tv.rate == 1 && tv.seeks == 0);
        run(&room, &tv, &s, T0 + 5000, 20000, LG);
        CHECK(tv.seeks == 0);   /* 1.6 s off stays */
    }

    /* someone else seeks: this TV follows at once, whatever waits for a drift */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1, .playing = 1, .pos = 101 };
        PartySync s = { .drift_seek_ok = T0 + 60000 };   /* (a drift seek just happened) */
        run(&room, &tv, &s, T0, 2000, LG);
        CHECK(tv.seeks == 0);
        PartyPlay moved = { .playing = 1, .pos = 600, .at = T0 + 2000 };
        run(&moved, &tv, &s, T0 + 2000, 4000, LG);
        CHECK(tv.seeks == 1 && fabs(tv.pos - target(&moved, T0 + 6000)) < 0.4);
    }

    /* a pause in the room stops this TV at once, even mid-seek or mid-nudge; it is moved only
       when it is really somewhere else */
    {
        PartyPlay room = { .playing = 1, .pos = 100, .at = T0 - 1000 };
        Sim tv = { .rate = 1, .seek_cost = 0.9, .obeys_rate = 1, .playing = 1, .pos = 102.5 };
        PartySync s = { .anchor_pos = 100, .anchor_at = T0 - 1000 };   /* in step with this play already, drifting since */
        run(&room, &tv, &s, T0, 1000, LG);
        CHECK(tv.rate == PARTY_NUDGE_SLOW);
        PartyPlay paused = { .playing = 0, .pos = 104.2, .at = T0 + 1000 };
        PartyLocal l = { .loaded = 1, .playing = 1, .pos = tv.pos };
        PartyCmd c;
        party_plan(&paused, T0 + 1000, &l, &s, &c);
        CHECK(c.pause && c.rate == 1 && c.seek < 0);   /* the same frame: no waiting */
        sim_apply(&tv, &c, T0 + 1000);
        run(&paused, &tv, &s, T0 + 1000, 5000, LG);
        CHECK(!tv.playing && tv.pauses == 1 && tv.seeks == 0);   /* 0.6 s off: left there */
        PartyPlay far = { .playing = 0, .pos = 300, .at = T0 + 6000 };
        run(&far, &tv, &s, T0 + 6000, 4000, LG);
        CHECK(tv.pos == 300 && tv.seeks == 1);
        /* landing a seek while the room pauses: the pause is not held back */
        PartySync s2 = { .seek_at = T0, .seek_to = 50 };
        PartyLocal l2 = { .loaded = 1, .playing = 1, .buffering = 1, .pos = 50 };
        party_plan(&paused, T0 + 300, &l2, &s2, &c);
        CHECK(c.pause);
    }

    /* someone pressed play: this TV plays at once, from where it is when that is close */
    {
        PartyPlay room = { .playing = 1, .pos = 42, .at = T0 - 400 };   /* pressed 0.4 s ago over there */
        PartyLocal l = { .loaded = 1, .playing = 0, .pos = 42 };
        PartySync s = { .anchor_pos = 42, .anchor_at = T0 - 5000 };
        PartyCmd c;
        party_plan(&room, T0, &l, &s, &c);
        CHECK(c.play && c.seek < 0);
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
