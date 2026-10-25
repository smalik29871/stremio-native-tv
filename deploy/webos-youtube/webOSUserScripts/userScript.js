/* Runs inside YouTube's TV page (webOS loads it into every page this app opens).
 * Back returns to Cuelight. "loop" (the fireplace): the video repeats under a clock and any
 * key ends it. "play" and "break": it closes when the video Cuelight asked for has ended. */
(function () {
  "use strict";
  function params(raw) {
    try { return JSON.parse(raw || "{}") || {}; } catch (e) { return {}; }
  }
  var p = params(window.PalmSystem && window.PalmSystem.launchParams);
  var mode = p.mode || "play", want = p.v || "";
  var BACK = 461;

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
  setInterval(function () {
    var v = document.querySelector("video");
    if (!v) return;
    if (mode === "loop") { v.loop = true; return; }
    if (!v.paused && v.currentTime > 2 && onWanted() && !advert()) seenPlaying = true;
    if (seenPlaying && v.ended && !advert()) close();
  }, 500);

  /* Cuelight asks again while this is open: the new video, in its mode */
  document.addEventListener("webOSRelaunch", function (e) {
    var q = (e && e.detail) || params(window.PalmSystem && window.PalmSystem.launchParams);
    if (!q.v) return;
    mode = q.mode || "play"; want = q.v; seenPlaying = false;
    if (mode === "loop") showClock();
    location.hash = "#/watch?v=" + q.v + (q.t ? "&t=" + (q.t | 0) + "s" : "");
  }, true);
})();
