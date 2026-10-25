/* Runs inside YouTube's TV page (webOS loads it into every page this app opens).
 * Back returns to Cuelight. "loop" (the fireplace): the video repeats under a clock and any
 * key ends it. "play" and "break": it closes when the video Cuelight asked for has ended.
 * Cuelight's Settings also decide whether it skips the sponsor segments viewers have marked
 * (SponsorBlock) and whether YouTube's player is asked to leave its adverts out. */
(function () {
  "use strict";
  function params(raw) {
    try { return JSON.parse(raw || "{}") || {}; } catch (e) { return {}; }
  }
  var p = params(window.PalmSystem && window.PalmSystem.launchParams);
  var mode = p.mode || "play", want = p.v || "";
  var skipSponsors = p.skipSponsors !== 0, blockAds = p.blockAds === 1;
  var BACK = 461;

  /* Adverts: the player is told about them in its JSON (where to break, what to show). Taking
   * those parts out before the player reads them leaves the video alone. */
  var AD_KEYS = ["adPlacements", "adSlots", "playerAds"];
  function stripAds(o, depth) {
    if (!o || typeof o !== "object" || depth > 2) return;
    if (Array.isArray(o)) { for (var i = 0; i < o.length && i < 8; i++) stripAds(o[i], depth + 1); return; }
    for (var k = 0; k < AD_KEYS.length; k++) if (AD_KEYS[k] in o) delete o[AD_KEYS[k]];
    if (o.playerResponse) stripAds(o.playerResponse, depth + 1);
  }
  var parse = JSON.parse;
  JSON.parse = function () {
    var out = parse.apply(this, arguments);
    if (blockAds) { try { stripAds(out, 0); } catch (e) { /* leave it as it came */ } }
    return out;
  };
  if (window.Response && Response.prototype.json) {   /* fetch's own parser does not go through JSON.parse */
    var json = Response.prototype.json;
    Response.prototype.json = function () {
      return json.apply(this, arguments).then(function (out) {
        if (blockAds) { try { stripAds(out, 0); } catch (e) { /* as it came */ } }
        return out;
      });
    };
  }

  function close() { try { window.close(); } catch (e) { /* the system closes it */ } }

  document.addEventListener("keydown", function (e) {
    if (e.keyCode === BACK || mode === "loop") { e.preventDefault(); e.stopImmediatePropagation(); close(); }
  }, true);

  var clock = null;
  function showClock() {
    if (clock) return;
    clock = document.createElement("div");
    clock.style.cssText = "position:fixed;right:5vw;bottom:7vh;z-index:2147483647;font:300 9vh/1 sans-serif;" +
                          "color:rgba(255,255,255,.88);text-shadow:0 0 2vh rgba(0,0,0,.6);pointer-events:none";
    document.documentElement.appendChild(clock);
    var tick = function () {
      var d = new Date();
      clock.textContent = d.getHours() + ":" + ("0" + d.getMinutes()).slice(-2);
    };
    tick();
    setInterval(tick, 10000);
  }
  if (mode === "loop") showClock();

  /* An advert plays in the same video element first: the end counts only once the page is on
   * the video asked for and no advert is showing. */
  function onWanted() { return !want || location.hash.indexOf("v=" + want) >= 0; }
  function advert() { return !!document.querySelector(".ad-showing, [class*='ad-overlay'], ytlr-ad-overlay-renderer, ytlr-player-ad-renderer"); }
  var seenPlaying = false;

  /* Sponsor segments, from SponsorBlock's public list. Only the first characters of the video's
   * hash are sent: the answer covers every video sharing them and the one playing is picked here. */
  var segs = [], segsFor = "", skipped = {}, lastT = 0;
  function currentId() { var m = /[?&]v=([\w-]{11})/.exec(location.hash); return m ? m[1] : ""; }
  function hex(buf) { return Array.prototype.map.call(new Uint8Array(buf), function (b) { return ("0" + b.toString(16)).slice(-2); }).join(""); }
  function getJson(url, done) {
    var x = new XMLHttpRequest();
    x.open("GET", url);
    x.timeout = 8000;
    x.onload = function () { var r = null; try { r = x.status === 200 ? parse(x.responseText) : null; } catch (e) { r = null; } done(r); };
    x.onerror = x.ontimeout = function () { done(null); };
    x.send();
  }
  function loadSegments(id) {
    segsFor = id; segs = []; skipped = {};
    var cats = "&categories=" + encodeURIComponent('["sponsor","selfpromo","interaction"]');
    var take = function (list) {
      if (segsFor !== id || !Array.isArray(list)) return;
      for (var i = 0; i < list.length; i++) {
        if (list[i] && list[i].videoID !== id) continue;
        var s = (list[i] && list[i].segments) || [];
        for (var j = 0; j < s.length; j++)
          if (s[j] && s[j].segment && (s[j].actionType || "skip") === "skip" && s[j].segment[1] > s[j].segment[0])
            segs.push({ from: +s[j].segment[0], to: +s[j].segment[1], id: s[j].UUID || (i + ":" + j) });
      }
    };
    var c = window.crypto && window.crypto.subtle;
    if (c && window.TextEncoder) {
      c.digest("SHA-256", new TextEncoder().encode(id)).then(function (h) {
        getJson("https://sponsor.ajay.app/api/skipSegments/" + hex(h).slice(0, 4) + "?" + cats.slice(1), take);
      }, function () { segsFor = ""; });
    } else {
      getJson("https://sponsor.ajay.app/api/skipSegments?videoID=" + id + cats, function (r) { take(Array.isArray(r) ? [{ videoID: id, segments: r }] : null); });
    }
  }
  var note = null, noteTimer = 0;
  function notice(text) {
    if (!note) {
      note = document.createElement("div");
      note.style.cssText = "position:fixed;left:5vw;bottom:7vh;z-index:2147483647;padding:1.4vh 2.2vh;border-radius:1.2vh;" +
                           "font:500 2.6vh/1 sans-serif;color:#fff;background:rgba(0,0,0,.72);pointer-events:none";
      document.documentElement.appendChild(note);
    }
    note.textContent = text; note.style.display = "block";
    clearTimeout(noteTimer);
    noteTimer = setTimeout(function () { note.style.display = "none"; }, 3000);
  }
  function skipSegments(v) {
    if (!skipSponsors || advert()) return;
    var id = currentId();
    if (id && id !== segsFor) loadSegments(id);
    var t = v.currentTime;
    if (mode === "loop" && t + 1 < lastT) skipped = {};   /* the fireplace came round again */
    lastT = t;
    for (var i = 0; i < segs.length; i++) {
      var s = segs[i];
      if (t >= s.from && t < s.to - 0.5 && !skipped[s.id]) {
        skipped[s.id] = 1;   /* once: seeking back into it plays it */
        v.currentTime = s.to;
        notice("Skipped a sponsor segment");
        return;
      }
    }
  }

  setInterval(function () {
    var v = document.querySelector("video");
    if (!v) return;
    skipSegments(v);
    if (mode === "loop") { v.loop = true; return; }
    if (!v.paused && v.currentTime > 2 && onWanted() && !advert()) seenPlaying = true;
    if (seenPlaying && v.ended && !advert()) close();
  }, 500);

  /* Cuelight asks again while this is open: the new video, in its mode */
  document.addEventListener("webOSRelaunch", function (e) {
    var q = (e && e.detail) || params(window.PalmSystem && window.PalmSystem.launchParams);
    if (!/^[\w-]{11}$/.test(q.v || "")) return;   /* whoever relaunches it: only a video id goes into the address */
    mode = q.mode || "play"; want = q.v; seenPlaying = false;
    skipSponsors = q.skipSponsors !== 0; blockAds = q.blockAds === 1;
    if (mode === "loop") showClock();
    var t = Math.max(0, q.t | 0);
    location.hash = "#/watch?v=" + q.v + (t ? "&t=" + t + "s" : "");
  }, true);
})();
