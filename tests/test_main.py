"""
test_main.py — End-to-end test runner.

For each registered test case:
  1. (Re)generate ngspice golden if --regen-golden flag is set
     OR if the golden file does not yet exist.
  2. Run the circuit engine on the same netlist.
  3. Diff engine output vs golden using diff.py logic.
  4. Print summary.

Usage:
    # First run (generates goldens, requires ngspice):
    python tests/test_main.py --regen-golden

    # Normal CI run (goldens already committed):
    python tests/test_main.py

    # Single test:
    python tests/test_main.py --filter rc_dc

    # Relax tolerance:
    python tests/test_main.py --tol-rel 5e-3
"""

import argparse
import sys
import time
from pathlib import Path

#  resolve paths 
_TESTS_DIR   = Path(__file__).parent
_RUNNER_DIR  = _TESTS_DIR / "runner"
_NETLISTS    = _TESTS_DIR / "netlists"
_GOLDEN_DIR  = _TESTS_DIR / "golden"
_RESULTS_DIR = _TESTS_DIR / "results"

sys.path.insert(0, str(_RUNNER_DIR))
from runner.run_ngspice import generate_golden
from runner.run_engine  import generate_result
from runner.diff        import compare, print_report

# 
# Test case registry
# Add new test cases here — just (stem_name, netlist_filename) tuples.
# 
TEST_CASES = [
    # (name,      netlist file)
    ("rc_dc",   "rc_dc.cir"),
    ("rc_ac",   "rc_ac.cir"),
    # Add more here, e.g.:
]


#  ANSI 
def _c(code, text): return f"\033[{code}m{text}\033[0m"
BOLD  = lambda t: _c("1",  t)
GREEN = lambda t: _c("32", t)
RED   = lambda t: _c("31", t)
CYAN  = lambda t: _c("36", t)
YELLOW= lambda t: _c("33", t)


# 
# Run one test
# 

def run_test(
    name: str,
    netlist_file: str,
    regen_golden: bool,
    tol_rel: float,
    tol_abs: float,
) -> bool:
    """Return True if the test passes."""
    netlist_path = _NETLISTS / netlist_file
    golden_path  = _GOLDEN_DIR  / f"{name}.json"
    result_path  = _RESULTS_DIR / f"{name}.json"

    _GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    _RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    print(BOLD(CYAN(f"\n── Test: {name} ({'netlist: ' + netlist_file}) ──────────────────")))

    #  Step 1: golden 
    if regen_golden or not golden_path.exists():
        try:
            generate_golden(netlist_path, golden_path)
        except Exception as ex:
            print(RED(f"  [GOLDEN FAIL] {ex}"))
            return False
    else:
        print(f"  [golden] Using cached {golden_path}")

    #  Step 2: engine 
    try:
        generate_result(netlist_path, result_path)
    except Exception as ex:
        print(RED(f"  [ENGINE FAIL] {ex}"))
        return False

    #  Step 3: diff 
    import json
    golden = json.loads(golden_path.read_text())
    result = json.loads(result_path.read_text())

    dr = compare(golden, result, tol_rel=tol_rel, tol_abs=tol_abs)
    print_report(str(golden_path), str(result_path), dr, tol_rel, tol_abs)

    return dr.passed


# 
# Main
# 

def main():
    parser = argparse.ArgumentParser(description="CircuitEngine vs ngspice test runner")
    parser.add_argument("--regen-golden", action="store_true",
                        help="Re-run ngspice and overwrite golden files")
    parser.add_argument("--filter", "-f", default="",
                        help="Only run tests whose name contains this string")
    parser.add_argument("--tol-rel", type=float, default=1e-3)
    parser.add_argument("--tol-abs", type=float, default=1e-9)
    args = parser.parse_args()

    cases = [
        (name, nf) for name, nf in TEST_CASES
        if args.filter.lower() in name.lower()
    ]

    if not cases:
        print(RED(f"No test cases match filter: {args.filter!r}"))
        sys.exit(1)

    passed = []
    failed = []
    t0 = time.monotonic()

    for name, netlist_file in cases:
        ok = run_test(
            name, netlist_file,
            regen_golden=args.regen_golden,
            tol_rel=args.tol_rel,
            tol_abs=args.tol_abs,
        )
        (passed if ok else failed).append(name)

    elapsed = time.monotonic() - t0

    print()
    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print(BOLD(CYAN("  Test Summary")))
    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print(f"  Ran    : {len(cases)} test(s)  [{elapsed:.1f}s]")
    if passed:
        print(GREEN(f"  Passed : {len(passed)} — {passed}"))
    if failed:
        print(RED(  f"  Failed : {len(failed)} — {failed}"))
    print(BOLD(CYAN("════════════════════════════════════════════════════")))
    print()

    sys.exit(0 if not failed else 1)


if __name__ == "__main__":
    main()