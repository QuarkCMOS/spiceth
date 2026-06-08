/**
 * circuitEngine.ts  –  TypeScript wrapper around the CircuitEngine WASM module.
 *
 * Place this file in  src/lib/circuitEngine.ts  of your Vite + React project.
 *
 * The WASM binary (circuit_engine.js + circuit_engine.wasm) must be in the
 * Vite  public/wasm/  folder so they are served as static assets.
 *
 * Usage:
 *   import { getEngine, SimulationResult } from '@/lib/circuitEngine';
 *
 *   const engine = await getEngine();
 *   const result: SimulationResult = engine.simulate(netlistText);
 */

// ── JSON schema types (mirror of C++ structs) ─────────────────────────────────

export interface NodeValue {
  name: string;       // node name ("n1") or branch ("V1#I")
  type: "voltage" | "current";
  real: number;
  imag: number;       // 0 for DC/TRAN; non-zero for AC
}

export interface DataPoint {
  sweep_type: "time" | "frequency" | "operating_point" | "dc_sweep";
  sweep_value: number;
  values: NodeValue[];
}

export interface SimulationResult {
  success: boolean;
  error_msg: string;
  analysis_type: "op" | "dc" | "ac" | "tran";
  node_map: Record<string, number>;
  data: DataPoint[];
}

// ── Module singleton ──────────────────────────────────────────────────────────

/** The raw Emscripten module type (trimmed to what we use). */
interface CircuitEngineWasmModule {
  /** Embind binding: simulate(netlistText) → JSON string */
  simulate(netlist: string): string;
}

// Emscripten factory function type
declare function CircuitEngineModule(
  opts?: object
): Promise<CircuitEngineWasmModule>;

let _modulePromise: Promise<CircuitEngineWasmModule> | null = null;

/**
 * Load (and cache) the WASM module.
 * Safe to call multiple times; only loads once.
 *
 * @param wasmUrl  Path to circuit_engine.js (default: /wasm/circuit_engine.js)
 */
export async function loadEngine(
  wasmUrl = "/wasm/circuit_engine.js"
): Promise<CircuitEngine> {
  if (!_modulePromise) {
    _modulePromise = (async () => {
      // Dynamically import the Emscripten-generated JS glue code.
      // Vite treats dynamic imports to ?url or absolute paths as assets.
      const factory = (await import(/* @vite-ignore */ wasmUrl)).default as typeof CircuitEngineModule;
      const mod = await factory({
        // Tell Emscripten where the .wasm file lives (same directory as .js)
        locateFile: (filename: string) => {
          const base = wasmUrl.substring(0, wasmUrl.lastIndexOf("/") + 1);
          return base + filename;
        },
      });
      return mod;
    })();
  }

  const mod = await _modulePromise;
  return new CircuitEngine(mod);
}

/** Convenience: singleton instance (lazy). */
let _engineSingleton: CircuitEngine | null = null;

export async function getEngine(wasmUrl?: string): Promise<CircuitEngine> {
  if (!_engineSingleton) {
    _engineSingleton = await loadEngine(wasmUrl);
  }
  return _engineSingleton;
}

// ── High-level wrapper class ──────────────────────────────────────────────────

export class CircuitEngine {
  private mod: CircuitEngineWasmModule;

  constructor(mod: CircuitEngineWasmModule) {
    this.mod = mod;
  }

  /**
   * Run a simulation on the given netlist text.
   *
   * @param netlist  Raw SPICE-like netlist (the file content as a string).
   * @returns        Parsed SimulationResult.
   * @throws         If the WASM call itself throws (unexpected); simulation
   *                 errors are returned inside result.success = false.
   */
  simulate(netlist: string): SimulationResult {
    const json = this.mod.simulate(netlist);
    return JSON.parse(json) as SimulationResult;
  }

  // ── Convenience helpers ───────────────────────────────────────────────────

  /** Extract all voltage values from the first data point (useful for .OP). */
  static voltages(result: SimulationResult): NodeValue[] {
    if (!result.success || result.data.length === 0) return [];
    return result.data[0].values.filter((v) => v.type === "voltage");
  }

  /** Extract all current values from the first data point. */
  static currents(result: SimulationResult): NodeValue[] {
    if (!result.success || result.data.length === 0) return [];
    return result.data[0].values.filter((v) => v.type === "current");
  }

  /**
   * Get a time-series for a named node (TRAN analysis).
   * Returns { times, values } arrays, both the same length.
   */
  static tranSeries(
    result: SimulationResult,
    nodeName: string
  ): { times: number[]; values: number[] } {
    const times: number[] = [];
    const values: number[] = [];
    for (const pt of result.data) {
      const nv = pt.values.find((v) => v.name === nodeName);
      if (nv !== undefined) {
        times.push(pt.sweep_value);
        values.push(nv.real);
      }
    }
    return { times, values };
  }

  /**
   * Get magnitude (dB) and phase (deg) series for a named node (AC analysis).
   */
  static acBode(
    result: SimulationResult,
    nodeName: string
  ): { freqs: number[]; magnitudes_dB: number[]; phases_deg: number[] } {
    const freqs: number[] = [];
    const magnitudes_dB: number[] = [];
    const phases_deg: number[] = [];

    for (const pt of result.data) {
      const nv = pt.values.find((v) => v.name === nodeName);
      if (nv !== undefined) {
        const mag = Math.sqrt(nv.real ** 2 + nv.imag ** 2);
        freqs.push(pt.sweep_value);
        magnitudes_dB.push(20 * Math.log10(Math.max(mag, 1e-30)));
        phases_deg.push((Math.atan2(nv.imag, nv.real) * 180) / Math.PI);
      }
    }
    return { freqs, magnitudes_dB, phases_deg };
  }
}
