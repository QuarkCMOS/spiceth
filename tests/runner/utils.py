"""
utils.py — Parse ngspice binary/ASCII raw files into Python dicts.

ngspice rawfile format (both ASCII and binary):
    Header section (plain text):
        Title: <title>
        Date: <date>
        Plotname: <DC transfer characteristic | AC Analysis | Transient Analysis ...>
        Flags: <real | complex>
        No. Variables: <N>
        No. Points: <M>
        Variables:
            0   time        time        (or frequency, sweep_var ...)
            1   V(n1)       voltage
            2   I(v1)       current
            ...
        Binary:          ← signals start of binary block
        <raw binary data>

Binary layout:
    - If Flags = real:    each point is N doubles (64-bit LE), one per variable
    - If Flags = complex: each point is N * 2 doubles (real, imag pairs)

References:
    ngspice manual §17 (rawfile format)
"""

import struct
import json
import re
from pathlib import Path
from typing import Any


# 
# Low-level raw-file parser
# 

def parse_raw(raw_path: str | Path) -> dict[str, Any]:
    """
    Parse a ngspice .raw file (binary or ASCII).

    Returns a dict:
    {
        "plotname":   str,          # e.g. "DC transfer characteristic"
        "flags":      str,          # "real" or "complex"
        "n_vars":     int,
        "n_points":   int,
        "variables":  [{"index": int, "name": str, "type": str}, ...],
        "data":       {var_name: [values...]},   # float for real, (re,im) for complex
    }
    """
    raw_bytes = Path(raw_path).read_bytes()

    #  1. Split header from binary body 
    # Marker can be "Binary:\n" (Linux) or "Binary:\r\n" (Windows ngspice).
    # Search for the bare keyword first, then consume whatever line ending follows.
    def find_marker(data: bytes, keyword: bytes):
        """Return (start, end) of keyword+line-ending, or (None, None)."""
        idx = data.find(keyword)
        if idx == -1:
            return None, None
        end = idx + len(keyword)
        # consume \r\n or \n
        if end < len(data) and data[end:end+2] == b"\r\n":
            end += 2
        elif end < len(data) and data[end:end+1] == b"\n":
            end += 1
        return idx, end

    bin_start, bin_end = find_marker(raw_bytes, b"Binary:")
    val_start, val_end = find_marker(raw_bytes, b"Values:")

    if bin_start is not None:
        is_binary = True
        header_text = raw_bytes[:bin_start].decode("latin-1")
        body_bytes  = raw_bytes[bin_end:]
    elif val_start is not None:
        is_binary = False
        header_text = raw_bytes[:val_start].decode("latin-1")
        body_text   = raw_bytes[val_end:].decode("latin-1")
    else:
        raise ValueError(f"Cannot find 'Binary:' or 'Values:' marker in {raw_path}")

    #  2. Parse header key-value pairs 
    meta: dict[str, str] = {}
    variables: list[dict] = []
    in_vars = False

    for line in header_text.splitlines():
        line_stripped = line.strip()
        if not line_stripped:
            continue

        if in_vars:
            # Variable lines look like:  "\t0\ttime\ttime"
            # or                         " 0 time time"
            parts = line_stripped.split()
            if len(parts) >= 3 and parts[0].isdigit():
                variables.append({
                    "index": int(parts[0]),
                    "name":  parts[1],
                    "type":  parts[2],
                })
            else:
                in_vars = False  # end of variable block

        if line_stripped.lower().startswith("variables:"):
            in_vars = True
            continue

        # Regular "Key: value" pairs
        if ":" in line_stripped and not in_vars:
            key, _, value = line_stripped.partition(":")
            meta[key.strip().lower()] = value.strip()

    plotname  = meta.get("plotname", "unknown")
    flags     = meta.get("flags", "real").lower()
    n_vars    = int(meta.get("no. variables", len(variables)))
    n_points  = int(meta.get("no. points", 0))
    is_complex = "complex" in flags

    #  3. Decode data 
    data: dict[str, list] = {v["name"]: [] for v in variables}

    if is_binary:
        # Each point: n_vars doubles (real) or n_vars*2 doubles (complex)
        doubles_per_point = n_vars * (2 if is_complex else 1)
        expected_bytes = n_points * doubles_per_point * 8
        if len(body_bytes) < expected_bytes:
            raise ValueError(
                f"Binary data too short: got {len(body_bytes)} bytes, "
                f"expected {expected_bytes}"
            )
        fmt = f"<{doubles_per_point}d"
        stride = doubles_per_point * 8
        for i in range(n_points):
            point = struct.unpack_from(fmt, body_bytes, i * stride)
            for j, var in enumerate(variables):
                if is_complex:
                    data[var["name"]].append((point[j * 2], point[j * 2 + 1]))
                else:
                    data[var["name"]].append(point[j])
    else:
        # ASCII Values: block
        # Format per point:
        #   <index>\t<value0>
        #   \t<value1>
        #   ...
        lines = [l for l in body_text.splitlines() if l.strip()]
        i = 0
        pt_index = 0
        while i < len(lines) and pt_index < n_points:
            # First line of a point starts with the point index
            parts = lines[i].split()
            # parts[0] = point index, parts[1] = value of var 0
            if len(parts) < 2:
                i += 1
                continue
            vals = [parts[1]]
            i += 1
            for _ in range(n_vars - 1):
                if i < len(lines):
                    vals.append(lines[i].strip())
                    i += 1
            for j, var in enumerate(variables):
                raw_val = vals[j]
                if "," in raw_val:  # complex: "re,im"
                    re_s, im_s = raw_val.split(",")
                    data[var["name"]].append((float(re_s), float(im_s)))
                else:
                    data[var["name"]].append(float(raw_val))
            pt_index += 1

    return {
        "plotname":  plotname,
        "flags":     flags,
        "n_vars":    n_vars,
        "n_points":  n_points,
        "variables": variables,
        "data":      data,
    }


