#!/usr/bin/env bash
set -o pipefail

SCENARIO="${1:-scenario-judge-demo.yaml}"
TIMEOUT="${2:-65000}"

# Run the automated Wokwi scenario but display only the useful RideShield
# firmware output. The full raw log is simultaneously saved for evidence.
RAW_LOG="judge-demo-raw.log"

wokwi-cli . --scenario "$SCENARIO" --timeout "$TIMEOUT" 2>&1 \
  | tee "$RAW_LOG" \
  | awk '
    /^\[RideShield Full Automated/ { next }
    /^Wokwi CLI/ { next }
    /^Connected to Wokwi/ { next }
    /^Starting simulation/ { next }
    /^ESP-ROM:/ { next }
    /^Build:/ { next }
    /^rst:/ { next }
    /^SPIWP:/ { next }
    /^mode:/ { next }
    /^load:/ { next }
    /^entry / { next }
    /^E \([0-9]+\) ledc:/ { next }

    # Avoid repeating a full STATUS line for every countdown second.
    /^\[STATUS\] state=COUNTDOWN/ { next }

    # Firmware logs worth showing to judges.
    /^\[BOOT\]/ ||
    /^\[HELMET\]/ ||
    /^\[CRASH\]/ ||
    /^\[VERIFY\]/ ||
    /^\[COUNTDOWN\]/ ||
    /^\[CANCEL\]/ ||
    /^\[EMERGENCY\]/ ||
    /^\[LTE\]/ ||
    /^\[SD\]/ ||
    /^\[BLACKBOX\]/ ||
    /^\[RESET\]/ ||
    /^\[STATUS\] state=NOT_WORN/ ||
    /^\[STATUS\] state=READY/ ||
    /^       RIDESHIELD EMERGENCY ALERT/ ||
    /^  Event ID:/ ||
    /^  Event severity:/ ||
    /^  Pre-event speed:/ ||
    /^  GPS:/ ||
    /^  LTE:/ ||
    /^  SMS:/ ||
    /^===== \/events.csv =====/ ||
    /^event_id,time_ms,outcome/ ||
    /^[0-9]+,[0-9]+,(CANCELLED|CONFIRMED|REJECTED),/ ||
    /^===== END =====/ ||
    /Scenario completed successfully/ {
        print
        fflush()
    }
  '

