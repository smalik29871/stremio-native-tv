// Cuelight's key relay, a Cloudflare Worker. The app sends the few requests that need a service
// key here without the key; the relay adds it from its own secrets and passes the answer back,
// so the keys never ship inside the app. Only those requests are let through, each device has
// its own limit, and nothing it carries is logged.
//
// Secrets (set with `npx wrangler secret put NAME`, never in this file):
//   MDBLIST_KEY, OPENSUBTITLES_KEY, TMDB_API_KEY
// (Trakt isn't here: its apps sign in with PKCE, which needs no secret.)
// Storage: a KV namespace bound as IDS keeps the IMDb ids it has looked up and the letterbox's
// colours of films' cuts (optional; without it neither is kept).

const UPSTREAM = {
  mdblist: 'https://mdblist.com/api',
  opensubtitles: 'https://api.opensubtitles.com/api/v1',
  tmdb: 'https://api.themoviedb.org/3',
};

// what the app asks for, and nothing else
const ROUTES = [
  { svc: 'mdblist', method: 'GET', path: '/', secret: 'MDBLIST_KEY' },
  { svc: 'opensubtitles', method: 'POST', path: '/login', secret: 'OPENSUBTITLES_KEY' },
  { svc: 'opensubtitles', method: 'GET', path: '/subtitles', secret: 'OPENSUBTITLES_KEY' },
  { svc: 'opensubtitles', method: 'POST', path: '/download', secret: 'OPENSUBTITLES_KEY' },
  { svc: 'tmdb', method: 'GET', path: '/ids', secret: 'TMDB_API_KEY' },
];

const MAX_BODY = 8 * 1024;
const PER_MINUTE = 60;   // requests a minute from one address, per service

// A per-isolate count: Cloudflare runs many isolates, so this is a floor, not a wall. A
// rate-limiting binding named LIMITER (see wrangler.toml) is used instead when there is one.
const counts = new Map();
async function allowed(env, key) {
  if (env.LIMITER) return (await env.LIMITER.limit({ key })).success;
  const minute = Math.floor(Date.now() / 60000), c = counts.get(key);
  if (!c || c.minute !== minute) { counts.set(key, { minute, n: 1 }); if (counts.size > 5000) counts.clear(); return true; }
  return ++c.n <= PER_MINUTE;
}

// IMDb ids for TMDB titles (?movie=603,604&tv=1396): a person's titles or a filtered list name
// up to 24, each a lookup of its own; here they run at once, and KV keeps the answers, so each
// title is looked up once for every device. Ids TMDB didn't answer for are left out (the app
// asks for those itself); "" means the title has no IMDb id.
const MAX_IDS = 40;
async function tmdbIds(url, env, key) {
  const want = [];
  for (const kind of ['movie', 'tv'])
    for (const id of (url.searchParams.get(kind) || '').split(',').filter(Boolean)) {
      if (!/^\d{1,9}$/.test(id)) return json(400, { error: 'TMDB ids are numbers' });
      want.push([kind, id]);
    }
  if (!want.length || want.length > MAX_IDS) return json(400, { error: 'between 1 and ' + MAX_IDS + ' ids' });
  const out = { movie: {}, tv: {} };
  await Promise.all(want.map(async ([kind, id]) => {
    const k = kind[0] + ':' + id;
    let v = env.IDS ? await env.IDS.get(k).catch(() => null) : null;
    if (v === null) {
      try {
        const r = await fetch(`${UPSTREAM.tmdb}/${kind}/${id}/external_ids?api_key=${encodeURIComponent(key)}`, { headers: { Accept: 'application/json' } });
        if (!r.ok) return;
        const j = await r.json();
        v = typeof j.imdb_id === 'string' && /^tt\d{1,10}$/.test(j.imdb_id) ? j.imdb_id : '-';
      } catch { return; }
      if (env.IDS) await env.IDS.put(k, v, { expirationTtl: 180 * 86400 }).catch(() => {});   // past the free writes a day: not kept
    }
    out[kind][id] = v === '-' ? '' : v;
  }));
  return json(200, out);
}