# 
# Detect analysis type from plotname
# 

def detect_analysis_type(plotname: str) -> str:
    """Map ngspice plotname → 'dc' | 'ac' | 'tran' | 'op'."""
    p = plotname.lower()
    if "dc" in p or "transfer" in p:
        return "dc"
    if "ac" in p:
        return "ac"
    if "tran" in p:
        return "tran"
    if "operating" in p or "op" in p:
        return "op"
    return "unknown"


# 
# Convert parsed raw → engine-compatible JSON schema
# 

def raw_to_engine_json(raw: dict[str, Any]) -> dict[str, Any]:
    """
    Convert a parsed ngspice raw dict to the same JSON schema as
    CircuitEngine::to_json() produces:

    {
      "success": true,
      "error_msg": "",
      "analysis_type": "dc",
      "node_map": {"vcc": 0, "mid": 1, ...},
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
    analysis_type = detect_analysis_type(raw["plotname"])
    variables = raw["variables"]
    data_raw = raw["data"]
    is_complex = "complex" in raw["flags"]
    n_points = raw["n_points"]

    # First variable is always the sweep axis (time / frequency / sweep_var)
    sweep_var = variables[0]
    sweep_name = sweep_var["name"].lower()  # "time", "frequency", ...

    if "time" in sweep_name:
        sweep_type = "time"
    elif "freq" in sweep_name:
        sweep_type = "frequency"
    else:
        sweep_type = "operating_point"

    # Build node_map from non-sweep variables
    node_map: dict[str, int] = {}
    idx = 0
    for v in variables[1:]:
        node_map[_canonical_name(v["name"])] = idx
        idx += 1

    # Build data points
    data_points = []
    sweep_values = data_raw[sweep_var["name"]]

    for i in range(n_points):
        sweep_val = sweep_values[i]
        if isinstance(sweep_val, tuple):
            sweep_val = sweep_val[0]  # take real part of sweep axis

        values = []
        for v in variables[1:]:
            raw_val = data_raw[v["name"]][i]
            if isinstance(raw_val, tuple):
                re, im = raw_val
            else:
                re, im = float(raw_val), 0.0

            vtype = _infer_type(v["name"], v["type"])
            values.append({
                "name": _canonical_name(v["name"]),
                "type": vtype,
                "real": re,
                "imag": im,
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


def _canonical_name(name: str) -> str:
    """
    Normalise ngspice variable names to match engine output.
    e.g. "v(mid)"  → "mid"
         "v(vcc)"  → "vcc"
         "i(v1)"   → "V1#I"
         "@r1[i]"  → kept as-is (rarely needed)
    """
    m = re.match(r"^v\((.+)\)$", name, re.IGNORECASE)
    if m:
        return m.group(1).lower()

    m = re.match(r"^i\((.+)\)$", name, re.IGNORECASE)
    if m:
        src = m.group(1).upper()
        return f"{src}#I"

    return name


def _infer_type(name: str, ngspice_type: str) -> str:
    """Map ngspice variable type string → 'voltage' | 'current'."""
    t = ngspice_type.lower()
    if "voltage" in t or name.lower().startswith("v("):
        return "voltage"
    if "current" in t or name.lower().startswith("i("):
        return "current"
    return "voltage"  # default


# 
# Convenience helpers
# 

def load_json(path: str | Path) -> dict:
    return json.loads(Path(path).read_text())


def save_json(obj: Any, path: str | Path, indent: int = 2) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(obj, indent=indent))