"""
diff.py — Compare engine JSON output against ngspice golden reference.

Usage:
    python diff.py <golden.json> <result.json> [--tol-rel 1e-3] [--tol-abs 1e-9]

Exit code:
    0  — all checks pass
    1  — one or more mismatches found

Comparison strategy
-------------------
We do NOT require perfect numerical equality (floating-point differences between
two different solvers are normal).  Instead we check:

1. Structural checks (always strict):
   - analysis_type must match
   - same set of variable names
   - same number of data points

2. Numerical checks (with tolerance):
   - For each data point, for each variable:
       |engine - golden| ≤ max(tol_abs, tol_rel * |golden|)
   Default: tol_rel=1e-3 (0.1%), tol_abs=1e-9

3. Sweep axis check:
   - Sweep values (time / frequency) must match within tol_rel.
"""

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


#  ANSI colours 
def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m"

GREEN  = lambda t: _c("32", t)
RED    = lambda t: _c("31", t)
YELLOW = lambda t: _c("33", t)
BOLD   = lambda t: _c("1",  t)
CYAN   = lambda t: _c("36", t)


# 
# Helpers
# 

def _close(a: float, b: float, tol_rel: float, tol_abs: float) -> bool:
    """Return True if a and b are within tolerance."""
    return abs(a - b) <= max(tol_abs, tol_rel * abs(b))


def _var_names(point: dict) -> list[str]:
    return [v["name"] for v in point["values"]]


def _find_value(point: dict, name: str) -> dict | None:
    for v in point["values"]:
        if v["name"] == name:
            return v
    return None


# 
# Main comparison
# 

class DiffResult:
    def __init__(self):
        self.errors:   list[str] = []
        self.warnings: list[str] = []
        self.checks:   int = 0

    @property
    def passed(self) -> bool:
        return len(self.errors) == 0


def compare(
    golden: dict[str, Any],
    result: dict[str, Any],
    tol_rel: float = 1e-3,
    tol_abs: float = 1e-9,
    max_errors: int = 20,
) -> DiffResult:
    dr = DiffResult()

    def err(msg: str):
        if len(dr.errors) < max_errors:
            dr.errors.append(msg)

    def warn(msg: str):
        dr.warnings.append(msg)

    #  1. Top-level structural checks 
    dr.checks += 1
    if golden["analysis_type"] != result["analysis_type"]:
        err(
            f"analysis_type mismatch: golden={golden['analysis_type']!r} "
            f"engine={result['analysis_type']!r}"
        )

    dr.checks += 1
    g_points = golden["data"]
    r_points = result["data"]

    if len(g_points) != len(r_points):
        err(
            f"Point count mismatch: golden={len(g_points)} engine={len(r_points)}"
        )
        # Still try to compare what we can
        n_pts = min(len(g_points), len(r_points))
    else:
        n_pts = len(g_points)

    if n_pts == 0:
        warn("No data points to compare.")
        return dr

    # Variable names from first point
    g_names = set(_var_names(g_points[0]))
    r_names = set(_var_names(r_points[0]))

    dr.checks += 1
    missing_in_engine = g_names - r_names
    extra_in_engine   = r_names - g_names
    if missing_in_engine:
        err(f"Variables missing in engine output: {sorted(missing_in_engine)}")
    if extra_in_engine:
        warn(f"Extra variables in engine output (not in golden): {sorted(extra_in_engine)}")

    # Variables to compare (intersection)
    common_names = g_names & r_names

    #  2. Point-by-point numerical comparison 
    for i in range(n_pts):
        gp = g_points[i]
        rp = r_points[i]

        # Sweep axis
        dr.checks += 1
        g_sweep = gp["sweep_value"]
        r_sweep = rp["sweep_value"]
        if not _close(g_sweep, r_sweep, tol_rel=1e-6, tol_abs=1e-30):
            err(
                f"Point[{i}] sweep_value mismatch: "
                f"golden={g_sweep:.6g} engine={r_sweep:.6g}"
            )

        # Each variable
        for name in sorted(common_names):
            gv = _find_value(gp, name)
            rv = _find_value(rp, name)
            if gv is None or rv is None:
                continue

            for part in ("real", "imag"):
                dr.checks += 1
                g_val = gv[part]
                r_val = rv[part]
                if not _close(g_val, r_val, tol_rel, tol_abs):
                    rel_err = abs(g_val - r_val) / max(abs(g_val), tol_abs)
                    err(
                        f"Point[{i}] {name}.{part}: "
                        f"golden={g_val:.6g} engine={r_val:.6g} "
                        f"(rel_err={rel_err:.2e})"
                    )

    return dr


# 
# Report printer
# 

def print_report(
    golden_path: str,
    result_path: str,
    dr: DiffResult,
    tol_rel: float,
    tol_abs: float,
) -> None:
    print()
    print(BOLD(CYAN("══════════════════════════════════════════════════")))
    print(BOLD(CYAN("  CircuitEngine ↔ ngspice diff report")))
    print(BOLD(CYAN("══════════════════════════════════════════════════")))
    print(f"  Golden : {golden_path}")
    print(f"  Result : {result_path}")
    print(f"  Tol    : rel={tol_rel:.0e}  abs={tol_abs:.0e}")
    print(BOLD(CYAN("──────────────────────────────────────────────────")))

    if dr.warnings:
        for w in dr.warnings:
            print(YELLOW(f"  ⚠  {w}"))

    if dr.passed:
        print(GREEN(f"  ✓  All {dr.checks} checks passed."))
    else:
        n_shown = len(dr.errors)
        print(RED(f"  ✗  {n_shown} error(s) found (out of {dr.checks} checks):"))
        for e in dr.errors:
            print(RED(f"       • {e}"))

    print(BOLD(CYAN("══════════════════════════════════════════════════")))
    print()


# 
# CLI
# 

def main():
    parser = argparse.ArgumentParser(
        description="Compare engine JSON output against ngspice golden reference."
    )
    parser.add_argument("golden", help="Path to golden JSON (ngspice reference)")
    parser.add_argument("result", help="Path to engine result JSON")
    parser.add_argument("--tol-rel", type=float, default=1e-3,
                        help="Relative tolerance (default: 1e-3 = 0.1%%)")
    parser.add_argument("--tol-abs", type=float, default=1e-9,
                        help="Absolute tolerance (default: 1e-9)")
    parser.add_argument("--max-errors", type=int, default=20,
                        help="Max errors to display (default: 20)")
    args = parser.parse_args()

    golden = json.loads(Path(args.golden).read_text())
    result = json.loads(Path(args.result).read_text())

    dr = compare(
        golden, result,
        tol_rel=args.tol_rel,
        tol_abs=args.tol_abs,
        max_errors=args.max_errors,
    )
    print_report(args.golden, args.result, dr, args.tol_rel, args.tol_abs)

    sys.exit(0 if dr.passed else 1)


if __name__ == "__main__":
    main()