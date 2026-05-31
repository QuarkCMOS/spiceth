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
   - same number of data points (for .tran: adaptive-timestep tolerance applied)

2. Numerical checks (with tolerance):
   - For each data point, for each variable:
       |engine - golden| <= max(tol_abs, tol_rel * |golden|)
   Default: tol_rel=1e-3 (0.1%), tol_abs=1e-9

3. Sweep axis check:
   - Sweep values (time / frequency) must match within tol_rel.
   - For .tran with different point counts (adaptive timestep), engine values
     are linearly interpolated onto the golden time grid before comparison.

Statistics computed (always):
   - max_abs_error        per-signal and global
   - max_rel_error        per-signal and global
   - worst_case_signal    signal name with largest max_rel_error
   - RMSE                 per-signal and global
"""

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


# ── ANSI colours ─────────────────────────────────────────────────────────────
def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m"

GREEN  = lambda t: _c("32", t)
RED    = lambda t: _c("31", t)
YELLOW = lambda t: _c("33", t)
BOLD   = lambda t: _c("1",  t)
CYAN   = lambda t: _c("36", t)
DIM    = lambda t: _c("2",  t)


# ── Helpers ───────────────────────────────────────────────────────────────────

def _close(a: float, b: float, tol_rel: float, tol_abs: float) -> bool:
    return abs(a - b) <= max(tol_abs, tol_rel * abs(b))


def _abs_err(a: float, b: float) -> float:
    return abs(a - b)


def _rel_err(a: float, b: float, tol_abs: float) -> float:
    return abs(a - b) / max(abs(b), tol_abs)


def _var_names(point: dict) -> list[str]:
    return [v["name"] for v in point["values"]]


def _find_value(point: dict, name: str) -> dict | None:
    for v in point["values"]:
        if v["name"] == name:
            return v
    return None


def _interp(x: float, xs: list[float], ys: list[float]) -> float:
    """Linear interpolation of ys at x, given sorted xs."""
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    # binary search
    lo, hi = 0, len(xs) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if xs[mid] <= x:
            lo = mid
        else:
            hi = mid
    t = (x - xs[lo]) / (xs[hi] - xs[lo]) if xs[hi] != xs[lo] else 0.0
    return ys[lo] + t * (ys[hi] - ys[lo])


# ── Per-signal statistics ─────────────────────────────────────────────────────

class SignalStats:
    """Accumulates error statistics for one (signal, part) pair."""
    def __init__(self, name: str):
        self.name = name
        self.max_abs = 0.0
        self.max_rel = 0.0
        self.sum_sq  = 0.0
        self.n       = 0

    def update(self, abs_e: float, rel_e: float):
        self.max_abs = max(self.max_abs, abs_e)
        self.max_rel = max(self.max_rel, rel_e)
        self.sum_sq += abs_e ** 2
        self.n += 1

    @property
    def rmse(self) -> float:
        return math.sqrt(self.sum_sq / self.n) if self.n else 0.0


# ── Main comparison ───────────────────────────────────────────────────────────

class DiffResult:
    def __init__(self):
        self.errors:        list[str]              = []
        self.warnings:      list[str]              = []
        self.checks:        int                    = 0
        self.signal_stats:  dict[str, SignalStats] = {}   # key = "name.part"
        self.global_max_abs:   float = 0.0
        self.global_max_rel:   float = 0.0
        self.global_sum_sq:    float = 0.0
        self.global_n:         int   = 0
        self.worst_case_signal: str  = ""
        self.analysis_type:    str   = ""
        self.adaptive_tran:    bool  = False   # True when tran point counts differ

    @property
    def passed(self) -> bool:
        return len(self.errors) == 0

    @property
    def global_rmse(self) -> float:
        return math.sqrt(self.global_sum_sq / self.global_n) if self.global_n else 0.0

    def _update_global(self, abs_e: float, rel_e: float):
        self.global_max_abs = max(self.global_max_abs, abs_e)
        if rel_e > self.global_max_rel:
            self.global_max_rel = rel_e
        self.global_sum_sq += abs_e ** 2
        self.global_n += 1

    def _record(self, key: str, abs_e: float, rel_e: float):
        if key not in self.signal_stats:
            self.signal_stats[key] = SignalStats(key)
        self.signal_stats[key].update(abs_e, rel_e)
        self._update_global(abs_e, rel_e)
        # track worst-case signal by max_rel
        if self.signal_stats[key].max_rel >= self.global_max_rel:
            self.worst_case_signal = key


def compare(
    golden: dict[str, Any],
    result: dict[str, Any],
    tol_rel: float = 1e-2,
    tol_abs: float = 1e-8,
    max_errors: int = 20,
) -> DiffResult:
    dr = DiffResult()

    def err(msg: str):
        if len(dr.errors) < max_errors:
            dr.errors.append(msg)

    def warn(msg: str):
        dr.warnings.append(msg)

    # ── 1. Top-level structural checks ────────────────────────────────────────
    dr.checks += 1
    g_atype = golden.get("analysis_type", "")
    r_atype = result.get("analysis_type", "")
    dr.analysis_type = g_atype

    if g_atype != r_atype:
        err(
            f"analysis_type mismatch: golden={g_atype!r} "
            f"engine={r_atype!r}"
        )

    dr.checks += 1
    g_points = golden["data"]
    r_points = result["data"]

    is_tran = (g_atype == "tran")
    adaptive = is_tran and len(g_points) != len(r_points)
    dr.adaptive_tran = adaptive

    if len(g_points) != len(r_points):
        if is_tran:
            warn(
                f"Point count differs (golden={len(g_points)} engine={len(r_points)}): "
                f"adaptive-timestep interpolation will be used."
            )
        else:
            err(
                f"Point count mismatch: golden={len(g_points)} engine={len(r_points)}"
            )

    if not g_points:
        warn("No data points to compare.")
        return dr

    if not r_points:
        err("Engine returned no data points (empty 'data' list).")
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

    common_names = g_names & r_names

    # ── 2. Build lookup structures for adaptive tran ──────────────────────────
    # For adaptive tran we interpolate engine values onto the golden time grid.
    # For all other cases we compare point-by-point.
    if adaptive:
        r_sweep_vals = [rp["sweep_value"] for rp in r_points]
        # per-variable arrays for engine
        r_series: dict[str, dict[str, list[float]]] = {}
        for name in common_names:
            r_series[name] = {"real": [], "imag": []}
        for rp in r_points:
            for name in common_names:
                rv = _find_value(rp, name)
                if rv:
                    r_series[name]["real"].append(rv["real"])
                    r_series[name]["imag"].append(rv["imag"])
                else:
                    r_series[name]["real"].append(0.0)
                    r_series[name]["imag"].append(0.0)

    # ── 3. Point-by-point numerical comparison ────────────────────────────────
    for i, gp in enumerate(g_points):
        g_sweep = gp["sweep_value"]

        if adaptive:
            # Interpolate engine onto golden sweep value
            r_sweep_at_i = g_sweep   # golden defines the reference grid
        else:
            if i >= len(r_points):
                break
            rp = r_points[i]
            r_sweep = rp["sweep_value"]

            # Sweep axis check — skip for .op (dummy sweep values differ by convention)
            if g_atype != "op":
                dr.checks += 1
                if not _close(g_sweep, r_sweep, tol_rel=1e-6, tol_abs=1e-30):
                    err(
                        f"Point[{i}] sweep_value mismatch: "
                        f"golden={g_sweep:.6g} engine={r_sweep:.6g} "
                        f"(abs_err={_abs_err(g_sweep, r_sweep):.3e})"
                    )

        for name in sorted(common_names):
            gv = _find_value(gp, name)
            if gv is None:
                continue

            for part in ("real", "imag"):
                dr.checks += 1
                g_val = gv[part]

                if adaptive:
                    r_val = _interp(
                        g_sweep,
                        r_sweep_vals,
                        r_series[name][part],
                    )
                else:
                    rv = _find_value(rp, name)
                    r_val = rv[part] if rv else 0.0

                abs_e = _abs_err(g_val, r_val)
                rel_e = _rel_err(g_val, r_val, tol_abs)
                key   = f"{name}.{part}"

                dr._record(key, abs_e, rel_e)

                if not _close(g_val, r_val, tol_rel, tol_abs):
                    err(
                        f"Point[{i}] {name}.{part}: "
                        f"golden={g_val:.6g}  engine={r_val:.6g}  "
                        f"abs_err={abs_e:.3e}  rel_err={rel_e:.3e}"
                    )

    return dr


# ── Report printer ────────────────────────────────────────────────────────────

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
    if dr.adaptive_tran:
        print(YELLOW("  Mode   : adaptive-timestep (.tran) — engine interpolated onto golden grid"))
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

    # ── Statistics block ──────────────────────────────────────────────────────
    if dr.global_n > 0:
        print(BOLD(CYAN("──────────────────────────────────────────────────")))
        print(BOLD("  Accuracy statistics:"))
        print(f"    global max_abs_error  : {dr.global_max_abs:.4e}")
        print(f"    global max_rel_error  : {dr.global_max_rel:.4e}  ({dr.global_max_rel*100:.4f}%)")
        print(f"    global RMSE           : {dr.global_rmse:.4e}")
        print(f"    worst_case_signal     : {dr.worst_case_signal or 'n/a'}")

        # Per-signal breakdown (sorted by max_rel descending)
        if dr.signal_stats:
            print(BOLD(CYAN("──────────────────────────────────────────────────")))
            print(BOLD("  Per-signal breakdown (top 5 by max_rel_error):"))
            top = sorted(
                dr.signal_stats.values(),
                key=lambda s: s.max_rel,
                reverse=True,
            )[:5]
            for s in top:
                flag = RED("  ✗") if s.max_rel > tol_rel else GREEN("  ✓")
                print(
                    f"{flag}  {s.name:<25}"
                    f"  max_abs={s.max_abs:.3e}"
                    f"  max_rel={s.max_rel:.3e}"
                    f"  RMSE={s.rmse:.3e}"
                )

    print(BOLD(CYAN("══════════════════════════════════════════════════")))
    print()


# ── CLI ───────────────────────────────────────────────────────────────────────

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
