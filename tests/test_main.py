"""
test_main.py — End-to-end test runner.

For each registered test case:
  1. (Re)generate ngspice golden if --regen-golden flag is set
     OR if the golden file does not yet exist.
  2. Run the circuit engine on the same netlist.
  3. Diff engine output vs golden using diff.py logic.
  4. Print per-test report + suite-wide accuracy summary.

Usage:
    # First run (generates goldens, requires ngspice):
    python tests/test_main.py --regen-golden

    # Normal CI run (goldens already committed):
    python tests/test_main.py

    # Single test:
    python tests/test_main.py --filter rc_dc

    # Relax tolerance:
    python tests/test_main.py --tol-rel 5e-3

    # Mark tests that are known to fail (they still run, but suite exits 0):
    python tests/test_main.py --expect-fail FloatingNode ShortedVoltageSource
"""

import argparse
import math
import sys
import time
from pathlib import Path

# ── resolve paths ─────────────────────────────────────────────────────────────
_TESTS_DIR   = Path(__file__).parent
_RUNNER_DIR  = _TESTS_DIR / "runner"
_NETLISTS    = _TESTS_DIR / "netlists"
_GOLDEN_DIR  = _TESTS_DIR / "golden"
_RESULTS_DIR = _TESTS_DIR / "results"

sys.path.insert(0, str(_RUNNER_DIR))
from runner.run_ngspice import generate_golden
from runner.run_engine  import generate_result
from runner.diff        import compare, print_report, DiffResult

# ──────────────────────────────────────────────────────────────────────────────
# Test case registry
# Add new test cases here — just (stem_name, netlist_filename) tuples.
# ──────────────────────────────────────────────────────────────────────────────
TEST_CASES = [
    # (name,                 netlist file)
    ("rc_dc",               "rc_dc.cir"),
    ("rc_ac",               "rc_ac.cir"),
    ("OP_001",              "OP_001.cir"),
    ("OP_002",              "OP_002.cir"),
    ("OP_003",              "OP_003.cir"),
    ("OP_004",              "OP_004.cir"),
    ("DC_001",              "DC_001.cir"),
    ("DC_002",              "DC_002.cir"),
    ("DC_003",              "DC_003.cir"),
    ("AC_001",              "AC_001.cir"),
    ("AC_002",              "AC_002.cir"),
    ("AC_003",              "AC_003.cir"),
    ("AC_004",              "AC_004.cir"),
    ("TRAN_001",            "TRAN_001.cir"),
    ("TRAN_002",            "TRAN_002.cir"),
    ("TRAN_003",            "TRAN_003.cir"),
    ("TRAN_004",            "TRAN_004.cir"),
    ("SIN_001",             "SIN_001.cir"),
    ("SIN_002",             "SIN_002.cir"),
    ("PULSE_001",           "PULSE_001.cir"),
    ("PULSE_002",           "PULSE_002.cir"),
    ("VCVS",                "VCVS.cir"),
    ("VCCS",                "VCCS.cir"),
    ("CCCS",                "CCCS.cir"),
    ("CCVS",                "CCVS.cir"),
    ("DIODE_001",           "DIODE_001.cir"),
    ("DIODE_002",           "DIODE_002.cir"),
    ("FloatingNode",        "FloatingNode.cir"),
    ("Voltage_Source_Loop", "Voltage_Source_Loop.cir"),
    ("ShortedVoltageSource","ShortedVoltageSource.cir"),
    ("RC_Lowpass",          "RC_Lowpass.cir"),
    ("RLC_Resonance",       "RLC_Resonance.cir"),
    ("DiodeRectifier",      "DiodeRectifier.cir"),
    ("RLStep",              "RLStep.cir"),
    # Add more here, e.g.:
]

# ── ANSI ──────────────────────────────────────────────────────────────────────
def _c(code, text): return f"\033[{code}m{text}\033[0m"
BOLD   = lambda t: _c("1",  t)
GREEN  = lambda t: _c("32", t)
RED    = lambda t: _c("31", t)
CYAN   = lambda t: _c("36", t)
YELLOW = lambda t: _c("33", t)
DIM    = lambda t: _c("2",  t)


# ──────────────────────────────────────────────────────────────────────────────
# Suite-level statistics accumulator
# ──────────────────────────────────────────────────────────────────────────────

class SuiteStats:
    """Accumulates accuracy numbers across all passing tests."""
    def __init__(self):
        self.max_abs   = 0.0
        self.max_rel   = 0.0
        self.sum_sq    = 0.0
        self.n         = 0
        self.worst_test: str = ""

    def absorb(self, name: str, dr: "DiffResult"):
        if dr.global_n == 0:
            return
        if dr.global_max_abs > self.max_abs:
            self.max_abs = dr.global_max_abs
        if dr.global_max_rel > self.max_rel:
            self.max_rel = dr.global_max_rel
            self.worst_test = f"{name} / {dr.worst_case_signal}"
        self.sum_sq += dr.global_sum_sq
        self.n      += dr.global_n

    @property
    def rmse(self) -> float:
        return math.sqrt(self.sum_sq / self.n) if self.n else 0.0


