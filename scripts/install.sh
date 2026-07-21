#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="/data/UserData/schwung/modules/tools/signalscope"
BUILD_ROOT="${SIGNALSCOPE_BUILD_ROOT:?Set SIGNALSCOPE_BUILD_ROOT to the build output directory}"
SSH_TARGET="${SSH_TARGET:-ableton@move.local}"
ROOT_SSH_TARGET="${ROOT_SSH_TARGET:-root@move.local}"

scp -r "$BUILD_ROOT/dist/signalscope/"* "$SSH_TARGET:$MODULE_DIR/"
ssh "$SSH_TARGET" "chmod 0755 '$MODULE_DIR/signalscope-temperature.sh'"

scp "$SCRIPT_DIR/signalscope-temperature.init" "$ROOT_SSH_TARGET:/etc/init.d/signalscope-temperature"
ssh "$ROOT_SSH_TARGET" \
    "chmod 0755 /etc/init.d/signalscope-temperature && update-rc.d signalscope-temperature defaults && /etc/init.d/signalscope-temperature restart"
