#!/usr/bin/env bash
# Runs one bandwidth profile interactively with full logs, leaving the report in build/.
#   scripts/scenario.sh step_down [hybrid|throughput|buffer] [extra bitglide args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE="${1:?usage: scenario.sh <profile> [abr] [bitglide args...]}"
ABR="${2:-hybrid}"
shift $(( $# >= 2 ? 2 : 1 ))

PROFILE_FILE="$ROOT/harness/profiles/${PROFILE}.json"
[[ -f "$PROFILE_FILE" ]] || { echo "no such profile: $PROFILE_FILE" >&2; exit 2; }
[[ -x "$ROOT/build/bitglide" ]] || "$ROOT/scripts/build.sh"
[[ -f "$ROOT/build/media/master.m3u8" ]] || python3 "$ROOT/harness/gen_content.py" --out "$ROOT/build/media"

PORT="${PORT:-8089}"
python3 "$ROOT/harness/hls_origin.py" --root "$ROOT/build/media" --port "$PORT" --profile "$PROFILE_FILE" &
ORIGIN_PID=$!
trap 'kill "$ORIGIN_PID" 2>/dev/null || true' EXIT
sleep 0.5

REPORT="$ROOT/build/report_${PROFILE}_${ABR}.json"
"$ROOT/build/bitglide" --abr "$ABR" --report "$REPORT" --log-level info "$@" \
  "http://127.0.0.1:${PORT}/master.m3u8"
echo "report: $REPORT"
