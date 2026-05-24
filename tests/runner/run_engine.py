"""
run_engine.py — Compile (if needed) and run the CircuitEngine on a netlist.

Usage:
    python run_engine.py <netlist.sp> [--out results/foo.json]
                                      [--engine path/to/circuit_engine]
                                      [--eigen  path/to/eigen/]

Eigen (header-only) must be available. Priority order:
  1. --eigen CLI flag
  2. EIGEN_DIR / OS environment variable
  3. Auto-detected from common install locations
  4. Downloaded automatically via pip (eigenpy) or vcpkg if none found
"""

import argparse
import os
import subprocess
import sys
import json
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from utils import save_json

#  paths 
_TESTS_DIR   = Path(__file__).parent.parent
_PROJECT_DIR = _TESTS_DIR.parent
_SRC_DIR     = _PROJECT_DIR / "src"
_BUILD_DIR   = _PROJECT_DIR / "build"
_ENGINE_BIN  = _BUILD_DIR / ("circuit_engine.exe" if sys.platform == "win32" else "circuit_engine")


#  Eigen auto-detection 

def _find_eigen_in_vscode() -> Path | None:
    """Look for Eigen include paths in .vscode/c_cpp_properties.json."""
    vscode_file = _PROJECT_DIR / ".vscode" / "c_cpp_properties.json"
    if not vscode_file.exists():
        return None

    try:
        import json as _json
        data = _json.loads(vscode_file.read_text(encoding="utf-8"))
    except Exception:
        return None

    configs = data.get("configurations", [])
    for cfg in configs:
        paths = cfg.get("includePath", [])
        if isinstance(paths, str):
            paths = [paths]
        for p in paths:
            if not isinstance(p, str):
                continue
            expanded = p.replace("${workspaceFolder}", str(_PROJECT_DIR))
            candidate = Path(expanded)
            if (candidate / "Eigen" / "Dense").exists():
                return candidate
    return None


def find_eigen() -> Path | None:
    """Search common locations for the Eigen include directory."""
    candidates = [
        # Environment variable (highest priority)
        os.environ.get("EIGEN_DIR"),
        # VS Code C/C++ include paths
        str(_find_eigen_in_vscode()) if _find_eigen_in_vscode() is not None else None,
        # Project-local (vendor/eigen or third_party/eigen)
        str(_PROJECT_DIR / "vendor" / "eigen"),
        str(_PROJECT_DIR / "third_party" / "eigen"),
        str(_PROJECT_DIR / "eigen"),
        # vcpkg (Windows typical)
        r"C:\vcpkg\installed\x64-windows\include",
        r"C:\vcpkg\installed\x86-windows\include",
        # Common Linux/macOS paths
        "/usr/include/eigen3",
        "/usr/local/include/eigen3",
        "/opt/homebrew/include/eigen3",
        "/opt/homebrew/opt/eigen/include/eigen3",
    ]

    for c in candidates:
        if c is None:
            continue
        p = Path(c)
        # Valid Eigen dir must contain Eigen/Dense
        if (p / "Eigen" / "Dense").exists():
            return p

    return None


