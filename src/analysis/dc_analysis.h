#pragma once
#include "mna/mna_builder.h"
#include "solver/solver.h"
#include "core/types.h"
#include <stdexcept>

namespace CircuitEngine {

class DCAnalysis {
public:
    DCAnalysis(Circuit& circuit, MNABuilder& builder, Solver& solver)
        : circuit_(circuit), builder_(builder), solver_(solver) {}

    /// Run DC operating point.
    /// Returns a SimulationResult with one DataPoint (sweep_value = 0).
    SimulationResult run()
    {
        SimulationResult result;
        result.analysis_type     = AnalysisType::DC;
        result.analysis_type_str = "dc";
        result.node_map          = circuit_.node_map;

        try {
            StampContext ctx;
            ctx.mode = SimMode::DC;

            auto [A, z] = builder_.build(ctx);
            Eigen::VectorXcd x = Solver::solve_linear(A, z);

            DataPoint pt;
            pt.sweep_type  = "operating_point";
            pt.sweep_value = 0.0;

            // Node voltages
            for (const auto& [name, idx] : circuit_.node_map) {
                NodeValue nv;
                nv.name = name;
                nv.type = "voltage";
                nv.real = x(idx).real();
                nv.imag = 0.0;
                pt.values.push_back(std::move(nv));
            }

            // Branch currents (voltage sources / inductors)
            auto vs_index = builder_.build_vs_index(SimMode::DC);
            for (const auto& [name, idx] : vs_index) {
                NodeValue nv;
                nv.name = name + "#I";
                nv.type = "current";
                nv.real = x(idx).real();
                nv.imag = 0.0;
                pt.values.push_back(std::move(nv));
            }

            result.data.push_back(std::move(pt));
            result.success = true;
        }
        catch (const std::exception& e) {
            result.success   = false;
            result.error_msg = e.what();
        }

        return result;
    }

private:
    Circuit&    circuit_;
    MNABuilder& builder_;
    Solver&     solver_;
};

} // namespace CircuitEngine
