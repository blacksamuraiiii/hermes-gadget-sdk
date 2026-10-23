#!/usr/bin/env bash
# Start, stop and inspect the USB relay that carries a gadget's link on COM5.
#
#   tools/relay.sh start            # background, no console window
#   tools/relay.sh start --visible  # a console window (close it to stop)
#   tools/relay.sh stop
#   tools/relay.sh status
#
# The relay has to run on Windows (WSL cannot see the COM port) and has to keep
# running: the device reaches the gateway only through it. Log: tools/relay.log.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PS='/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe'
# Windows-side paths. The working directory is derived from this script's own
# location, so a clone works wherever it sits. The relay's Python lives in a
# conda env on the Windows side; point RELAY_ENV_DIR at it (Windows syntax), e.g.
#   export RELAY_ENV_DIR='C:\Users\<you>\miniconda3\envs\gadget-relay'
: "${RELAY_ENV_DIR:=${USERPROFILE:-}/miniconda3/envs/gadget-relay}"
ENV_DIR="${RELAY_ENV_DIR//\//\\}"
WIN_DIR="$(wslpath -w "$HERE" 2>/dev/null || echo "$HERE")"
ARGS="'-u','usb_relay.py','--target','ws://127.0.0.1:8765','--path','/gadget','--log-level','INFO','--log-file','relay.log'"

relay_pid() {
  "$PS" -NoProfile -Command \
    "(Get-CimInstance Win32_Process -Filter \"Name='python.exe' OR Name='pythonw.exe'\" | Where-Object {\$_.CommandLine -like '*usb_relay*'}).ProcessId" \
    2>/dev/null | tr -d '\r' | head -1
}

case "${1:-status}" in
  start)
    pid="$(relay_pid)"
    if [ -n "$pid" ]; then
      echo "relay already running (pid $pid)"
      exit 0
    fi
    if [ "${2:-}" = "--visible" ]; then
      exe="$ENV_DIR\\python.exe"
      style="Normal"
    else
      exe="$ENV_DIR\\pythonw.exe"   # pythonw has no console at all
      style="Hidden"
    fi
    "$PS" -NoProfile -Command \
      "Start-Process -FilePath '$exe' -ArgumentList $ARGS -WorkingDirectory '$WIN_DIR' -WindowStyle $style"
    sleep 6
    pid="$(relay_pid)"
    if [ -z "$pid" ]; then
      echo "relay did not start; see $HERE/relay.log" >&2
      exit 1
    fi
    echo "relay started (pid $pid, window: ${2:-hidden})"
    grep -E "online \(paired\)|failed|error" "$HERE/relay.log" | tail -3 || true
    ;;
  stop)
    pid="$(relay_pid)"
    if [ -z "$pid" ]; then
      echo "relay is not running"
      exit 0
    fi
    /mnt/c/Windows/System32/taskkill.exe /PID "$pid" /F >/dev/null
    echo "relay stopped (pid $pid)"
    ;;
  status)
    pid="$(relay_pid)"
    if [ -z "$pid" ]; then
      echo "relay: stopped"
    else
      echo "relay: running (pid $pid)"
      echo -n "last device line: "
      grep "\[dev\]" "$HERE/relay.log" | tail -1 || echo "(none)"
    fi
    ;;
  *)
    echo "usage: relay.sh {start [--visible]|stop|status}" >&2
    exit 2
    ;;
esac
