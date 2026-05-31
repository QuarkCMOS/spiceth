"""
utils.py — Parse ngspice ASCII raw files (pure Python), then convert to engine JSON.

ngspice ASCII raw format:
    Title: ...
    Date: ...
    Plotname: AC Analysis
    Flags: complex
    No. Variables: 4
    No. Points: 165
    Variables:
        0   frequency   frequency
        1   v(vin)      voltage
        2   v(vout)     voltage
        3   i(v1)       current
    Values:
        0   1.000000e+01,0.000000e+00
            9.901e-01,-9.891e-02
            ...
        1   1.057e+01,0.000000e+00
            ...

Key syntax rules:
  - Multiple plots in one file are separated by a blank line before the next "Title:" or "Plotname:".
  - Each point block starts with "<index>\\t<sweep_value_real>,<sweep_value_imag>" (complex)
    or "<index>\\t<value>" (real).
  - Subsequent variable values are on separate lines (possibly with leading whitespace).
  - Complex values use "real,imag" comma notation.
  - For binary raw files this parser does NOT apply (binary is rare in practice).

Engine JSON schema (same as CircuitEngine::to_json()):
{
  "success":       true,
  "error_msg":     "",
  "analysis_type": "dc" | "ac" | "tran" | "op",
  "node_map":      {"<name>": <index>, ...},
  "data": [
    {
      "sweep_type":  "time" | "frequency" | "operating_point",
      "sweep_value": float,
      "values": [
        {"name": str, "type": "voltage"|"current", "real": float, "imag": float},
        ...
      ]
    },
    ...
  ]
}
"""

import json
import re
from pathlib import Path
from typing import Any


# ─────────────────────────────────────────────────────────────────────────────
# Helpers: analysis type / name canonicalisation
# ─────────────────────────────────────────────────────────────────────────────

def detect_analysis_type(plotname: str) -> str:
    """Map ngspice plotname → 'dc' | 'ac' | 'tran' | 'op'."""
    p = plotname.lower().strip()
    if p in ("operating point", "op", "op analysis", "operating"):
        return "op"
    if "operating" in p:
        return "op"
    if "dc" in p or "transfer" in p:
        return "dc"
    if p.startswith("transient") or p == "tran" or "transient" in p:
        return "tran"
    if p.startswith("ac") or "ac analysis" in p or " ac " in p or p == "ac":
        return "ac"
    return "unknown"


def _canonical_name(name: str) -> str:
    """
    Normalise ngspice variable names to match engine output.
      v(mid)  → mid
      v(vcc)  → vcc
      i(v1)   → V1#I
    """
    m = re.match(r"^v\((.+)\)$", name, re.IGNORECASE)
    if m:
        return m.group(1).lower()

    m = re.match(r"^i\((.+)\)$", name, re.IGNORECASE)
    if m:
        src = m.group(1).upper()
        return f"{src}#I"

    return name


def _infer_type(name: str) -> str:
    """Infer 'voltage' | 'current' from the trace name."""
    n = name.lower()
    if n.startswith("v(") or n == "frequency" or n == "time":
        return "voltage"
    if n.startswith("i("):
        return "current"
    # ngspice variable section has a type column — used by caller when available
    return "voltage"


def _parse_complex(s: str) -> tuple[float, float]:
    """Parse 'real,imag' or just 'real' into (float, float)."""
    s = s.strip()
    if "," in s:
        parts = s.split(",", 1)
        return float(parts[0]), float(parts[1])
    return float(s), 0.0


# ─────────────────────────────────────────────────────────────────────────────
# Core ASCII raw parser
# ─────────────────────────────────────────────────────────────────────────────

