// Cuelight's key relay, a Cloudflare Worker. The app sends the few requests that need a service
// key here without the key; the relay adds it from its own secrets and passes the answer back,
// so the keys never ship inside the app. Only those requests are let through, each device has
// its own limit, and nothing it carries is logged.
//
// Secrets (set with `npx wrangler secret put NAME`, never in this file):
//   MDBLIST_KEY, OPENSUBTITLES_KEY
// (Trakt isn't here: its apps sign in with PKCE, which needs no secret.)

const UPSTREAM = {
  mdblist: 'https://mdblist.com/api',
  opensubtitles: 'https://api.opensubtitles.com/api/v1',
};

// what the app asks for, and nothing else
const ROUTES = [
  { svc: 'mdblist', method: 'GET', path: '/', secret: 'MDBLIST_KEY' },
  { svc: 'opensubtitles', method: 'POST', path: '/login', secret: 'OPENSUBTITLES_KEY' },
  { svc: 'opensubtitles', method: 'GET', path: '/subtitles', secret: 'OPENSUBTITLES_KEY' },
  { svc: 'opensubtitles', method: 'POST', path: '/download', secret: 'OPENSUBTITLES_KEY' },
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

const json = (status, obj) => new Response(JSON.stringify(obj), { status, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/([a-z]+)(\/.*)?$/);
    const svc = m && m[1], path = (m && m[2]) || '/';
    const route = ROUTES.find(r => r.svc === svc && r.path === path && r.method === request.method);
    if (!route) return json(404, { error: 'not a relayed request' });
    const key = env[route.secret];
    if (!key) return json(503, { error: 'this relay has no key for ' + svc });   // the app falls back
    const who = request.headers.get('CF-Connecting-IP') || 'unknown';
    if (!(await allowed(env, svc + ':' + who))) return json(429, { error: 'too many requests' });

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
