import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import worker from '../src/worker.js';

const env = { MDBLIST_KEY: 'mkey', OPENSUBTITLES_KEY: 'okey', TMDB_API_KEY: 'tkey' };
let sent;
beforeEach(() => {
  sent = [];
  globalThis.fetch = async (url, init) => {
    sent.push({ url, ...init });
    return new Response('{"ok":true}', { status: 200, headers: { 'Content-Type': 'application/json' } });
  };
});
let ip = 0;
const call = (path, init = {}, e = env) =>
  worker.fetch(new Request('https://relay.test' + path, { ...init, headers: { 'CF-Connecting-IP': '10.0.0.' + (++ip % 250), ...(init.headers || {}) } }), e);

test('MDBList: the key goes in the address, only an IMDb id passes', async () => {
  assert.equal((await call('/mdblist/?i=tt0111161')).status, 200);
  assert.equal(sent[0].url, 'https://mdblist.com/api/?apikey=mkey&i=tt0111161');
  assert.equal((await call('/mdblist/?i=../../x')).status, 400);
  assert.equal(sent.length, 1);
});

test('OpenSubtitles: the key goes in a header, the viewer token and search pass', async () => {
  await call('/opensubtitles/subtitles?imdb_id=111161&languages=en', { headers: { Authorization: 'Bearer abc.def', 'User-Agent': 'StremioNativeTV v0.10.88' } });
  assert.equal(sent[0].url, 'https://api.opensubtitles.com/api/v1/subtitles?imdb_id=111161&languages=en');
  assert.equal(sent[0].headers['Api-Key'], 'okey');
  assert.equal(sent[0].headers['Authorization'], 'Bearer abc.def');
  assert.equal(sent[0].headers['User-Agent'], 'StremioNativeTV v0.10.88');
  await call('/opensubtitles/download', { method: 'POST', body: '{"file_id":222}' });
  assert.equal(sent[1].body, '{"file_id":222}');
});

const tmdbFetch = async (url) => {
  sent.push({ url });
  const m = url.match(/\/(movie|tv)\/(\d+)\/external_ids/);
  if (m[2] === '404') return new Response('{}', { status: 404 });
  return new Response(JSON.stringify({ imdb_id: m[2] === '5' ? null : 'tt' + (1000 + Number(m[2])) }), { status: 200 });
};
const kv = () => { const m = new Map(); return { m, get: async (k) => (m.has(k) ? m.get(k) : null), put: async (k, v) => { m.set(k, v); } }; };

test('TMDB ids: all looked up at once, kept, and given back by kind', async () => {
  globalThis.fetch = tmdbFetch;
  const IDS = kv();
  const r = await call('/tmdb/ids?movie=1,5,404&tv=7', {}, { ...env, IDS });
  assert.equal(r.status, 200);
  assert.deepEqual(await r.json(), { movie: { 1: 'tt1001', 5: '' }, tv: { 7: 'tt1007' } });
  assert.ok(sent.every(s => s.url.includes('api_key=tkey')));
  assert.equal(IDS.m.get('m:1'), 'tt1001');
  assert.equal(IDS.m.get('m:5'), '-');
  assert.equal(IDS.m.has('m:404'), false);
  sent = [];
  const again = await call('/tmdb/ids?movie=1,5', {}, { ...env, IDS });
  assert.deepEqual(await again.json(), { movie: { 1: 'tt1001', 5: '' }, tv: {} });
  assert.equal(sent.length, 0);
});

test('TMDB ids work without storage, and refuse what is not an id', async () => {
  globalThis.fetch = tmdbFetch;
  assert.deepEqual(await (await call('/tmdb/ids?tv=7')).json(), { movie: {}, tv: { 7: 'tt1007' } });
  assert.equal((await call('/tmdb/ids?movie=1,abc')).status, 400);
  assert.equal((await call('/tmdb/ids')).status, 400);
  assert.equal((await call('/tmdb/ids?movie=' + Array.from({ length: 41 }, (_, i) => i + 1).join(','))).status, 400);
});

test('anything else is refused without reaching a service', async () => {
  for (const [path, init] of [['/trakt/oauth/device/token', { method: 'POST', body: '{}' }], ['/opensubtitles/infos/user', {}], ['/tmdb/movie/1', {}], ['/tmdb/3/movie/1', {}], ['/', {}]])
    assert.equal((await call(path, init)).status, 404, path);
  assert.equal(sent.length, 0);
});

