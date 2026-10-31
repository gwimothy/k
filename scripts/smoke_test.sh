#!/usr/bin/env bash
# Automated end-to-end check of a built engine:
#   1. unit tests
#   2. dedicated server runs N ticks and exits cleanly
#   3. (if a display or xvfb-run is available) the full client boots, flies forward
#      with scripted input, and writes screenshots
#
# Usage: scripts/smoke_test.sh [build-dir]
set -euo pipefail

BUILD_DIR="${1:-build}"
BIN="$BUILD_DIR/bin"
EXE="$BIN/k"
[[ -x "$EXE" || -x "$EXE.exe" ]] || { echo "No engine binary in $BIN - build first"; exit 1; }

echo "== Unit tests"
"$BIN/k_tests"

echo "== Dedicated server (132 ticks)"
out=$("$EXE" -dedicated +map devroom +host_maxticks 132 2>&1)
echo "$out"
grep -q "host_maxticks reached" <<<"$out" || { echo "FAIL: dedicated server didn't run to completion"; exit 1; }
if grep -q "couldn't find maps\|is invalid" <<<"$out"; then echo "FAIL: map didn't load on the server"; exit 1; fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "== Skipping client test (Linux/Xvfb only)"
  exit 0
fi

RUNNER=()
if [[ -z "${DISPLAY:-}" ]]; then
  if command -v xvfb-run >/dev/null; then
    RUNNER=(xvfb-run -a -s "-screen 0 1280x720x24")
  else
    echo "== Skipping client test (no display and no xvfb-run)"
    exit 0
  fi
fi

echo "== Client (scripted input + screenshots)"
SHOTS="$BIN/base/screenshots"
rm -rf "$SHOTS"
# Xvfb has no DRI3/GLX acceleration; EGL + Mesa llvmpipe gives a software GL 4.5 context.
export SDL_VIDEO_FORCE_EGL=1
# Note: screenshot captures at the end of the frame, so each one is followed by +wait.
out=$("${RUNNER[@]}" "$EXE" -w 1280 -h 720 -nosound \
  +mat_vsync 0 +cl_showfps 1 +cl_showpos 1 +sv_cheats 1 +developer_io 1 +snd_show 1 \
  +wait 30 +screenshot +wait \
  ++forward +wait 60 +-forward +wait 10 +getpos \
  +setpos 380 -420 0 +setang 6 150 +r_showbounds 1 +wait 10 +screenshot +wait \
  +setpos 1700 -200 0 +setang 15 30 +r_showbounds 0 +wait 10 +screenshot +wait \
  +setpos 960 112 0 +setang 0 0 +wait 5 ++use +wait 3 +-use +wait 30 \
  +setpos 784 -816 9 +wait 120 +getpos \
  +toggleconsole +cvarlist sv_ +wait 5 +screenshot +wait \
  +quit 2>&1)
echo "$out" | grep -v "GL:"
count=$(ls "$SHOTS"/*.png 2>/dev/null | wc -l)
[[ "$count" -eq 4 ]] || { echo "FAIL: expected 4 screenshots, got $count"; exit 1; }
grep -q "Connected to server" <<<"$out" || { echo "FAIL: client never connected"; exit 1; }
grep -q "World: " <<<"$out" || { echo "FAIL: compiled map didn't load"; exit 1; }
# +forward for 60 frames must have moved the player off the origin.
grep -qE "setpos [1-9][0-9]*\.[0-9]+ " <<<"$out" || { echo "FAIL: scripted movement didn't move the player"; exit 1; }
if grep -q "failed to compile\|failed to link" <<<"$out"; then echo "FAIL: shader errors"; exit 1; fi
# Entities: the welcome message, the button opening the door, the elevator lifting the player.
grep -q "Welcome to the dev room" <<<"$out" || { echo "FAIL: logic_auto/point_message didn't fire"; exit 1; }
grep -q "corridor_door' : Open" <<<"$out" || { echo "FAIL: the button didn't open the door"; exit 1; }
grep -qE "setpos 784.00 -816.00 20[0-9]\." <<<"$out" || { echo "FAIL: the elevator didn't carry the player up"; exit 1; }
# Physics + sound: crates fall and clatter on map start, the door rumbles.
grep -q "sound: Door.Move" <<<"$out" || { echo "FAIL: no door sound"; exit 1; }
grep -q "sound: Ambient.Hum" <<<"$out" || { echo "FAIL: ambient_generic didn't play"; exit 1; }

echo "== Multiplayer (dedicated server process + remote client process)"
PORT=27555
SRV_LOG="$BUILD_DIR/server_smoke.log"
"$EXE" -dedicated +hostport $PORT +map devroom +sv_cheats 1 +host_maxticks 2000 >"$SRV_LOG" 2>&1 </dev/null &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null || true' EXIT
sleep 1
out=$("${RUNNER[@]}" "$EXE" -w 640 -h 480 -nosound +name smoke_remote +connect 127.0.0.1:$PORT \
  +wait 120 ++forward +wait 40 +-forward +wait 30 +getpos +status +wait 5 +quit 2>&1)
echo "$out" | grep -E "Connect|client:|setpos" || true
kill $SRV_PID 2>/dev/null || true
wait $SRV_PID 2>/dev/null || true
grep -q "Connected to server" <<<"$out" || { echo "FAIL: remote client didn't connect"; cat "$SRV_LOG"; exit 1; }
grep -q "client: remote on 'devroom'" <<<"$out" || { echo "FAIL: remote client status missing"; exit 1; }
grep -qE "setpos (1[3-9][0-9]|2[0-9][0-9])\." <<<"$out" || { echo "FAIL: remote client didn't move (prediction/commands)"; exit 1; }
grep -q "smoke_remote connected from 127.0.0.1" "$SRV_LOG" || { echo "FAIL: server didn't see the client"; cat "$SRV_LOG"; exit 1; }
grep -q "smoke_remote left" "$SRV_LOG" || { echo "FAIL: server didn't see the client leave"; cat "$SRV_LOG"; exit 1; }

echo "== All smoke tests passed ($count screenshots in $SHOTS)"
