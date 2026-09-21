#!/usr/bin/env python3
"""HIL sequence for the Uno ECU bench (NI cDAQ-9173 + NI-9401 + NI-9263).

The HIL twin of tests/sil/run_sil_suite.sh: same firmware, same cases, same
acceptance limits from tests/limits.env. The switches are driven by the NI-9401
instead of by the emulator, the temperature sensor is replaced by the NI-9263,
and the lamps are read back on the NI-9401.

    python3 tests/hil/ni_sequence.py --list           # enumerate NI devices
    python3 tests/hil/ni_sequence.py --check-limits   # parse limits, no hardware
    python3 tests/hil/ni_sequence.py --case TC-HIL-03 # one case
    python3 tests/hil/ni_sequence.py --all            # full sequence

Channel plan and reasoning: docs/04-hil-plan.md.

NOT YET EXECUTED. Run it on the bench machine, or against an NI gRPC Device
Server there: the DAQmx driver installs on a cloud VM but its kernel modules do
not load, so not even simulated devices work. This file has been checked for
syntax and limit parsing only. Bring it up one case at a time, starting with
--list, then TC-HIL-01, before trusting a number it prints.

Safety: the NI-9263 is a +/-10 V module and an Uno analog pin is rated to
Vcc + 0.5 V. Every commanded voltage goes through clamp_volts(), and the
hardware clamp of note N3 in docs/07-wiring-chart.md is fitted as well. Do not
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
DIO = "cDAQ1Mod1"        # NI-9401: lines 0-3 out (switches), 4-7 in (lamps)
AO = "cDAQ1Mod2"         # NI-9263
CHASSIS = "cDAQ1"

# The NI-9401 sets direction per nibble, not per line, which is why the four
# switches are one nibble and the four observed outputs are the other.
SWITCH_LINES = ["hazard", "turnl", "turnr", "head"]      # lines 0-3, Uno D2/D4/D5/D7
LAMP_LINES = ["lamp_left", "lamp_right", "fan", "overtemp"]  # lines 4-7, D8/D9/D11/D12
DO_CHANS = f"{DIO}/port0/line0:3"
DI_CHANS = f"{DIO}/port0/line4:7"

TEMP_AO = f"{AO}/ao0"    # Uno A0, 10 mV per degree C
AO_MIN_V = 0.0
AO_MAX_V = 5.0           # never raise this: it is the Uno's absolute limit

# Hardware-timed switch/lamp measurement. 10 kHz gives 0.1 ms resolution on a
# 100 ms requirement, which is the margin REQ-SAFE-100 deserves.
CLOCK_HZ = 10_000
DO_SAMPLE_CLOCK = f"/{CHASSIS}/do/SampleClock"
DO_START_TRIGGER = f"/{CHASSIS}/do/StartTrigger"

# The headlamp output (D10) and the heartbeat (D13) do not fit in the eight
# NI-9401 lines, so they are observed in the telemetry line over USB serial.
SERIAL_BAUD = 115200
TELEMETRY_COLUMNS = ["t_ms", "coolantC", "fan", "overtemp",
                     "head", "turnL", "turnR", "hazard", "loop_max_ms"]


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


def temp_volts(deg_c: float) -> float:
    """The sensor the firmware expects: 10 mV per degree C."""
    return clamp_volts(deg_c / 100.0)


@dataclass
class Result:
    case: str
    checks: list[tuple[str, bool, str]] = field(default_factory=list)

    def check_max(self, desc: str, value: float, high: float) -> None:
        self.checks.append((desc, value <= high, f"{value:.3f} <= {high:g}"))

    def check_range(self, desc: str, value: float, low: float, high: float) -> None:
        self.checks.append((desc, low <= value <= high,
                            f"{value:.3f} in [{low:g}, {high:g}]"))

    def check_eq(self, desc: str, value, want) -> None:
        self.checks.append((desc, value == want, f"got {value}, want {want}"))

    def report(self) -> bool:
        print(f"== {self.case} ==")
        for desc, ok, detail in self.checks:
            print(f"  {'PASS' if ok else 'FAIL'}  {self.case} {desc} ({detail})")
        return all(ok for _, ok, _ in self.checks)


class Rig:
    """The three DAQmx tasks and the telemetry port, in one place.

    Bring-up order matters: analog output to a safe level first, then the
    switch outputs to "open", then the lamp inputs.
    """

    def __init__(self, serial_port: str | None = None) -> None:
        import nidaqmx  # imported here so --list can report a missing driver
        from nidaqmx.constants import AcquisitionType, LineGrouping

        self._nidaqmx = nidaqmx
        self._acq = AcquisitionType
        self._grouping = LineGrouping

        self.ao = nidaqmx.Task("ao")
        self.ao.ao_channels.add_ao_voltage_chan(TEMP_AO, min_val=-10.0, max_val=10.0)
        self.set_temp_c(25)

        self.do = nidaqmx.Task("do")
        self.do.do_channels.add_do_chan(DO_CHANS,
                                        line_grouping=LineGrouping.CHAN_PER_LINE)
        self.switches()   # every switch open

        self.di = nidaqmx.Task("di")
        self.di.di_channels.add_di_chan(DI_CHANS,
                                        line_grouping=LineGrouping.CHAN_PER_LINE)
        self.di.start()

        self.serial = None
        if serial_port:
            import serial  # pyserial, only needed for the telemetry checks
            self.serial = serial.Serial(serial_port, SERIAL_BAUD, timeout=1.0)
            time.sleep(2.0)   # opening the port resets the Uno

    # -- stimulus ----------------------------------------------------------
    def set_temp_c(self, deg_c: float) -> None:
        self.ao.write(temp_volts(deg_c), auto_start=True)

    def switches(self, hazard: bool = False, turnl: bool = False,
                 turnr: bool = False, head: bool = False) -> None:
        """Close or open the four switches. HIGH means closed."""
        self.do.write([hazard, turnl, turnr, head], auto_start=True)

    # -- observation -------------------------------------------------------
    def lamps(self) -> dict[str, bool]:
        return dict(zip(LAMP_LINES, self.di.read()))

    def telemetry(self) -> dict[str, int] | None:
        """The most recent telemetry record, or None if serial is not in use."""
        if not self.serial:
            return None
        self.serial.reset_input_buffer()
        for _ in range(20):
            line = self.serial.readline().decode("ascii", "replace").strip()
            fields = line.split("\t")
            if len(fields) == len(TELEMETRY_COLUMNS) and fields[0].isdigit():
                return {k: int(v) for k, v in zip(TELEMETRY_COLUMNS, fields)}
        return None

    def watch_lamp(self, signal: str, seconds: float) -> list[float]:
        """Times, in ms from the start, of every rising edge of one lamp."""
        index = LAMP_LINES.index(signal)
        edges: list[float] = []
        previous = self.lamps()[signal]
        t0 = time.monotonic()
        while time.monotonic() - t0 < seconds:
            level = self.di.read()[index]
            if level and not previous:
                edges.append((time.monotonic() - t0) * 1000.0)
            previous = level
            time.sleep(0.002)
        return edges

    def hazard_response_ms(self, presses: int, hold_ms: int = 250,
                           gap_ms: int = 250) -> list[float]:
        """Press the hazard switch repeatedly and time each lamp response.

        Both tasks run off the same chassis sample clock and the DI task starts
        on the DO start trigger, so a press and the lamp that answers it are in
        the same sample index space: the latency is a sample count, not a
        software timestamp. Anything software-timed would be measuring Windows,
        not the ECU.

        Pressing repeatedly is the point. A job that blocks for 160 ms only
        delays the presses that land while it is busy (DEF-102).
        """
        hold = int(CLOCK_HZ * hold_ms / 1000)
        gap = int(CLOCK_HZ * gap_ms / 1000)
        per_press = hold + gap
        total = presses * per_press

        hazard = []
        for _ in range(presses):
            hazard.extend([True] * hold + [False] * gap)
        idle = [False] * total
        waveform = [hazard, idle, idle, idle]   # hazard, turnl, turnr, head

        self.do.stop()
        self.do.timing.cfg_samp_clk_timing(
            rate=CLOCK_HZ, sample_mode=self._acq.FINITE, samps_per_chan=total)

        self.di.stop()
        self.di.timing.cfg_samp_clk_timing(
            source=DO_SAMPLE_CLOCK, rate=CLOCK_HZ,
            sample_mode=self._acq.FINITE, samps_per_chan=total)
        self.di.triggers.start_trigger.cfg_dig_edge_start_trig(DO_START_TRIGGER)

        self.di.start()
        self.do.write(waveform, auto_start=True)
        self.do.wait_until_done(timeout=total / CLOCK_HZ + 5.0)
        samples = self.di.read(number_of_samples_per_channel=total)
        left = samples[LAMP_LINES.index("lamp_left")]
        right = samples[LAMP_LINES.index("lamp_right")]

        responses = []
        for press in range(presses):
            start = press * per_press
            window = range(start, start + per_press)
            lit = next((i for i in window if left[i] or right[i]), None)
            # A press that never lit a lamp is a failure, not a missing sample.
            responses.append((lit - start) * 1000.0 / CLOCK_HZ
                             if lit is not None else 9999.0)

        self._restore_on_demand()
        return responses

    def _restore_on_demand(self) -> None:
        timing = self._nidaqmx.constants.SampleTimingType.ON_DEMAND
        for task in (self.do, self.di):
            task.stop()
            task.timing.samp_timing_type = timing
        self.di.start()
        self.switches()

    def close(self) -> None:
        try:
            self.switches()
            self.set_temp_c(25)
        except Exception:  # noqa: BLE001 - closing must not mask a failure
            pass
        for task in (self.do, self.di, self.ao):
            try:
                task.stop()
                task.close()
            except Exception:  # noqa: BLE001
                pass
        if self.serial:
            self.serial.close()


def settle(seconds: float = 0.5) -> None:
    """Long enough for the firmware's 50 ms sensor timer and the RC on A0."""
    time.sleep(seconds)


