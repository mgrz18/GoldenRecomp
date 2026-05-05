#!/bin/bash
# GoldenRecomp multi-agent harness
#
# Builds, runs, scores. Outputs structured JSON for agent consumption.
#
# Usage:
#   scripts/harness.sh [run-duration-seconds] [--no-build]
#
# Defaults: 90s run, build first.
# Output: JSON to stdout, all stderr to /tmp/harness.stderr
#
# Env vars are passed through (set them on caller's environment).
# Best-known config:
#   GE_FORCE_SHADE=1 GE_RAW_VTX_COLOR=1
#   GE_DEEP_SHADOW=1 GE_REMAP_VTX=1 GE_LOCK_MATRICES=1
#   GE_DUMP_ALL=1

set -e

REPO="/Users/miguel_garcia/Documents/Proyectos/1964/GoldenRecomp"
DURATION="${1:-90}"
SKIP_BUILD=0
[ "$2" = "--no-build" ] && SKIP_BUILD=1

cd "$REPO"

# 1. Build
if [ "$SKIP_BUILD" = "0" ]; then
  echo "[harness] building..." >&2
  if ! ninja -C build GoldenRecomp 2>&1 | tail -3 >&2; then
    echo '{"error": "build_failed", "stage": "build"}'
    exit 1
  fi
fi

# 2. Cleanup previous PPMs
rm -f /tmp/ge_fb_*.ppm

# 3. Run
LOG="/tmp/harness_run_$$_$(date +%s).log"
echo "[harness] running for ${DURATION}s..." >&2
"$REPO/build/GoldenRecomp.app/Contents/MacOS/GoldenRecomp" -level_10 \
  >/dev/null 2>"$LOG" &
APP_PID=$!
sleep "$DURATION"
kill $APP_PID 2>/dev/null || true
sleep 2
kill -9 $APP_PID 2>/dev/null || true

# 4. Score
cnt() { grep -c "$1" "$LOG" 2>/dev/null | tr -d '\n' || echo 0; }
cntE() { grep -cE "$1" "$LOG" 2>/dev/null | tr -d '\n' || echo 0; }
DONES=$(cnt 'type=2')
RETRACES=$(cnt 'type=1')
JUNK=$(cnt 'type=-')
HEAL=$(cnt 'healed')
LEARN=$(cnt 'learned good')
FULLSYNC=$(cnt '\[State::fullSync')
PUBFB=$(cnt '\[State::publishFB')
TRI_VISIBLE=$(cnt '\[drawTri.*visible=1')
TRI_NAN=$(cntE 'v0=\(nan|v0=\(inf|v0=\(-inf')
TRI_DRAWN=$(grep "drawIndexedTri stats" "$LOG" 2>/dev/null | tail -1 | grep -oE 'drawn=[0-9]+' | grep -oE '[0-9]+' || echo 0)
[ -z "$TRI_DRAWN" ] && TRI_DRAWN=0
LOADBLOCK=$(cnt '\[RDP::loadBlock')
SETTILE=$(cnt '\[RDP::setTile')
SETTIMG=$(cnt '\[F3D::setTextureImage')
MTX_REJECT=$(cnt 'RSP::matrix MALFORMED')
CRASH=$(cntE 'CRASH|signal 11|SIGSEGV')

# Per-PPM analysis: max non-zero hex, count of FBs > threshold, top-3 file paths
TOP_NZ=0
TOP_FILE=""
FBS_TOTAL=0
FBS_VISIBLE=0
declare -a SAMPLES
for f in /tmp/ge_fb_*.ppm; do
  [ -e "$f" ] || continue
  FBS_TOTAL=$((FBS_TOTAL + 1))
  NZ=$(tail -c +20 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n' | tr -d '0' | wc -c | tr -d ' ')
  [ -z "$NZ" ] && NZ=0
  if [ "$NZ" -gt 100 ]; then
    FBS_VISIBLE=$((FBS_VISIBLE + 1))
  fi
  if [ "$NZ" -gt "$TOP_NZ" ]; then
    TOP_NZ=$NZ
    TOP_FILE="$f"
  fi
done

# Sample first 5 visible PPMs into samples array
i=0
for f in /tmp/ge_fb_*.ppm; do
  [ -e "$f" ] || continue
  [ $i -ge 5 ] && break
  NZ=$(tail -c +20 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n' | tr -d '0' | wc -c | tr -d ' ')
  if [ "$NZ" -gt 100 ]; then
    SAMPLES[$i]="$f:$NZ"
    i=$((i + 1))
  fi
done

# Last 3 fbPair lines
FBPAIR_INFO=$(grep "\[fbPair #" "$LOG" 2>/dev/null | head -3 | tr '\n' '|' | sed 's/"/\\"/g')

# Last 3 drawTri samples
TRI_SAMPLES=$(grep "\[drawTri #" "$LOG" 2>/dev/null | head -3 | tr '\n' '|' | sed 's/"/\\"/g')

# Last 3 matrix MALFORMED
MTX_INFO=$(grep "RSP::matrix MALFORMED" "$LOG" 2>/dev/null | head -3 | tr '\n' '|' | sed 's/"/\\"/g')

# Output JSON
cat <<EOF
{
  "duration_s": $DURATION,
  "scheduler": {
    "dones": $DONES,
    "retraces": $RETRACES,
    "junk_msgs": $JUNK,
    "heal_count": $HEAL,
    "learn_count": $LEARN
  },
  "rendering": {
    "fullsync_count": $FULLSYNC,
    "publish_fb_count": $PUBFB,
    "tri_visible": $TRI_VISIBLE,
    "tri_nan": $TRI_NAN,
    "tri_drawn_total": $TRI_DRAWN,
    "loadblock_count": $LOADBLOCK,
    "settile_count": $SETTILE,
    "settimg_count": $SETTIMG,
    "matrix_malformed": $MTX_REJECT,
    "crashes": $CRASH
  },
  "framebuffers": {
    "fbs_total": $FBS_TOTAL,
    "fbs_with_content": $FBS_VISIBLE,
    "top_nonzero": $TOP_NZ,
    "top_file": "$TOP_FILE"
  },
  "diagnostics": {
    "fbpair_lines": "$FBPAIR_INFO",
    "tri_samples": "$TRI_SAMPLES",
    "matrix_malformed_lines": "$MTX_INFO",
    "log_path": "$LOG"
  }
}
EOF
