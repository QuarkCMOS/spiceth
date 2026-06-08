em++ E:\File\HUST\projects\spiceth_wasm\circuit_wasm.cpp ^
  -I E:\File\HUST\projects\spiceth_wasm\src ^
  -I D:\ADMIN\eigen-5.0.0 ^
  -O3 ^
  -std=c++20 ^
  -s WASM=1 ^
  -s MODULARIZE=1 ^
  -s EXPORT_NAME=CircuitEngineModule ^
  -s EXPORTED_FUNCTIONS="['_simulate_json','_malloc','_free']" ^
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap']" ^
  -s ALLOW_MEMORY_GROWTH=1 ^
  -s FILESYSTEM=0 ^
  -s NO_EXIT_RUNTIME=1 ^
  -lembind ^
  -o circuit_engine.js





# CircuitEngine – WASM Port

Porting engine C++ sang WebAssembly để chạy trực tiếp trên web (React + Vite).

---

## Những thay đổi so với bản gốc

| File | Thay đổi |
|---|---|
| `src/parser/netlist_parser.h` | **Rewritten**: bỏ `<fstream>`, thay `parse_netlist(path)` bằng `parse_netlist_string(content)` |
| `src/engine.h` | **Rewritten**: bỏ `simulate_file(path)`, thêm `simulate_string(content)` |
| `src/main.cpp` | **Không dùng nữa** (CLI-only, WinAPI, process, console ANSI) |
| `circuit_wasm.cpp` | **Mới**: Emscripten binding layer (C export + Embind) |
| `CMakeLists.txt` | **Mới**: build script cho Emscripten |
| `circuitEngine.ts` | **Mới**: TypeScript wrapper cho React/Vite |

**Tất cả files còn lại** (`types.h`, `solver.h`, `mna_builder.h`, `stamp_context.h`, tất cả components, `json_export.h`, `*_analysis.h`) **không cần sửa** — chúng đã portable hoàn toàn.

---

## Yêu cầu

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) ≥ 3.1
- CMake ≥ 3.20
- Eigen3 (tự động fetch nếu không có)

---

## Build WASM

```bash
# 1. Kích hoạt Emscripten
source /path/to/emsdk/emsdk_env.sh

# 2. Build
mkdir build && cd build
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j4

# Output: build/dist/circuit_engine.js + circuit_engine.wasm
```

---

## Tích hợp vào React + Vite

### 1. Copy WASM output vào Vite project

```bash
cp build/dist/circuit_engine.js  your-vite-app/public/wasm/
cp build/dist/circuit_engine.wasm  your-vite-app/public/wasm/
cp circuitEngine.ts  your-vite-app/src/lib/
```

### 2. Cấu hình Vite (vite.config.ts)

```ts
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  // Không cần config thêm gì — WASM được serve từ public/wasm/
  // Nếu cần tối ưu, thêm:
  optimizeDeps: {
    exclude: ['circuit_engine']
  }
})
```

### 3. Sử dụng trong React component

```tsx
import React, { useState, useEffect } from 'react'
import { getEngine, SimulationResult, CircuitEngine } from '@/lib/circuitEngine'

const DEMO_NETLIST = `
* Simple RC circuit
V1 in 0 DC 5
R1 in out 1k
C1 out 0 1u
.OP
.END
`

export default function SimulatorPage() {
  const [result, setResult] = useState<SimulationResult | null>(null)
  const [netlist, setNetlist] = useState(DEMO_NETLIST.trim())
  const [loading, setLoading] = useState(false)
  const [engineReady, setEngineReady] = useState(false)

  // Pre-load WASM on mount
  useEffect(() => {
    getEngine().then(() => setEngineReady(true))
  }, [])

  const runSimulation = async () => {
    setLoading(true)
    try {
      const engine = await getEngine()
      const res = engine.simulate(netlist)
      setResult(res)
    } finally {
      setLoading(false)
    }
  }

  return (
    <div>
      <textarea
        value={netlist}
        onChange={e => setNetlist(e.target.value)}
        rows={10}
        style={{ width: '100%', fontFamily: 'monospace' }}
      />
      <button onClick={runSimulation} disabled={!engineReady || loading}>
        {loading ? 'Simulating...' : 'Run Simulation'}
      </button>

      {result && !result.success && (
        <p style={{ color: 'red' }}>Error: {result.error_msg}</p>
      )}

      {result?.success && (
        <div>
          <h3>Analysis: {result.analysis_type.toUpperCase()}</h3>
          {CircuitEngine.voltages(result).map(v => (
            <div key={v.name}>V({v.name}) = {v.real.toFixed(6)} V</div>
          ))}
          {CircuitEngine.currents(result).map(c => (
            <div key={c.name}>I({c.name}) = {(c.real * 1000).toFixed(6)} mA</div>
          ))}
        </div>
      )}
    </div>
  )
}
```