# --- cases -----------------------------------------------------------------
# One case per group of requirements, matching the SIL suite case for case.

def tc_hil_01(rig: Rig, lim: dict[str, float]) -> Result:
    """Thermal control: fan, warning lamp and hysteresis. REQ-THRM-010..014."""
    r = Result("TC-HIL-01")
    margin = lim["TEMP_STIM_MARGIN_C"]

    rig.set_temp_c(80)
    settle(1.0)
    r.check_eq("cold 80C: fan off", rig.lamps()["fan"], False)
    r.check_eq("cold 80C: overtemp lamp off", rig.lamps()["overtemp"], False)
    record = rig.telemetry()
    if record:
        r.check_range("cold 80C: reported temp", record["coolantC"],
                      80 - lim["TEMP_REPORT_TOL_C"], 80 + lim["TEMP_REPORT_TOL_C"])

    rig.set_temp_c(lim["FAN_ON_C"] + margin)
    settle(1.0)
    r.check_eq("fan-on temperature: fan on", rig.lamps()["fan"], True)
    r.check_eq("fan-on temperature: overtemp lamp off", rig.lamps()["overtemp"], False)

    rig.set_temp_c(lim["OVERTEMP_C"] + margin)
    settle(1.0)
    r.check_eq("over-temperature: warning lamp on", rig.lamps()["overtemp"], True)

    # Hysteresis: cool to just above the off point, the fan must stay on.
    rig.set_temp_c(lim["FAN_OFF_C"] + 2)
    settle(1.0)
    r.check_eq("hysteresis: fan stays on above off point", rig.lamps()["fan"], True)
    rig.set_temp_c(lim["FAN_OFF_C"] - 2)
    settle(1.0)
    r.check_eq("hysteresis: fan off below off point", rig.lamps()["fan"], False)

    rig.set_temp_c(25)
    return r


