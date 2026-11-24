#!/usr/bin/env bash
# End-to-end test on Windows: portable OBS + this plugin -> local mediamtx.
# Needs tools/obs-portable (OBS copy with portable_mode.txt + plugin),
# tools/mediamtx and tools/ffmpeg. See TOOLS.md.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/tools"
PROFILE="$T/obs-portable/config/obs-studio/basic/profiles/Untitled"
cp "$ROOT/tests/e2e/seed-config.json" "$PROFILE/obs-multistream.json"
rm -f "$T/obs-portable/config/obs-studio/logs/"*.txt
rm -rf "$T/obs-portable/config/obs-studio/.sentinel"  # leftover from a killed run would trigger the safe-mode prompt

( cd "$T/mediamtx" && ./mediamtx.exe "$(cygpath -m "$ROOT/tests/e2e/mediamtx-test.yml")" > "$T/mediamtx.log" 2>&1 < /dev/null ) &
sleep 2
( cd "$T/obs-portable/bin/64bit" && MULTISTREAM_TEST_AUTOSTART=2000 MULTISTREAM_TEST_EXIT_AFTER=30000 \
    ./obs64.exe --portable --disable-shutdown-check --disable-updater > /dev/null 2>&1 ) &
OBS_PID=$!
sleep 22
PASS=0; FAIL=0
for k in a b c d; do
  OUT=$(timeout 15 "$T/ffmpeg/bin/ffprobe.exe" -v error -show_entries stream=codec_name,width,height,bit_rate -of compact "rtmp://127.0.0.1:1935/live/$k" 2>&1 | tr '\n' ' ')
  echo "live/$k: $OUT"
  if [ "$k" = d ]; then
    if echo "$OUT" | grep -q "codec_name=h264"; then echo "  FAIL: disabled destination is streaming"; FAIL=$((FAIL+1)); else echo "  ok (disabled, not streaming)"; PASS=$((PASS+1)); fi
  else
    if echo "$OUT" | grep -q "codec_name=h264" && echo "$OUT" | grep -q "codec_name=aac"; then echo "  ok"; PASS=$((PASS+1)); else echo "  FAIL"; FAIL=$((FAIL+1)); fi
  fi
done
for i in $(seq 1 40); do tasklist | grep -qi obs64.exe || break; sleep 1; done
if tasklist | grep -qi obs64.exe; then echo "FAIL OBS did not exit (hang) - killing"; taskkill /F /IM obs64.exe > /dev/null; FAIL=$((FAIL+1)); fi
taskkill /F /IM mediamtx.exe > /dev/null 2>&1
LOG=$(ls -t "$T/obs-portable/config/obs-studio/logs/"*.txt | head -1)
echo "--- plugin log lines"
grep "obs-multistream" "$LOG"
grep -q "\[Test E (dead server)\] stopped with error" "$LOG" && { echo "ok  dead server reported"; PASS=$((PASS+1)); } || { echo "FAIL dead server not reported"; FAIL=$((FAIL+1)); }
grep -q "\[Test F (no key)\] skipped" "$LOG" && { echo "ok  missing key skipped"; PASS=$((PASS+1)); } || { echo "FAIL missing key not skipped"; FAIL=$((FAIL+1)); }
grep -q "\[Test D (disabled)\]" "$LOG" && { echo "FAIL disabled destination touched"; FAIL=$((FAIL+1)); }
grep -qi "crash" "$LOG" && { echo "FAIL crash in log"; FAIL=$((FAIL+1)); }
grep -q "plugin unloaded" "$LOG" && { echo "ok  clean unload"; PASS=$((PASS+1)); } || { echo "FAIL no clean unload"; FAIL=$((FAIL+1)); }
grep -q '"key": "a"' "$PROFILE/obs-multistream.json" && { echo "ok  config kept"; PASS=$((PASS+1)); } || { echo "FAIL config lost"; FAIL=$((FAIL+1)); }
echo "E2E: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
