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
out=$("${RUNNER[@]}" "$EXE" -w 1280 -h 720 \
  +mat_vsync 0 +cl_showfps 1 +cl_showpos 1 +sv_cheats 1 \
  +wait 30 +screenshot +wait \
  ++forward +wait 60 +-forward +wait 10 +getpos \
  +setpos 700 -700 40 +setang -8 135 +r_showbounds 1 +wait 10 +screenshot +wait \
  +toggleconsole +cvarlist sv_ +wait 5 +screenshot +wait \
  +quit 2>&1)
echo "$out" | grep -v "GL:"
count=$(ls "$SHOTS"/*.png 2>/dev/null | wc -l)
[[ "$count" -eq 3 ]] || { echo "FAIL: expected 3 screenshots, got $count"; exit 1; }
grep -q "Connected to server" <<<"$out" || { echo "FAIL: client never connected"; exit 1; }
# +forward for 60 frames must have moved the player off the origin.
grep -qE "setpos [1-9][0-9]*\.[0-9]+ " <<<"$out" || { echo "FAIL: scripted movement didn't move the player"; exit 1; }
if grep -q "failed to compile\|failed to link" <<<"$out"; then echo "FAIL: shader errors"; exit 1; fi
echo "== All smoke tests passed ($count screenshots in $SHOTS)"
