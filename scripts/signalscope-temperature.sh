#!/bin/sh

OUTPUT=/run/signalscope-cpu-temp
INTERVAL=${SIGNALSCOPE_TEMP_INTERVAL:-5}

cleanup() {
    rm -f "$OUTPUT"
}

trap cleanup EXIT
trap 'exit 0' INT TERM

while :; do
    value=$(/usr/bin/vcgencmd measure_temp 2>/dev/null) || value=
    if [ -n "$value" ]; then
        temporary="${OUTPUT}.tmp.$$"
        printf '%s\n' "$value" > "$temporary"
        chmod 0644 "$temporary"
        mv -f "$temporary" "$OUTPUT"
    fi
    sleep "$INTERVAL"
done
