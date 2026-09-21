#!/usr/bin/env bash
# Checks that the pin map in docs/08-wiring-chart.md section 8 still matches the
# pin constants in uno_baseline/uno_baseline.ino.
#
# A wiring change that is not reflected in the firmware (or the reverse) is the
# most common bench fault, so this runs in the SIL suite.
#
# Usage: tests/check_pinmap.sh

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH="$ROOT/uno_baseline/uno_baseline.ino"
CHART="$ROOT/docs/08-wiring-chart.md"

fails=0

# firmware constant : token expected in the chart line for that pin
map="
injectorPin:injector
fuelPumpRelay:fuel pump
throttlePlatePin:throttle PWM
engineIndicator:engine run
starterIndicator:cranking
safetyPin:overspeed
crankTDCOut:crank TDC stimulus
crankPreTDCOut:crank pre-TDC stimulus
hallTDC:TDC
hallPreTDC:pre-TDC
adcMAT:MAT
adcCHT:CHT
adcMAP:MAP
adcLambda:lambda
adcTPS:TPS
adcPot:throttle demand
"

# Section 8 of the chart, i.e. the fenced pin map.
chart_block="$(awk '/^## 8\./,0' "$CHART")"

while IFS=: read -r name token; do
  [ -z "$name" ] && continue

  pin="$(sed -n "s/^const int ${name} *= *\([A-Z0-9]*\).*/\1/p" "$SKETCH" | head -n1)"
  if [ -z "$pin" ]; then
    printf '  FAIL  pinmap: %s not found in %s\n' "$name" "${SKETCH#"$ROOT"/}"
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
    printf '  FAIL  pinmap: %s is %s in firmware, not documented as "%s" on %s in %s section 8\n' \
      "$name" "$label" "$token" "$label" "${CHART#"$ROOT"/}"
    fails=$((fails + 1))
  fi
done <<EOF
$map
EOF

if [ "$fails" -ne 0 ]; then
  echo "pinmap: $fails mismatch(es)"
fi
exit $((fails > 0))
