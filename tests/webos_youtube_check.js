// Cuelight YouTube (deploy/webos-youtube) in a browser engine, with YouTube's page faked: the
// launch page opens the right address, and the script inside YouTube's page closes on Back, loops
// the fireplace under a clock, closes when the video asked for ends (not after an advert), skips
// the sponsor segments SponsorBlock lists (asking by hash prefix) and leaves adverts out when asked.
//   node tests/webos_youtube_check.js     (needs Playwright, as the smoke screenshots do)
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const DIR = path.join(__dirname, '..', 'deploy', 'webos-youtube');
const script = fs.readFileSync(path.join(DIR, 'webOSUserScripts/userScript.js'), 'utf8');
async function page(browser, params, opts = {}) {
  const ctx = await browser.newContext();
  const p = await ctx.newPage();
  if (opts.sponsors) await p.route('https://sponsor.ajay.app/**', r => { opts.asked.push(r.request().url()); r.fulfill({ contentType: 'application/json', headers: { 'access-control-allow-origin': '*' }, body: JSON.stringify(opts.sponsors) }); });
  await p.addInitScript(([params, script]) => {
    window.PalmSystem = { launchParams: JSON.stringify(params) };
    window.__closed = 0; window.close = () => { window.__closed++; };
    document.addEventListener('DOMContentLoaded', () => { const s = document.createElement('script'); s.textContent = script; document.head.appendChild(s); });
  }, [params, script]);
  await p.route('https://www.youtube.com/**', r => r.request().url().includes('/youtubei/')
    ? r.fulfill({ contentType: 'application/json', body: JSON.stringify({ adPlacements: [1], playerAds: [2], videoDetails: { videoId: params.v } }) })
    : r.fulfill({ contentType: 'text/html', body: '<html><head></head><body><video id=v muted></video></body></html>' }));
  await p.goto('https://www.youtube.com/tv#/watch?v=' + params.v);
  return p;
}
(async () => {
  const b = await chromium.launch();
  let ok = true; const check = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); ok = ok && c; };
  // Back closes
  let p = await page(b, { v: 'abcdefghijk', mode: 'play' });
  await p.keyboard.down('Escape'); // not Back: stays
  await p.evaluate(() => document.dispatchEvent(new KeyboardEvent('keydown', { keyCode: 461, bubbles: true })));
  check(await p.evaluate(() => window.__closed) === 1, 'Back (461) closes in play mode');
  // loop: video.loop set, clock shown, any key closes
  p = await page(b, { v: 'abcdefghijk', mode: 'loop' });
  await p.waitForTimeout(700);
  check(await p.evaluate(() => document.querySelector('video').loop) === true, 'loop mode repeats the video');
  check(await p.evaluate(() => /\d+:\d\d/.test([...document.querySelectorAll('div')].map(d => d.textContent).join(' '))), 'loop mode shows the clock');
  await p.evaluate(() => document.dispatchEvent(new KeyboardEvent('keydown', { keyCode: 13, bubbles: true })));
  check(await p.evaluate(() => window.__closed) === 1, 'any key ends the fireplace');
  // play: closes when the wanted video ended, not during an advert
  p = await page(b, { v: 'abcdefghijk', mode: 'play' });
  await p.evaluate(() => {
    const v = document.querySelector('video');
    let t = 0, paused = false, ended = false;
    Object.defineProperty(v, 'currentTime', { get: () => t });
    Object.defineProperty(v, 'paused', { get: () => paused });
    Object.defineProperty(v, 'ended', { get: () => ended });
    window.__set = (a, b2, c) => { t = a; paused = b2; ended = c; };
    document.body.classList.add('ad-showing');
  });
  await p.evaluate(() => __set(5, false, false)); await p.waitForTimeout(700);
  await p.evaluate(() => __set(5, true, true)); await p.waitForTimeout(700);
  check(await p.evaluate(() => window.__closed) === 0, 'an advert ending does not close it');
  await p.evaluate(() => { document.body.classList.remove('ad-showing'); __set(30, false, false); }); await p.waitForTimeout(700);
  await p.evaluate(() => __set(120, true, true)); await p.waitForTimeout(700);
  check(await p.evaluate(() => window.__closed) >= 1, 'the video ending closes it');
  // adverts: left out of the player's JSON only when asked
  const AD = '{"adPlacements":[1],"adSlots":[2],"playerResponse":{"playerAds":[3],"streamingData":{}},"videoDetails":{}}';
  for (const blockAds of [1, 0]) {
    p = await page(b, { v: 'abcdefghijk', mode: 'play', blockAds });
    await p.waitForTimeout(200);
    const got = await p.evaluate(s => JSON.parse(s), AD);
    const fetched = await p.evaluate(() => fetch('/youtubei/v1/player').then(r => r.json()));
    const clean = !('adPlacements' in got) && !('adSlots' in got) && !('playerAds' in got.playerResponse) && 'streamingData' in got.playerResponse && !('adPlacements' in fetched) && 'videoDetails' in fetched;
    check(blockAds ? clean : 'adPlacements' in got && 'adPlacements' in fetched, blockAds ? 'adverts are taken out of the player\'s JSON (parsed and fetched)' : 'adverts are left alone when not asked');
  }
  // sponsor segments: asked by hash prefix, skipped once, with a notice; nothing asked when off
  const SEGS = [{ videoID: 'zzzzzzzzzzz', segments: [{ category: 'sponsor', actionType: 'skip', segment: [1, 50], UUID: 'other' }] },
                { videoID: 'abcdefghijk', segments: [{ category: 'sponsor', actionType: 'skip', segment: [10, 20], UUID: 'a' },
                                                     { category: 'poi_highlight', actionType: 'poi', segment: [30, 30], UUID: 'b' }] }];
  for (const skipSponsors of [1, 0]) {
    const asked = [];
    p = await page(b, { v: 'abcdefghijk', mode: 'play', skipSponsors }, { sponsors: SEGS, asked });
    await p.evaluate(() => {
      const v = document.querySelector('video');
      let t = 0;
      Object.defineProperty(v, 'currentTime', { get: () => t, set: x => { t = x; } });
      Object.defineProperty(v, 'paused', { get: () => false });
    });
    await p.waitForTimeout(1200);
    await p.evaluate(() => { document.querySelector('video').currentTime = 12; });
    await p.waitForTimeout(800);
    const t = await p.evaluate(() => document.querySelector('video').currentTime);
    if (skipSponsors) {
      check(asked.length === 1 && /\/api\/skipSegments\/[0-9a-f]{4}\?/.test(asked[0]) && !asked[0].includes('abcdefghijk'), 'SponsorBlock is asked by hash prefix only: ' + (asked[0] || 'nothing asked'));
      check(t === 20, 'the sponsor segment is skipped (12s -> ' + t + 's)');
      check(await p.evaluate(() => document.documentElement.textContent.includes('Skipped a sponsor segment')), 'a notice says it skipped');
      await p.evaluate(() => { document.querySelector('video').currentTime = 15; });
      await p.waitForTimeout(800);
      check(await p.evaluate(() => document.querySelector('video').currentTime) === 15, 'seeking back into it plays it');
    } else {
      check(asked.length === 0 && t === 12, 'off: SponsorBlock is not asked and nothing is skipped');
    }
  }
  // relaunched by anyone: only a video id goes into the address
  p = await page(b, { v: 'abcdefghijk', mode: 'play' });
  await p.evaluate(() => document.dispatchEvent(new CustomEvent('webOSRelaunch', { detail: { v: 'x"/><img src=x>', t: 5 } })));
  check(await p.evaluate(() => location.hash) === '#/watch?v=abcdefghijk', 'a relaunch with something other than a video id is ignored');
  await p.evaluate(() => document.dispatchEvent(new CustomEvent('webOSRelaunch', { detail: { v: 'zyxwvutsrqp', t: -3 } })));
  check(await p.evaluate(() => location.hash) === '#/watch?v=zyxwvutsrqp', 'a relaunch with a video id opens it (a negative start dropped)');
  // the launch page

      for (const [params, want] of [[{ v: 'dQw4w9WgXcQ', t: 42 }, 'https://www.youtube.com/tv#/watch?v=dQw4w9WgXcQ&t=42s'],
                                [{ v: 'bad"><script>' }, 'https://www.youtube.com/tv'], [{}, 'https://www.youtube.com/tv']]) {
    const p = await (await b.newContext()).newPage();
    await p.addInitScript(pp => { window.PalmSystem = { launchParams: JSON.stringify(pp) }; }, params);
    let got = null;
    await p.route('https://www.youtube.com/**', r => { got = r.request().url(); r.fulfill({ contentType: 'text/html', body: '' }); });
    await p.goto('file://' + path.join(DIR, 'index.html'));
    await p.waitForTimeout(300);
    const url = p.url();
    const good = url === want;
    console.log((good ? 'ok   ' : 'FAIL ') + JSON.stringify(params) + ' -> ' + url);
    ok = ok && good;
  }
  await b.close();
  process.exit(ok ? 0 : 1);
})();
