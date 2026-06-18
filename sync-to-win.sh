#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-/mnt/c/work/maxchat-c}"

if [[ ! -d "$SOURCE_DIR/src" || ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
  echo "ERROR: run this from the MaxChat C++ source tree." >&2
  exit 1
fi

if ! command -v rsync >/dev/null 2>&1; then
  echo "ERROR: rsync is required for a clean sync." >&2
  echo "Install it in WSL with: sudo apt install rsync" >&2
  exit 1
fi

mkdir -p "$TARGET_DIR"

sync_dir() {
  local name="$1"
  if [[ -d "$SOURCE_DIR/$name" ]]; then
    mkdir -p "$TARGET_DIR/$name"
    rsync -a --delete "$SOURCE_DIR/$name/" "$TARGET_DIR/$name/"
  fi
}

copy_file() {
  local name="$1"
  if [[ -f "$SOURCE_DIR/$name" ]]; then
    cp -p "$SOURCE_DIR/$name" "$TARGET_DIR/$name"
  fi
}

for dir in src tests assets resources licenses third_party themes; do
  sync_dir "$dir"
done

for file in \
  .clang-format \
  .gitignore \
  CMakeLists.txt \
  LICENSE \
  README.md \
  THIRD_PARTY_NOTICES.md \
  PORT_PLAN.md \
  STATUS.md \
  HANDOFF.md \
  WINDOWS_SETUP.md \
  build.bat \
  sync-to-win.sh; do
  copy_file "$file"
done

# build.bat must have CRLF endings on Windows: cmd.exe seeks labels by byte
# offset, and LF-only files make later `call :label`s (e.g. copy_assets) fail.
# Force it on the synced copy regardless of the repo file's endings.
if [[ -f "$TARGET_DIR/build.bat" ]]; then
  sed -i 's/\r\?$/\r/' "$TARGET_DIR/build.bat"
fi

# The packaged app reads themes/ and wallpapers/ from disk next to the exe
# (fonts/sounds/icons are embedded). Refresh an existing dist-win so new or
# edited assets show up without rerunning build.bat.
if [[ -d "$TARGET_DIR/dist-win" ]]; then
  mkdir -p "$TARGET_DIR/dist-win/assets"
  for dir in themes wallpapers dictionaries scripts; do
    if [[ -d "$SOURCE_DIR/assets/$dir" ]]; then
      rsync -a --delete "$SOURCE_DIR/assets/$dir/" "$TARGET_DIR/dist-win/assets/$dir/"
    fi
  done
  # The importable theme-pack gallery ships as a top-level themes/ folder next to
  # the exe (not under assets/); refresh it too.
  if [[ -d "$SOURCE_DIR/themes" ]]; then
    rsync -a --delete "$SOURCE_DIR/themes/" "$TARGET_DIR/dist-win/themes/"
  fi
  echo "Refreshed runtime assets in $TARGET_DIR/dist-win/assets"
fi

echo "Synced MaxChat C++ source to $TARGET_DIR"
echo "Windows build command: C:\\work\\maxchat-c\\build.bat"
