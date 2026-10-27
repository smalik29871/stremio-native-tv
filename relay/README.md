# Cuelight key relay

A Cloudflare Worker that holds the app's own keys for Trakt (the client secret), MDBList and
OpenSubtitles, so the packages don't have to carry them. The app sends the few requests that
need one of these keys here, without the key; the relay adds it and passes the answer back.

- Only the requests the app makes get through: Trakt sign-in and renewal, MDBList ratings for
  an IMDb id, OpenSubtitles sign-in, search and download links. Anything else is a 404.
- Each address gets 60 requests a minute per service, bodies are capped at 8 KB, answers are
  never cached and nothing a request carries is logged.
- A key someone entered in Settings goes straight to the service, never through here.
- When the relay is down or has no key for a service, the app goes direct with the key built
  into the package (if the build has one) and tries the relay again ten minutes later;
  without a built-in key that feature quietly stays off until then.
- TMDB is not relayed: the app asks it for too much (every poster and page), so its key stays
  built in, scrambled.

The Trakt **client ID** stays in the app as well (as it must: every Trakt request names it);
only the secret moves here. Use the same Trakt app and OpenSubtitles key here as in the
GitHub secrets, or sign-ins made one way won't renew the other.

## Deploying it (once)

You need a free Cloudflare account and Node.js 18 or newer.

```sh
cd relay
npm install
npx wrangler login                          # opens the browser to sign in to Cloudflare
npx wrangler secret put TRAKT_CLIENT_ID     # each asks for the value; paste it there
npx wrangler secret put TRAKT_CLIENT_SECRET
npx wrangler secret put MDBLIST_KEY
npx wrangler secret put OPENSUBTITLES_KEY
npx wrangler deploy
```

`deploy` prints the relay's address, e.g. `https://cuelight-keys.<your-name>.workers.dev`.
Put it on the first line of `relay/url.txt` and commit that file: builds made after it send
these requests to the relay. The address isn't a secret.

The first `secret put` before any `deploy` may offer to create the Worker; say yes.

## Changing a key

`npx wrangler secret put NAME` again with the new value. It takes effect at once, for every
installed copy of the app, with no new release. Revoke the old key at the service afterwards.

Once the relay works on your devices you can delete the matching GitHub secrets
(`TRAKT_CLIENT_SECRET`, `MDBLIST_KEY`, `OPENSUBTITLES_API_KEY`) so new packages carry no
copy at all; the app then relies on the relay for those three.

## Checking it

```sh
npm test                                                      # the relay's own tests
curl -s https://cuelight-keys.<your-name>.workers.dev/mdblist/?i=tt0111161 | head -c 200
```

The free plan allows 100,000 requests a day, far more than the app's sign-ins, ratings and
subtitle searches need. Cloudflare's dashboard (Workers > cuelight-keys > Metrics) shows the
counts.