def download_eigen(target_dir: Path) -> Path:
    """Download Eigen headers into project/vendor/eigen/ using pip or direct."""
    eigen_dir = target_dir / "vendor" / "eigen"
    if (eigen_dir / "Eigen" / "Dense").exists():
        return eigen_dir

    print("  [engine] Eigen not found — downloading via pip install eigen ...", flush=True)
    try:
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "eigen", "--quiet"],
            check=True
        )
        # pip eigen installs headers; find them
        result = subprocess.run(
            [sys.executable, "-c",
             "import eigen, pathlib; print(pathlib.Path(eigen.__file__).parent)"],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            pip_dir = Path(result.stdout.strip())
            if (pip_dir / "Eigen" / "Dense").exists():
                return pip_dir
    except Exception:
        pass

    # Fallback: download zip from gitlab
    print("  [engine] pip eigen failed — downloading from gitlab ...", flush=True)
    import urllib.request, zipfile, io
    url = "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip"
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            zdata = resp.read()
        with zipfile.ZipFile(io.BytesIO(zdata)) as zf:
            eigen_dir.mkdir(parents=True, exist_ok=True)
            for member in zf.namelist():
                # extract only Eigen/ subfolder
                parts = Path(member).parts
                if len(parts) >= 2 and parts[1] == "Eigen":
                    rel = Path(*parts[1:])
                    dest = eigen_dir / rel
                    if member.endswith("/"):
                        dest.mkdir(parents=True, exist_ok=True)
                    else:
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        dest.write_bytes(zf.read(member))
        if (eigen_dir / "Eigen" / "Dense").exists():
            print(f"  [engine] Eigen downloaded → {eigen_dir}", flush=True)
            return eigen_dir
    except Exception as e:
        raise RuntimeError(
            f"Could not download Eigen automatically: {e}\n"
            f"Please install Eigen manually and either:\n"
            f"  • Set env var: EIGEN_DIR=C:\\path\\to\\eigen\n"
            f"  • Pass flag:   python run_engine.py ... --eigen C:\\path\\to\\eigen\n"
            f"  • Put headers: {_PROJECT_DIR / 'vendor' / 'eigen' / 'Eigen' / 'Dense'}"
        )

    raise RuntimeError("Eigen download failed.")


#  Build 

def _needs_rebuild(src_dir: Path, binary: Path) -> bool:
    if not binary.exists():
        return True
    bin_mtime = binary.stat().st_mtime
    for ext in ("*.cpp", "*.h"):
        for f in src_dir.rglob(ext):
            if f.stat().st_mtime > bin_mtime:
                return True
    return False


def compile_engine(src_dir: Path, binary: Path, eigen_dir: Path) -> None:
    """Compile with g++ (C++17)."""
    binary.parent.mkdir(parents=True, exist_ok=True)

    cpp_files = sorted(
        f for f in src_dir.rglob("*.cpp")
        if not f.name.startswith("test_")
    )
    if not cpp_files:
        raise FileNotFoundError(f"No .cpp files found under {src_dir}")

    cmd = [
        "g++", "-std=c++17", "-O2", "-Wall",
        f"-I{src_dir}",
        f"-I{eigen_dir}",
        *[str(f) for f in cpp_files],
        "-o", str(binary),
    ]
    print(f"  [engine] Compiling {len(cpp_files)} .cpp file(s) ...", flush=True)
    print(f"  [engine] Eigen    : {eigen_dir}", flush=True)
    print(f"  [engine] Command  : {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("── g++ stderr ──────────────────────────────────")
        print(result.stderr)
        print("────────────────────────────────────────────────")
        raise RuntimeError("Compilation failed")
    print(f"  [engine] Binary   → {binary}", flush=True)


def run_engine(netlist_path: Path, binary: Path) -> dict:
    """Run the engine binary and return parsed JSON dict."""
    cmd = [str(binary), str(netlist_path)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode not in (0, 2):
        print("── engine stderr ───────────────────────────────")
        print(result.stderr)
        print("────────────────────────────────────────────────")
        raise RuntimeError(f"Engine exited with code {result.returncode}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        print("── engine stdout (non-JSON) ─────────────────────")
        print(result.stdout[:500])
        print("────────────────────────────────────────────────")
        raise RuntimeError(f"Engine output is not valid JSON: {e}") from e


def generate_result(
    netlist_path: Path,
    out_path: Path,
    engine_bin: Path | None = None,
    src_dir: Path | None = None,
    eigen_dir: Path | None = None,
) -> dict:
    if src_dir is None:
        src_dir = _SRC_DIR
    if engine_bin is None:
        engine_bin = _ENGINE_BIN

    # Resolve Eigen
    if eigen_dir is None:
        eigen_dir = find_eigen()
    if eigen_dir is None:
        print("  [engine] Eigen not found in standard locations, trying to download ...", flush=True)
        eigen_dir = download_eigen(_PROJECT_DIR)

    if _needs_rebuild(src_dir, engine_bin):
        compile_engine(src_dir, engine_bin, eigen_dir)
    else:
        print(f"  [engine] Binary up-to-date: {engine_bin}", flush=True)

    print(f"  [engine] Running {netlist_path.name} ...", flush=True)
    result = run_engine(netlist_path, engine_bin)
    save_json(result, out_path)
    print(f"  [engine] Result saved → {out_path}")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("netlist")
    parser.add_argument("--out",    "-o", default=None)
    parser.add_argument("--engine", "-e", default=None)
    parser.add_argument("--src",    "-s", default=None)
    parser.add_argument("--eigen",        default=None,
                        help="Path to Eigen include dir (contains Eigen/Dense)")
    args = parser.parse_args()

    netlist_path = Path(args.netlist).resolve()
    if not netlist_path.exists():
        print(f"Error: netlist not found: {netlist_path}", file=sys.stderr)
        sys.exit(1)

    out_path   = Path(args.out)    if args.out    else (_TESTS_DIR / "results" / (netlist_path.stem + ".json"))
    engine_bin = Path(args.engine) if args.engine else None
    src_dir    = Path(args.src)    if args.src    else None
    eigen_dir  = Path(args.eigen)  if args.eigen  else None

    generate_result(netlist_path, out_path, engine_bin, src_dir, eigen_dir)


if __name__ == "__main__":
    main()