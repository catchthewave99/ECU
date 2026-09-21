#!/usr/bin/env bash
# Stage 1 SIL regression suite for uno_baseline.
#
# Builds the Uno firmware, runs it inside simavr through tests/sil/sil_runner and
# asserts the acceptance criteria in docs/04-stage1-vv-plan.md. Every case here has
# a HIL twin executed with the NI hardware in Stage 2 (docs/05-stage2-hil-ni-plan.md),
# so the same numbers can be compared across the SIL/HIL boundary.
#
# Usage: tests/sil/run_sil_suite.sh [path-to-arduino-cli]

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CLI="${1:-arduino-cli}"
# All tolerance bands live in tests/limits.env so a HIL finding updates one file
# and both stages inherit it (docs/09-change-control.md).
# shellcheck source=../limits.env
. "$ROOT/tests/limits.env"

BUILD="$ROOT/build/uno_baseline"
ELF="$BUILD/uno_baseline.ino.elf"
RESULTS="$ROOT/build/sil-results"

fails=0
pass() { printf '  PASS  %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

check_range() { # id description value low high
  local id="$1" desc="$2" val="$3" low="$4" high="$5"
  if awk -v v="$val" -v l="$low" -v h="$high" 'BEGIN{exit !(v>=l && v<=h)}'; then
    pass "$id $desc ($val in [$low, $high])"
  else
    fail "$id $desc ($val outside [$low, $high])"
  fi
}

check_eq() { # id description value expected
  local id="$1" desc="$2" val="$3" want="$4"
  if [ "$val" = "$want" ]; then
    pass "$id $desc ($val)"
  else
    fail "$id $desc (got $val, want $want)"
  fi
}

echo "== build =="
mkdir -p "$BUILD" "$RESULTS"
"$CLI" compile -b arduino:avr:uno --output-dir "$BUILD" "$ROOT/uno_baseline" >"$RESULTS/compile.log" 2>&1 ||
  { echo "firmware build failed, see $RESULTS/compile.log"; exit 1; }
grep -E "Sketch uses|Global variables" "$RESULTS/compile.log" || true
make -C "$HERE" >/dev/null || { echo "harness build failed"; exit 1; }

run_case() { # name  extra args...
  local name="$1"; shift
  "$HERE/sil_runner" "$ELF" "$@" >"$RESULTS/$name.log" 2>&1
  cat "$RESULTS/$name.log" | grep -E "^(tdc_period_us|inj_pulse_us|inj_to_tdc_us|crank_speed|inj_pulses|safety_pin_d9)" || true
}

stat_field() { # log  row-label  key(min|mean|median|max|n|asserts|level)
  awk -v lbl="$2" -v key="$3" '$1==lbl { for (i=2;i<=NF;i++) if (index($i, key"=")==1) { split($i,a,"="); print a[2] } }' "$RESULTS/$1.log"
}

value_field() { # log  row-label -> second column
  awk -v lbl="$2" '$1==lbl { print $2 }' "$RESULTS/$1.log"
}

last_hmi() { # log  column-index (1=t 2=MAT 3=CHT 4=MAP 5=AFR 6=TPS 7=RPM 8=pulse 9=corr 10=state)
  awk -v c="$2" '/^[0-9]/ && NF==10 { v=$c } END { print v+0 }' "$RESULTS/$1.log"
}

echo
echo "== TC-SIL-00  documented pin map matches the firmware =="
"$ROOT/tests/check_pinmap.sh" >"$RESULTS/pinmap.log" 2>&1
if [ $? -eq 0 ]; then
  pass "TC-SIL-00 pin map ($(grep -c PASS "$RESULTS/pinmap.log") pins)"
else
  grep FAIL "$RESULTS/pinmap.log" || true
  fail "TC-SIL-00 pin map (see $RESULTS/pinmap.log)"
fi

echo
echo "== TC-SIL-01  engine stopped: no crank, no injection, state STOP =="
run_case stopped --rpm 0 --ms 3000
check_eq  TC-SIL-01 "injection pulses"      "$(value_field stopped inj_pulses)" "0"
check_eq  TC-SIL-01 "reported state"        "$(last_hmi stopped 10)" "0"
check_eq  TC-SIL-01 "reported rpm"          "$(last_hmi stopped 7)" "0"

echo
echo "== TC-SIL-02  crank at 1200 rpm: STARTING then IDLE within 2 s =="
run_case idle1200 --rpm 1200 --ms 4000 --cht-mv 4000
check_eq  TC-SIL-02 "reported state"        "$(last_hmi idle1200 10)" "1"
check_range TC-SIL-02 "reported rpm"        "$(last_hmi idle1200 7)" "$RPM_1200_MIN" "$RPM_1200_MAX"

echo
echo "== TC-SIL-03  base pulse follows the CHT look-up table =="
# 4.00 V -> 81 degC -> baseInt[80 degC] = 2800 us
run_case lut2800 --rpm 2000 --ms 6000 --cht-mv 4000
check_range TC-SIL-03 "pulse at 81 degC"    "$(stat_field lut2800 inj_pulse_us median)" "$PULSE_81C_MIN" "$PULSE_81C_MAX"
# 3.30 V -> 22 degC -> baseInt[20 degC] = 3400 us
run_case lut3400 --rpm 2000 --ms 6000 --cht-mv 3300
check_range TC-SIL-03 "pulse at 22 degC"    "$(stat_field lut3400 inj_pulse_us median)" "$PULSE_22C_MIN" "$PULSE_22C_MAX"

echo
echo "== TC-SIL-04  measured speed matches the crank stimulus =="
for r in 800 2000 4400; do
  run_case "speed$r" --rpm "$r" --ms 8000 --cht-mv 4000
  m=$(value_field "speed$r" crank_speed)
  lo=$(awk -v r="$r" -v t="$STIMULUS_SPEED_TOL_PCT" 'BEGIN{print r*(1-t/100)}')
  hi=$(awk -v r="$r" -v t="$STIMULUS_SPEED_TOL_PCT" 'BEGIN{print r*(1+t/100)}')
  check_range TC-SIL-04 "stimulus $r rpm"   "$m" "$lo" "$hi"
  lo=$(awk -v r="$r" -v t="$ECU_SPEED_TOL_PCT" 'BEGIN{print r*(1-t/100)}')
  hi=$(awk -v r="$r" -v t="$ECU_SPEED_TOL_PCT" 'BEGIN{print r*(1+t/100)}')
  check_range TC-SIL-04 "ECU rpm at $r"     "$(last_hmi "speed$r" 7)" "$lo" "$hi"
done

echo
echo "== TC-SIL-05  injection phasing equals the commanded advance =="
# 30 deg before TDC at 2000 rpm = 2500 us; allowance covers the 125 us crank tick
run_case phase --rpm 2000 --ms 6000 --cht-mv 4000
check_range TC-SIL-05 "injector rise to TDC" "$(stat_field phase inj_to_tdc_us median)" "$PHASE_2000RPM_MIN" "$PHASE_2000RPM_MAX"

echo
echo "== TC-SIL-06  overspeed safety output asserts above 4000 rpm =="
run_case overspeed --rpm 6000 --ms 8000 --cht-mv 4000
check_eq  TC-SIL-06 "D9 level"              "$(stat_field overspeed safety_pin_d9 level)" "1"

echo
echo "== TC-SIL-07  no injection is scheduled with an invalid duration (DEF-004) =="
run_case guard --rpm 2000 --ms 6000 --cht-mv 4000
pulses=$(stat_field guard inj_pulse_us n)
maxw=$(stat_field guard inj_pulse_us max)
check_range TC-SIL-07 "max pulse width"     "$maxw" 0 "$PULSE_ABS_MAX_US"
check_range TC-SIL-07 "pulses observed"     "$pulses" "$PULSE_COUNT_MIN" "$PULSE_COUNT_MAX"

echo
echo "== TC-SIL-08  start enrichment applies while STARTING (DEF-008) =="
# Window ends before the state machine reaches IDLE, so every pulse is enriched:
# baseInt[80 degC] 2800 us x 1.25 = 3500 us
run_case enrich --rpm 2000 --ms 1200 --cht-mv 4000
check_range TC-SIL-08 "pulse while STARTING" "$(stat_field enrich inj_pulse_us median)" "$PULSE_ENRICH_MIN" "$PULSE_ENRICH_MAX"
check_eq  TC-SIL-08 "reported state"         "$(last_hmi enrich 10)" "2"

echo
if [ "$fails" -eq 0 ]; then
  echo "SIL suite: all checks passed (logs in $RESULTS)"
else
  echo "SIL suite: $fails check(s) failed (logs in $RESULTS)"
fi
exit $((fails > 0))
