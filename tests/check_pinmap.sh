#!/usr/bin/env bash
# Checks that the pin map in docs/07-wiring-chart.md section 7 still matches the
# pin constants in consolidated/ecu.h.
#
# A wiring change that is not reflected in the firmware (or the reverse) is the
# most common bench fault, and it is exactly the shape of DEF-101, so this runs
# as part of the SIL suite.
#
# Usage: tests/check_pinmap.sh [-DDEFECT_SHARED_PIN=1 ...]
#   Any arguments are passed to the preprocessor, so the same check can be
#   pointed at a defect build to show that it catches the collision.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="$ROOT/consolidated/ecu.h"
CHART="$ROOT/docs/07-wiring-chart.md"

fails=0

# Read the header the way the compiler does, so that the pins checked are the
# ones a normal (defect-free) build actually uses and not the disabled branch
# of an #if.
HEADER_TEXT="$(mktemp)"
trap 'rm -f "$HEADER_TEXT"' EXIT
if ! gcc -E -P -x c++ "$@" "$HEADER" >"$HEADER_TEXT" 2>/dev/null; then
  echo "  FAIL  pinmap: cannot preprocess ${HEADER#"$ROOT"/}"
  exit 1
fi

# firmware constant : token expected on that pin's line in the chart
map="
PIN_TEMP_SENSOR:coolant temperature
PIN_HAZARD_SW:hazard switch
PIN_TURN_L_SW:turn stalk left
PIN_TURN_R_SW:turn stalk right
PIN_HEADLAMP_SW:headlamp switch
PIN_LAMP_LEFT:left lamp
PIN_LAMP_RIGHT:right lamp
PIN_HEADLAMP:headlamps
PIN_FAN_RELAY:cooling fan relay
PIN_OVERTEMP:overtemp warning
PIN_HEARTBEAT:heartbeat
"

# Section 7 of the chart, i.e. the fenced pin map.
chart_block="$(awk '/^## 7\./,0' "$CHART")"

while IFS=: read -r name token; do
  [ -z "$name" ] && continue

  pin="$(sed -n "s/^const int ${name} *= *\([A-Z0-9]*\).*/\1/p" "$HEADER_TEXT" | head -n1)"
  if [ -z "$pin" ]; then
    printf '  FAIL  pinmap: %s not found in %s\n' "$name" "${HEADER#"$ROOT"/}"
    fails=$((fails + 1))
    continue
  fi
  case "$pin" in
    A*) label="$pin" ;;
    *)  label="D$pin" ;;
  esac

  if printf '%s\n' "$chart_block" | grep -qE "^ +${label} +.*${token}"; then
    printf '  PASS  pinmap: %-18s %-3s %s\n' "$name" "$label" "$token"
  else
    printf '  FAIL  pinmap: %s is %s in firmware, not documented as "%s" on %s in %s section 7\n' \
      "$name" "$label" "$token" "$label" "${CHART#"$ROOT"/}"
    fails=$((fails + 1))
  fi
done <<EOF
$map
EOF

# No two functions may claim the same input pin. This is the automated form of
# DEF-101: the defect build fails here, and the shipped build must not.
dupes="$(sed -n 's/^const int \(PIN_[A-Z_]*\) *= *\([A-Z0-9]*\).*/\2/p' "$HEADER_TEXT" \
         | sort | uniq -d | tr '\n' ' ')"
if [ -n "${dupes// /}" ]; then
  printf '  FAIL  pinmap: pin(s) used by more than one signal: %s\n' "$dupes"
  fails=$((fails + 1))
else
  printf '  PASS  pinmap: %-18s every signal has its own pin\n' "no collisions"
fi

if [ "$fails" -ne 0 ]; then
  echo "pinmap: $fails mismatch(es)"
fi
exit $((fails > 0))