test('a service without a key answers 503, so the app falls back', async () => {
  const r = await call('/mdblist/?i=tt1', {}, { ...env, MDBLIST_KEY: undefined });
  assert.equal(r.status, 503);
  assert.equal(sent.length, 0);
});

test('large bodies are refused', async () => {
  const r = await call('/opensubtitles/login', { method: 'POST', body: 'x'.repeat(9000) });
  assert.equal(r.status, 413);
});

test('one address is limited per minute', async () => {
  let last;
  for (let i = 0; i < 61; i++)
    last = await worker.fetch(new Request('https://relay.test/mdblist/?i=tt1', { headers: { 'CF-Connecting-IP': '192.0.2.9' } }), env);
  assert.equal(last.status, 429);
});

test('the rate-limit binding is used when there is one', async () => {
  const r = await call('/mdblist/?i=tt1', {}, { ...env, LIMITER: { limit: async () => ({ success: false }) } });
  assert.equal(r.status, 429);
});

test('a service that does not answer gives 502', async () => {
  globalThis.fetch = async () => { throw new Error('down'); };
  assert.equal((await call('/mdblist/?i=tt1')).status, 502);
});

test('answers are never cached', async () => {
  const r = await call('/mdblist/?i=tt1');
  assert.equal(r.headers.get('Cache-Control'), 'no-store');
});

const line = (t, c) => `${t} ${c.repeat(48)}`;
test('Glow: kept by file hash, added to, never replaced, and checked', async () => {
  const IDS = kv(), e = { ...env, IDS };
  assert.equal((await call('/glow/4b67d1bb41215387', {}, e)).status, 404);
  const up = await call('/glow/4b67d1bb41215387', { method: 'POST', body: ['ar 2.39', line('2.50', 'a'), line('6.25', 'b')].join('\n') + '\n' }, e);
  assert.deepEqual(await up.json(), { cuts: 2 });
  await call('/glow/4b67d1bb41215387', { method: 'POST', body: ['ar 1.78', line('6.25', 'f'), line('11.00', 'c')].join('\n') }, e);
  const got = await (await call('/glow/4b67d1bb41215387', {}, e)).text();
  assert.equal(got, ['ar 2.39', line('2.50', 'a'), line('6.25', 'b'), line('11.00', 'c')].join('\n') + '\n');   // what was kept first stays
  assert.equal((await call('/glow/not-a-hash', {}, e)).status, 400);
  assert.equal((await call('/glow/4b67d1bb41215387', { method: 'POST', body: 'http://example.com/film.mkv' }, e)).status, 400);
  assert.equal((await call('/glow/4b67d1bb41215387', { method: 'POST', body: line('1', 'a') + ' extra' }, e)).status, 400);
  assert.equal((await call('/glow/4b67d1bb41215387', {}, env)).status, 503);   // no storage: nothing kept
  assert.equal(sent.length, 0);   // nothing went anywhere else
});

test('Verdicts: counted by kind of TV and file, asked for many at once, checked', async () => {
  const IDS = kv(), e = { ...env, IDS }, a = '0123456789abcdef', b = 'fedcba9876543210';
  for (const said of ['fail', 'fail', 'ok']) assert.equal((await call(`/verdicts/${a}?d=webos9`, { method: 'POST', body: said }, e)).status, 200);
  await call(`/verdicts/${b}?d=android-shield`, { method: 'POST', body: 'ok' }, e);
  assert.deepEqual(await (await call(`/verdicts?d=webos9&k=${a},${b}`, {}, e)).json(), { [a]: [1, 2] });   // b was seen on another kind only
  assert.deepEqual(await (await call(`/verdicts?d=android-shield&k=${b}`, {}, e)).json(), { [b]: [1, 0] });
  assert.equal((await call(`/verdicts/${a}?d=webos9`, { method: 'POST', body: 'http://example.com/film.mkv' }, e)).status, 400);
  assert.equal((await call(`/verdicts/not-a-key?d=webos9`, { method: 'POST', body: 'ok' }, e)).status, 400);
  assert.equal((await call(`/verdicts?d=Web%20OS&k=${a}`, {}, e)).status, 400);
  assert.equal((await call(`/verdicts?d=webos9&k=${Array(61).fill(a).join(',')}`, {}, e)).status, 400);
  assert.equal((await call(`/verdicts?d=webos9&k=${a}`, {}, env)).status, 503);   // no storage: nothing kept
  assert.equal(sent.length, 0);
});
