/* See party.h. */
#include "party.h"
#include "keyrelay.h"
#include "http.h"
#include "log.h"
#include "../vendor/cJSON.h"
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WAIT_TIMEOUT_S = 40, LOST_MS = 8000, SEEK_SETTLE_MS = 400 };

static SDL_mutex *g_mx;
static SDL_Thread *g_thread;
static volatile int g_stop;
static PartyView g_view;
static char g_member[12], g_token[40];
static char g_join_code[8], g_join_body[200];   /* what the thread does first: "" starts a party */
static double g_offset;          /* room ms = SDL ticks + this */
static double g_best_rtt;
static int g_clocked;
static unsigned g_session;
static Uint32 g_seek_due; static double g_seek_pos;   /* a seek kept back while the viewer is still seeking */

static void lock(void) { if (!g_mx) g_mx = SDL_CreateMutex(); SDL_LockMutex(g_mx); }
static void unlock(void) { SDL_UnlockMutex(g_mx); }

static const char *base(void) { return keyrelay_base(KR_PARTY); }

double party_now_ms(void) { return (double)SDL_GetTicks() + g_offset; }

/* The room's time t came back rtt ms after the request went: it was said about halfway. A
 * smoothed offset (Syncplay's 0.85 weight), from quick answers only: a held one says little. */
static void clock_sample(Uint32 sent, Uint32 got, double t)
{
    double rtt = (double)(got - sent), off = t + rtt / 2 - got;
    if (t <= 0 || rtt < 0 || rtt > 3000) return;
    if (!g_clocked || rtt < g_best_rtt * 0.6) { g_offset = off; g_best_rtt = rtt; g_clocked = 1; return; }   /* a much quicker trip says more */
    if (rtt > g_best_rtt * 2 + 60) return;   /* a slow trip: its middle is a guess */
    g_offset = 0.85 * g_offset + 0.15 * off;
    g_best_rtt = rtt < g_best_rtt ? rtt : g_best_rtt * 1.02;
}

