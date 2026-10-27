import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import worker from '../src/worker.js';

const env = { MDBLIST_KEY: 'mkey', OPENSUBTITLES_KEY: 'okey' };
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

test('anything else is refused without reaching a service', async () => {
  for (const [path, init] of [['/trakt/oauth/device/token', { method: 'POST', body: '{}' }], ['/opensubtitles/infos/user', {}], ['/tmdb/movie/1', {}], ['/', {}]])
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
