/**
 * circuit_wasm.cpp  –  Emscripten binding for CircuitEngine.
 *
 * Exposes a single JS-callable function:
 *
 *   Module.simulate(netlistText: string): string   → JSON result
 *
 * Build (requires Emscripten SDK ≥ 3.1):
 *
 *   em++ circuit_wasm.cpp \
 *     -I. \
 *     -I/path/to/eigen \
 *     -O3 \
 *     -std=c++20 \
 *     -s WASM=1 \
 *     -s MODULARIZE=1 \
 *     -s EXPORT_NAME="CircuitEngineModule" \
 *     -s EXPORTED_FUNCTIONS='["_simulate_json"]' \
 *     -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","allocateUTF8","_free"]' \
 *     -s ALLOW_MEMORY_GROWTH=1 \
 *     -s NO_EXIT_RUNTIME=1 \
 *     --no-entry \
 *     -o circuit_engine.js
 *
 * OR use the Embind path (simpler from JS):
 *
 *   Add  -lembind  and the EMSCRIPTEN_BINDINGS block below becomes active.
 *
 * No filesystem (MEMFS, NODEFS, IDBFS) is mounted.
 * No pthreads, no sockets, no WinAPI.
 */

#include "src/engine.h"
#include "src/json_export.h"

#include <emscripten/emscripten.h>
#include <emscripten/bind.h>

using namespace CircuitEngine;

// ── Low-level C export (usable via ccall/cwrap from JS) ──────────────────────

/**
 * Simulate a netlist supplied as a UTF-8 C string.
 * Returns a heap-allocated UTF-8 JSON string; the caller must free() it.
 *
 * JS usage via cwrap:
 *   const simulate_json = Module.cwrap('simulate_json', 'string', ['string']);
 *   const json = simulate_json(netlistText);
 */
extern "C" {

EMSCRIPTEN_KEEPALIVE
char* simulate_json(const char* netlist_cstr)
{
    std::string content(netlist_cstr ? netlist_cstr : "");
    SimulationResult res = simulate_string(content);
    std::string json = to_json(res);

    // Allocate on the WASM heap so JS can free() it
    char* buf = static_cast<char*>(malloc(json.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, json.c_str(), json.size() + 1);
    return buf;
}

} // extern "C"

// ── Embind bindings (simpler, cleaner API from JS/TS) ────────────────────────
// Requires linking with -lembind

EMSCRIPTEN_BINDINGS(circuit_engine) {
    /**
     * JS/TS API:
     *
     *   import createModule from './circuit_engine.js';
     *
     *   const Module = await createModule();
     *   const json: string = Module.simulate(netlistText);
     *   const result = JSON.parse(json);
     */
    emscripten::function("simulate",
        emscripten::optional_override([](const std::string& netlist) -> std::string {
            SimulationResult res = simulate_string(netlist);
            return to_json(res);
        })
    );
}
