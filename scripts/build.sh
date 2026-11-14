#!/usr/bin/env bash
# S10CarOS: sync LineageOS and build beyond2lte. GUARDED: needs S10CAROS_CONFIRM_BUILD=1.
# Commands: init | sync | build | all   (default: all)
# Env: S10CAROS_SRC (default ~/android/lineage), S10CAROS_BRANCH (default lineage-23.2),
#      S10CAROS_JOBS (repo sync jobs, default 4), S10CAROS_SKIP_VERIFY=1 to bypass host check.
# Real sync is >100 GB download; the full build needs the disk/RAM that verify.sh checks.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
SRC="${S10CAROS_SRC:-$HOME/android/lineage}"
BRANCH="${S10CAROS_BRANCH:-lineage-23.2}"
JOBS="${S10CAROS_JOBS:-4}"
CMD="${1:-all}"

if [ "${S10CAROS_CONFIRM_BUILD:-}" != "1" ]; then
    echo "REFUSING: set S10CAROS_CONFIRM_BUILD=1 to run (downloads >100 GB, needs 64 GB RAM / 400 GB disk)." >&2
    exit 3
fi
case "$CMD" in init|sync|build|all) ;; *) echo "usage: $0 [init|sync|build|all]" >&2; exit 2 ;; esac
export PATH="$HOME/bin:$PATH"
command -v repo >/dev/null || { echo "repo missing: run scripts/setup.sh" >&2; exit 1; }

if [ "${S10CAROS_SKIP_VERIFY:-}" != "1" ]; then
    "$HERE/verify.sh" || { echo "verify.sh failed; fix or set S10CAROS_SKIP_VERIFY=1 (and lower S10CAROS_MIN_*)" >&2; exit 1; }
fi

do_init() {
    mkdir -p "$SRC"; cd "$SRC"
    repo init -u https://github.com/LineageOS/android.git -b "$BRANCH" --git-lfs --no-clone-bundle
    mkdir -p .repo/local_manifests
    cp "$ROOT/manifests/local_manifests/s10caros.xml" .repo/local_manifests/s10caros.xml
}
do_sync() { cd "$SRC"; repo sync -j"$JOBS" -c --no-tags --no-clone-bundle; }
do_build() {
    cd "$SRC"
    # envsetup.sh / breakfast use unset vars
    set +eu
    # shellcheck disable=SC1091
    source build/envsetup.sh
    breakfast beyond2lte userdebug || { echo "breakfast failed" >&2; exit 1; }
    export USE_CCACHE=1 CCACHE_EXEC="$(command -v ccache)"
    ccache -M 50G >/dev/null 2>&1 || true
    brunch beyond2lte userdebug
    echo "Artifacts: $SRC/out/target/product/beyond2lte/ (lineage-*.zip, recovery.img)"
}
case "$CMD" in
    init) do_init ;;
    sync) do_sync ;;
    build) do_build ;;
    all) do_init; do_sync; do_build ;;
esac
