# 09 - Change control

The failure mode this bench exists to demonstrate a fix for: moving from SIL to
HIL, someone widens a tolerance or edits a unit test so the run goes green, and
six months later nobody can say what the ECU is actually required to do. Four
rules prevent it.

## Rule 1 - one place for every number

Acceptance limits live only in `tests/limits.env`. The SIL suite sources it; the
NI sequence reads it. No test contains a literal tolerance, and no document
restates one in prose - documents name the variable.

A limit change is therefore always a one-file, reviewable diff.

## Rule 2 - no limit changes without a defect row

Changing a value in `tests/limits.env` requires, in the same commit:

1. a row in `docs/07-defect-log.md` with the evidence (`SIL` or `HIL`) and the
   measurement that motivated it, and
2. the old and new values in the commit message.

If the honest reason is "HIL measured something else", that is a finding about
the design or the uncertainty budget, and it gets written down as one. Widening a
band to pass is only legitimate when the budget in
`docs/05-stage2-hil-ni-plan.md` section 6 explains the width.

## Rule 3 - configuration identity in every record

A test result is meaningless without the artefact that produced it. Every record
names:

| Field | Source |
| --- | --- |
| firmware commit | `git rev-parse --short HEAD` |
| build figures | `build/sil-results/compile.log` flash/SRAM lines |
| limits commit | same repo, so the same sha unless limits were changed separately |
| wiring groups fitted | `docs/08-wiring-chart.md` A/B/C/D |
| rig configuration | NI module slots and channel list |
| 5 V rail voltage, ambient | measured, per `docs/04-stage1-vv-plan.md` section 5 |

## Rule 4 - the reference firmware is not edited

The root `ECU_*.ino` sketch stays as it was received. Findings about it become
DEF rows; fixes happen in `uno_baseline/`. That is what makes it possible to say
which behaviour came from the original design and which from the bench port
(DEV rows).

If the original firmware is ever intended to be maintained here rather than
referenced, do it as a deliberate, separate change: port the DEF fixes into it,
and delete the reference/bench distinction from these documents in the same
commit.

## Change types and what each requires

| Change | Requires |
| --- | --- |
| Firmware behaviour | DEF or requirement update, SIL suite re-run, traceability row updated |
| Pin assignment | `uno_baseline` constants and `docs/08-wiring-chart.md` section 8 in one commit; `tests/check_pinmap.sh` enforces it (TC-SIL-00) |
| New test case | requirement it verifies, limit variable, row in `docs/06-traceability-matrix.md`, and both a SIL and a HIL twin where physically possible |
| Limit value | Rule 2 |
| New requirement | remove the matching O-REQ gap in `docs/03-requirements.md`, add the case |
| Wiring | the group table plus a re-run of that group's bench cases |
| Rig change (module, slot, channel) | `docs/05-stage2-hil-ni-plan.md` sections 1, 2 and 8, and a re-run of the affected HIL cases |

## Review checklist

Before merging anything that touches firmware, limits or wiring:

- [ ] `tests/sil/run_sil_suite.sh` passes, and the number of checks has not
      silently gone down
- [ ] `tests/check_pinmap.sh` passes
- [ ] every changed limit has a DEF row
- [ ] `docs/06-traceability-matrix.md` still has a case for every requirement it
      claims to verify
- [ ] any new O-REQ gap is written down rather than left implicit
- [ ] the commit message says what was measured, not just what was changed
