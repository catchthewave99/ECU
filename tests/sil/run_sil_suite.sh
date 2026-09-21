#!/usr/bin/env bash
# SIL suite: runs the real compiled firmware inside simavr and checks it against
# tests/limits.env.
#
# The same cases run against a single function on its own and against the
# consolidated board. Nothing in this file knows which build it is testing
# beyond the .elf it is handed, and no limit is written here - that is what
# makes "the consolidated firmware passes the unchanged tests" a meaningful
# claim.
#
# Usage:
#   tests/sil/run_sil_suite.sh                # standalone + consolidated
#   tests/sil/run_sil_suite.sh --defects      # also run the defective build
#                                             # and show which tests it fails
#
# Exit code 0 means every test passed.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# shellcheck disable=SC1091
source tests/limits.env

LOGS="$ROOT/build/sil-logs"
RUNNER="$ROOT/tests/sil/sil_runner"
FQBN="arduino:avr:uno"
WITH_DEFECTS=0
[ "${1:-}" = "--defects" ] && WITH_DEFECTS=1

pass=0; fail=0
current_log=""

say()  { printf '%s\n' "$*"; }
head2() { printf '\n--- %s\n' "$*"; }

check() { # check <description> <actual> <op> <expected>
  local what="$1" got="$2" op="$3" want="$4" ok
  ok="$(awk -v a="$got" -v b="$want" -v op="$op" 'BEGIN{
    if (op=="<=") print (a+0 <= b+0);
    else if (op==">=") print (a+0 >= b+0);
    else if (op=="==") print (a+0 == b+0);
    else if (op=="!=") print (a+0 != b+0);
    else print 0;
  }')"
  if [ "$ok" = "1" ]; then
    printf '  PASS  %-46s %s %s %s\n' "$what" "$got" "$op" "$want"
    pass=$((pass + 1))
  else
    printf '  FAIL  %-46s %s %s %s   (%s)\n' "$what" "$got" "$op" "$want" "${current_log#"$ROOT"/}"
    fail=$((fail + 1))
  fi
}

between() { # between <description> <actual> <min> <max>
  check "$1 >= $3" "$2" ">=" "$3"
  check "$1 <= $4" "$2" "<=" "$4"
}

run() { # run <case-name> <elf> [runner args...]
  local name="$1" elf="$2"; shift 2
  current_log="$LOGS/$name.txt"
  "$RUNNER" "$elf" "$@" >"$current_log" 2>&1
}

level()  { awk -v p="$1" '$1==p {sub(/.*level=/,"",$2); print $2+0}' "$current_log" | tail -n1; }
rises()  { awk -v p="$1" '$1==p {for(i=1;i<=NF;i++) if($i ~ /^rises=/){sub(/rises=/,"",$i); print $i+0}}' "$current_log" | tail -n1; }
stat()   { awk -v l="$1" -v f="$2" '$1==l {for(i=1;i<=NF;i++) if($i ~ "^"f"="){sub(f"=","",$i); print $i+0}}' "$current_log" | tail -n1; }
loopmax(){ awk '$1=="loop_ms" {for(i=1;i<=NF;i++) if($i ~ /^max=/){sub(/max=/,"",$i); print $i+0}}' "$current_log" | tail -n1; }
# Last telemetry line, column 2, is the temperature the firmware reported.
reported_c() { awk -F'\t' '$1 ~ /^[0-9]+$/ && NF>=2 {print $2}' "$current_log" | tail -n1; }

# ---------------------------------------------------------------- build ----
mkdir -p "$LOGS"
make -s -C tests/sil || exit 2

build() { # build <sketch-dir> <output-name> [extra -D flags]
  local sketch="$1" out="$2" flags="${3:-}"
  local args=(compile -b "$FQBN" --output-dir "build/$out" --quiet)
  [ -n "$flags" ] && args+=(--build-property "compiler.cpp.extra_flags=$flags")
  arduino-cli "${args[@]}" "$sketch" >"$LOGS/build-$out.txt" 2>&1 || {
    say "BUILD FAILED: $sketch (see ${LOGS#"$ROOT"/}/build-$out.txt)"
    exit 2
  }
  say "built $sketch -> build/$out"
}

say "=== build ==="
build functions/thermal thermal
build functions/body    body
build functions/hazard  hazard
build consolidated      consolidated
if [ "$WITH_DEFECTS" = "1" ]; then
  build consolidated consolidated_defect "-DDEFECT_SHARED_PIN=1 -DDEFECT_BLOCKING_SENSOR=1"
fi

THERMAL_ELF="build/thermal/thermal.ino.elf"
BODY_ELF="build/body/body.ino.elf"
HAZARD_ELF="build/hazard/hazard.ino.elf"
CONS_ELF="build/consolidated/consolidated.ino.elf"
DEFECT_ELF="build/consolidated_defect/consolidated.ino.elf"

