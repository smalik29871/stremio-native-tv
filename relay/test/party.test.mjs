import { test } from 'node:test';
import assert from 'node:assert/strict';
import worker, { Room } from '../src/worker.js';
import { positionAt } from '../src/party.js';

// A room as Cloudflare holds it, without the WebSockets (those are checked under wrangler dev:
// relay/test/party-live.mjs).
function rooms() {
  const objects = new Map();
  return {
    objects,
    idFromName: name => name,
    get: id => {
      if (!objects.has(id)) {
        const data = new Map();
        const ctx = {
          alarm: null,
          storage: {
            get: async k => structuredClone(data.get(k)),
            put: async (k, v) => { data.set(k, structuredClone(v)); },
            deleteAll: async () => data.clear(),
            setAlarm: async t => { ctx.alarm = t; },
          },
          getWebSockets: () => [],
          acceptWebSocket: () => { throw new Error('no WebSockets here'); },
        };
        const room = new Room(ctx, {});
        room.data = data;
        objects.set(id, room);
      }
      const room = objects.get(id);
      return { fetch: (url, init) => room.fetch(new Request(url, init)) };
    },
  };
}
let ip = 0;
const call = (env, path, body) => worker.fetch(new Request('https://relay.test/party' + path, {
  method: body === undefined ? 'GET' : 'POST', body: body === undefined ? undefined : JSON.stringify(body),
  headers: { 'CF-Connecting-IP': '10.1.0.' + (++ip % 250) },
}), env);
const start = async (env, name = 'Sam') => (await call(env, '', { name, device: 'webos10' })).json();
const join = async (env, code, name) => (await call(env, `/${code}/join`, { name, device: 'android-pixel-8' })).json();
const act = async (env, code, who, act, extra = {}) => (await call(env, `/${code}/do`, { member: who.member, token: who.token, act, ...extra })).json();

test('a party has a code a person can read off a TV, and its first member', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env);
  assert.match(a.code, /^[A-HJ-NP-Z2-9]{6}$/);
  assert.equal(a.members.length, 1);
  assert.equal(a.members[0].name, 'Sam');
  assert.match(a.token, /^[0-9a-f]{32}$/);
  assert.equal(a.lead, 2500);
  assert.ok(Math.abs(a.t - Date.now()) < 1000);
  assert.ok(!JSON.stringify(a.members).includes(a.token));
});

test('joining: by code (any case), up to eight, a wrong code is no party', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env);
  const b = await join(env, a.code.toLowerCase(), 'Alex');
  assert.equal(b.members.length, 2);
  assert.deepEqual(b.notice && [b.notice.who, b.notice.what], ['Alex', 'join']);
  for (let i = 0; i < 6; i++) await join(env, a.code, 'Guest');
  assert.equal((await call(env, `/${a.code}/join`, { name: 'Ninth' })).status, 409);
  assert.equal((await call(env, '/ZZZZZZ/join', { name: 'x' })).status, 404);
  assert.equal((await call(env, '/ab/join', { name: 'x' })).status, 404);
});

test('only a member with its token acts or waits', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env);
  assert.equal((await call(env, `/${a.code}/do`, { member: a.member, token: 'f'.repeat(32), act: 'pause', pos: 3 })).status, 403);
  assert.equal((await call(env, `/${a.code}/wait?member=${a.member}&token=nope&v=-1`)).status, 403);
  assert.equal((await call(env, `/${a.code}/wait?member=${a.member}&token=${a.token}&v=-1`)).status, 200);
});

test('a TV that is behind gets the room at once', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env);
  await act(env, a.code, a, 'pause', { pos: 61 });
  const r = await (await call(env, `/${a.code}/wait?member=${a.member}&token=${a.token}&v=${a.v}`)).json();
  assert.equal(r.v, a.v + 1);
  assert.deepEqual([r.play.playing, r.play.pos], [false, 61]);
});

test('a start waits until every TV has the episode loaded, then is set 2.5 s ahead', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env), b = await join(env, a.code, 'Alex');
  let s = await act(env, a.code, a, 'load', { title: { meta: 'tt0903747', video: 'tt0903747:1:1', name: 'Breaking Bad S1 E1', type: 'series' } });
  assert.equal(s.title.video, 'tt0903747:1:1');
  assert.equal(s.title.rel, '');   // no fingerprint given: none (and only 16 hex digits are kept)
  s = await act(env, a.code, a, 'load', { title: { meta: 'tt0903747', video: 'tt0903747:1:1', name: 'Breaking Bad S1 E1', type: 'series', rel: '0123456789abcdef' } });
  assert.equal(s.title.rel, '0123456789abcdef');
  s = await act(env, a.code, a, 'load', { title: { meta: 'tt0903747', video: 'tt0903747:1:1', name: 'x', type: 'series', rel: 'https://debrid/x?token=1' } });
  assert.equal(s.title.rel, '');
  assert.equal(s.play.playing, false);
  s = await act(env, a.code, a, 'ready', { ready: true });
  assert.equal(s.play.playing, false);   // Alex is still loading
  const before = Date.now();
  s = await act(env, a.code, b, 'ready', { ready: true });
  assert.equal(s.play.playing, true);
  assert.equal(s.play.pos, 0);
  assert.ok(s.play.at >= before + 2500 && s.play.at <= Date.now() + 2500);
});

