#!/usr/bin/env bash
# AndroidPlay host verification for building LineageOS 23.2 (beyond2lte).
# Thresholds come from wiki.lineageos.org/devices/beyond2lte/build (checked 2026-11-14):
#   64 GB RAM and 400 GB storage "for lineage-21 and up". Override with env vars.
# Exit code: 0 all requirements met, 1 one or more unmet.
set -u
export PATH="$HOME/bin:$PATH"
SRC_DIR="${ANDROIDPLAY_SRC:-$HOME/android/lineage}"
MIN_DISK_GB="${ANDROIDPLAY_MIN_DISK_GB:-400}"
MIN_RAM_GB="${ANDROIDPLAY_MIN_RAM_GB:-64}"
MIN_CORES="${ANDROIDPLAY_MIN_CORES:-4}"
fail=0
ok()   { printf '[ OK ] %s\n' "$*"; }
bad()  { printf '[FAIL] %s\n' "$*"; fail=1; }
warn() { printf '[WARN] %s\n' "$*"; }

[ "$(uname -m)" = "x86_64" ] && ok "arch x86_64" || bad "arch $(uname -m) (need x86_64)"
if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "${ID:-}:${VERSION_ID:-}" in
        ubuntu:2[0-9].*|debian:1[1-9]*|debian:[2-9][0-9]*) ok "distro ${PRETTY_NAME:-$ID}" ;;
        *) warn "distro ${PRETTY_NAME:-unknown} is not Ubuntu/Debian; setup.sh will not work" ;;
    esac
fi

ram_gb=$(awk '/MemTotal/{printf "%d", $2/1024/1024 + 0.5}' /proc/meminfo)
swap_gb=$(awk '/SwapTotal/{printf "%d", $2/1024/1024}' /proc/meminfo)
if [ "$ram_gb" -ge "$MIN_RAM_GB" ]; then ok "RAM ${ram_gb} GB (>= ${MIN_RAM_GB})"
else bad "RAM ${ram_gb} GB < ${MIN_RAM_GB} GB documented for lineage-21+ (swap/zram: ${swap_gb} GB; slower, may OOM at link/dex steps)"; fi

cores=$(nproc)
[ "$cores" -ge "$MIN_CORES" ] && ok "CPU cores $cores" || bad "CPU cores $cores < $MIN_CORES"

probe="$SRC_DIR"
while [ ! -d "$probe" ] && [ "$probe" != "/" ]; do probe=$(dirname "$probe"); done
free_gb=$(df -BG --output=avail "$probe" | tail -1 | tr -dc '0-9')
if [ "$free_gb" -ge "$MIN_DISK_GB" ]; then ok "free disk ${free_gb} GB at $probe (>= ${MIN_DISK_GB})"
else bad "free disk ${free_gb} GB at $probe < ${MIN_DISK_GB} GB documented for lineage-21+"; fi

# Java: Android 16 builds use prebuilts/jdk/jdk21 from the source tree; host JDK optional.
if command -v java >/dev/null 2>&1; then
    ok "host java: $(java -version 2>&1 | head -1) (informational; build uses in-tree JDK)"
else
    warn "no host java (not required, in-tree prebuilts/jdk is used)"
fi

for t in git curl python3 make gcc g++ bison flex gperf zip unzip rsync xsltproc xxd lz4 \
         mkfs.erofs mksquashfs ccache git-lfs protoc convert schedtool pngcrush; do
    command -v "$t" >/dev/null 2>&1 && ok "tool $t" || bad "missing tool $t (run scripts/setup.sh)"
done
command -v repo >/dev/null 2>&1 && ok "tool repo ($(repo --version 2>/dev/null | head -1))" || bad "missing tool repo (run scripts/setup.sh)"
command -v adb  >/dev/null 2>&1 && ok "tool adb" || warn "adb not installed (needed only to talk to a device, not to build)"
command -v cmake >/dev/null 2>&1 && ok "cmake" || warn "cmake not installed (not required by LineageOS wiki package list)"

if dpkg -s gcc-multilib g++-multilib lib32z1-dev >/dev/null 2>&1; then ok "multilib packages"
else bad "multilib packages missing (gcc-multilib g++-multilib lib32z1-dev)"; fi

git config --global user.email >/dev/null 2>&1 && git config --global user.name >/dev/null 2>&1 \
    && ok "git identity configured" || bad "git user.name/user.email not set (repo requires it)"

[ "$(ulimit -n)" -ge 4096 ] && ok "ulimit -n $(ulimit -n)" || bad "ulimit -n $(ulimit -n) < 4096"

echo
if [ "$fail" -eq 0 ]; then echo "RESULT: host meets requirements"; else echo "RESULT: host does NOT meet requirements (see FAIL lines)"; fi
exit "$fail"
