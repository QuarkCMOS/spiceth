#pragma once
/**
 * engine.h  –  Single public header for CircuitEngine (WASM build).
 *
 * CHANGES from original:
 *   - simulate_file(path)  →  REMOVED (no filesystem in WASM).
 *   - simulate_string(content) added: takes raw netlist text.
 *   - simulate(circuit, builder, solver) unchanged.
 *
 * Typical usage from the WASM binding layer (circuit_wasm.cpp):
 *
 *   #include "engine.h"
 *   using namespace CircuitEngine;
 *
 *   SimulationResult res = simulate_string(netlist_text);
 *   std::string json = to_json(res);
 */

#include "core/types.h"
#include "mna/stamp_context.h"
#include "mna/mna_builder.h"
#include "solver/solver.h"
#include "parser/netlist_parser.h"
#include "analysis/dc_analysis.h"
#include "analysis/ac_analysis.h"
#include "analysis/tran_analysis.h"

namespace CircuitEngine {

/// Convenience wrapper: selects the correct analysis based on circuit.analysis.type.
inline SimulationResult simulate(Circuit& circuit,
                                  MNABuilder& builder,
                                  Solver& solver)
{
    switch (circuit.analysis.type) {
    case AnalysisType::OP:
    case AnalysisType::DC:
        return DCAnalysis(circuit, builder, solver).run();
    case AnalysisType::AC:
        return ACAnalysis(circuit, builder, solver).run();
    case AnalysisType::TRAN:
        return TransientAnalysis(circuit, builder, solver).run();
    default: {
        SimulationResult err;
        err.success   = false;
        err.error_msg = "Unknown analysis type";
        return err;
    }
    }
}

/**
 * Parse netlist from a string (raw text content) and run simulation.
 * This is the primary entry point for WASM / web usage.
 * The caller (JS/TS) must load the netlist text and pass it in.
 */
inline SimulationResult simulate_string(const std::string& netlist_content)
{
    try {
        Circuit    circuit = parse_netlist_string(netlist_content);
        MNABuilder builder(circuit);
        Solver     solver;
        return simulate(circuit, builder, solver);
    }
    catch (const std::exception& e) {
        SimulationResult err;
        err.success   = false;
        err.error_msg = e.what();
        return err;
    }
}

} // namespace CircuitEngine