static void copy_str(char *dst, size_t n, const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    snprintf(dst, n, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

/* The room's answer into the view (locked by the caller); 0 when it was not one. */
static int take_room(const char *body, int force)
{
    cJSON *j = cJSON_Parse(body ? body : "");
    const cJSON *v = cJSON_GetObjectItem(j, "v");
    if (!cJSON_IsNumber(v) || cJSON_GetObjectItem(j, "same")) { cJSON_Delete(j); return 0; }
    if (!force && g_view.state == PARTY_IN && (unsigned)v->valuedouble < g_view.v) { cJSON_Delete(j); return 1; }   /* older than what we have */
    PartyView *w = &g_view;
    w->v = (unsigned)v->valuedouble;
    copy_str(w->code, sizeof w->code, j, "code");
    const cJSON *m;
    w->n_members = 0;
    cJSON_ArrayForEach(m, cJSON_GetObjectItem(j, "members")) {
        if (w->n_members == PARTY_MEMBERS) break;
        PartyMember *pm = &w->members[w->n_members++];
        copy_str(pm->id, sizeof pm->id, m, "id");
        copy_str(pm->name, sizeof pm->name, m, "name");
        copy_str(pm->device, sizeof pm->device, m, "device");
        pm->ready = cJSON_IsTrue(cJSON_GetObjectItem(m, "ready"));
        pm->buffering = cJSON_IsTrue(cJSON_GetObjectItem(m, "buffering"));
    }
    const cJSON *t = cJSON_GetObjectItem(j, "title");
    w->has_title = cJSON_IsObject(t);
    copy_str(w->meta, sizeof w->meta, t, "meta");
    copy_str(w->video, sizeof w->video, t, "video");
    copy_str(w->title, sizeof w->title, t, "name");
    copy_str(w->type, sizeof w->type, t, "type");
    const cJSON *p = cJSON_GetObjectItem(j, "play");
    w->has_play = cJSON_IsObject(p);
    if (w->has_play) {
        w->play.playing = cJSON_IsTrue(cJSON_GetObjectItem(p, "playing"));
        w->play.pos = cJSON_GetNumberValue(cJSON_GetObjectItem(p, "pos"));
        w->play.at = cJSON_GetNumberValue(cJSON_GetObjectItem(p, "at"));
        if (isnan(w->play.pos)) w->play.pos = 0;
        if (isnan(w->play.at)) w->play.at = 0;
    }
    const cJSON *lead = cJSON_GetObjectItem(j, "lead");
    w->lead_ms = cJSON_IsNumber(lead) ? lead->valueint : 2500;
    const cJSON *n = cJSON_GetObjectItem(j, "notice");
    if (cJSON_IsObject(n)) {
        unsigned nn = (unsigned)cJSON_GetNumberValue(cJSON_GetObjectItem(n, "n"));
        if (nn != w->notice.n) {
            w->notice.n = nn;
            copy_str(w->notice.who, sizeof w->notice.who, n, "who");
            copy_str(w->notice.by, sizeof w->notice.by, n, "by");
            copy_str(w->notice.what, sizeof w->notice.what, n, "what");
            copy_str(w->notice.title, sizeof w->notice.title, n, "name");
            copy_str(w->notice.emoji, sizeof w->notice.emoji, n, "emoji");
        }
    }
    const cJSON *mem = cJSON_GetObjectItem(j, "member"), *tok = cJSON_GetObjectItem(j, "token");
    if (cJSON_IsString(mem) && cJSON_IsString(tok)) {
        snprintf(g_member, sizeof g_member, "%s", mem->valuestring);
        snprintf(g_token, sizeof g_token, "%s", tok->valuestring);
        snprintf(w->me, sizeof w->me, "%s", g_member);
    }
    w->state = PARTY_IN;
    w->reconnecting = 0;
    cJSON_Delete(j);
    return 1;
}

static void end_party(const char *why)
{
    g_view.state = PARTY_ENDED;
    snprintf(g_view.error, sizeof g_view.error, "%s", why);
    LOGI("party: %s", why);
}

static int party_thread(void *arg)
{
    (void)arg;
    void *keep = NULL;
    char url[600];
    HttpResponse r;
    lock();
    int joining = g_join_code[0] != 0;
    if (joining) snprintf(url, sizeof url, "%s/%s/join", base() ? base() : "", g_join_code);
    else snprintf(url, sizeof url, "%s", base() ? base() : "");
    char body[200];
    snprintf(body, sizeof body, "%s", g_join_body);
    unlock();
    HttpOptions o = { .post_json = body, .timeout_s = 15, .stop = &g_stop, .keep = &keep };
    Uint32 sent = SDL_GetTicks();
    http_request_ex(url, &o, &r);
    Uint32 got = SDL_GetTicks();
    lock();
    if (r.status == 200 && take_room(r.body, 1)) {
        cJSON *j = cJSON_Parse(r.body);
        clock_sample(sent, got, cJSON_GetNumberValue(cJSON_GetObjectItem(j, "t")));
        cJSON_Delete(j);
        LOGI("party: %s party %s (%d in)", joining ? "joined" : "started", g_view.code, g_view.n_members);
    } else if (!g_stop) {
        end_party(r.status == 404 ? "There is no party with that code" : r.status == 409 ? "That party is full" :
                  r.status == 0 ? "Couldn't reach the party" : "The party couldn't start");
    }
    unlock();
    http_response_free(&r);

    for (int i = 0; i < 3 && !g_stop; i++) {   /* three quick trips to set the clock (a wait that is behind is answered at once) */
        lock();
        int in = g_view.state == PARTY_IN;
        snprintf(url, sizeof url, "%s/%s/wait?member=%s&token=%s&v=-1", base() ? base() : "", g_view.code, g_member, g_token);
        unlock();
        if (!in) break;
        HttpOptions q = { .timeout_s = 5, .stop = &g_stop, .keep = &keep };
        sent = SDL_GetTicks();
        http_request_ex(url, &q, &r);
        got = SDL_GetTicks();
        if (r.status == 200) {
            cJSON *j = cJSON_Parse(r.body);
            lock(); clock_sample(sent, got, cJSON_GetNumberValue(cJSON_GetObjectItem(j, "t"))); unlock();
            cJSON_Delete(j);
        }
        http_response_free(&r);
    }
    LOGI("party: the room's clock is %+.0f ms from ours (best trip %.0f ms)", g_offset, g_best_rtt);

    int fails = 0;
    Uint32 last_ok = SDL_GetTicks();
    while (!g_stop) {
        lock();
        if (g_view.state != PARTY_IN) { unlock(); break; }
        snprintf(url, sizeof url, "%s/%s/wait?member=%s&token=%s&v=%u", base() ? base() : "", g_view.code, g_member, g_token, g_view.v);
        unlock();
        HttpOptions w = { .timeout_s = WAIT_TIMEOUT_S, .quiet_s = WAIT_TIMEOUT_S, .stop = &g_stop, .keep = &keep };   /* (the room holds it up to 25 s, silent) */
        sent = SDL_GetTicks();
        http_request_ex(url, &w, &r);
        got = SDL_GetTicks();
        if (g_stop) { http_response_free(&r); break; }
        lock();
        if (r.status == 200) {
            fails = 0; last_ok = got;
            cJSON *j = cJSON_Parse(r.body);
            if (got - sent < 400) clock_sample(sent, got, cJSON_GetNumberValue(cJSON_GetObjectItem(j, "t")));   /* answered at once */
            cJSON_Delete(j);
            take_room(r.body, 0);
            g_view.reconnecting = 0;
        } else if (r.status == 403 || r.status == 404) {
            end_party(r.status == 404 ? "The party has ended" : "This TV was dropped from the party");
        } else {
            fails++;
            if (got - last_ok > LOST_MS && !g_view.reconnecting) { g_view.reconnecting = 1; LOGW("party: the room isn't answering (%ld), trying again", r.status); }
        }
        unlock();
        http_response_free(&r);
        if (fails) for (int i = 0; i < (fails < 5 ? fails : 5) * 10 && !g_stop; i++) SDL_Delay(100);   /* 1 s, 2 s ... 5 s apart */
    }
    http_keep_free(&keep);
    return 0;
}

static void stop_thread(void)
{
    if (!g_thread) return;
    g_stop = 1;
    SDL_WaitThread(g_thread, NULL);
    g_thread = NULL;
    g_stop = 0;
}

static void begin(const char *code, const char *name, const char *device)
{
    party_leave();
    if (!base()) {
        lock(); memset(&g_view, 0, sizeof g_view); end_party("Cue Party needs the Cuelight relay, which this build has no address for"); unlock();
        return;
    }
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "name", name && *name ? name : "Guest");
    cJSON_AddStringToObject(b, "device", device ? device : "");
    char *s = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    lock();
    memset(&g_view, 0, sizeof g_view);
    g_view.state = PARTY_CONNECTING;
    g_session++;
    g_member[0] = g_token[0] = 0;
    g_clocked = 0;
    size_t n = 0;
    for (const char *c = code ? code : ""; *c && n + 1 < sizeof g_join_code; c++)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')) g_join_code[n++] = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    g_join_code[n] = 0;
    snprintf(g_join_body, sizeof g_join_body, "%s", s ? s : "{}");
    unlock();
    free(s);
    g_thread = SDL_CreateThread(party_thread, "party", NULL);
    if (!g_thread) { lock(); end_party("The party couldn't start"); unlock(); }
}

void party_start(const char *name, const char *device) { begin(NULL, name, device); }
void party_join(const char *code, const char *name, const char *device) { begin(code && *code ? code : "-", name, device); }

/* Acts go one at a time, in the order they happened: a "ready" overtaking the "load" before it
 * (which makes everyone not ready) would leave the room waiting for this TV for ever. */
enum { QUEUE = 16 };
static char *g_queue[QUEUE];
static int g_queued, g_sending;

static void on_done(HttpResponse *r, void *user);

static void send_next(void)
{
    if (g_sending || !g_queued) return;
    char *s = g_queue[0];
    memmove(g_queue, g_queue + 1, sizeof g_queue[0] * (size_t)--g_queued);
    lock();
    int in = g_view.state == PARTY_IN;
    char url[300];
    snprintf(url, sizeof url, "%s/%s/do", base() ? base() : "", g_view.code);
    unlock();
    if (in && base()) {
        HttpOptions o = { .post_json = s, .timeout_s = 10 };
        g_sending = http_request_async_ex(url, &o, on_done, (void *)(uintptr_t)SDL_GetTicks()) != 0;
    }
    free(s);
    if (!g_sending) send_next();
}

static void on_done(HttpResponse *r, void *user)
{
    Uint32 sent = (Uint32)(uintptr_t)user, got = SDL_GetTicks();
    g_sending = 0;
    send_next();
    if (r->status != 200 || !r->body) { if (r->status) LOGW("party: the room said %ld to that", r->status); return; }
    lock();
    if (g_view.state == PARTY_IN) {
        cJSON *j = cJSON_Parse(r->body);
        clock_sample(sent, got, cJSON_GetNumberValue(cJSON_GetObjectItem(j, "t")));
        cJSON_Delete(j);
        take_room(r->body, 0);
    }
    unlock();
}

/* One act for the room (cJSON body without member and token; taken over). A leave is waited
 * for at exit, so the others hear of it at once. */
static void act(cJSON *b)
{
    const cJSON *what = cJSON_GetObjectItem(b, "act");
    int leaving = cJSON_IsString(what) && !strcmp(what->valuestring, "leave");
    lock();
    int in = g_view.state == PARTY_IN;
    char url[300];
    snprintf(url, sizeof url, "%s/%s/do", base() ? base() : "", g_view.code);
    cJSON_AddStringToObject(b, "member", g_member);
    cJSON_AddStringToObject(b, "token", g_token);
    unlock();
    char *s = in && base() ? cJSON_PrintUnformatted(b) : NULL;
    cJSON_Delete(b);
    if (!s) return;
    if (leaving) {   /* now, whatever waits: it must reach the room before the app closes */
        HttpOptions o = { .post_json = s, .timeout_s = 10, .deliver = 1 };
        http_request_async_ex(url, &o, NULL, NULL);
        free(s);
        return;
    }
    if (g_queued == QUEUE) { LOGW("party: too much to tell the room at once; dropped one"); free(s); return; }
    g_queue[g_queued++] = s;
    send_next();
}

static cJSON *act_body(const char *what, double pos)
{
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "act", what);
    if (pos >= 0) {
        cJSON_AddNumberToObject(b, "pos", floor(pos * 1000 + 0.5) / 1000);
        cJSON_AddNumberToObject(b, "at", floor(party_now_ms()));   /* when, by the room's clock: it goes on from then, not from when the room heard */
    }
    return b;
}

