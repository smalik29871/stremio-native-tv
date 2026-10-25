// Cuelight YouTube (deploy/webos-youtube) in a browser engine, with YouTube's page faked: the
// launch page opens the right address, and the script inside YouTube's page closes on Back, loops
// the fireplace under a clock, and closes when the video asked for ends (not after an advert).
//   node tests/webos_youtube_check.js     (needs Playwright, as the smoke screenshots do)
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const DIR = path.join(__dirname, '..', 'deploy', 'webos-youtube');
const script = fs.readFileSync(path.join(DIR, 'webOSUserScripts/userScript.js'), 'utf8');
async function page(browser, params, opts = {}) {
  const ctx = await browser.newContext();
  const p = await ctx.newPage();
  await p.addInitScript(([params, script]) => {
    window.PalmSystem = { launchParams: JSON.stringify(params) };
    window.__closed = 0; window.close = () => { window.__closed++; };
    document.addEventListener('DOMContentLoaded', () => { const s = document.createElement('script'); s.textContent = script; document.head.appendChild(s); });
  }, [params, script]);
  await p.route('https://www.youtube.com/**', r => r.fulfill({ contentType: 'text/html', body: '<html><head></head><body><video id=v muted></video></body></html>' }));
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
