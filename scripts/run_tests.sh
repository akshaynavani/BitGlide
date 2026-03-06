#!/usr/bin/env bash
# Builds BitGlide, runs the C++ unit tests, then (unless --unit-only) the network scenarios.
#   scripts/run_tests.sh               # unit + all scenarios with the hybrid ABR
#   scripts/run_tests.sh --unit-only
#   scripts/run_tests.sh --compare     # scenarios for throughput, buffer and hybrid
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_ONLY=0
ABRS=(hybrid)
for arg in "$@"; do
  case "$arg" in
    --unit-only) UNIT_ONLY=1 ;;
    --compare) ABRS=(throughput buffer hybrid) ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

"$ROOT/scripts/build.sh"

echo "== unit tests =="
(cd "$ROOT/build" && ctest --output-on-failure)

if [[ "$UNIT_ONLY" == 1 ]]; then
  exit 0
fi

command -v python3 >/dev/null || { echo "python3 is required for scenario tests" >&2; exit 1; }

echo "== scenario tests =="
python3 "$ROOT/harness/gen_content.py" --out "$ROOT/build/media" --segments 30 --duration 2
python3 "$ROOT/harness/run_scenarios.py" \
  --binary "$ROOT/build/bitglide" \
  --media "$ROOT/build/media" \
  --abr "${ABRS[@]}" \
  --json-out "$ROOT/build/scenario_results.json"
