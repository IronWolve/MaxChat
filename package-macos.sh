#!/usr/bin/env bash
# Build a self-contained, ad-hoc-signed macOS app, ZIP and disk image.
set -euo pipefail
source_file=${BASH_SOURCE[0]}
while [[ -L "$source_file" ]]; do
    source_dir=$(cd -P -- "$(dirname -- "$source_file")" && pwd)
    source_file=$(readlink "$source_file")
    [[ "$source_file" == /* ]] || source_file="$source_dir/$source_file"
done
REPO_DIR=$(cd -P -- "$(dirname -- "$source_file")" && pwd)
PROJECT_DIR=$(cd -- "$REPO_DIR/.." && pwd)
cd -- "$PROJECT_DIR"
[[ $(uname -s) == Darwin ]] || { echo 'This packager must run on macOS.' >&2; exit 1; }
if ! command -v cmake >/dev/null; then
    for candidate in .cache/toolchains/cmake-*/CMake.app/Contents/bin; do
        if [[ -x "$candidate/cmake" ]]; then export PATH="$PROJECT_DIR/$candidate:$PATH"; break; fi
    done
fi
if [[ -x .cache/toolchains/ninja/ninja ]]; then export PATH="$PROJECT_DIR/.cache/toolchains/ninja:$PATH"; fi
for tool in cmake ninja python3 sips iconutil codesign ditto hdiutil lipo; do
    command -v "$tool" >/dev/null || { echo "Missing build prerequisite: $tool" >&2; exit 1; }
done
QT_SDK=${MAXCHAT_QT_ROOT:-.cache/qt-sdk/macos}
[[ "$QT_SDK" == /* ]] || QT_SDK="$PROJECT_DIR/$QT_SDK"
[[ -x "$QT_SDK/bin/macdeployqt" ]] || { echo 'Select a Qt macOS SDK with MAXCHAT_QT_ROOT.' >&2; exit 1; }
arch=${MAXCHAT_MAC_ARCH:-$(uname -m)}
case "$arch" in arm64|x86_64) ;; *) echo 'Choose arm64 or x86_64.' >&2; exit 1 ;; esac
jobs=${MAXCHAT_BUILD_JOBS:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'MAXCHAT_BUILD_JOBS must be positive.' >&2; exit 1; }
source "$REPO_DIR/tools/macos-env.sh"
project_lock
if pid=$(owned_pid); then fail "Close the project-owned app (PID $pid) before rebuilding."; fi
for directory in run run/macos run/packages; do
    [[ ! -L "$directory" ]] || fail 'Refusing a symlinked build/output directory.'
done
umask 077
mkdir -p run/build-macos run/macos run/packages logs .cache/clang tmp
export TMPDIR="$PROJECT_DIR/tmp" TMP="$PROJECT_DIR/tmp" TEMP="$PROJECT_DIR/tmp"
export CLANG_MODULE_CACHE_PATH="$PROJECT_DIR/.cache/clang"
BUILD=run/build-macos
fresh_arg=
if [[ -f "$BUILD/CMakeCache.txt" ]]; then
    while IFS='=' read -r key value; do
        case "$key" in
            CMAKE_HOME_DIRECTORY:INTERNAL) [[ "$value" == "$REPO_DIR" ]] || fresh_arg=--fresh ;;
            CMAKE_CACHEFILE_DIR:INTERNAL) [[ "$value" == "$PROJECT_DIR/$BUILD" ]] || fresh_arg=--fresh ;;
        esac
    done < "$BUILD/CMakeCache.txt"
fi
printf '[MaxChat] Source: repo/ | Build: %s | Architecture: %s\n' "$BUILD" "$arch"
cmake ${fresh_arg:+"$fresh_arg"} -S repo -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_SDK" -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DBUILD_TESTING="${MAXCHAT_BUILD_TESTS:-OFF}"
cmake --build "$BUILD" --parallel "$jobs" --target maxchat-c maxchat-secrets maxchat-script-worker
VERSION=$(python3 -c 'import plistlib, sys; print(plistlib.load(open(sys.argv[1], "rb"))["CFBundleShortVersionString"])' "$BUILD/MaxChat.app/Contents/Info.plist")
STAGE=run/macos/package-stage
APP="$STAGE/MaxChat.app"
# This directory contains only the previous generated packaging stage.
[[ ! -L "$STAGE" ]] || { echo 'Refusing a symlinked package stage.' >&2; exit 1; }
rm -rf -- "$STAGE"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
install -m755 "$BUILD/MaxChat.app/Contents/MacOS/MaxChat" "$APP/Contents/MacOS/MaxChat"
install -m755 "$BUILD/maxchat-secrets" "$APP/Contents/MacOS/maxchat-secrets"
install -m755 "$BUILD/maxchat-script-worker" "$APP/Contents/MacOS/maxchat-script-worker"
install -m644 "$BUILD/MaxChat.app/Contents/Info.plist" "$APP/Contents/Info.plist"
cmake -DSOURCE_ROOT=repo -DDESTINATION_ROOT="$APP/Contents/Resources" -P repo/packaging/stage-assets.cmake
ICONSET="$BUILD/MaxChat.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" repo/assets/icons/maxchat.png --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" repo/assets/icons/maxchat.png --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/MaxChat.icns"
mkdir -p "$APP/Contents/PlugIns/platforms"
install -m755 "$QT_SDK/plugins/platforms/libqoffscreen.dylib" "$APP/Contents/PlugIns/platforms/libqoffscreen.dylib"
"$QT_SDK/bin/macdeployqt" "$APP" -always-overwrite -no-codesign \
    "-executable=$PROJECT_DIR/$APP/Contents/MacOS/maxchat-secrets" \
    "-executable=$PROJECT_DIR/$APP/Contents/MacOS/maxchat-script-worker" -verbose=1
# Keep only the selected architecture, then sign every nested binary before its bundle.
python3 repo/packaging/sign-macos-bundle.py "$APP" "$arch"
codesign --verify --deep --strict --verbose=2 "$APP"
python3 repo/packaging/check-macos-bundle.py "$APP" "$arch"
ZIP="run/packages/MaxChat-$VERSION-macos-$arch.zip"
DMG="run/packages/MaxChat-$VERSION-macos-$arch.dmg"
[[ ! -L "$ZIP" && ! -L "$DMG" ]] || { echo 'Refusing a symlinked package output.' >&2; exit 1; }
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname MaxChat -srcfolder "$STAGE" -ov -format UDZO "$DMG"
# Publish a complete runtime copy and preserve the previous owned bundle.
deployment=$(mktemp -d "$PROJECT_DIR/run/macos/deploy.XXXXXX")
ditto "$APP" "$deployment/MaxChat.app"
if [[ -e run/macos/MaxChat.app ]]; then
    mv run/macos/MaxChat.app "run/macos/MaxChat.previous.$(date -u +%Y%m%dT%H%M%S).$$.app"
fi
mv "$deployment/MaxChat.app" run/macos/MaxChat.app
rmdir "$deployment"
mkdir -p run/launchers
for name in macos-env.sh start-macos.sh stop-macos.sh; do install -m755 "repo/tools/$name" "run/launchers/$name"; done
printf '[MaxChat] Built %s and %s. Ad-hoc signed; not notarized. No application was launched.\n' "$ZIP" "$DMG"
