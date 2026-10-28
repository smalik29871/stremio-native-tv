# Cue Party: watching together

Friends in different homes watch the same episode at the same moment, each on their own TV
and from their own sources. Only what is on and where it is goes between them: the title and
episode, the position, play, pause and seek. Never the video, a link or an account.

## What was checked before building

| Piece | Finding |
|---|---|
| Cloudflare free plan | Durable Objects are on the Workers Free plan since April 2025 (SQLite-backed only): 100,000 requests a day, over that requests fail rather than cost. |
| Cost of a room | A Durable Object answering a held request is billed for that time; one whose only connections are hibernatable WebSockets is not. So TVs wait on the Worker, and the Worker waits on the room by WebSocket. |
| WebSockets on the TV | The app uses the LG's own libcurl (loaded at run time), whose WebSocket support can't be relied on. TVs hold an ordinary request open instead (long-polling). |
| The room design | Prototyped with `wrangler dev`: a change reached a waiting TV 29 ms after it was sent; a TV that is behind is answered at once; a held request returns after 25 s with nothing new. |
| The room as built | `relay/src/party.js`, checked under `wrangler dev` (`relay/test/party-live.mjs`): a TV that is behind is answered in 5 ms, a waiting TV 14 ms after a pause is sent, two waiting TVs by the same change, a quiet wait after 25 s. Each wait is one room request (the change itself comes over the WebSocket), so a TV in a party makes about 150 requests an hour. |
| HTTP on the TV | The open request runs on its own thread, not one of the six pooled connections that addons, artwork and playback share. |
| Seek time | Neither player says when a seek is done, so since 0.10.95 a seek counts as done once it plays on past its target (Diagnostics > Speed > Seek recovery). Measured: the LG (webOS 10.3.2) 918 ms, and about 2 s for a long jump; an Android phone 1.4 s on average, 1.8 s at most over 5 seeks. So commands are set 2.5 s ahead. |
| Speed nudging | Android's player takes any speed (pitch kept). Whether the LG plays 0.95x smoothly with sound is unknown; the first nudge checks that the position's pace really changed, and a TV where it did not is corrected by seeks instead. |

## How it works

- **The room** (Cloudflare Durable Object, one per party): who is in, the title and episode,
  and the playback state `{playing, position, at}` with a version. TVs wait for the version to
  change through the Worker; the room hibernates between events.
- **Joining**: a party code and QR code on the TV (the number pad and QR from Sync with another
  device); a phone can join as well. Codes are random and end when the party is empty.
- **Clock**: every answer carries the room's time; each TV keeps a smoothed offset and round
  trip (moving average, as Syncplay does), so "play from 12:04 at T" means the same moment
  everywhere.
- **Commands at a room time**: play, pause and seek are sent a little ahead (about the slowest
  TV's seek time) and each TV acts at that moment, so all start together rather than one
  network trip apart (Jellyfin SyncPlay's approach).
- **Ready check**: a start waits until every TV has its source loaded (Syncplay's ready state),
  with "Waiting for Alex" on the others.
- **Buffering**: when a TV buffers, the others pause and say which one they wait for; they go on
  together.
- **Pause realigns**: a pause moves every TV to the pauser's position (Syncplay's sync on pause).
- **Drift**: a small difference is played away (a few percent slower or faster for a second or
  two) where the TV can; a large one is a seek. Starting points from the projects below (to be
  set from the TVs' measured seek times): Syncplay seeks back at 4 s ahead and forward at 5 s
  behind, and slows to 0.95x from 1.5 s ahead until within 0.1 s; Jellyfin nudges speed from
  60 ms to 3 s off and seeks from 0.4 s.
- **Different copies**: each TV picks its own source; the same release is preferred when both
  have it (the file fingerprint from the play verdicts), and when the lengths differ by more
  than a few seconds the party says so (Syncplay warns at 2.5 s).
- **On screen**: "Start a Cue Party" on the title page and in the player's menu, a pill with who
  is in, short notices ("Sam paused"), and a few quick reactions instead of chat (typing on a
  remote is slow).

## Sources

- Syncplay (Apache-2.0): https://github.com/Syncplay/syncplay — `syncplay/constants.py`
  (thresholds) and `syncplay/client.py` (rewind, fast-forward, slow-down, ready to unpause).
- Jellyfin SyncPlay (GPL: ideas only, no code): `jellyfin-web/src/plugins/syncPlay/core/PlaybackCore.js`
  (commands at server time, speed-to-sync and skip-to-sync, buffering).
- Cloudflare: https://developers.cloudflare.com/changelog/2025-04-07-durable-objects-free-tier/ ,
  https://developers.cloudflare.com/durable-objects/platform/pricing ,
  https://developers.cloudflare.com/durable-objects/reference/faq