/* What this TV did shows in its own view at once (the room confirms it a moment later), so the
 * plan doesn't undo it meanwhile. */
static void expect(int playing, double pos)
{
    lock();
    if (g_view.state == PARTY_IN) {
        double now = party_now_ms();
        g_view.has_play = 1;
        g_view.play.playing = playing;
        g_view.play.pos = pos;
        g_view.play.at = now;   /* this TV goes on at once; the others catch up */
    }
    unlock();
}

void party_load(const char *meta, const char *video, const char *title, const char *type, double pos)
{
    if (!party_active()) return;
    cJSON *b = act_body("load", pos), *t = cJSON_AddObjectToObject(b, "title");
    cJSON_AddStringToObject(t, "meta", meta ? meta : "");
    cJSON_AddStringToObject(t, "video", video ? video : "");
    cJSON_AddStringToObject(t, "name", title ? title : "");
    cJSON_AddStringToObject(t, "type", type ? type : "");
    lock();
    if (g_view.state == PARTY_IN) {   /* (as the room will have it) */
        g_view.has_title = 1;
        snprintf(g_view.meta, sizeof g_view.meta, "%s", meta ? meta : "");
        snprintf(g_view.video, sizeof g_view.video, "%s", video ? video : "");
        snprintf(g_view.title, sizeof g_view.title, "%s", title ? title : "");
        snprintf(g_view.type, sizeof g_view.type, "%s", type ? type : "");
    }
    unlock();
    expect(0, pos);
    LOGI("party: this TV put on %s", video ? video : "?");
    act(b);
}

