#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")"
source "$SCRIPT_DIR/project-env.sh"
cd -- "$PROJECT_DIR"
case ${1:-} in
    --help|-h)
        printf 'Usage: build.sh [--check]\nBuilds Release into run/build and deploys run/app without launching it.\nRequires installed CMake, Ninja, a C++20 compiler and Qt 6 development libraries.\nMAXCHAT_BUILD_JOBS sets parallel jobs (default: 2). No downloads or installs.\n'
        exit 0 ;;
    --check|'') ;;
    *) fail 'Usage: build.sh [--check]' ;;
esac
[[ $# -le 1 ]] || fail 'Usage: build.sh [--check]'
for tool in cmake ninja c++; do
    command -v "$tool" >/dev/null || fail "Missing build prerequisite: $tool"
    info "Found $tool on PATH"
done
[[ -f repo/CMakeLists.txt ]] || fail 'Source checkout missing: repo/'
info "Project: ${PROJECT_DIR##*/}"
info "Source: repo/"
info "Build: run/build/ (Release; tests disabled)"
info "Runtime: $APP_REL/"
if [[ ${1:-} == --check ]]; then
    info 'Path/tool checks passed. Qt development components are checked during configure.'
    exit 0
fi
project_environment
project_lock
if pid=$(owned_pid); then fail "Close the running application (PID $pid) before rebuilding."; fi
jobs=${MAXCHAT_BUILD_JOBS:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'MAXCHAT_BUILD_JOBS must be a positive integer.'
info "Building with $jobs jobs; output: logs/build.log"
# Prefix maps prevent __FILE__/debug metadata from embedding the checkout path.
# Relocated CMake caches are refreshed by project_configure.
(
    project_configure run/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build run/build --parallel "$jobs" --target maxchat-c maxchat-secrets maxchat-script-worker
    cmake -DSOURCE_ROOT=repo -DDESTINATION_ROOT="$APP_REL" \
        -P repo/packaging/stage-assets.cmake
    # Qt's dependency manifest stages runtime libraries/plugins and rewrites
    # runtime lookup paths relative to the installed executable.
    cmake --install run/build --prefix "$APP_DIR" --component Runtime
    for name in project-env.sh start.sh stop.sh; do
        install -m755 "repo/tools/$name" "run/launchers/$name"
    done
) 2>&1 | tee logs/build.log
info 'Build and deployment complete. Start with ./start.sh; rebuild after source or bundled asset changes.'