def _parse_ascii_raw(text: str) -> list[dict]:
    """
    Parse an ngspice ASCII raw file and return a list of plot dicts.
    Each plot dict:
      {
        "plotname": str,
        "flags":    "real" | "complex",
        "n_vars":   int,
        "n_points": int,
        "variables": [{"index": int, "name": str, "type": str}, ...],
        "data":     {name: [(re,im), ...] or [float, ...]},
        "has_axis": bool,
      }
    """
    lines = text.splitlines()
    plots = []
    i = 0
    n = len(lines)

    def _next_plot_start(from_i: int) -> int:
        """Find next line that starts a new plot header."""
        for j in range(from_i, n):
            lo = lines[j].strip().lower()
            if lo.startswith("title:") or lo.startswith("plotname:"):
                return j
        return n

    while i < n:
        # Skip blank lines between plots
        while i < n and not lines[i].strip():
            i += 1
        if i >= n:
            break

        # ── Read header key-value pairs until "Variables:" ──────────────────
        plotname  = ""
        flags     = "real"
        n_vars    = 0
        n_points  = 0
        variables = []

        while i < n:
            raw_line = lines[i]
            line = raw_line.strip()
            i += 1

            lo = line.lower()

            if lo.startswith("title:"):
                pass  # ignore title text
            elif lo.startswith("date:"):
                pass
            elif lo.startswith("plotname:"):
                plotname = line[len("plotname:"):].strip()
            elif lo.startswith("flags:"):
                flags = line[len("flags:"):].strip().lower()
            elif lo.startswith("no. variables:"):
                n_vars = int(line[len("no. variables:"):].strip())
            elif lo.startswith("no. points:"):
                n_points = int(line[len("no. points:"):].strip())
            elif lo.startswith("variables:"):
                # Parse variable table — each row is:
                #   <index>\t<name>\t<type>[\t...]
                # ngspice pseudo-variable names to ignore.
                # "all" appears as a spurious row when ngspice's 'write' command
                # is used with no explicit 'save' list; it is a print-flag token,
                # not an actual circuit variable.
                _PSEUDO_VARS = {"all"}

                for _ in range(n_vars):
                    if i >= n:
                        break
                    vline = lines[i].strip()
                    i += 1
                    parts = re.split(r"\s+", vline, maxsplit=3)
                    vidx  = int(parts[0]) if parts else 0
                    vname = parts[1] if len(parts) > 1 else f"var{vidx}"
                    vtype_raw = parts[2].lower() if len(parts) > 2 else "voltage"

                    # Skip ngspice pseudo-variables
                    if vname.lower() in _PSEUDO_VARS:
                        continue

                    if "current" in vtype_raw:
                        vtype = "current"
                    elif "frequency" in vtype_raw:
                        vtype = "voltage"  # sweep axis — treat as voltage for compat
                    else:
                        vtype = "voltage"
                    variables.append({"index": vidx, "name": vname, "type": vtype})
                break  # after Variables: section we expect Values: or Binary:
            elif lo.startswith("values:") or lo.startswith("binary:"):
                # Degenerate: no Variables: header (shouldn't happen, but safe)
                i -= 1
                break

        # Skip blank lines
        while i < n and not lines[i].strip():
            i += 1

        # ── Read Values: or Binary: ──────────────────────────────────────────
        if i >= n:
            break

        section_line = lines[i].strip().lower()
        if section_line.startswith("binary:"):
            # Binary plots: skip to next plot (we can't parse binary here)
            i += 1
            i = _next_plot_start(i)
            continue

        if section_line.startswith("values:"):
            i += 1
        # else: fall through — treat remaining lines as values

        is_complex = "complex" in flags
        n_actual   = n_vars if n_vars > 0 else 1

        # Each data point occupies n_vars lines.
        # The first line of each point is: "<point_index>\t<val0>"
        # (point_index can be 0-based integer)
        # Subsequent lines are "   <val1>", "   <val2>", ...
        raw_vals: list[list[str]] = []   # raw_vals[point][var]

        point_lines: list[str] = []

        # Collect all value lines up to next plot header or EOF
        next_plot = _next_plot_start(i)

        j = i
        while j < next_plot:
            vl = lines[j]
            j += 1
            if not vl.strip():
                continue
            point_lines.append(vl)

        i = next_plot   # advance outer pointer

        # Group point_lines into chunks of n_actual lines per point
        # The first line of each chunk starts with a digit (the point index)
        chunks: list[list[str]] = []
        current_chunk: list[str] = []

        for pl in point_lines:
            stripped = pl.strip()
            # A new point starts when the line begins with an integer index
            # (possibly preceded by whitespace)
            if re.match(r"^\d+\s", stripped) and current_chunk:
                chunks.append(current_chunk)
                current_chunk = []
            current_chunk.append(stripped)

        if current_chunk:
            chunks.append(current_chunk)

        # Now parse each chunk
        data: dict[str, list] = {v["name"]: [] for v in variables}

        for chunk in chunks:
            if not chunk:
                continue
            # chunk[0] = "<index> <val0>"  (index + first variable on same line)
            first = chunk[0]
            # Split index from value: everything after the first whitespace token
            m = re.match(r"^(\d+)\s+(.*)", first)
            if not m:
                continue
            val0_str = m.group(2).strip()

            var_strings = [val0_str] + [c.strip() for c in chunk[1:]]

            for vi, vs in enumerate(var_strings):
                if vi >= len(variables):
                    break
                vname = variables[vi]["name"]
                if is_complex:
                    data[vname].append(_parse_complex(vs))
                else:
                    data[vname].append((float(vs), 0.0))

        # Determine has_axis:
        # OP plots have "operating point" plotname and no meaningful sweep axis.
        atype = detect_analysis_type(plotname)
        has_axis = atype in {"dc", "ac", "tran"}

        plots.append({
            "plotname":  plotname,
            "flags":     "complex" if is_complex else "real",
            "n_vars":    n_vars,
            "n_points":  n_points,
            "variables": variables,
            "data":      data,
            "has_axis":  has_axis,
        })

    return plots