void party_play(double pos)  { if (!party_active()) return; expect(1, pos); act(act_body("play", pos)); }
void party_pause(double pos) { if (!party_active()) return; g_seek_due = 0; expect(0, pos); act(act_body("pause", pos)); }

/* A held seek key sends many: the room hears where it ended up. */
void party_seek(double pos)
{
    if (!party_active()) return;
    lock();
    int playing = g_view.play.playing;
    unlock();
    expect(playing, pos);
    g_seek_pos = pos;
    g_seek_due = SDL_GetTicks() + SEEK_SETTLE_MS;
    if (!g_seek_due) g_seek_due = 1;
}

void party_ready(int ready)
{
    if (!party_active()) return;
    cJSON *b = act_body("ready", -1);
    cJSON_AddBoolToObject(b, "ready", ready);
    act(b);
}

void party_buffering(int on)
{
    if (!party_active()) return;
    cJSON *b = act_body("buffering", -1);
    cJSON_AddBoolToObject(b, "on", on);
    act(b);
}

void party_react(const char *emoji)
{
    if (!party_active()) return;
    cJSON *b = act_body("react", -1);
    cJSON_AddStringToObject(b, "emoji", emoji ? emoji : "");
    act(b);
}

void party_leave(void)
{
    lock();
    int was = g_view.state == PARTY_IN;
    unlock();
    if (was) {   /* the others hear at once (else the room finds out after a minute) */
        g_seek_due = 0;
        act(act_body("leave", -1));
        LOGI("party: left");
    }
    stop_thread();
    while (g_queued) free(g_queue[--g_queued]);   /* what was still to tell this party */
    lock(); memset(&g_view, 0, sizeof g_view); g_member[0] = g_token[0] = 0; unlock();
}