def tc_hil_02(rig: Rig, lim: dict[str, float]) -> Result:
    """Body control: headlamps and the turn-signal flasher. REQ-BODY-020..023."""
    r = Result("TC-HIL-02")

    rig.switches(head=False)
    settle()
    record = rig.telemetry()
    if record:
        r.check_eq("headlamp switch open: lamps off", record["head"], 0)
    rig.switches(head=True)
    settle()
    record = rig.telemetry()
    if record:
        r.check_eq("headlamp switch closed: lamps on", record["head"], 1)
    rig.switches()

    rig.switches(turnl=True)
    edges = rig.watch_lamp("lamp_left", 3.0)
    right_edges = rig.watch_lamp("lamp_right", 0.5)
    rig.switches()
    r.check_eq("left stalk: left lamp flashes", len(edges) >= 2, True)
    r.check_eq("left stalk: right lamp dark", len(right_edges), 0)
    if len(edges) >= 2:
        period = statistics.median([b - a for a, b in zip(edges, edges[1:])])
        r.check_range("left stalk: flash full period (ms)", period,
                      2 * lim["FLASH_HALF_MS_MIN"], 2 * lim["FLASH_HALF_MS_MAX"])
    return r


def tc_hil_03(rig: Rig, lim: dict[str, float]) -> Result:
    """REQ-SAFE-100: hazard response, the case this bench exists for."""
    r = Result("TC-HIL-03")
    presses = int(lim["HAZARD_PRESS_COUNT"])
    responses = rig.hazard_response_ms(presses)
    for i, ms in enumerate(responses):
        print(f"  DATA  press {i + 1}: {ms:.3f} ms")
    r.check_eq("every press answered", len(responses), presses)
    r.check_max("slowest response (ms)", max(responses, default=9999.0),
                lim["HAZARD_RESPONSE_MAX_MS"])
    return r