# ─────────────────────────────────────────────────────────────────────────────
# Public API: parse_raw  (replaces spicelib-backed version)
# ─────────────────────────────────────────────────────────────────────────────

def parse_raw(raw_path: str | Path) -> dict[str, Any]:
    """
    Read a ngspice ASCII raw file and return a normalised dict.
    Picks the first non-'constants' plot from multi-plot raw files.
    """
    text = Path(raw_path).read_text(encoding="utf-8", errors="replace")

    plots = _parse_ascii_raw(text)

    if not plots:
        raise RuntimeError(f"No plots found in raw file: {raw_path}")

    # Select the analysis plot, skipping "constants"
    plot = None
    for p in plots:
        if p["plotname"].lower().strip() != "constants":
            plot = p
            break
    if plot is None:
        plot = plots[-1]   # fallback

    return plot


# ─────────────────────────────────────────────────────────────────────────────
# Converter: parse_raw dict → engine JSON dict
# ─────────────────────────────────────────────────────────────────────────────

def raw_to_engine_json(raw: dict[str, Any]) -> dict[str, Any]:
    """
    Convert the dict from parse_raw() to the engine-compatible JSON schema.
    """
    analysis_type = detect_analysis_type(raw["plotname"])
    variables     = raw["variables"]
    data_raw      = raw["data"]
    is_complex    = "complex" in raw["flags"]
    n_points      = raw["n_points"]
    has_axis      = raw.get("has_axis", analysis_type in {"dc", "ac", "tran"})

    # Actual number of parsed points (may differ from header if file is truncated)
    if variables:
        first_name = variables[0]["name"]
        if first_name in data_raw:
            n_points = len(data_raw[first_name])

    if has_axis and variables:
        sweep_var   = variables[0]
        data_vars   = variables[1:]
        sweep_name  = sweep_var["name"].lower()
        if "time" in sweep_name:
            sweep_type = "time"
        elif "freq" in sweep_name:
            sweep_type = "frequency"
        else:
            sweep_type = "operating_point"
        sweep_values = data_raw.get(sweep_var["name"], [])
    else:
        data_vars    = variables
        sweep_type   = "operating_point"
        sweep_values = [(0.0, 0.0)] * n_points

    # Build node_map from data variables
    node_map: dict[str, int] = {}
    for idx, v in enumerate(data_vars):
        node_map[_canonical_name(v["name"])] = idx

    # Build data points
    data_points = []
    for i in range(n_points):
        if i >= len(sweep_values):
            break
        sv = sweep_values[i]
        sweep_val = sv[0] if isinstance(sv, tuple) else float(sv)

        values = []
        for v in data_vars:
            vname    = v["name"]
            vlist    = data_raw.get(vname, [])
            raw_val  = vlist[i] if i < len(vlist) else (0.0, 0.0)
            re_v, im_v = raw_val if isinstance(raw_val, tuple) else (float(raw_val), 0.0)

            values.append({
                "name": _canonical_name(vname),
                "type": v["type"],
                "real": re_v,
                "imag": im_v,
            })

        data_points.append({
            "sweep_type":  sweep_type,
            "sweep_value": sweep_val,
            "values":      values,
        })

    return {
        "success":       True,
        "error_msg":     "",
        "analysis_type": analysis_type,
        "node_map":      node_map,
        "data":          data_points,
    }


# ─────────────────────────────────────────────────────────────────────────────
# Convenience helpers
# ─────────────────────────────────────────────────────────────────────────────

def load_json(path: str | Path) -> dict:
    return json.loads(Path(path).read_text())


def save_json(obj: Any, path: str | Path, indent: int = 2) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(obj, indent=indent))