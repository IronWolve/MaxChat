#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")"
source "$SCRIPT_DIR/project-env.sh"
case ${1:-} in
    --help|-h)
        printf 'Usage: start.sh [--status | --help | application arguments]\nStarts the built Linux desktop app in the background. NO_COLOR=1 disables colors.\nSettings: .config/maxchat/  Logs: logs/  Runtime: run/app/\n'
        exit 0 ;;
    --status)
        if pid=$(owned_pid); then info "Running: PID $pid"; else info 'Stopped (no owned process).'; fi
        if [[ -x "$APP_BIN" ]]; then info "Runtime available: $APP_REL/maxchat"; else info 'Runtime not built. Run ./build.sh when ready.'; fi
        exit 0 ;;
esac
[[ -x "$APP_BIN" && -x "$APP_DIR/maxchat-secrets" ]] || fail 'Runtime not built. Run ./build.sh first.'
[[ -n ${DISPLAY:-} || -n ${WAYLAND_DISPLAY:-} || ${QT_QPA_PLATFORM:-} == offscreen ]] || fail 'No graphical display is available.'
project_environment
project_lock
if pid=$(owned_pid); then info "Already running: PID $pid"; exit 0; fi
info "Project: ${PROJECT_DIR##*/}"
info "Runtime: $APP_REL/"
info "Profile: .config/maxchat/"
info "Display backend: ${QT_QPA_PLATFORM:-Qt automatic selection}; log: logs/maxchat.log"
cd -- "$APP_DIR"
nohup ./maxchat "$@" >> "$LOG_FILE" 2>&1 < /dev/null 9>&- &
pid=$!
printf '%s\n' "$pid" > "$PID_FILE"
sleep 0.5
if ! kill -0 "$pid" 2>/dev/null; then
    rm -f -- "$PID_FILE"
    fail "Application exited during startup. See logs/maxchat.log"
fi
info "Process started: PID $pid (GUI readiness not checked). Stop with ./stop.sh."
