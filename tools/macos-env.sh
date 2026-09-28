#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
APP_BIN="$PROJECT_DIR/run/macos/MaxChat.app/Contents/MacOS/MaxChat"
PID_FILE="$PROJECT_DIR/tmp/maxchat.pid"
LOG_FILE="$PROJECT_DIR/logs/maxchat.log"
color='' reset=''
if [[ -t 1 && -z ${NO_COLOR:-} ]]; then color=$'\033[1;36m'; reset=$'\033[0m'; fi
info() { printf '%s[MaxChat]%s %s\n' "$color" "$reset" "$*"; }
fail() { printf '[MaxChat] ERROR: %s\n' "$*" >&2; exit 1; }
owned_pid() {
    local candidate
    [[ -f "$PID_FILE" && ! -L "$PID_FILE" ]] || return 1
    read -r candidate < "$PID_FILE" || return 1
    [[ "$candidate" =~ ^[1-9][0-9]*$ ]] || return 1
    [[ $(ps -p "$candidate" -o uid= 2>/dev/null | tr -d ' ') == "$(id -u)" ]] || return 1
    lsof -a -p "$candidate" -d txt -Fn 2>/dev/null | grep -Fqx "n$APP_BIN" || return 1
    printf '%s\n' "$candidate"
}
project_environment() {
    umask 077
    mkdir -p "$PROJECT_DIR"/{.config/maxchat,.cache/maxchat,logs/chat,tmp}
    [[ ! -L "$PID_FILE" && ! -L "$LOG_FILE" ]] || fail 'Refusing a symlinked lifecycle file.'
    export MAXCHAT_PROFILE_DIR="$PROJECT_DIR/.config/maxchat"
    export MAXCHAT_CACHE_DIR="$PROJECT_DIR/.cache/maxchat"
    export TMPDIR="$PROJECT_DIR/tmp" TMP="$PROJECT_DIR/tmp" TEMP="$PROJECT_DIR/tmp"
    if [[ ! -e "$MAXCHAT_PROFILE_DIR/logs" && ! -L "$MAXCHAT_PROFILE_DIR/logs" ]]; then
        ln -s ../../logs/chat "$MAXCHAT_PROFILE_DIR/logs"
    fi
}

project_lock() {
    mkdir -p "$PROJECT_DIR/tmp"
    LOCK_DIR="$PROJECT_DIR/tmp/lifecycle.lock.d"
    mkdir "$LOCK_DIR" 2>/dev/null || fail 'Another lifecycle operation owns tmp/lifecycle.lock.d.'
    trap 'lifecycle_exit_status=$?; rmdir -- "$LOCK_DIR" 2>/dev/null || true; exit "$lifecycle_exit_status"' EXIT
}
