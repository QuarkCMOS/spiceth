#pragma once
/**
 * engine.h  –  Single public header for the CircuitEngine.
 *
 * C# / WPF integration options:
 *   A) P/Invoke via a thin C-export DLL (see engine_capi.h).
 *   B) C++/CLI wrapper (include this header from a managed C++ project).
 *   C) JSON bridge: call the engine from a CLI subprocess, parse stdout JSON.
 *
 * Typical usage from C++ (or C++/CLI):
 *
 *   #include "engine.h"
 *   using namespace CircuitEngine;
 *
 *   Circuit circ = parse_netlist("my_circuit.cir");
 *   MNABuilder builder(circ);
 *   Solver     solver;
 *
 *   SimulationResult res = simulate(circ, builder, solver);
 *   if (res.success) { ... }
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

/// Convenience: parse netlist file and run simulation in one call.
inline SimulationResult simulate_file(const std::string& netlist_path)
{
    try {
        Circuit    circuit = parse_netlist(netlist_path);
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
