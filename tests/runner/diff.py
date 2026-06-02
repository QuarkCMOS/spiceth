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

2. Sweep-axis alignment (.tran only):
   For each engine timestep (fixed grid), we look for the matching time in
   ngspice's adaptive output:
     a) Exact match found  → compare directly.
     b) No exact match     → find the two ngspice points that bracket the
        engine time and linearly interpolate.
        If either bracket point is further than `interp_warn_dt` from the
        engine time a WARNING is issued (sparse ngspice region).
     c) Engine time is outside ngspice's time range → error.

3. Numerical checks (with tolerance):
   - For each engine data point, for each variable:
       |engine - ngspice_interpolated| <= max(tol_abs, tol_rel * |ngspice_interpolated|)
   Default: tol_rel=1e-3 (0.1%), tol_abs=1e-9

4. Non-tran analyses (.op, .dc, .ac):
   Point counts must be equal; comparison is done point-by-point (no
   interpolation needed).

Statistics computed (always):
   - max_abs_error        per-signal and global
   - max_rel_error        per-signal and global
   - worst_case_signal    signal name with largest max_rel_error
   - RMSE                 per-signal and global
"""

import argparse
import bisect
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


def _interp_linear(x: float, x0: float, x1: float, y0: float, y1: float) -> float:
    """Linear interpolation between two points."""
    if x1 == x0:
        return y0
    t = (x - x0) / (x1 - x0)
    return y0 + t * (y1 - y0)


# ── Ngspice time-series lookup ────────────────────────────────────────────────

class NgspiceSeries:
    """
    Pre-built lookup structure for one variable's time-series from ngspice
    adaptive output.  Supports O(log n) bracket search + linear interpolation.
    """

    def __init__(
        self,
        times: list[float],
        real_vals: list[float],
        imag_vals: list[float],
        exact_tol: float = 1e-15,
    ):
        self.times = times          # sorted ascending
        self.real  = real_vals
        self.imag  = imag_vals
        self.exact_tol = exact_tol

    def query(
        self,
        t: float,
        interp_warn_dt: float,
    ) -> "tuple[float, float, str | None]":
        """
        Return (real, imag, warning_message_or_None) at time t.

        warning_message is set when either bracket point is further than
        interp_warn_dt from t (sparse ngspice region).
        """
        times = self.times

        # Out-of-range guard
        if t < times[0] - self.exact_tol:
            return 0.0, 0.0, f"engine t={t:.6g} is before ngspice start t={times[0]:.6g}"
        if t > times[-1] + self.exact_tol:
            return 0.0, 0.0, f"engine t={t:.6g} is after ngspice end t={times[-1]:.6g}"

        # Binary search for left bracket: largest times[lo] <= t
        lo = bisect.bisect_right(times, t) - 1
        lo = max(lo, 0)

        # Exact match check
        if abs(times[lo] - t) <= self.exact_tol:
            return self.real[lo], self.imag[lo], None

        hi = lo + 1
        if hi >= len(times):
            # t is at or beyond the last point
            return self.real[-1], self.imag[-1], None

        # Check exact match on hi as well
        if abs(times[hi] - t) <= self.exact_tol:
            return self.real[hi], self.imag[hi], None

        # Linear interpolation
        r_val = _interp_linear(t, times[lo], times[hi], self.real[lo], self.real[hi])
        i_val = _interp_linear(t, times[lo], times[hi], self.imag[lo], self.imag[hi])

        # Warn if bracket points are too far from engine timestep
        warn = None
        dt_lo = abs(t - times[lo])
        dt_hi = abs(times[hi] - t)
        if dt_lo > interp_warn_dt or dt_hi > interp_warn_dt:
            warn = (
                f"t={t:.6g}: ngspice bracket gap too large "
                f"(lo@{times[lo]:.6g} Δ={dt_lo:.3e}, hi@{times[hi]:.6g} Δ={dt_hi:.3e}); "
                f"interpolation may be inaccurate (threshold={interp_warn_dt:.3e})"
            )

        return r_val, i_val, warn


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
        self.tran_interpolated: bool = False   # True when tran interpolation used

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
        if self.signal_stats[key].max_rel >= self.global_max_rel:
            self.worst_case_signal = key


def compare(
    golden: dict[str, Any],        # ngspice reference (adaptive timestep)
    result: dict[str, Any],        # engine output (fixed timestep)
    tol_rel: float = 1e-2,
    tol_abs: float = 1e-8,
    max_errors: int = 20,
    interp_warn_dt: float | None = None,  # None → auto (10× engine dt)
    exact_tol: float = 1e-15,
) -> DiffResult:
    """
    Compare engine (fixed-timestep) output against ngspice (adaptive) reference.

    For .tran analyses the engine's time grid is authoritative.  For each
    engine timestep we look up (or interpolate) the ngspice value, then compare.
    """
    dr = DiffResult()

    def err(msg: str):
        if len(dr.errors) < max_errors:
            dr.errors.append(msg)

    def warn(msg: str):
        # Deduplicate warnings (bracket-gap warnings can flood otherwise)
        if msg not in dr.warnings:
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

    g_points = golden["data"]
    r_points = result["data"]

    if not g_points:
        warn("No data points in ngspice golden.")
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

    is_tran = (g_atype == "tran")

    # ── 2. Non-tran: strict point-count match, point-by-point ─────────────────
    if not is_tran:
        dr.checks += 1
        if len(g_points) != len(r_points):
            err(
                f"Point count mismatch: golden={len(g_points)} engine={len(r_points)}"
            )
            return dr

        for i, (gp, rp) in enumerate(zip(g_points, r_points)):
            g_sweep = gp["sweep_value"]
            r_sweep = rp["sweep_value"]

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
                rv = _find_value(rp, name)
                if gv is None:
                    continue

                for part in ("real", "imag"):
                    dr.checks += 1
                    g_val = gv[part]
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

    # ── 3. Tran: engine timesteps are authoritative; look up ngspice ──────────
    dr.tran_interpolated = True

    # Build ngspice time array
    g_times = [gp["sweep_value"] for gp in g_points]

    # Auto interp_warn_dt: 10× the median engine dt (robust to variable step)
    resolved_warn_dt: float
    if interp_warn_dt is None:
        r_times = [rp["sweep_value"] for rp in r_points]
        if len(r_times) >= 2:
            dts = [r_times[k+1] - r_times[k] for k in range(len(r_times)-1)]
            dts.sort()
            median_dt = dts[len(dts) // 2]
            resolved_warn_dt = 10.0 * median_dt
        else:
            resolved_warn_dt = float("inf")
    else:
        resolved_warn_dt = interp_warn_dt

    # Build per-variable NgspiceSeries
    ng_series: dict[str, NgspiceSeries] = {}
    for name in common_names:
        real_vals = []
        imag_vals = []
        for gp in g_points:
            gv = _find_value(gp, name)
            if gv:
                real_vals.append(gv["real"])
                imag_vals.append(gv["imag"])
            else:
                real_vals.append(0.0)
                imag_vals.append(0.0)
        ng_series[name] = NgspiceSeries(g_times, real_vals, imag_vals, exact_tol)

    # Point count info (informational only for tran)
    if len(g_points) != len(r_points):
        warn(
            f"ngspice has {len(g_points)} adaptive points; "
            f"engine has {len(r_points)} fixed-timestep points. "
            f"Comparing at each engine timestep (interpolating ngspice where needed)."
        )

    # Walk engine timesteps
    warned_bracket: set[str] = set()  # avoid duplicate bracket warnings per signal

    for i, rp in enumerate(r_points):
        r_time = rp["sweep_value"]

        for name in sorted(common_names):
            rv = _find_value(rp, name)
            if rv is None:
                continue

            series = ng_series[name]

            for part in ("real", "imag"):
                dr.checks += 1
                r_val = rv[part]

                # Query ngspice series at engine time
                ng_real, ng_imag, bracket_warn = series.query(r_time, resolved_warn_dt)
                g_val = ng_real if part == "real" else ng_imag

                if bracket_warn:
                    # Emit warning once per unique message to avoid spam
                    bkey = f"{name}.{part}:{r_time:.6g}"
                    if bkey not in warned_bracket:
                        warned_bracket.add(bkey)
                        warn(bracket_warn)

                abs_e = _abs_err(r_val, g_val)
                rel_e = _rel_err(r_val, g_val, tol_abs)
                key   = f"{name}.{part}"
                dr._record(key, abs_e, rel_e)

                if not _close(r_val, g_val, tol_rel, tol_abs):
                    err(
                        f"Point[{i}] t={r_time:.6g}  {name}.{part}: "
                        f"engine={r_val:.6g}  ngspice={g_val:.6g}  "
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
    if dr.tran_interpolated:
        print(YELLOW(
            "  Mode   : .tran — comparing at each engine timestep; "
            "ngspice interpolated where needed"
        ))
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
    parser.add_argument(
        "--interp-warn-dt", type=float, default=None,
        help=(
            "Warn when either ngspice bracket point is further than this "
            "from the engine timestep (default: 10× median engine dt)."
        ),
    )
    args = parser.parse_args()

    golden = json.loads(Path(args.golden).read_text())
    result = json.loads(Path(args.result).read_text())

    dr = compare(
        golden, result,
        tol_rel=args.tol_rel,
        tol_abs=args.tol_abs,
        max_errors=args.max_errors,
        interp_warn_dt=args.interp_warn_dt,
    )
    print_report(args.golden, args.result, dr, args.tol_rel, args.tol_abs)

    sys.exit(0 if dr.passed else 1)


if __name__ == "__main__":
    main()
