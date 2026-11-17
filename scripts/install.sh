#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="/data/UserData/schwung/modules/tools/signalscope"
BUILD_ROOT="${SIGNALSCOPE_BUILD_ROOT:?Set SIGNALSCOPE_BUILD_ROOT to the build output directory}"
SSH_TARGET="${SSH_TARGET:-ableton@move.local}"
ROOT_SSH_TARGET="${ROOT_SSH_TARGET:-root@move.local}"

scp -r "$BUILD_ROOT/dist/signalscope/"* "$SSH_TARGET:$MODULE_DIR/"
ssh "$ROOT_SSH_TARGET" 'set -eu
    service=/etc/init.d/signalscope-temperature
    temporary=$(mktemp /etc/init.d/.signalscope-temperature.XXXXXX)
    trap '\''rm -f "$temporary"'\'' EXIT
    cat > "$temporary"
    sh -n "$temporary"
    chmod 0755 "$temporary"
    if [ -e "$service" ]; then
        pid=
        if [ -r /run/signalscope-temperature.pid ]; then
            IFS= read -r pid < /run/signalscope-temperature.pid || pid=
        fi
        case "$pid" in
            ""|0|1|*[!0-9]*) pid= ;;
        esac
        "$service" stop
        if [ -n "$pid" ] && [ -s "/proc/$pid/cmdline" ] &&
            tr "\000" "\n" < "/proc/$pid/cmdline" | grep -Eq "^(/etc/init.d/signalscope-temperature|/data/UserData/schwung/modules/tools/signalscope/signalscope-temperature.sh)$"; then
            echo "Existing temperature worker is still running; stop it before replacing the service." >&2
            exit 1
        fi
    fi
    mv -f "$temporary" "$service"
    update-rc.d signalscope-temperature defaults
    "$service" start' < "$SCRIPT_DIR/signalscope-temperature.init"