# --------------------------------------------------------------- checks ----
# Each group is written once and called with an .elf, so a standalone function
# and the consolidated board are judged by the same code and the same numbers.

# The headlamp switch is D7 on the consolidated board but D2 on the standalone
# body sketch, which owns the whole board. The runner accepts either the signal
# name or the pin name, so the caller says which line to drive.
thermal_tests() { # thermal_tests <tag> <elf>
  local tag="$1" elf="$2"
  head2 "$tag: thermal control"

  # Not quiet: this case also reads the temperature the firmware printed.
  run "$tag-thermal-cold" "$elf" --ms 1500 --temp-mv 800
  check "$tag cold 80C: fan off"            "$(level fan)"      "==" 0
  check "$tag cold 80C: overtemp lamp off"  "$(level overtemp)" "==" 0
  check "$tag cold 80C: reported temp"      "$(reported_c)" ">=" "$((80 - TEMP_REPORT_TOL_C))"
  check "$tag cold 80C: reported temp"      "$(reported_c)" "<=" "$((80 + TEMP_REPORT_TOL_C))"

  # Drive one degree past each switching point. The ADC quantises to about
  # half a degree, so commanding exactly 95.0 C can land at 94 C in firmware;
  # that is the bench's resolution, not a fault, hence TEMP_STIM_MARGIN_C.
  local hot_mv=$(( (FAN_ON_C + TEMP_STIM_MARGIN_C) * 10 ))
  local over_mv=$(( (OVERTEMP_C + TEMP_STIM_MARGIN_C) * 10 ))

  run "$tag-thermal-hot" "$elf" --ms 1500 --temp-mv "$hot_mv" --quiet
  check "$tag ${FAN_ON_C}C: fan on"          "$(level fan)"      "==" 1
  check "$tag ${FAN_ON_C}C: overtemp lamp off" "$(level overtemp)" "==" 0

  run "$tag-thermal-overtemp" "$elf" --ms 1500 --temp-mv "$over_mv" --quiet
  check "$tag ${OVERTEMP_C}C: fan on"        "$(level fan)"      "==" 1
  check "$tag ${OVERTEMP_C}C: overtemp lamp on" "$(level overtemp)" "==" 1

  # Hysteresis: hot, then cool to just above the off point. The fan must stay
  # on, or it would chatter on and off around the switching temperature.
  run "$tag-thermal-hyst-hold" "$elf" --ms 2500 --temp-mv "$hot_mv" \
      --at 1200 "temp=$(( (FAN_OFF_C + 2) * 10 ))" --quiet
  check "$tag hysteresis: fan stays on above off point" "$(level fan)" "==" 1

  run "$tag-thermal-hyst-off" "$elf" --ms 2500 --temp-mv "$hot_mv" \
      --at 1200 "temp=$(( (FAN_OFF_C - 2) * 10 ))" --quiet
  check "$tag hysteresis: fan off below off point" "$(level fan)" "==" 0
}

body_tests() { # body_tests <tag> <elf> <headlamp-switch-signal>
  local tag="$1" elf="$2" head_sw="$3"
  head2 "$tag: body control"

  run "$tag-body-head-off" "$elf" --ms 1200 --temp-mv 800 --quiet
  check "$tag headlamp switch open: lamp off" "$(level headlamp)" "==" 0

  run "$tag-body-head-on" "$elf" --ms 1200 --temp-mv 800 --at 300 "$head_sw=1" --quiet
  check "$tag headlamp switch closed: lamp on" "$(level headlamp)" "==" 1

  # Left stalk held for two seconds: the left lamp flashes, the right does not.
  run "$tag-body-turn-left" "$elf" --ms 3000 --temp-mv 800 --at 300 turnl=1 --quiet
  check "$tag left stalk: left lamp flashes"  "$(rises lamp_left)"  ">=" 2
  check "$tag left stalk: right lamp dark"    "$(rises lamp_right)" "==" 0
  between "$tag left stalk: flash half period" "$(stat flash_half_ms median)" \
          "$FLASH_HALF_MS_MIN" "$FLASH_HALF_MS_MAX"

  run "$tag-body-turn-right" "$elf" --ms 3000 --temp-mv 800 --at 300 turnr=1 --quiet
  check "$tag right stalk: right lamp flashes" "$(rises lamp_right)" ">=" 2
  check "$tag right stalk: left lamp dark"     "$(rises lamp_left)"  "==" 0
}

