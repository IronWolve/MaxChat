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
install -Dm755 "$BUILD_DIR/maxchat-secrets" "$APPDIR/usr/bin/maxchat-secrets"
cmake -DSOURCE_ROOT="$ROOT" -DDESTINATION_ROOT="$APPDIR/usr/bin" \
      -P "$ROOT/packaging/stage-assets.cmake"

# 3. Fetch linuxdeploy + the Qt plugin (cached).
echo "==> Fetching linuxdeploy tools"
mkdir -p "$TOOLS"
fetch() {
    local url="$1" output="$2" digest="$3"
    if [ -f "$output" ]; then
        printf '%s  %s\n' "$digest" "$output" | sha256sum --check --status - || {
            echo "ERROR: cached packaging tool does not match the reviewed digest: $(basename "$output")" >&2
            echo "Remove that cached tool to fetch the pinned release asset." >&2
            return 1
        }
    else
        curl -fL --retry 3 -H 'Accept: application/octet-stream' -o "$output.part" "$url"
        if ! printf '%s  %s\n' "$digest" "$output.part" | sha256sum --check --status -; then
            rm -f -- "$output.part"
            echo "ERROR: downloaded packaging tool failed digest verification." >&2
            return 1
        fi
        mv -- "$output.part" "$output"
    fi
    chmod +x "$output"
}
LD="$TOOLS/linuxdeploy-x86_64.AppImage"
# Immutable asset IDs plus publisher-reported SHA-256 digests. Review both when
# upgrading; never execute an unverified mutable continuous-release download.
fetch "https://api.github.com/repos/linuxdeploy/linuxdeploy/releases/assets/538917371" "$LD" \
      "36a2d7e274d12e1050d0e9ecfe11d339ed54720b2bec464c286d53f8b07f5c62"
fetch "https://api.github.com/repos/linuxdeploy/linuxdeploy-plugin-qt/releases/assets/525032210" \
      "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" \
      "cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617"

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
QT_LIBS="$("$QMAKE" -query QT_INSTALL_LIBS)"
install -Dm644 "$QT_PLUGINS/platforms/libqoffscreen.so" \
    "$APPDIR/usr/plugins/platforms/libqoffscreen.so"

# Step B2: bundle the Wayland platform plugin + shell-integration plugins + the Qt
# Wayland client lib, so on a Wayland session (incl. WSLg) the app runs natively
# instead of through XWayland. XWayland's override-redirect popups leave ghost
# artifacts from menus; native Wayland composites them cleanly. Qt auto-selects
# wayland when WAYLAND_DISPLAY is set and falls back to the bundled xcb on X11.
if [ -f "$QT_PLUGINS/platforms/libqwayland.so" ]; then
    install -Dm644 "$QT_PLUGINS/platforms/libqwayland.so" \
        "$APPDIR/usr/plugins/platforms/libqwayland.so"
    for sub in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
        if [ -d "$QT_PLUGINS/$sub" ]; then
            for p in "$QT_PLUGINS/$sub"/*.so; do
                [ -e "$p" ] && install -Dm644 "$p" "$APPDIR/usr/plugins/$sub/$(basename "$p")"
            done
        fi
    done
    # Bundle the client-side Qt wayland libs that exist; linuxdeploy resolves
    # their system deps (libwayland-client/cursor/egl, libxkbcommon).
    WAYLAND_LIB_ARGS=()
    for lib in libQt6WaylandClient libQt6WlShellIntegration libQt6WaylandEglClientHwIntegration; do
        [ -e "$QT_LIBS/$lib.so.6" ] && WAYLAND_LIB_ARGS+=(--library "$QT_LIBS/$lib.so.6")
    done
fi

# Step C: package the AppDir into the AppImage (linuxdeploy resolves + bundles the
# Wayland libs' system dependencies via --library, then patches rpaths).
( cd "$OUT" && "$LD" --appdir "$APPDIR" \
    --desktop-file "$ROOT/packaging/maxchat.desktop" \
    "${WAYLAND_LIB_ARGS[@]}" \
    --output appimage )

echo "==> Done: $(ls -1 "$OUT"/MaxChat-*-x86_64.AppImage 2>/dev/null | tail -1)"
