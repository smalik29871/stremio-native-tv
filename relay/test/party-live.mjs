// The parts of a party the unit tests can't hold: a TV's wait answered the moment the room
// changes, and held for 25 s when nothing does. Against a running relay:
//   npx wrangler dev --port 8799 &  node test/party-live.mjs http://127.0.0.1:8799
import assert from 'node:assert/strict';

const base = (process.argv[2] || 'http://127.0.0.1:8799') + '/party';
const post = async (path, body) => (await fetch(base + path, { method: 'POST', body: JSON.stringify(body) })).json();
const wait = async (who, v) => {
  const t0 = Date.now();
  const r = await fetch(`${base}/${who.code}/wait?member=${who.member}&token=${who.token}&v=${v}`);
  return { ms: Date.now() - t0, status: r.status, room: await r.json() };
};

const a = await post('', { name: 'Sam', device: 'webos10' });
const b = await post(`/${a.code}/join`, { name: 'Alex', device: 'android-pixel-8' });
b.code = a.code;
console.log(`party ${a.code}: ${b.members.length} in`);

// behind: at once
let w = await wait(a, a.v);
assert.equal(w.room.v, b.v);
assert.ok(w.ms < 500, `behind, answered after ${w.ms} ms`);
console.log(`a TV that is behind: answered after ${w.ms} ms`);

// up to date: answered when Alex pauses, half a second later
const held = wait(a, b.v);
await new Promise(r => setTimeout(r, 500));
const sent = Date.now();
await post(`/${a.code}/do`, { member: b.member, token: b.token, act: 'pause', pos: 42 });
w = await held;
const after = Date.now() - sent;
assert.equal(w.status, 200);
assert.deepEqual([w.room.v, w.room.play.pos, w.room.notice.who, w.room.notice.what], [b.v + 1, 42, 'Alex', 'pause']);
assert.ok(w.ms >= 500 && after < 300, `held ${w.ms} ms, ${after} ms after the change`);
console.log(`a waiting TV: held ${w.ms} ms, answered ${after} ms after the pause`);

// two TVs waiting: both answered by one change
const both = [wait(a, w.room.v), wait(b, w.room.v)];
await new Promise(r => setTimeout(r, 300));
await post(`/${a.code}/do`, { member: a.member, token: a.token, act: 'react', emoji: '👏' });
const [wa, wb] = await Promise.all(both);
assert.deepEqual([wa.room.notice.emoji, wb.room.notice.emoji], ['👏', '👏']);
w = wa;
console.log(`two waiting TVs: both answered (${wa.ms} and ${wb.ms} ms)`);

// a wrong token is never held
const denied = await wait({ ...a, token: 'nope' }, w.room.v);
assert.equal(denied.status, 403);

// nothing new: 25 s, then "the same", with the time
if (!process.env.QUICK) {
  const v = w.room.v;
  w = await wait(a, v);
  assert.deepEqual([w.room.v, w.room.same], [v, true]);
  assert.ok(w.ms >= 24500 && w.ms < 27000, `held ${w.ms} ms`);
  assert.ok(Math.abs(w.room.t - Date.now()) < 1000);
  console.log(`nothing new: answered after ${w.ms} ms`);
}

await post(`/${a.code}/do`, { member: a.member, token: a.token, act: 'leave' });
await post(`/${a.code}/do`, { member: b.member, token: b.token, act: 'leave' });
assert.equal((await fetch(`${base}/${a.code}/join`, { method: 'POST', body: '{}' })).status, 404);
console.log('everyone left: the party is gone');
