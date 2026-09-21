# 08 - Change control

The failure mode this bench exists to demonstrate a fix for: an agent - or a
tired engineer - makes a failing safety test pass by widening a tolerance, and
six months later nobody can say what the ECU is actually required to do. Five
rules prevent it.

## Rule 1 - one place for every number

Acceptance limits live only in `tests/limits.env`. The SIL suite sources it; the
NI sequence reads it. No test contains a literal tolerance, and no document
restates one in prose - documents name the variable.

A limit change is therefore always a one-file, reviewable diff.

## Rule 2 - no limit changes without a defect row

Changing a value in `tests/limits.env` requires, in the same commit:

1. a row in `docs/06-defect-log.md` with the evidence (`SIL` or `HIL`) and the
   measurement that motivated it, and
2. the old and new values in the commit message.

`HAZARD_RESPONSE_MAX_MS` is the one number that is not negotiable at all. If a
build cannot meet it, the build changes.

## Rule 3 - the engineer approves safety-critical changes

An agent may diagnose a safety failure, write the fix and re-run the suite. A
human reviews and approves any change that touches:

* `tests/limits.env`
* anything in `consolidated/` that affects hazard timing
* the requirements in `docs/02-requirements.md`

Green tests are evidence for that review, not a substitute for it.

## Rule 4 - configuration identity in every record

A test result is meaningless without the artefact that produced it. Every record
names:

| Field | Source |
| --- | --- |
| firmware commit | `git rev-parse --short HEAD` |
| build figures | `build/sil-logs/build-consolidated.txt` flash/SRAM lines |
| limits commit | same repo, so the same sha unless limits were changed separately |
| wiring groups fitted | `docs/07-wiring-chart.md` A/B/C |
| rig configuration | NI module slots and channel list |
| 5 V rail voltage, ambient | measured, per `docs/03-test-plan.md` |

## Rule 5 - the reference firmware is not edited

`reference/` holds the engine firmware this repository started as, plus its
analysis documents. It is kept for provenance and is not part of the
experiment. Nothing in `reference/` is built or tested by the suite.

## Change types and what each requires

| Change | Requires |
| --- | --- |
| Firmware behaviour | requirement or defect row, SIL suite re-run, traceability row updated |
| Pin assignment | `consolidated/ecu.h` and `docs/07-wiring-chart.md` section 7 in one commit; `tests/check_pinmap.sh` enforces it |
| New test case | the requirement it verifies, the limit variable, a row in `docs/05-traceability.md`, and both a SIL and a HIL twin where physically possible |
| Limit value | Rule 2, plus Rule 3 if it is a safety limit |
| New requirement | add it to `docs/02-requirements.md` and add the case |
| Wiring | the group table plus a re-run of that group's bench cases |
| Rig change (module, slot, channel) | `docs/04-hil-plan.md` and a re-run of the affected HIL cases |

## Review checklist

Before merging anything that touches firmware, limits or wiring:

- [ ] `tests/sil/run_sil_suite.sh` passes, and the number of checks has not
      silently gone down
- [ ] `grep -rn "delay(" consolidated/` finds nothing outside the DEF-102 block
- [ ] every changed limit has a defect row and a named approver
- [ ] `docs/05-traceability.md` still has a case for every requirement
- [ ] the commit message says what was measured, not just what was changed