# ──────────────────────────────────────────────────────────────────────────────
# Run one test
# ──────────────────────────────────────────────────────────────────────────────

def run_test(
    name: str,
    netlist_file: str,
    regen_golden: bool,
    tol_rel: float,
    tol_abs: float,
) -> "tuple[bool, DiffResult | None]":
    """Return (passed, DiffResult|None)."""
    netlist_path = _NETLISTS / netlist_file
    golden_path  = _GOLDEN_DIR  / f"{name}.json"
    result_path  = _RESULTS_DIR / f"{name}.json"

    _GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    _RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    print(BOLD(CYAN(f"\n── Test: {name} (netlist: {netlist_file}) ──────────────────")))

    # Step 1: golden
    if regen_golden or not golden_path.exists():
        try:
            generate_golden(netlist_path, golden_path)
        except Exception as ex:
            print(RED(f"  [GOLDEN FAIL] {ex}"))
            return False, None
    else:
        print(DIM(f"  [golden] Using cached {golden_path}"))

    # Step 2: engine
    try:
        generate_result(netlist_path, result_path)
    except Exception as ex:
        print(RED(f"  [ENGINE FAIL] {ex}"))
        return False, None

    # Step 3: diff
    import json
    golden = json.loads(golden_path.read_text())
    result = json.loads(result_path.read_text())

    dr = compare(golden, result, tol_rel=tol_rel, tol_abs=tol_abs)
    print_report(str(golden_path), str(result_path), dr, tol_rel, tol_abs)

    return dr.passed, dr


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="CircuitEngine vs ngspice test runner")
    parser.add_argument("--regen-golden", action="store_true",
                        help="Re-run ngspice and overwrite golden files")
    parser.add_argument("--filter", "-f", default="",
                        help="Only run tests whose name contains this string")
    parser.add_argument("--tol-rel", type=float, default=1e-2)
    parser.add_argument("--tol-abs", type=float, default=1e-8)
    parser.add_argument(
        "--expect-fail", nargs="*", default=[],
        metavar="NAME",
        help=(
            "Test names that are expected to fail. "
            "They still run and are reported, but do NOT cause a non-zero exit. "
            "Example: --expect-fail FloatingNode ShortedVoltageSource"
        ),
    )
    args = parser.parse_args()

    expect_fail: set[str] = set(args.expect_fail or [])

    cases = [
        (name, nf) for name, nf in TEST_CASES
        if args.filter.lower() in name.lower()
    ]

    if not cases:
        print(RED(f"No test cases match filter: {args.filter!r}"))
        sys.exit(1)

    passed_names:       list[str] = []
    failed_names:       list[str] = []
    xfail_names:        list[str] = []   # expected-fail that indeed failed
    xpass_names:        list[str] = []   # expected-fail that surprisingly passed
    errored_names:      list[str] = []   # engine/golden hard errors
    suite = SuiteStats()
    t0 = time.monotonic()

    for name, netlist_file in cases:
        ok, dr = run_test(
            name, netlist_file,
            regen_golden=args.regen_golden,
            tol_rel=args.tol_rel,
            tol_abs=args.tol_abs,
        )

        if dr is None:
            # Hard error (engine/golden crash)
            errored_names.append(name)
            if name not in expect_fail:
                failed_names.append(name)
            else:
                xfail_names.append(name)
            continue

        is_xfail = name in expect_fail

        if ok:
            suite.absorb(name, dr)
            if is_xfail:
                xpass_names.append(name)   # unexpected pass
                passed_names.append(name)
            else:
                passed_names.append(name)
        else:
            if is_xfail:
                xfail_names.append(name)
            else:
                failed_names.append(name)

    elapsed = time.monotonic() - t0

    # ── Suite summary ─────────────────────────────────────────────────────────
    print()
    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print(BOLD(CYAN("  Test Summary")))
    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print(f"  Ran      : {len(cases)} test(s)  [{elapsed:.1f}s]")

    if passed_names:
        print(GREEN(f"  PASS     : {len(passed_names)} — {passed_names}"))
    if failed_names:
        print(RED(  f"  FAIL     : {len(failed_names)} — {failed_names}"))
    if xfail_names:
        print(YELLOW(f"  XFAIL    : {len(xfail_names)} (expected) — {xfail_names}"))
    if xpass_names:
        print(YELLOW(f"  XPASS    : {len(xpass_names)} (unexpected pass!) — {xpass_names}"))

    # ── Suite-wide accuracy (only from passing tests) ─────────────────────────
    if suite.n > 0:
        print(BOLD(CYAN("────────────────────────────────────────────────────")))
        print(BOLD("  Suite accuracy (passing tests only):"))
        print(f"    Samples compared  : {suite.n}")
        print(f"    max_abs_error     : {suite.max_abs:.4e}")
        print(f"    max_rel_error     : {suite.max_rel:.4e}  ({suite.max_rel*100:.4f}%)")
        print(f"    RMSE              : {suite.rmse:.4e}")
        print(f"    worst_case        : {suite.worst_test or 'n/a'}")

    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print()

    # Exit 0 only when there are no unexpected failures
    sys.exit(0 if not failed_names else 1)


if __name__ == "__main__":
    main()
