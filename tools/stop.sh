#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")"
source "$SCRIPT_DIR/project-env.sh"
case ${1:-} in
    --help|-h) printf 'Usage: stop.sh\nSends TERM only to the recorded process owned by this project.\n'; exit 0 ;;
    '') ;;
    *) fail 'Usage: stop.sh' ;;
esac
[[ $# == 0 ]] || fail 'Usage: stop.sh'
if ! pid=$(owned_pid); then info 'Stopped (no owned process).'; exit 0; fi
project_environment
project_lock
pid=$(owned_pid) || { info 'Already stopped.'; exit 0; }
info "Stopping project process: PID $pid"
kill -TERM "$pid"
for ((attempt=0; attempt<50; attempt++)); do
    if ! owned_pid >/dev/null; then
        rm -f -- "$PID_FILE"
        info 'Stopped.'
        exit 0
    fi
    sleep 0.2
done
fail 'Process has not exited after 10 seconds; no force kill was sent.'