---

## API JavaScript/TypeScript

```ts
import { getEngine } from '@/lib/circuitEngine'

const engine = await getEngine()           // load WASM once, cached
const result = engine.simulate(netlistText) // { success, analysis_type, data, ... }

// Helpers
CircuitEngine.voltages(result)             // NodeValue[] từ điểm đầu tiên
CircuitEngine.currents(result)             // NodeValue[]
CircuitEngine.tranSeries(result, 'out')    // { times[], values[] }
CircuitEngine.acBode(result, 'out')        // { freqs[], magnitudes_dB[], phases_deg[] }
```

---

## Tại sao không cần sửa các file còn lại

- **`core/types.h`** — chỉ dùng STL standard (string, vector, complex, optional, unordered_map). ✅
- **`mna/mna_builder.h`** — dùng Eigen (header-only, WASM-compatible). ✅
- **`mna/stamp_context.h`** — chỉ Eigen + STL. ✅
- **`solver/solver.h`** — Eigen LU decomposition. ✅
- **`components/*.h`** — pure math, STL, Eigen. ✅
- **`analysis/*_analysis.h`** — pure math, STL, Eigen. ✅
- **`json_export.h`** — chỉ STL (ostringstream). ✅
- **`html_export.h`** — chỉ STL (ostringstream). ✅ (có thể dùng nếu muốn export HTML từ WASM)



import { useState } from 'react';
import './App.css';

import Sidebar from './components/Sidebar';
import Canvas from './components/Canvas';
import PropertiesPanel from './components/PropertiesPanel';

import type {
  CircuitComponent,
  Wire,
} from './types';

import { generateNetlist } from './utils/netlist';

export default function App() {
  const [components, setComponents] = useState<CircuitComponent[]>([]);
  const [wires, setWires] = useState<Wire[]>([]);
  const [selectedComponent,setSelectedComponent] = useState<CircuitComponent | null>(null);
  const [selectedWire, setSelectedWire] = useState<string | null>(null);
  const [selectedTool, setSelectedTool] = useState<string | null>(null);

  function updateComponent(updated: CircuitComponent) {
    setComponents((prev) =>
      prev.map((c) =>
        c.uuid === updated.uuid
          ? updated
          : c
      )
    );
    setSelectedComponent(updated);
  }

  function deleteSelectedComponent() {
    if (!selectedComponent) return;
    setComponents((prev) =>
      prev.filter(
        (c) =>
          c.uuid !== selectedComponent.uuid
      )
    );
    setSelectedComponent(null);
  }

  function deleteSelectedWire() {
    if (!selectedWire) return;
    setWires((prev) =>
      prev.filter(
        (w) =>
          w.id !== selectedWire
      )
    );
    setSelectedWire(null);
  }

  return (
    <div className="app">
      <Sidebar
        selectedTool={selectedTool}
        setSelectedTool={setSelectedTool}
      />
      <div style={{ flex: 1, minWidth: 0, overflow: 'hidden', display: 'flex' }}>
        <Canvas
          components={components}
          wires={wires}
          selectedComponent={selectedComponent}
          selectedWire={selectedWire}
          selectedTool={selectedTool}
          setSelectedTool={setSelectedTool}
          setComponents={setComponents}
          setWires={setWires}
          setSelectedComponent={setSelectedComponent}
          setSelectedWire={setSelectedWire}
        />
      </div>

      <div className="rightPanel">
        <PropertiesPanel
          selected={selectedComponent}
          onUpdate={updateComponent}
          onDelete={deleteSelectedComponent}
        />
        <div className="netlistPanel">
          <h3>Netlist</h3>
          <textarea
            value={generateNetlist(components,wires)}
            readOnly
          />
          <button
            onClick={deleteSelectedWire}
          >
            Delete Wire
          </button>
        </div>
      </div>
    </div>
  );
}

