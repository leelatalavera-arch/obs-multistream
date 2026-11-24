#!/usr/bin/env bash
# Reconnect test: server goes away mid-stream and comes back.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; T="$ROOT/tools"
PROFILE="$T/obs-portable/config/obs-studio/basic/profiles/Untitled"
cp "$ROOT/tests/e2e/seed-config.json" "$PROFILE/obs-multistream.json"
rm -f "$T/obs-portable/config/obs-studio/logs/"*.txt; rm -rf "$T/obs-portable/config/obs-studio/.sentinel"
CFG="$(cygpath -m "$ROOT/tests/e2e/mediamtx-test.yml")"
( cd "$T/mediamtx" && ./mediamtx.exe "$CFG" > "$T/mediamtx.log" 2>&1 < /dev/null ) &
sleep 2
( cd "$T/obs-portable/bin/64bit" && MULTISTREAM_TEST_AUTOSTART=2000 MULTISTREAM_TEST_EXIT_AFTER=45000 \
    ./obs64.exe --portable --disable-shutdown-check --disable-updater > /dev/null 2>&1 ) &
sleep 15; taskkill /F /IM mediamtx.exe > /dev/null; echo "server killed"
sleep 6; ( cd "$T/mediamtx" && ./mediamtx.exe "$CFG" > "$T/mediamtx2.log" 2>&1 < /dev/null ) & echo "server back"
sleep 20
OUT=$(timeout 15 "$T/ffmpeg/bin/ffprobe.exe" -v error -show_entries stream=codec_name -of compact "rtmp://127.0.0.1:1935/live/a" 2>&1 | tr '\n' ' ')
echo "after reconnect live/a: $OUT"
for i in $(seq 1 40); do tasklist | grep -qi obs64.exe || break; sleep 1; done
taskkill /F /IM mediamtx.exe > /dev/null 2>&1
LOG=$(ls -t "$T/obs-portable/config/obs-studio/logs/"*.txt | head -1)
grep "obs-multistream\]" "$LOG" | grep -E "reconnect|live|unloaded" | cut -c1-120
echo "$OUT" | grep -q h264 && grep -q "\[Test A (shared)\] reconnecting" "$LOG" && echo "RECONNECT: PASS" || echo "RECONNECT: FAIL"
