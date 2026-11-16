#!/usr/bin/env bash
# Level-1 (software) test runner. Runs every host-side suite; exits nonzero if any fails or is skipped
# because a tool is missing (a skipped suite is reported as SKIP and counts as failure with --strict).
# Levels 2-5 (Android, USB, CarPlay, real hardware) are NOT covered here: see docs/validation.md.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STRICT=0; [ "${1:-}" = "--strict" ] && STRICT=1
BUILD="${ANDROIDPLAY_BUILD_DIR:-$ROOT/.build}"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
pass=0; fail=0; skip=0
declare -a REPORT

run() { # name, command...
  local name="$1"; shift
  local log="$BUILD/$name.log"; mkdir -p "$BUILD"
  if "$@" >"$log" 2>&1; then pass=$((pass+1)); REPORT+=("PASS  $name"); else fail=$((fail+1)); REPORT+=("FAIL  $name  (log: $log)"); fi
}
skipit() { skip=$((skip+1)); REPORT+=("SKIP  $1  ($2)"); }

# --- CarPlayService (CMake, ASan+UBSan) ---
CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && CMAKE="$(ls "$SDK"/cmake/*/bin/cmake 2>/dev/null | tail -1)"
if [ -n "$CMAKE" ] && command -v g++ >/dev/null; then
  CP="$BUILD/carplay"
  run carplay-configure "$CMAKE" -S "$ROOT/services/CarPlayService" -B "$CP" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined"
  run carplay-build "$CMAKE" --build "$CP" -j"$(nproc)"
  CTEST="$(dirname "$CMAKE")/ctest"; [ -x "$CTEST" ] || CTEST="$(command -v ctest || true)"
  if [ -n "$CTEST" ]; then run carplay-tests "$CTEST" --test-dir "$CP" --output-on-failure
  else skipit carplay-tests "ctest missing"; fi
else
  skipit carplay "cmake or g++ missing (setup: scripts/setup.sh installs cmake)"
fi

# --- GadgetManager (make) ---
if command -v make >/dev/null && command -v g++ >/dev/null; then
  run gadgetmanager make -C "$ROOT/usb/GadgetManager" test
else skipit gadgetmanager "make/g++ missing"; fi

# --- script syntax ---
bad=0; for f in "$ROOT"/scripts/*.sh; do bash -n "$f" || bad=1; done
if [ $bad -eq 0 ]; then pass=$((pass+1)); REPORT+=("PASS  scripts-syntax"); else fail=$((fail+1)); REPORT+=("FAIL  scripts-syntax"); fi

# --- CarLauncher core (plain Java, no Android) ---
if [ -x "$ROOT/packages/CarLauncher/build.sh" ]; then
  if command -v javac >/dev/null; then run launcher-core "$ROOT/packages/CarLauncher/build.sh" test
  else skipit launcher-core "javac missing"; fi
fi

# --- InputBridge core (plain Java) ---
if [ -x "$ROOT/services/InputBridge/build.sh" ]; then
  if command -v javac >/dev/null; then run inputbridge-core "$ROOT/services/InputBridge/build.sh" test
  else skipit inputbridge-core "javac missing"; fi
fi

echo "=== AndroidPlay Level-1 test report ($(date -u +%FT%TZ)) ==="
printf '%s\n' "${REPORT[@]}"
echo "pass=$pass fail=$fail skip=$skip"
[ $fail -eq 0 ] && { [ $STRICT -eq 0 ] || [ $skip -eq 0 ]; }
