#!/usr/bin/env bash
# AndroidPlay host setup: installs LineageOS 23.2 build deps (Ubuntu/Debian), `repo`, git-lfs.
# Package list: wiki.lineageos.org/devices/beyond2lte/build (checked 2026-11-14); all names
# confirmed present in Ubuntu 24.04 apt. Idempotent. Needs sudo for apt.
# Usage: scripts/setup.sh [--dry-run]
set -euo pipefail
DRY=0
for a in "$@"; do
    case "$a" in
        --dry-run|-n) DRY=1 ;;
        -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
        *) echo "unknown arg: $a" >&2; exit 2 ;;
    esac
done
run() { if [ "$DRY" = 1 ]; then echo "DRY-RUN: $*"; else echo "+ $*"; "$@"; fi; }

PKGS=(bc bison build-essential ccache curl erofs-utils flex g++-multilib gcc-multilib git git-lfs
      gnupg gperf imagemagick kmod protobuf-compiler python3-protobuf python-is-python3 python3
      lib32readline-dev lib32z1-dev libdw-dev libelf-dev libgnutls28-dev lz4 libsdl1.2-dev
      libssl-dev libxml2 libxml2-utils lzop pngcrush rsync schedtool squashfs-tools xsltproc
      xxd zip unzip zlib1g-dev)
# Ubuntu 26.04+ renames libxml2 -> libxml2-16 (per wiki)
if [ -r /etc/os-release ]; then . /etc/os-release; fi
if [ "${ID:-}" = ubuntu ] && [ "${VERSION_ID%%.*}" -ge 26 ] 2>/dev/null; then
    PKGS=("${PKGS[@]/libxml2/libxml2-16}")
fi

command -v apt-get >/dev/null || { echo "apt-get not found: Ubuntu/Debian only" >&2; exit 1; }
missing=()
for p in "${PKGS[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
if [ "${#missing[@]}" -gt 0 ]; then
    SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    run $SUDO apt-get update
    run $SUDO apt-get install -y "${missing[@]}"
else
    echo "apt packages: all installed"
fi

mkdir -p "$HOME/bin" 2>/dev/null || true
if [ ! -x "$HOME/bin/repo" ]; then
    if [ "$DRY" = 1 ]; then
        echo "DRY-RUN: curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o ~/bin/repo && chmod a+x ~/bin/repo"
    else
        curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o "$HOME/bin/repo"
        chmod a+x "$HOME/bin/repo"
    fi
else
    echo "repo: already at ~/bin/repo"
fi

if [ "$DRY" = 0 ]; then
    git lfs install
    git config --global trailer.changeid.key "Change-Id"
    echo "NOTE: ensure ~/bin is in PATH. Set git identity if unset:"
    echo "  git config --global user.name 'Your Name'; git config --global user.email you@example.com"
else
    echo "DRY-RUN: git lfs install; git config --global trailer.changeid.key Change-Id"
fi
echo "setup done (dry-run=$DRY). Next: scripts/verify.sh"
