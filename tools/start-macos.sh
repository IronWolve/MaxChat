#!/usr/bin/env bash
set -euo pipefail
source_file=${BASH_SOURCE[0]}
while [[ -L "$source_file" ]]; do
    source_dir=$(cd -P -- "$(dirname -- "$source_file")" && pwd)
    source_file=$(readlink "$source_file")
    [[ "$source_file" == /* ]] || source_file="$source_dir/$source_file"
done
source "$(dirname -- "$source_file")/macos-env.sh"
case ${1:-} in
    --help|-h) printf 'Usage: start.sh [--status]\nRuns the built Mac app with a private project profile; does not build it.\n'; exit 0 ;;
    --status)
        if pid=$(owned_pid); then info "Running owned PID $pid"; else info 'Not running through this launcher.'; fi
        [[ ! -x "$APP_BIN" ]] || info 'Runtime available: run/macos/MaxChat.app'
        exit 0 ;;
    '') ;;
    *) fail 'Usage: start.sh [--status]' ;;
esac
project_environment
project_lock
[[ -x "$APP_BIN" ]] || fail 'Build the app first with ./build.sh.'
if pid=$(owned_pid); then info "Already running: PID $pid"; exit 0; fi
cd -- "$PROJECT_DIR/run/macos"
nohup "$APP_BIN" >> "$LOG_FILE" 2>&1 </dev/null &
pid=$!
printf '%s\n' "$pid" > "$PID_FILE"
sleep 1
owned_pid >/dev/null || fail 'Startup exited; inspect logs/maxchat.log.'
info "Started native $(uname -m) app: PID $pid"
info 'Profile: .config/maxchat | Cache: .cache/maxchat | Log: logs/maxchat.log'
info 'This confirms the process is running; connection state is shown in the app.'
