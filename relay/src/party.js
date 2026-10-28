// Cue Party: friends in different homes watch the same thing at the same moment, each on their
// own TV and from their own sources. A party is a room (a Durable Object, one per party code):
// who is in, what is on, and where playback is. Nothing else goes through it: no video, no
// link, no account. See docs/CUE-PARTY.md.
//
// TVs wait for the room to change through the Worker (a request held up to 25 s); the Worker
// waits on the room through a hibernatable WebSocket, so a quiet room costs no duration.

const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';   // no 0/O, 1/I: read off a TV across a room
const CODE = /^[A-Z2-9]{6}$/;
const MAX_MEMBERS = 8;
const LEAD_MS = 2500;        // a play or seek is for this far ahead: the slowest seek measured (an LG's long jump, ~2 s) and a margin
const GONE_MS = 60000;       // a TV not heard from for this long has left
const SEEN_MS = 30000;       // how often a waiting TV's last sign of life is written down
const WAIT_MS = 25000;       // how long a TV's wait is held when nothing changes
const MAX_BODY = 4096;

const json = (status, obj) => new Response(JSON.stringify(obj), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
const clean = (s, n) => String(s ?? '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, n);
const token = () => [...crypto.getRandomValues(new Uint8Array(16))].map(b => b.toString(16).padStart(2, '0')).join('');

// Where playback is at time t (ms): a play scheduled for later stands at its position until then.
export function positionAt(play, t) {
  if (!play) return 0;
  return play.playing && t > play.at ? play.pos + (t - play.at) / 1000 : play.pos;
}

export class Room {
  constructor(ctx, env) { this.ctx = ctx; this.env = env; }

  async load() { return (await this.ctx.storage.get('room')) || null; }
  view(room, now) {   // what TVs see: never the members' tokens
    const members = Object.entries(room.members).map(([id, m]) => ({ id, name: m.name, device: m.device, ready: m.ready, buffering: m.buffering }));
    return { code: room.code, v: room.v, t: now, title: room.title, play: room.play, members, notice: room.notice, lead: LEAD_MS };
  }
  async save(room, notice, now) {
    room.v++;
    if (notice) room.notice = { ...notice, at: now, n: (room.notice?.n || 0) + 1 };
    await this.ctx.storage.put('room', room);
    const msg = JSON.stringify(this.view(room, now));   // the change itself, so the Worker needs no second call
    for (const ws of this.ctx.getWebSockets()) { try { ws.send(msg); } catch { /* gone */ } }
  }
  member(room, body) {
    const m = room.members[body.member];
    return m && room.tokens[body.member] === body.token ? m : null;
  }

  async fetch(request) {
    const url = new URL(request.url), now = Date.now(), op = url.pathname.slice(1);
    const body = request.method === 'POST' ? await request.json().catch(() => ({})) : {};
    let room = await this.load();

    if (op === 'create') {
      if (room) return json(409, { error: 'taken' });   // (the Worker draws another code)
      room = { code: url.searchParams.get('code'), v: 0, created: now, members: {}, tokens: {}, title: null, play: null, notice: null, want: null };
      return this.join(room, body, now, true);
    }
    if (!room) return json(404, { error: 'no such party' });
    if (op === 'join') return this.join(room, body, now, false);

    if (op === 'wait') {   // a TV waiting for a change (through the Worker)
      const id = url.searchParams.get('member'), m = room.members[id];
      if (!m || room.tokens[id] !== url.searchParams.get('token')) return json(403, { error: 'not in this party' });
      if (now - m.seen > SEEN_MS) { m.seen = now; await this.ctx.storage.put('room', room); }   // (not every wait: writes are counted)
      if (Number(url.searchParams.get('v')) !== room.v || request.headers.get('Upgrade') !== 'websocket') return json(200, this.view(room, now));
      const pair = new WebSocketPair();   // up to date: the Worker waits on this for the next change
      this.ctx.acceptWebSocket(pair[1], [id]);
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    if (op !== 'do') return json(404, { error: 'unknown' });
    const m = this.member(room, body);
    if (!m) return json(403, { error: 'not in this party' });
    m.seen = now;
    const pos = Number(body.pos), at = Number.isFinite(pos) && pos >= 0 && pos < 864000 ? pos : null;
    const everyoneReady = () => Object.values(room.members).every(x => x.ready && !x.buffering);
    const start = (p) => { room.play = { playing: true, pos: p, at: now + LEAD_MS }; room.want = null; };
    switch (body.act) {
      case 'load': {   // something else is on: everyone loads it, then it starts together
        const t = body.title || {};
        room.title = { meta: clean(t.meta, 40), video: clean(t.video, 80), name: clean(t.name, 120), type: clean(t.type, 16) };
        for (const x of Object.values(room.members)) x.ready = false;
        room.play = { playing: false, pos: at ?? 0, at: now };
        room.want = { pos: at ?? 0 };
        await this.save(room, { who: m.name, what: 'load', name: room.title.name }, now);
        break;
      }
      case 'ready':   // this TV has the source loaded (or lost it)
        m.ready = !!body.ready;
        if (room.want && everyoneReady()) start(room.want.pos);
        await this.save(room, null, now);
        break;
      case 'play':
        if (at === null) return json(400, { error: 'a position is needed' });
        if (everyoneReady()) start(at); else { room.want = { pos: at }; room.play = { playing: false, pos: at, at: now }; }
        await this.save(room, { who: m.name, what: 'play' }, now);
        break;
      case 'pause':   // everyone stops at the pauser's position
        if (at === null) return json(400, { error: 'a position is needed' });
        room.play = { playing: false, pos: at, at: now }; room.want = null;
        await this.save(room, { who: m.name, what: 'pause' }, now);
        break;
      case 'seek':
        if (at === null) return json(400, { error: 'a position is needed' });
        if (room.play?.playing) room.play = { playing: true, pos: at, at: now + LEAD_MS };
        else room.play = { playing: false, pos: at, at: now };
        await this.save(room, { who: m.name, what: 'seek' }, now);
        break;
      case 'buffering': {   // while one TV waits for its source, the others wait with it
        const on = !!body.on;
        if (on === m.buffering) break;
        m.buffering = on;
        if (on && room.play?.playing) {
          room.want = { pos: positionAt(room.play, now) };
          room.play = { playing: false, pos: room.want.pos, at: now };
          await this.save(room, { who: m.name, what: 'buffering' }, now);
        } else {
          if (!on && room.want && everyoneReady()) start(room.want.pos);
          await this.save(room, null, now);
        }
        break;
      }
      case 'react':
        await this.save(room, { who: m.name, what: 'react', emoji: clean(body.emoji, 8) }, now);
        break;
      case 'leave':
        delete room.members[body.member]; delete room.tokens[body.member];
        if (!Object.keys(room.members).length) { await this.ctx.storage.deleteAll(); return json(200, { left: true }); }
        if (room.want && everyoneReady()) start(room.want.pos);
        await this.save(room, { who: m.name, what: 'leave' }, now);
        return json(200, { left: true });
      default:
        return json(400, { error: 'unknown act' });
    }
    return json(200, this.view(room, now));
  }

  async join(room, body, now, first) {
    if (Object.keys(room.members).length >= MAX_MEMBERS) return json(409, { error: 'the party is full' });
    const id = token().slice(0, 8), tok = token();
    room.members[id] = { name: clean(body.name, 24) || 'Guest', device: clean(body.device, 40), seen: now, ready: false, buffering: false };
    room.tokens[id] = tok;
    if (room.want) room.want = { pos: positionAt(room.play, now) };   // it waits for the newcomer to load too
    await this.save(room, first ? null : { who: room.members[id].name, what: 'join' }, now);
    await this.ctx.storage.setAlarm(now + GONE_MS);
    return json(200, { ...this.view(room, now), member: id, token: tok });
  }

  async alarm() {   // TVs that went quiet have left; an empty party is gone
    const room = await this.load(), now = Date.now();
    if (!room) return;
    let left = false;
    for (const [id, m] of Object.entries(room.members)) if (now - m.seen > GONE_MS && !this.ctx.getWebSockets(id).length) { delete room.members[id]; delete room.tokens[id]; left = true; }
    if (!Object.keys(room.members).length) { await this.ctx.storage.deleteAll(); return; }
    if (left) await this.save(room, { who: '', what: 'gone' }, now);
    await this.ctx.storage.setAlarm(now + GONE_MS);
  }

  async webSocketMessage() {}
  async webSocketClose(ws) { try { ws.close(); } catch { /* closed */ } }
}

// The Worker's side: /party (start one), /party/CODE/join, /party/CODE/do, /party/CODE/wait.
export async function party(request, env, url, path) {
  if (!env.ROOMS) return json(503, { error: 'this relay has no parties' });
  const parts = path.split('/').filter(Boolean);
  const room = code => env.ROOMS.get(env.ROOMS.idFromName(code));
  const body = request.method === 'POST' ? await request.text() : null;
  if (body && body.length > MAX_BODY) return json(413, { error: 'too large' });
  const call = (code, op, q = '', headers = {}) => room(code).fetch(`https://room/${op}${q}`, body === null ? { headers } : { method: 'POST', body, headers });

  if (!parts.length && request.method === 'POST') {   // a new party: a code nobody has
    for (let i = 0; i < 5; i++) {
      const code = Array.from(crypto.getRandomValues(new Uint8Array(6)), b => ALPHABET[b % ALPHABET.length]).join('');
      const r = await call(code, 'create', '?code=' + code);
      if (r.status !== 409) return r;
    }
    return json(503, { error: 'no free code' });
  }
  const code = (parts[0] || '').toUpperCase();
  if (!CODE.test(code) || parts.length !== 2) return json(404, { error: 'no such party' });
  if ((parts[1] === 'join' || parts[1] === 'do') && request.method === 'POST') return call(code, parts[1]);
  if (parts[1] !== 'wait' || request.method !== 'GET') return json(404, { error: 'unknown' });

  // a TV waiting for a change: answered at once when it is behind, else when the room changes (or after 25 s)
  const q = '?' + new URLSearchParams({ member: url.searchParams.get('member') || '', token: url.searchParams.get('token') || '', v: url.searchParams.get('v') || '-1' });
  const r = await call(code, 'wait', q, { Upgrade: 'websocket' });
  const ws = r.webSocket;
  if (!ws) return r;   // behind, or not in the party
  ws.accept();
  const changed = await new Promise(res => {
    const t = setTimeout(() => res(null), WAIT_MS);
    ws.addEventListener('message', e => { clearTimeout(t); res(e.data); });
    ws.addEventListener('close', () => { clearTimeout(t); res(null); });
  });
  try { ws.close(); } catch { /* closed */ }
  if (changed) return new Response(changed, { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
  return json(200, { v: Number(url.searchParams.get('v')), t: Date.now(), same: true });   // nothing new (no second call: requests are counted)
}
