#!/usr/bin/env bash
# Build a self-contained Linux AppImage for MaxChat: a single executable file
# with Qt bundled, so it runs on any recent x86-64 Linux without installing Qt.
# Output: dist-linux/MaxChat-<version>-x86_64.AppImage
#
# Requires: a working Qt6 build toolchain (cmake, ninja, g++, qt6-base-dev,
# qt6-multimedia-dev, qt6-tools-dev) + curl. The linuxdeploy tools are fetched
# automatically and cached under build-appimage/tools/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

VERSION="$(grep -oP 'VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | head -1)"
echo "==> Packaging MaxChat ${VERSION} (Linux AppImage)"

BUILD_DIR="$ROOT/build-release"
WORK="$ROOT/build-appimage"
APPDIR="$WORK/AppDir"
TOOLS="$WORK/tools"
OUT="$ROOT/dist-linux"

# 1. Release build.
echo "==> Building Release"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)"
[ -x "$BUILD_DIR/maxchat" ] || { echo "ERROR: $BUILD_DIR/maxchat not built"; exit 1; }

# 2. Stage AppDir. MaxChat loads themes/wallpapers/dictionaries from disk next to
#    the executable, so they live beside the binary in usr/bin/ (the theme-pack
#    gallery ships too).
echo "==> Staging AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BUILD_DIR/maxchat" "$APPDIR/usr/bin/maxchat"
cp -r "$ROOT/assets" "$APPDIR/usr/bin/assets"
cp -r "$ROOT/themes" "$APPDIR/usr/bin/themes"

# 3. Fetch linuxdeploy + the Qt plugin (cached).
echo "==> Fetching linuxdeploy tools"
mkdir -p "$TOOLS"
fetch() { [ -f "$2" ] || curl -fL --retry 3 -o "$2" "$1"; chmod +x "$2"; }
LD="$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LD"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
      "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"

# 4. Build the AppImage. EXTRACT_AND_RUN lets the tool AppImages run without FUSE
#    (e.g. inside WSL/containers). The Qt plugin finds Qt via QMAKE.
echo "==> Running linuxdeploy (bundling Qt)"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$(command -v qmake6 || command -v qmake)"
export VERSION
export PATH="$TOOLS:$PATH"   # so --plugin qt finds linuxdeploy-plugin-qt
mkdir -p "$OUT"

# Step A: deploy Qt + dependent libs into the AppDir (no AppImage yet).
"$LD" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/maxchat" \
    --desktop-file "$ROOT/packaging/maxchat.desktop" \
    --icon-file "$ROOT/assets/icons/maxchat.png" \
    --plugin qt

# Step B: also bundle the offscreen platform plugin so `--selftest` (which forces
# QT_QPA_PLATFORM=offscreen) works for headless smoke tests / CI. It needs only
# QtCore/QtGui, already exposed via the AppRun's LD_LIBRARY_PATH. Desktop users
# use the auto-deployed xcb plugin.
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
install -Dm644 "$QT_PLUGINS/platforms/libqoffscreen.so" \
    "$APPDIR/usr/plugins/platforms/libqoffscreen.so"

# Step C: package the AppDir into the AppImage.
( cd "$OUT" && "$LD" --appdir "$APPDIR" \
    --desktop-file "$ROOT/packaging/maxchat.desktop" \
    --output appimage )

echo "==> Done: $(ls -1 "$OUT"/MaxChat-*-x86_64.AppImage 2>/dev/null | tail -1)"