hazard_tests() { # hazard_tests <tag> <elf> [extra runner args...]
  local tag="$1" elf="$2"; shift 2
  head2 "$tag: hazard warning (REQ-SAFE-100)"

  run "$tag-hazard" "$elf" --ms $((1000 + HAZARD_PRESS_COUNT * 500 + 500)) \
      --taps "$HAZARD_PRESS_COUNT" --tap-ms 500 --quiet "$@"
  check "$tag hazard: every press answered" "$(stat hazard_response_ms n)" \
        "==" "$HAZARD_PRESS_COUNT"
  check "$tag hazard: slowest response (ms)" "$(stat hazard_response_ms max)" \
        "<=" "$HAZARD_RESPONSE_MAX_MS"
  check "$tag hazard: both lamps flash together" \
        "$(( $(rises lamp_left) - $(rises lamp_right) ))" "==" 0
  check "$tag hazard: slowest pass of loop() (ms)" "$(loopmax)" "<=" "$LOOP_MAX_MS"

  # The stalk must not take a hazard lamp over: a driver who leaves the stalk
  # on and then hits hazard still gets two lamps, not one.
  run "$tag-hazard-vs-stalk" "$elf" --ms 4000 --temp-mv 800 \
      --at 300 turnl=1 --at 600 hazard=1 --quiet "$@"
  check "$tag hazard beats the turn stalk" \
        "$(( $(rises lamp_left) - $(rises lamp_right) ))" "==" 0
  check "$tag hazard beats the turn stalk: right lamp flashing" \
        "$(rises lamp_right)" ">=" 2
}

# ------------------------------------------------- standalone functions ----
say ""
say "=== stage 1: each function on its own board (the known-good baseline) ==="
thermal_tests "thermal" "$THERMAL_ELF"
body_tests    "body"    "$BODY_ELF" d2
hazard_tests  "hazard"  "$HAZARD_ELF"

# --------------------------------------------------------- consolidated ----
say ""
say "=== stage 2: all three functions on one board (same tests, same limits) ==="
thermal_tests "cons" "$CONS_ELF"
body_tests    "cons" "$CONS_ELF" head
hazard_tests  "cons" "$CONS_ELF"

head2 "cons: integration"
# DEF-101 territory: if two functions share an input pin, the hazard switch
# also drives the headlamps.
run "cons-no-crosstalk" "$CONS_ELF" --ms 4000 --temp-mv 800 --taps 4 --tap-ms 500 --quiet
check "cons hazard switch does not touch headlamps" "$(rises headlamp)" "==" 0

# DEF-102 territory: the deadline has to hold while the other two functions are
# doing their work, not only on an idle board.
head2 "cons: hazard deadline with thermal and body busy"
run "cons-hazard-busy" "$CONS_ELF" --ms $((1000 + HAZARD_PRESS_COUNT * 500 + 500)) \
    --temp-mv "$((OVERTEMP_C * 10))" --taps "$HAZARD_PRESS_COUNT" --tap-ms 500 \
    --at 200 head=1 --at 250 turnl=1 --quiet
check "cons busy: every press answered" "$(stat hazard_response_ms n)" "==" "$HAZARD_PRESS_COUNT"
check "cons busy: slowest response (ms)" "$(stat hazard_response_ms max)" "<=" "$HAZARD_RESPONSE_MAX_MS"
check "cons busy: slowest pass of loop() (ms)" "$(loopmax)" "<=" "$LOOP_MAX_MS"

# ------------------------------------------------------------- pin map ----
head2 "pin map vs wiring chart"
if tests/check_pinmap.sh; then
  pass=$((pass + 1))
else
  fail=$((fail + 1))
fi

# ------------------------------------------- the defective build, on demand -
if [ "$WITH_DEFECTS" = "1" ]; then
  say ""
  say "=== stage 3: the defective consolidated build (expected to fail) ==="
  say "    These runs are evidence for docs/06-defect-log.md, not a gate."
  before_fail=$fail
  thermal_tests "defect" "$DEFECT_ELF"
  body_tests    "defect" "$DEFECT_ELF" head
  hazard_tests  "defect" "$DEFECT_ELF"
  run "defect-no-crosstalk" "$DEFECT_ELF" --ms 4000 --temp-mv 800 --taps 4 --tap-ms 500 --quiet
  check "defect hazard switch does not touch headlamps" "$(rises headlamp)" "==" 0
  head2 "defect: pin map vs wiring chart"
  if tests/check_pinmap.sh -DDEFECT_SHARED_PIN=1; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
  fi
  defect_fails=$((fail - before_fail))
  say ""
  say "    defective build failed $defect_fails check(s), as intended"
  # The defective build is a demonstration, so its failures do not count
  # towards the suite result.
  fail=$before_fail
fi

say ""
say "=== summary: $pass passed, $fail failed ==="
exit $((fail > 0))
