#!/usr/bin/env bash
# A second launch hands what it was given to the Cuelight that is open, and ends at once.
#   tests/single_instance.sh BUILD OUT PORT   (called by tests/smoke.sh)
BUILD="$1"; OUT="$2"; PORT="$3"
SI_PORT=$((PORT + 3000))
SI_HOME="$OUT/home_single"; rm -rf "$SI_HOME"; mkdir -p "$SI_HOME/.local/share/stremio/nativetv"
echo '{"prefSubs":"","setupDone":1}' > "$SI_HOME/.local/share/stremio/nativetv/settings.json"
# the first Cuelight stays open (its own group, ended below): a frame count would end it first on a fast machine, and the "second launch" would then be a whole app of its own
env -u SNTV_MULTI HOME="$SI_HOME" SNTV_PHONE_PORT=$SI_PORT \
    setsid xvfb-run -a -n "${SMOKE_XVFB_N:-99}" -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" > "$OUT/single_instance.log" 2>&1 &
FIRST=$!
for _ in $(seq 1 40); do grep -q "phone: type-on-your-phone page" "$OUT/single_instance.log" 2>/dev/null && break; sleep 0.5; done
T0=$(date +%s)
env -u SNTV_MULTI HOME="$SI_HOME" SNTV_PHONE_PORT=$SI_PORT timeout 20 "$BUILD/stremio-tv" "cuelight://sync" > "$OUT/single_instance_2.log" 2>&1; RC=$?
T1=$(date +%s)
for _ in $(seq 1 40); do grep -q "app: opened with a sync code" "$OUT/single_instance.log" 2>/dev/null && break; sleep 0.5; done
kill -- -"$FIRST" 2>/dev/null; wait "$FIRST" 2>/dev/null
[ "$RC" = 0 ] && [ $((T1 - T0)) -le 3 ] && ! grep -q "phone: type-on-your-phone page" "$OUT/single_instance_2.log" \
  && grep -q "open: handed over by another launch (with something to open)" "$OUT/single_instance.log" \
  && grep -q "app: opened with a sync code" "$OUT/single_instance.log" \
  || { echo "rc=$RC"; head -5 "$OUT/single_instance_2.log"; grep -E "open:|app: opened" "$OUT/single_instance.log"; exit 1; }
