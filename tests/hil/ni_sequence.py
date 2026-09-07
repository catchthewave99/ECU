#!/usr/bin/env python3
"""Stage 2 HIL sequence for the ECU bench (NI cDAQ-9173 + NI-9401 + NI-9263).

The HIL twin of tests/sil/run_sil_suite.sh: same firmware, same acceptance
limits from tests/limits.env, but the crank stimulus comes from the NI-9401, the
sensor voltages from the NI-9263, and the injector pulse is measured by a
chassis counter instead of by the emulator.

Channel assignment and the reasoning behind it: docs/05-stage2-hil-ni-plan.md.

    python3 tests/hil/ni_sequence.py --list                 # enumerate devices
    python3 tests/hil/ni_sequence.py --case TC-HIL-03       # one case
    python3 tests/hil/ni_sequence.py --all                  # full sequence

NOT YET EXECUTED. Run it on the bench machine, or against an NI gRPC Device
Server there: the DAQmx driver installs on a cloud VM but its kernel modules do
not load, so not even simulated devices work (docs/05 section 5). This file has
only been checked for syntax and limit parsing. Dry-run it on the rig one case
at a time (Step 7 in docs/01-plan-gaps-and-pitfalls.md) before trusting a number
it prints.

Safety: the NI-9263 is a +/-10 V module and an Uno analog pin is rated to
Vcc + 0.5 V. Every commanded voltage goes through clamp_volts(), and the
hardware clamp of note N3 in docs/08-wiring-chart.md is fitted as well. Do not
remove either.
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIMITS = ROOT / "tests" / "limits.env"

# --- rig configuration -----------------------------------------------------
# Verify with --list before a run; slots and device names come from NI MAX.
DIO = "cDAQ1Mod1"       # NI-9401, lines 0-3 out, lines 4-7 in
AO = "cDAQ1Mod2"        # NI-9263
CTR = "cDAQ1/_ctr0"     # chassis counter, input routed from DIO line 4

CRANK_TDC_LINE = f"{DIO}/port0/line0"
CRANK_PRETDC_LINE = f"{DIO}/port0/line1"
INJECTOR_PFI = f"/{DIO}/port0/line4"
DI_LINES = f"{DIO}/port0/line5:7"       # overspeed, engine running, cranking

AO_CHANNELS = {         # signal -> (channel, Uno pin)
    "cht": (f"{AO}/ao0", "A1"),
    "lambda": (f"{AO}/ao1", "A3"),
    "mat": (f"{AO}/ao2", "A0"),
    "throttle": (f"{AO}/ao3", "A5"),
}

AO_MIN_V = 0.0
AO_MAX_V = 5.0          # never raise this: it is the Uno's absolute limit
CRANK_DO_RATE_HZ = 100_000   # hardware-timed DO sample clock, 10 us resolution
CRANK_PULSE_US = 250
DEFAULTS_MV = {"mat": 3400, "cht": 2500, "lambda": 2295, "throttle": 512}


def load_limits(path: Path = LIMITS) -> dict[str, float]:
    """Read tests/limits.env. The SIL suite sources the same file (rule 1)."""
    limits: dict[str, float] = {}
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        limits[key.strip()] = float(value.strip())
    return limits


def clamp_volts(volts: float) -> float:
    """Hard limit on anything commanded to the NI-9263. See module docstring."""
    if volts < AO_MIN_V or volts > AO_MAX_V:
        print(f"  WARN  {volts:.3f} V clamped to the {AO_MIN_V}-{AO_MAX_V} V window")
    return min(max(volts, AO_MIN_V), AO_MAX_V)


def crank_waveform(rpm: int, advance_deg: int) -> list[list[bool]]:
    """One revolution of the two crank lines, active low, for a buffered DO task.

    Returns [tdc_samples, pretdc_samples] at CRANK_DO_RATE_HZ. Mirrors the
    firmware's own generator in uno_baseline/Bench.ino so the SIL and HIL
    stimuli have the same shape (DEV-004).
    """
    samples_per_rev = round(CRANK_DO_RATE_HZ * 60 / rpm)
    pulse = max(1, round(CRANK_PULSE_US * CRANK_DO_RATE_HZ / 1_000_000))
    pretdc_at = samples_per_rev - round(samples_per_rev * advance_deg / 360)

    tdc = [True] * samples_per_rev
    pretdc = [True] * samples_per_rev
    for i in range(pulse):
        tdc[i % samples_per_rev] = False
        pretdc[(pretdc_at + i) % samples_per_rev] = False
    return [tdc, pretdc]


@dataclass
class Result:
    case: str
    checks: list[tuple[str, bool, str]] = field(default_factory=list)

    def check_range(self, desc: str, value: float, low: float, high: float) -> None:
        ok = low <= value <= high
        self.checks.append((desc, ok, f"{value:.2f} in [{low:g}, {high:g}]"))

    def check_eq(self, desc: str, value, want) -> None:
        ok = value == want
        self.checks.append((desc, ok, f"got {value}, want {want}"))

    def report(self) -> bool:
        print(f"== {self.case} ==")
        for desc, ok, detail in self.checks:
            print(f"  {'PASS' if ok else 'FAIL'}  {self.case} {desc} ({detail})")
        return all(ok for _, ok, _ in self.checks)


class Rig:
    """Thin wrapper over the three DAQmx tasks the sequence needs.

    Kept in one place so the bring-up order is explicit: AO to a safe level
    first, then DI, then the counter, then the crank stimulus last.
    """

    def __init__(self) -> None:
        import nidaqmx  # imported here so --list can report a missing driver
        from nidaqmx.constants import AcquisitionType, Edge, LineGrouping

        self._nidaqmx = nidaqmx
        self._const = (AcquisitionType, Edge, LineGrouping)
        self.ao = nidaqmx.Task("ao")
        self.di = nidaqmx.Task("di")
        self.ctr = nidaqmx.Task("ctr")
        self.do = nidaqmx.Task("do")

        for chan, _pin in AO_CHANNELS.values():
            self.ao.ao_channels.add_ao_voltage_chan(chan, min_val=-10.0, max_val=10.0)
        self.di.di_channels.add_di_chan(DI_LINES, line_grouping=LineGrouping.CHAN_PER_LINE)
        # Two-edge separation would give phase directly; pulse width is the
        # primary measurement, so start there (docs/05 section 3).
        self.ctr.ci_channels.add_ci_pulse_width_chan(
            CTR, min_val=100e-6, max_val=50e-3
        ).ci_pulse_width_term = INJECTOR_PFI
        self.ctr.timing.cfg_implicit_timing(
            sample_mode=AcquisitionType.CONTINUOUS, samps_per_chan=1000
        )
        self.do.do_channels.add_do_chan(
            f"{CRANK_TDC_LINE},{CRANK_PRETDC_LINE}",
            line_grouping=LineGrouping.CHAN_PER_LINE,
        )

        self.set_sensors(DEFAULTS_MV)
        self.di.start()
        self.ctr.start()

    def set_sensors(self, millivolts: dict[str, int]) -> None:
        order = list(AO_CHANNELS)
        volts = [clamp_volts(millivolts.get(sig, DEFAULTS_MV.get(sig, 0)) / 1000.0)
                 for sig in order]
        self.ao.write(volts, auto_start=True)

    def crank(self, rpm: int, advance_deg: int = 30) -> None:
        AcquisitionType = self._const[0]
        self.do.stop()
        if rpm == 0:
            self.do.timing.samp_timing_type = self._nidaqmx.constants.SampleTimingType.ON_DEMAND
            self.do.write([True, True], auto_start=True)
            return
        samples = crank_waveform(rpm, advance_deg)
        self.do.timing.cfg_samp_clk_timing(
            rate=CRANK_DO_RATE_HZ,
            sample_mode=AcquisitionType.CONTINUOUS,
            samps_per_chan=len(samples[0]),
        )
        self.do.write(samples, auto_start=True)

    def pulse_widths_us(self, seconds: float) -> list[float]:
        self.ctr.in_stream.offset = 0
        time.sleep(seconds)
        try:
            raw = self.ctr.read(number_of_samples_per_channel=-1)
        except Exception as exc:  # noqa: BLE001 - report and let the case fail
            print(f"  WARN  counter read failed: {exc}")
            return []
        return [w * 1e6 for w in raw]

    def discretes(self) -> dict[str, bool]:
        overspeed, running, cranking = self.di.read()
        return {"overspeed": overspeed, "running": running, "cranking": cranking}

    def close(self) -> None:
        self.crank(0)
        self.set_sensors({k: 0 for k in AO_CHANNELS})
        for task in (self.do, self.ctr, self.di, self.ao):
            try:
                task.stop()
                task.close()
            except Exception:  # noqa: BLE001 - closing must not mask a failure
                pass


def median(values: list[float]) -> float:
    return statistics.median(values) if values else float("nan")


# --- cases -----------------------------------------------------------------

def tc_hil_01(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-01")
    rig.crank(0)
    time.sleep(1.0)
    widths = rig.pulse_widths_us(3.0)
    r.check_eq("injector edges with no crank", len(widths), 0)
    d = rig.discretes()
    r.check_eq("engine running", d["running"], False)
    r.check_eq("cranking", d["cranking"], False)
    return r


def tc_hil_02(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-02")
    rig.crank(0)
    time.sleep(1.0)
    rig.crank(1200)
    t0 = time.monotonic()
    cranking_seen = running_at = None
    while time.monotonic() - t0 < lim["STATE_SETTLE_MS"] / 1000 + 1.0:
        d = rig.discretes()
        if cranking_seen is None and d["cranking"]:
            cranking_seen = time.monotonic() - t0
        if d["running"]:
            running_at = time.monotonic() - t0
            break
        time.sleep(0.01)
    r.check_eq("cranking asserted", cranking_seen is not None, True)
    r.check_range("time to running (ms)", (running_at or 9.99) * 1000,
                  0, lim["STATE_SETTLE_MS"])
    return r


def tc_hil_03(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-03")
    rig.crank(2000)
    for mv, lo, hi in ((4000, lim["PULSE_81C_MIN"], lim["PULSE_81C_MAX"]),
                       (3300, lim["PULSE_22C_MIN"], lim["PULSE_22C_MAX"])):
        rig.set_sensors({**DEFAULTS_MV, "cht": mv})
        time.sleep(2.0)                      # settle the filter and the RC
        widths = rig.pulse_widths_us(3.0)
        r.check_range(f"pulse at {mv} mV", median(widths), lo, hi)
        if len(widths) >= 2:
            print(f"  INFO  n={len(widths)} sd={statistics.stdev(widths):.2f} us")
    return r


def tc_hil_04(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-04")
    rig.set_sensors({**DEFAULTS_MV, "cht": 4000})
    for rpm in (800, 2000, 4400):
        rig.crank(rpm)
        time.sleep(3.0)
        widths = rig.pulse_widths_us(3.0)
        expected_period_us = 60e6 / rpm
        observed = len(widths) / 3.0 * 60 if widths else 0
        tol = lim["ECU_SPEED_TOL_PCT"] / 100
        r.check_range(f"injection rate at {rpm} rpm", observed,
                      rpm * (1 - tol), rpm * (1 + tol))
        print(f"  INFO  {rpm} rpm: expected period {expected_period_us:.0f} us")
    return r


def tc_hil_06(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-06")
    rig.crank(2000)
    time.sleep(2.0)
    rig.crank(6000)
    t0 = time.monotonic()
    latency = None
    while time.monotonic() - t0 < 5.0:
        if rig.discretes()["overspeed"]:
            latency = (time.monotonic() - t0) * 1000
            break
        time.sleep(0.005)
    r.check_eq("overspeed asserted", latency is not None, True)
    # O-REQ-1: no requirement for the latency yet, so record it, do not judge it.
    print(f"  INFO  overspeed latency {latency if latency else float('nan'):.0f} ms"
          " (recorded against O-REQ-1, DEF-018)")
    return r


def tc_hil_07(rig: Rig, lim: dict[str, float]) -> Result:
    r = Result("TC-HIL-07")
    rig.set_sensors({**DEFAULTS_MV, "cht": 4000})
    rig.crank(2000)
    time.sleep(2.0)
    widths = rig.pulse_widths_us(6.0)
    r.check_range("max pulse width", max(widths, default=float("nan")),
                  0, lim["PULSE_ABS_MAX_US"])
    r.check_range("pulses observed", len(widths),
                  lim["PULSE_COUNT_MIN"], lim["PULSE_COUNT_MAX"])
    return r


def tc_hil_10(rig: Rig, lim: dict[str, float]) -> Result:
    """CHT sweep: reproduces the whole baseInt table. No SIL twin."""
    r = Result("TC-HIL-10")
    rig.crank(2000)
    table = []
    for mv in range(500, 4600, 100):
        rig.set_sensors({**DEFAULTS_MV, "cht": mv})
        time.sleep(1.0)
        table.append((mv, median(rig.pulse_widths_us(1.0))))
    for mv, width in table:
        print(f"  DATA  cht_mv={mv} pulse_us={width:.2f}")
    monotonic = all(b[1] <= a[1] + 1 for a, b in zip(table, table[1:]))
    r.check_eq("pulse width decreases with temperature", monotonic, True)
    return r


CASES = {
    "TC-HIL-01": tc_hil_01,
    "TC-HIL-02": tc_hil_02,
    "TC-HIL-03": tc_hil_03,
    "TC-HIL-04": tc_hil_04,
    "TC-HIL-06": tc_hil_06,
    "TC-HIL-07": tc_hil_07,
    "TC-HIL-10": tc_hil_10,
}
# Still to implement: TC-HIL-05 (needs two-edge separation against the crank
# DO edge), TC-HIL-08, TC-HIL-11 to TC-HIL-16. See docs/05 section 4.


def list_devices() -> int:
    try:
        import nidaqmx
    except Exception as exc:  # noqa: BLE001
        print(f"nidaqmx unavailable: {exc}")
        print("Install NI-DAQmx and `pip install nidaqmx` on the rig machine.")
        return 1
    system = nidaqmx.system.System.local()
    print(f"DAQmx driver {system.driver_version}")
    for dev in system.devices:
        print(f"  {dev.name:12s} {dev.product_type}")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--list", action="store_true", help="enumerate NI devices and exit")
    ap.add_argument("--case", action="append", help="case id, repeatable")
    ap.add_argument("--all", action="store_true", help="run every implemented case")
    ap.add_argument("--check-limits", action="store_true",
                    help="parse tests/limits.env and exit (no hardware needed)")
    args = ap.parse_args(argv)

    if args.list:
        return list_devices()

    limits = load_limits()
    if args.check_limits:
        for key in sorted(limits):
            print(f"{key}={limits[key]:g}")
        return 0

    selected = list(CASES) if args.all else (args.case or [])
    if not selected:
        ap.error("give --case, --all, --list or --check-limits")
    unknown = [c for c in selected if c not in CASES]
    if unknown:
        ap.error(f"unknown case(s): {', '.join(unknown)}")

    rig = Rig()
    ok = True
    try:
        for case in selected:
            ok &= CASES[case](rig, limits).report()
            print()
    finally:
        rig.close()

    print("HIL sequence: all checks passed" if ok else "HIL sequence: failures above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
