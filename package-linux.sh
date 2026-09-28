#!/usr/bin/env bash
# Build a self-contained Linux AppImage for MaxChat: a single executable file
# with Qt bundled. Supported system-library versions depend on the build host;
# inspect and record the resulting GLIBC/GLIBCXX requirements for each release.
# Output: ../run/packages/MaxChat-<version>-x86_64.AppImage
#
# Requires: a working Qt6 build toolchain (cmake, ninja, g++, qt6-base-dev,
# qt6-multimedia-dev, qt6-tools-dev) + curl, Python 3.11+ and dpkg metadata. The linuxdeploy tools are fetched
# automatically and cached under ../.cache/appimage-tools/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT/tools/project-env.sh"
project_environment
project_lock
cd "$PROJECT_DIR"

VERSION="$(grep -oP 'VERSION \K[0-9]+\.[0-9]+\.[0-9]+' repo/CMakeLists.txt | head -1)"
echo "==> Packaging MaxChat ${VERSION} (Linux AppImage)"

BUILD_DIR="run/build-release"
WORK="run/appimage"
APPDIR="$WORK/AppDir"
TOOLS=".cache/appimage-tools"
OUT="run/packages"

# 1. Release build.
echo "==> Building Release"
project_configure "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null
cmake --build "$BUILD_DIR" --parallel "${MAXCHAT_BUILD_JOBS:-2}"
[ -x "$BUILD_DIR/maxchat" ] || { echo "ERROR: $BUILD_DIR/maxchat not built"; exit 1; }

# 2. Stage AppDir. MaxChat loads themes/wallpapers/dictionaries from disk next to
#    the executable, so they live beside the binary in usr/bin/ (the theme-pack
#    gallery ships too).
echo "==> Staging AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BUILD_DIR/maxchat" "$APPDIR/usr/bin/maxchat"
install -Dm755 "$BUILD_DIR/maxchat-secrets" "$APPDIR/usr/bin/maxchat-secrets"
install -Dm755 "$BUILD_DIR/maxchat-script-worker" "$APPDIR/usr/bin/maxchat-script-worker"
cmake -DSOURCE_ROOT=repo -DDESTINATION_ROOT="$APPDIR/usr/bin" \
      -P "repo/packaging/stage-assets.cmake"

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
# appimagetool otherwise fetches a mutable runtime during packaging. Pin the
# loader as well as the build tools; its source/notices accompany the release.
fetch "https://api.github.com/repos/AppImage/type2-runtime/releases/assets/596078161" \
      "$TOOLS/runtime-x86_64" \
      "156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074"
export LDAI_RUNTIME_FILE="$PROJECT_DIR/$TOOLS/runtime-x86_64"

# 4. Build the AppImage. EXTRACT_AND_RUN lets the tool AppImages run without FUSE
#    (e.g. inside WSL/containers). The Qt plugin finds Qt via QMAKE.
echo "==> Running linuxdeploy (bundling Qt)"
export APPIMAGE_EXTRACT_AND_RUN=1
QT_SDK="${MAXCHAT_QT_ROOT:-.cache/qt-sdk/linux}"
if [[ "$QT_SDK" != /* ]]; then QT_SDK="$PROJECT_DIR/$QT_SDK"; fi
if [[ -x "$QT_SDK/bin/qmake" ]]; then
    export QMAKE="$QT_SDK/bin/qmake"
    export LD_LIBRARY_PATH="$QT_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    export QMAKE="$(command -v qmake6 || command -v qmake)"
fi
export VERSION
export PATH="$PROJECT_DIR/$TOOLS:$PATH"   # so --plugin qt finds linuxdeploy-plugin-qt
mkdir -p "$OUT"

# Step A: deploy Qt + dependent libs into the AppDir (no AppImage yet).
"$LD" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/maxchat" \
    --desktop-file "repo/packaging/maxchat.desktop" \
    --icon-file "repo/assets/icons/maxchat.png" \
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
WAYLAND_LIB_ARGS=()
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

# Complete dependency deployment before collecting notices for the actual files.
"$LD" --appdir "$APPDIR" "${WAYLAND_LIB_ARGS[@]}"
python3 repo/packaging/linux-runtime-notices.py "$APPDIR"

# Package the assembled runtime and its dependency notices.
( cd "$OUT" && "$PROJECT_DIR/$LD" --appdir "$PROJECT_DIR/$APPDIR" \
    --desktop-file "$PROJECT_DIR/repo/packaging/maxchat.desktop" \
    --output appimage )

echo "==> Done: $(ls -1 "$OUT"/MaxChat-*-x86_64.AppImage 2>/dev/null | tail -1)"