test('play while someone is loading waits for them; pause stops everyone where the pauser is', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env), b = await join(env, a.code, 'Alex');
  await act(env, a.code, a, 'ready', { ready: true });
  let s = await act(env, a.code, a, 'play', { pos: 120 });
  assert.equal(s.play.playing, false);
  s = await act(env, a.code, b, 'ready', { ready: true });   // the last one ready: a start everyone can make, ahead
  assert.deepEqual([s.play.playing, s.play.pos], [true, 120]);
  assert.ok(s.play.at > Date.now() + 2000);
  s = await act(env, a.code, b, 'pause', { pos: 131.4 });
  assert.deepEqual([s.play.playing, s.play.pos, s.notice.who, s.notice.what], [false, 131.4, 'Alex', 'pause']);
  assert.equal((await call(env, `/${a.code}/do`, { member: a.member, token: a.token, act: 'seek', pos: -4 })).status, 400);
});

test('one TV buffering pauses the rest where playback had got to; they go on together', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env), b = await join(env, a.code, 'Alex');
  await act(env, a.code, a, 'ready', { ready: true });
  await act(env, a.code, b, 'ready', { ready: true });
  let s = await act(env, a.code, a, 'play', { pos: 10 });
  const data = env.ROOMS.objects.get(a.code).data, room = data.get('room');   // as if it had been playing 1.5 s
  room.play.at -= 1500; data.set('room', room);
  s = await act(env, a.code, b, 'buffering', { on: true });
  assert.equal(s.play.playing, false);
  assert.ok(s.play.pos >= 11.4 && s.play.pos < 11.7, String(s.play.pos));
  assert.deepEqual([s.notice.who, s.notice.what], ['Alex', 'buffering']);
  s = await act(env, a.code, b, 'buffering', { on: false });
  assert.equal(s.play.playing, true);
  assert.ok(s.play.pos >= 11.4 && s.play.pos < 11.7);
});

test('a play or a seek goes on from when it happened on that TV; while paused a seek is just where it stands', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env);
  await act(env, a.code, a, 'ready', { ready: true });
  const t = Date.now() - 300;   // pressed 0.3 s before the room heard
  let s = await act(env, a.code, a, 'play', { pos: 5, at: t });
  assert.deepEqual([s.play.playing, s.play.pos, s.play.at], [true, 5, t]);
  s = await act(env, a.code, a, 'seek', { pos: 900, at: t + 100 });
  assert.deepEqual([s.play.playing, s.play.pos, s.play.at], [true, 900, t + 100]);
  s = await act(env, a.code, a, 'seek', { pos: 950, at: 12 });   // a clock far off: the room's own time
  assert.ok(Math.abs(s.play.at - Date.now()) < 1000);
  await act(env, a.code, a, 'pause', { pos: 951 });
  s = await act(env, a.code, a, 'seek', { pos: 30 });
  assert.deepEqual([s.play.playing, s.play.pos], [false, 30]);
});

test('names and reactions are kept short and plain', async () => {
  const env = { ROOMS: rooms() };
  const a = await (await call(env, '', { name: '<b>' + 'x'.repeat(100) + '\n' })).json();
  assert.equal(a.members[0].name, 'b' + 'x'.repeat(23));
  const s = await act(env, a.code, a, 'react', { emoji: '😂😂😂😂😂😂' });
  assert.equal(s.notice.emoji.length, 8);
  assert.equal((await call(env, '', { name: 'y'.repeat(5000) })).status, 413);
});

test('leaving, and TVs that went quiet, leave the party; an empty party is gone', async () => {
  const env = { ROOMS: rooms() };
  const a = await start(env), b = await join(env, a.code, 'Alex');
  const room = env.ROOMS.objects.get(a.code);
  assert.ok(room.ctx.alarm > Date.now());
  const st = room.data.get('room');
  st.members[b.member].seen -= 61000; room.data.set('room', st);
  await room.alarm();
  const left = room.data.get('room');
  assert.deepEqual(Object.keys(left.members), [a.member]);
  assert.equal(left.notice.what, 'gone');
  assert.equal((await act(env, a.code, a, 'leave')).left, true);
  assert.equal(room.data.size, 0);
  assert.equal((await call(env, `/${a.code}/join`, { name: 'Late' })).status, 404);
});

test('where playback is at a moment', () => {
  assert.equal(positionAt({ playing: true, pos: 100, at: 10000 }, 9000), 100);   // not started yet
  assert.equal(positionAt({ playing: true, pos: 100, at: 10000 }, 12500), 102.5);
  assert.equal(positionAt({ playing: false, pos: 100, at: 10000 }, 99999), 100);
});

test('a relay without rooms says so', async () => {
  assert.equal((await call({}, '', { name: 'Sam' })).status, 503);
});

test('a phone that scanned the QR code gets a page that opens Cuelight with the code, and nothing else', async () => {
  const env = { ROOMS: rooms() };
  const r = await worker.fetch(new Request('https://relay.test/party/7ckpvv'), env);
  assert.equal(r.status, 200);
  assert.match(r.headers.get('Content-Type'), /text\/html/);
  const html = await r.text();
  assert.ok(html.includes('href="cuelight://party?code=7CKPVV"') && html.includes('7CK PVV'));
  assert.equal((await worker.fetch(new Request('https://relay.test/party/%3Cscript%3E'), env)).status, 404);
});
