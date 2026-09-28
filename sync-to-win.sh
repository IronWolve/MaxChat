#!/usr/bin/env bash
# Sync only approved files into an explicitly selected Windows project/repo/.
set -euo pipefail
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SOURCE_DIR/.." && pwd)"
if [[ $# != 1 || ${1:-} == --help || ${1:-} == -h ]]; then
    printf 'Usage: sync-to-win.sh <destination-project>\nRelative destinations are resolved from this project root.\nCopies approved source into <destination-project>/repo; build repo/build.bat on Windows.\n'
    [[ ${1:-} == --help || ${1:-} == -h ]] && exit 0
    exit 1
fi
command -v rsync >/dev/null || { printf 'ERROR: rsync is required.\n' >&2; exit 1; }
case "$1" in
    /*) destination="$1" ;;
    *) destination="$PROJECT_DIR/$1" ;;
esac
TARGET_DIR="$(realpath -m -- "$destination/repo")"
case "$TARGET_DIR" in
    "$SOURCE_DIR"|"$SOURCE_DIR"/*) printf 'ERROR: target must be outside the source checkout.\n' >&2; exit 1 ;;
esac
mkdir -p -- "$TARGET_DIR"
rsync -a --files-from="$SOURCE_DIR/packaging/source-files.txt" -- "$SOURCE_DIR/" "$TARGET_DIR/"
printf 'Synced approved source to the selected project: %s/repo\nBuild on Windows using repo\\build.bat. Existing runtime and configuration were preserved.\n' "$1"