// The letterbox's colours of a film's cuts, by the file's hash (the 16 hex digits
// OpenSubtitles uses): "seconds" and 4 x 2 colours in hex, a line a cut. A TV that played the
// file sends what it saw, added to what is kept (a cut already kept stays as it is); the next
// TV to play the file gets them all at the start. Colours and times only, never a link. An
// "ar" line first may give the picture's shape (bars baked into the frame show only there).
const GLOW_LINE = /^\d{1,5}(\.\d{1,2})? [0-9a-f]{48}$/, GLOW_AR = /^ar [0-3]\.\d{2}$/;
const GLOW_MAX = 6000, GLOW_BODY = 400 * 1024;
async function glow(request, env, hash) {
  if (!/^[0-9a-f]{16}$/.test(hash)) return json(400, { error: 'a file hash is 16 hex digits' });
  if (!env.IDS) return json(503, { error: 'this relay keeps nothing' });
  const k = 'g:' + hash;
  if (request.method === 'GET') {
    const v = await env.IDS.get(k).catch(() => null);
    return v === null ? json(404, { error: 'none for this file yet' }) : new Response(v, { status: 200, headers: { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' } });
  }
  const body = await request.text();
  if (body.length > GLOW_BODY) return json(413, { error: 'too large' });
  const lines = body.split('\n').filter(Boolean);
  const ar = GLOW_AR.test(lines[0]) ? lines.shift() : null;
  if (!lines.length || lines.length > GLOW_MAX || !lines.every(l => GLOW_LINE.test(l))) return json(400, { error: 'lines of seconds and 48 hex digits' });
  const cuts = new Map();
  let keptAr = null;
  for (const l of ((await env.IDS.get(k).catch(() => null)) || '').split('\n').filter(Boolean)) {
    if (GLOW_AR.test(l)) keptAr = l; else cuts.set(l.slice(0, l.indexOf(' ')), l);
  }
  const before = cuts.size;
  for (const l of lines) { const t = l.slice(0, l.indexOf(' ')); if (!cuts.has(t)) cuts.set(t, l); }
  if (cuts.size === before && (keptAr || !ar)) return json(200, { cuts: before });   // nothing new: no write
  const kept = [...cuts.values()].sort((a, b) => parseFloat(a) - parseFloat(b)).slice(0, GLOW_MAX);
  if (keptAr || ar) kept.unshift(keptAr || ar);
  await env.IDS.put(k, kept.join('\n') + '\n', { expirationTtl: 365 * 86400 }).catch(() => {});   // past the free writes a day: not kept
  return json(200, { cuts: cuts.size });
}

const json = (status, obj) => new Response(JSON.stringify(obj), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/([a-z]+)(\/.*)?$/);
    const svc = m && m[1], path = (m && m[2]) || '/';
    if (svc === 'glow' && (request.method === 'GET' || request.method === 'POST')) {   // no key: the relay's own storage
      const who = request.headers.get('CF-Connecting-IP') || 'unknown';
      if (!(await allowed(env, 'glow:' + who))) return json(429, { error: 'too many requests' });
      return glow(request, env, path.slice(1));
    }
    const route = ROUTES.find(r => r.svc === svc && r.path === path && r.method === request.method);
    if (!route) return json(404, { error: 'not a relayed request' });
    const key = env[route.secret];
    if (!key) return json(503, { error: 'this relay has no key for ' + svc });   // the app falls back
    const who = request.headers.get('CF-Connecting-IP') || 'unknown';
    if (!(await allowed(env, svc + ':' + who))) return json(429, { error: 'too many requests' });

    if (svc === 'tmdb') return tmdbIds(url, env, key);

    let body = null;
    if (request.method === 'POST') {
      body = await request.text();
      if (body.length > MAX_BODY) return json(413, { error: 'too large' });
    }
    const headers = {
      'Accept': 'application/json',
      'User-Agent': request.headers.get('User-Agent') || 'Cuelight',
    };
    let target = UPSTREAM[svc] + path;

    if (svc === 'mdblist') {
      const i = url.searchParams.get('i') || '';
      if (!/^tt\d{1,10}$/.test(i)) return json(400, { error: 'an IMDb id is needed' });
      target = UPSTREAM.mdblist + '/?apikey=' + encodeURIComponent(key) + '&i=' + i;
    } else {   // opensubtitles: its search parameters as the app sent them
      target += url.search;
      headers['Api-Key'] = key;
      if (body !== null) headers['Content-Type'] = 'application/json';
      const auth = request.headers.get('Authorization');   // a signed-in viewer's own token
      if (auth && /^Bearer [\w.-]{1,2048}$/.test(auth)) headers['Authorization'] = auth;
    }

    let up;
    try {
      up = await fetch(target, { method: request.method, headers, body, redirect: 'follow' });
    } catch {
      return json(502, { error: svc + ' did not answer' });
    }
    return new Response(up.body, {
      status: up.status,
      headers: { 'Content-Type': up.headers.get('Content-Type') || 'application/json', 'Cache-Control': 'no-store' },
    });
  },
};
