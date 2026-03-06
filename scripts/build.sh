#!/usr/bin/env bash
# Configure and build BitGlide. Usage: scripts/build.sh [Debug|Release|RelWithDebInfo] [--sanitize]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-RelWithDebInfo}"
SANITIZE=OFF
for arg in "$@"; do
  [[ "$arg" == "--sanitize" ]] && SANITIZE=ON
done
[[ "$BUILD_TYPE" == --* ]] && BUILD_TYPE=RelWithDebInfo

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DBITGLIDE_SANITIZE="$SANITIZE"
cmake --build "$ROOT/build" -j "$JOBS"
echo "built: $ROOT/build/bitglide"
