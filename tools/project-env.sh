#!/usr/bin/env bash
# Shared by source tools/ and the deployed run/launchers/ copy.
set -euo pipefail
PROJECT_DIR="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/../.." && pwd)"
APP_REL="run/app"
APP_DIR="$PROJECT_DIR/$APP_REL"
APP_BIN="$APP_DIR/maxchat"
PID_FILE="$PROJECT_DIR/tmp/maxchat.pid"
LOG_FILE="$PROJECT_DIR/logs/maxchat.log"
color='' reset=''
if [[ -t 1 && -z ${NO_COLOR:-} && ${TERM:-dumb} != dumb ]]; then
    color=$'\033[1;36m'; reset=$'\033[0m'
fi
info() { printf '%s[MaxChat]%s %s\n' "$color" "$reset" "$*"; }
fail() { printf '[MaxChat] ERROR: %s\n' "$*" >&2; exit 1; }
owned_pid() {
    local candidate
    [[ -f "$PID_FILE" ]] || return 1
    read -r candidate < "$PID_FILE" || return 1
    [[ "$candidate" =~ ^[1-9][0-9]*$ && -O /proc/$candidate ]] || return 1
    [[ $(readlink -- "/proc/$candidate/exe" 2>/dev/null) == "$APP_BIN" ]] || return 1
    printf '%s\n' "$candidate"
}
project_environment() {
    umask 077
    mkdir -p "$PROJECT_DIR"/{run/app,run/launchers,.config/maxchat,.cache,logs/chat,tmp}
    export MAXCHAT_PROFILE_DIR="$PROJECT_DIR/.config/maxchat"
    export MAXCHAT_CACHE_DIR="$PROJECT_DIR/.cache/maxchat"
    export XDG_CONFIG_HOME="$PROJECT_DIR/.config"
    export XDG_CACHE_HOME="$PROJECT_DIR/.cache"
    export XDG_DATA_HOME="$PROJECT_DIR/.config/data"
    export XDG_STATE_HOME="$PROJECT_DIR/logs/state"
    export TMPDIR="$PROJECT_DIR/tmp" TMP="$PROJECT_DIR/tmp" TEMP="$PROJECT_DIR/tmp"
    export CCACHE_DIR="$PROJECT_DIR/.cache/ccache"
    export PYTHONPYCACHEPREFIX="$PROJECT_DIR/.cache/python"
    if [[ ! -e "$PROJECT_DIR/.config/maxchat/logs" && ! -L "$PROJECT_DIR/.config/maxchat/logs" ]]; then
        ln -s ../../logs/chat "$PROJECT_DIR/.config/maxchat/logs"
    fi
}
project_lock() {
    command -v flock >/dev/null || fail 'flock is required.'
    exec 9>"$PROJECT_DIR/tmp/lifecycle.lock"
    flock -n 9 || fail 'Another build/start/stop operation is in progress.'
}

# Run configure from the project root with relative source/output arguments.
# CMake itself stores absolute paths in its local cache. Refresh that disposable
# cache when this project has moved instead of depending on its former location.
project_configure() (
    local build_relative="$1" key value cached_source='' cached_build='' cached_qt=''
    shift
    case "$build_relative" in
        run/build|run/build-release) ;;
        *) fail 'Unexpected project build directory.' ;;
    esac
    cd -- "$PROJECT_DIR"
    local fresh=()
    if [[ -f "$build_relative/CMakeCache.txt" ]]; then
        while IFS='=' read -r key value; do
            case "$key" in
                CMAKE_HOME_DIRECTORY:INTERNAL) cached_source="$value" ;;
                CMAKE_CACHEFILE_DIR:INTERNAL) cached_build="$value" ;;
                Qt6_DIR:PATH) cached_qt="$value" ;;
            esac
        done < "$build_relative/CMakeCache.txt"
        if [[ "$cached_source" != "$PROJECT_DIR/repo" ||
              "$cached_build" != "$PROJECT_DIR/$build_relative" ]]; then
            info "Project location changed; refreshing the local $build_relative CMake cache."
            fresh+=(--fresh)
        fi
    fi
    local qt=() sdk="${MAXCHAT_QT_ROOT:-.cache/qt-sdk/linux}"
    if [[ "$sdk" != /* ]]; then sdk="$PROJECT_DIR/$sdk"; fi
    if [[ -f "$sdk/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        qt+=("-DCMAKE_PREFIX_PATH=$sdk")
        if [[ -n "$cached_qt" && "$cached_qt" != "$sdk/lib/cmake/Qt6" ]]; then
            info 'Qt SDK changed; refreshing the local build cache.'
            fresh=(--fresh)
        fi
    elif [[ -n ${MAXCHAT_QT_ROOT:-} ]]; then
        fail 'The configured Qt SDK is missing Qt6Config.cmake.'
    fi
    cmake "${fresh[@]}" -S repo -B "$build_relative" "${qt[@]}" "$@"
)
