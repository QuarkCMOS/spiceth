"""
run_ngspice.py — Run ngspice on a netlist and write golden JSON output.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from utils import parse_raw, raw_to_engine_json, save_json


#  Set this if ngspice is not on PATH 
NGSPICE_BIN = r"E:\ADMIN\AppData\Spice64\bin\ngspice.exe"
# Windows example: NGSPICE_BIN = r"E:\ADMIN\AppData\Spice64\bin\ngspice.exe"


def run_ngspice(netlist_path: Path, raw_path: Path) -> None:
    """Wrap netlist with .control block, invoke ngspice -b, produce raw file."""
    original = netlist_path.read_text(encoding="utf-8", errors="replace")

    # Strip any existing .control/.endc block so we don't conflict
    lines = original.splitlines()
    filtered = []
    in_control = False
    for line in lines:
        stripped = line.strip().lower()
        if stripped.startswith(".control"):
            in_control = True
        elif stripped.startswith(".endc"):
            in_control = False
        elif not in_control:
            filtered.append(line)

    # ngspice 'write' command needs forward slashes, no spaces in path
    raw_str = str(raw_path).replace("\\", "/")

    control_block = (
        "\n"
        ".control\n"
        "run\n"
        f"write {raw_str}\n"
        "quit\n"
        ".endc\n"
    )

    wrapper_content = "\n".join(filtered) + control_block

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".cir", delete=False, encoding="utf-8"
    ) as tmp:
        tmp.write(wrapper_content)
        tmp_path = Path(tmp.name)

    cmd = [NGSPICE_BIN, "-b", str(tmp_path)]
    print(f"  [ngspice] cmd     : {' '.join(cmd)}", flush=True)
    print(f"  [ngspice] netlist : {netlist_path}", flush=True)
    print(f"  [ngspice] raw out : {raw_path}", flush=True)
    print(f"  [ngspice] wrapper :", flush=True)
    for i, l in enumerate(wrapper_content.splitlines(), 1):
        print(f"    {i:3}: {l}")

    try:
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        except FileNotFoundError:
            raise RuntimeError(
                f"ngspice binary not found: {NGSPICE_BIN!r}\n"
                f"  → Set NGSPICE_BIN in run_ngspice.py to the full path.\n"
                f"  → Example: NGSPICE_BIN = r'E:\\ADMIN\\AppData\\Spice64\\bin\\ngspice.exe'"
            )

        # Always print ngspice output so we can see what happened
        print("── ngspice stdout ──────────────────────────────")
        print(result.stdout)
        print("── ngspice stderr ──────────────────────────────")
        print(result.stderr)
        print("────────────────────────────────────────────────")

        raw_size = raw_path.stat().st_size if raw_path.exists() else -1
        print(f"  [ngspice] raw file size: {raw_size} bytes", flush=True)

        if not raw_path.exists() or raw_size == 0:
            raise RuntimeError(
                f"ngspice did not produce a raw file for {netlist_path.name}.\n"
                f"Exit code: {result.returncode}"
            )
    finally:
        tmp_path.unlink(missing_ok=True)


def generate_golden(netlist_path: Path, out_path: Path) -> dict:
    """Full pipeline: netlist → ngspice raw → golden JSON."""
    with tempfile.TemporaryDirectory() as tmpdir:
        raw_path = Path(tmpdir) / "sim.raw"
        print(f"  [ngspice] Running {netlist_path.name} ...", flush=True)
        run_ngspice(netlist_path, raw_path)

        print(f"  [ngspice] Parsing raw file ...", flush=True)
        raw = parse_raw(raw_path)

    golden = raw_to_engine_json(raw)
    save_json(golden, out_path)
    print(f"  [ngspice] Golden saved → {out_path}")
    return golden


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("netlist")
    parser.add_argument("--out", "-o", default=None)
    args = parser.parse_args()

    netlist_path = Path(args.netlist).resolve()
    if not netlist_path.exists():
        print(f"Error: netlist not found: {netlist_path}", file=sys.stderr)
        sys.exit(1)

    out_path = Path(args.out) if args.out else (
        Path(__file__).parent.parent / "golden" / (netlist_path.stem + ".json")
    )
    generate_golden(netlist_path, out_path)


if __name__ == "__main__":
    main()

#  DEBUG HELPER (chạy trực tiếp để dump raw file) 
def dump_raw(raw_path_str: str):
    raw = Path(raw_path_str)
    data = raw.read_bytes()
    print(f"Size: {len(data)} bytes")
    print("=== First 500 bytes as text (latin-1) ===")
    print(data[:500].decode("latin-1", errors="replace"))
    print("=== Hex dump first 200 bytes ===")
    for i in range(0, min(200, len(data)), 16):
        chunk = data[i:i+16]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  {i:04x}: {hex_part:<48}  {asc_part}")

if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "--dump":
    dump_raw(sys.argv[2])