void party_shutdown(void) { party_leave(); }

int party_active(void)
{
    if (g_seek_due && (Sint32)(SDL_GetTicks() - g_seek_due) >= 0) {   /* a seek that has settled goes now */
        g_seek_due = 0;
        act(act_body("seek", g_seek_pos));
    }
    return g_view.state == PARTY_CONNECTING || g_view.state == PARTY_IN;
}

unsigned party_session(void) { return g_session; }

void party_view(PartyView *out) { lock(); *out = g_view; unlock(); }

const char *party_code(void) { return g_view.state == PARTY_IN ? g_view.code : ""; }

/* --- the plan --------------------------------------------------------------------------- */

void party_plan(const PartyPlay *room, double now, const PartyLocal *l, PartySync *s, PartyCmd *out)
{
    memset(out, 0, sizeof *out);
    out->seek = -1;
    if (!room || !l || !l->loaded) return;
    double cost = s->seek_cost > 0 ? s->seek_cost : 1.2;   /* the LG's ~0.9-2 s and a phone's ~1.4 s, until this TV's own is known */
    if (s->backoff < 1) s->backoff = 1;

    if (s->seek_at > 0) {   /* a seek while playing: what it cost, once it plays on from there */
        if (l->playing && !l->buffering && l->pos > s->seek_to + 0.2 && l->pos < s->seek_to + 6) {
            double lost = (now - s->seek_at) / 1000 - (l->pos - s->seek_to);
            if (lost >= 0 && lost < 8) s->seek_cost = s->seek_cost > 0 ? 0.7 * s->seek_cost + 0.3 * lost : lost;
            s->seek_at = 0;
        } else if (now - s->seek_at > 10000 || !room->playing) s->seek_at = 0;
    }
    int landing = s->seek_at > 0;
    int fresh = room->pos != s->anchor_pos || room->at != s->anchor_at;   /* someone played, paused or moved it */
    s->anchor_pos = room->pos; s->anchor_at = room->at;

    if (!room->playing) {   /* a pause: at once, whatever is under way; moved only when really elsewhere */
        if (s->rate && s->rate != 1) { out->rate = 1; s->rate = 1; }
        if (l->playing) out->pause = 1;
        if (!landing && now >= s->hold_until && fabs(l->pos - room->pos) > PARTY_ALIGN) { out->seek = room->pos; s->hold_until = now + 3000; }
        return;
    }
    if (now < room->at) {   /* a start once everyone has the video: be there, paused, when it comes */
        if (s->rate && s->rate != 1) { out->rate = 1; s->rate = 1; }
        if (l->playing) out->pause = 1;
        if (!landing && now >= s->hold_until && fabs(l->pos - room->pos) > PARTY_ALIGN) { out->seek = room->pos; s->hold_until = now + 3000; }
        return;
    }
    double target = room->pos + (now - room->at) / 1000, drift = l->pos - target, ad = fabs(drift);
    if (!l->playing) {   /* playing again: at once; a seek first only when far from it */
        if (l->buffering) return;   /* (a stall: the room hears of it and waits) */
        if (!landing && (ad > PARTY_SEEK_AHEAD || (fresh && ad > PARTY_ALIGN))) {
            out->seek = target + cost;   /* where playback will be once the seek is done */
            s->seek_at = now; s->seek_to = out->seek; s->seeks++;
            s->drift_seek_ok = now + PARTY_SEEK_GAP_MS;
        }
        out->play = 1;
        return;
    }
    if (landing) return;
    if (fresh && ad > PARTY_ALIGN) {   /* someone moved it: there too, now (theirs, not a drift: no waiting) */
        if (s->rate && s->rate != 1) { out->rate = 1; s->rate = 1; }
        out->seek = target + cost;
        s->seek_at = now; s->seek_to = out->seek; s->seeks++;
        s->drift_seek_ok = now + PARTY_SEEK_GAP_MS;
        return;
    }
    if (now < s->hold_until) return;
    if ((drift > PARTY_SEEK_AHEAD * s->backoff || -drift > PARTY_SEEK_BEHIND * s->backoff) && now >= s->drift_seek_ok) {
        if (s->rate && s->rate != 1) { out->rate = 1; s->rate = 1; }
        out->seek = target + cost;
        s->seek_at = now; s->seek_to = out->seek; s->seeks++;
        if (s->last_drift_seek > 0 && now - s->last_drift_seek < 120000 && s->backoff < 4) s->backoff *= 1.5;   /* it keeps slipping: correct it less often */
        s->last_drift_seek = now;
        s->drift_seek_ok = now + PARTY_SEEK_GAP_MS;
        return;
    }
    if (l->no_rate || s->rate_ok < 0) return;   /* this player keeps its speed: differences under the seek limits stay */
    if (s->rate && s->rate != 1) {   /* nudging */
        if (ad < PARTY_NUDGE_DONE || (drift > 0) != (s->nudge_drift > 0)) {
            out->rate = 1; s->rate = 1;
            if (!s->rate_ok) s->rate_ok = 1;
            return;
        }
        if (!s->rate_ok && now - s->nudge_at > 4000) {   /* 4 s at 5%: 0.2 s closer if the speed really changed */
            if (fabs(s->nudge_drift) - ad < 0.07) { s->rate_ok = -1; out->rate = 1; s->rate = 1; }
            else s->rate_ok = 1;
        }
        return;
    }
    if (ad > PARTY_NUDGE_DRIFT) {
        s->rate = drift > 0 ? PARTY_NUDGE_SLOW : PARTY_NUDGE_FAST;
        out->rate = s->rate;
        s->nudge_at = now; s->nudge_drift = drift; s->nudges++;
    }
}
