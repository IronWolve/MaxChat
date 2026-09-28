#!/usr/bin/env bash
set -euo pipefail
source_file=${BASH_SOURCE[0]}
while [[ -L "$source_file" ]]; do
    source_dir=$(cd -P -- "$(dirname -- "$source_file")" && pwd)
    source_file=$(readlink "$source_file")
    [[ "$source_file" == /* ]] || source_file="$source_dir/$source_file"
done
source "$(dirname -- "$source_file")/macos-env.sh"
case ${1:-} in --help|-h) echo 'Usage: stop.sh — TERM only the recorded project-owned app.'; exit 0 ;; '') ;; *) fail 'Usage: stop.sh' ;; esac
project_lock
if ! pid=$(owned_pid); then info 'No recorded project-owned app is running.'; exit 0; fi
kill -TERM "$pid"
for ((attempt=0; attempt<30; attempt++)); do
    if ! owned_pid >/dev/null; then rm -f -- "$PID_FILE"; info "Stopped PID $pid"; exit 0; fi
    sleep 0.1
done
fail 'The app has not exited yet; no forced termination was attempted.'
