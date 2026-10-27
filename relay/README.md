# Cuelight key relay

A Cloudflare Worker that holds the app's own keys for MDBList and OpenSubtitles, so the
packages don't have to carry them. The app sends the few requests that need one of these keys
here, without the key; the relay adds it and passes the answer back.

- Only the requests the app makes get through: MDBList ratings for an IMDb id, OpenSubtitles
  sign-in, search and download links. Anything else is a 404.
- Each address gets 60 requests a minute per service, bodies are capped at 8 KB, answers are
  never cached and nothing a request carries is logged.
- A key someone entered in Settings goes straight to the service, never through here.
- When the relay is down or has no key for a service, the app goes direct with the key built
  into the package (if the build has one) and tries the relay again ten minutes later;
  without a built-in key that feature quietly stays off until then.
- TMDB is not relayed: the app asks it for too much (every poster and page), so its key stays
  built in, scrambled.
- Trakt is not relayed either: its apps sign in with PKCE, which needs only the client ID, and
  that ID is public by design (every Trakt request names it).

Use the same OpenSubtitles key here as in the GitHub secrets while both exist, so a sign-in
made through one works through the other.

## Deploying it (once)

In the browser, with a free Cloudflare account (dash.cloudflare.com):

1. Workers & Pages > Create > Start with Hello World. The first time, pick a `workers.dev`
   subdomain. Name the Worker `cuelight-keys` and Deploy.
2. Edit code: replace everything with `src/worker.js`, then Deploy.
3. The Worker's Settings > Variables and Secrets > Add, type Secret: `MDBLIST_KEY` and
   `OPENSUBTITLES_KEY`. Deploy.

Or from a terminal, with Node.js 18 or newer (this also sets up Cloudflare's shared rate limit
from `wrangler.toml`):

```sh
cd relay
npm install
npx wrangler login                          # opens the browser to sign in to Cloudflare
npx wrangler secret put MDBLIST_KEY         # each asks for the value; paste it there
npx wrangler secret put OPENSUBTITLES_KEY
npx wrangler deploy
```

The relay's address is `https://cuelight-keys.<your-subdomain>.workers.dev`. Put it on the
first line of `relay/url.txt` and commit that file: builds made after it send these requests
to the relay. The address isn't a secret.

## Changing a key

Replace the secret (Settings > Variables and Secrets, or `npx wrangler secret put NAME`). It
takes effect at once, for every installed copy of the app, with no new release. Revoke the old
key at the service afterwards.

Once the relay works on your devices you can delete the matching GitHub secrets
(`MDBLIST_KEY`, `OPENSUBTITLES_API_KEY`) so new packages carry no copy at all; the app then
relies on the relay for those two.

## Checking it

```sh
npm test                                                      # the relay's own tests
curl -s https://cuelight-keys.<your-subdomain>.workers.dev/mdblist/?i=tt0111161 | head -c 200
```

The free plan allows 100,000 requests a day, far more than the app's ratings and subtitle
searches need. The Worker's Metrics tab shows the counts.