def tc_hil_04(rig: Rig, lim: dict[str, float]) -> Result:
    """The same deadline with the other two functions busy. REQ-SAFE-100."""
    r = Result("TC-HIL-04")
    rig.set_temp_c(lim["OVERTEMP_C"] + lim["TEMP_STIM_MARGIN_C"])
    settle(1.0)
    presses = int(lim["HAZARD_PRESS_COUNT"])
    responses = rig.hazard_response_ms(presses)
    r.check_max("slowest response, fan running (ms)",
                max(responses, default=9999.0), lim["HAZARD_RESPONSE_MAX_MS"])
    record = rig.telemetry()
    if record:
        r.check_max("slowest pass of loop() (ms)", record["loop_max_ms"],
                    lim["LOOP_MAX_MS"])
    rig.set_temp_c(25)
    return r


def tc_hil_05(rig: Rig, lim: dict[str, float]) -> Result:
    """Integration: one switch, one function. REQ-BODY-021, REQ-SAFE-103.

    DEF-101 shows up here as headlamps that follow the hazard switch.
    """
    r = Result("TC-HIL-05")
    rig.switches(hazard=True)
    settle()
    record = rig.telemetry()
    if record:
        r.check_eq("hazard switch does not touch the headlamps", record["head"], 0)
        r.check_eq("hazard switch is seen by the hazard function", record["hazard"], 1)
    lamps = rig.watch_lamp("lamp_left", 1.5)
    right = rig.watch_lamp("lamp_right", 1.5)
    r.check_eq("both hazard lamps flash", len(lamps) >= 1 and len(right) >= 1, True)

    # Stalk first, then hazard: hazard must take both lamps.
    rig.switches(turnl=True)
    settle()
    rig.switches(turnl=True, hazard=True)
    right = rig.watch_lamp("lamp_right", 1.5)
    r.check_eq("hazard beats the turn stalk", len(right) >= 1, True)
    rig.switches()
    return r


CASES = {
    "TC-HIL-01": tc_hil_01,
    "TC-HIL-02": tc_hil_02,
    "TC-HIL-03": tc_hil_03,
    "TC-HIL-04": tc_hil_04,
    "TC-HIL-05": tc_hil_05,
}


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
    ap.add_argument("--all", action="store_true", help="run every case")
    ap.add_argument("--port", help="Uno serial port for telemetry, e.g. /dev/ttyACM0")
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

    rig = Rig(args.port)
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
