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

    Component* find_sweep_target(const std::string& name)
    {
        for (auto& c : circuit_.components) {
            if (c->name() == name)
                return c.get();
        }
        return nullptr;
    }

    /// Run DC operating point.
    /// Returns a SimulationResult with one DataPoint (sweep_value = 0).
    SimulationResult run()
    {
        SimulationResult result;

        result.analysis_type = AnalysisType::DC;
        result.analysis_type_str = (circuit_.analysis.type == AnalysisType::OP) ? "op" : "dc";
        result.node_map = circuit_.node_map;

        try {
            // .OP
            if (circuit_.analysis.type == AnalysisType::OP) {
                StampContext ctx;
                ctx.mode = SimMode::DC;
                auto [A, z] = builder_.build(ctx);
                Eigen::VectorXcd x = Solver::solve_linear(A, z);
                DataPoint pt;
                pt.sweep_type = "operating_point";
                pt.sweep_value = 0.0;
                for (const auto& [name, idx] : circuit_.node_map) {
                    NodeValue nv;
                    nv.name = name;
                    nv.type = "voltage";
                    nv.real = x(idx).real();
                    pt.values.push_back(std::move(nv));
                }
                auto vs_index = builder_.build_vs_index(SimMode::DC);

                for (const auto& [name, idx] : vs_index) {
                    NodeValue nv;
                    nv.name = name + "#I";
                    nv.type = "current";
                    nv.real = x(idx).real();
                    pt.values.push_back(std::move(nv));
                }
                result.data.push_back(std::move(pt));
            }
            // .DC sweep
            else {
                auto& dc = circuit_.analysis.dc;
                Component* comp = find_sweep_target(dc.target);

                if (!comp)
                    throw std::runtime_error(".DC target not found: " + dc.target);

                auto* vsrc = dynamic_cast<VoltageSource*>(comp);
                auto* isrc = dynamic_cast<CurrentSource*>(comp);

                if (!vsrc && !isrc)
                    throw std::runtime_error(".DC currently supports only V/I sources");

                double value = dc.start;
                auto done = [&]() {
                    if (dc.step > 0)
                        return value > dc.stop + 1e-15;
                    else
                        return value < dc.stop - 1e-15;
                };
                while (!done()) {
                    // set source value
                    if (vsrc)
                        vsrc->set_dc_value(value);
                    if (isrc)
                        isrc->set_dc_value(value);
                    StampContext ctx;
                    ctx.mode = SimMode::DC;
                    auto [A, z] = builder_.build(ctx);
                    Eigen::VectorXcd x = Solver::solve_linear(A, z);
                    DataPoint pt;
                    pt.sweep_type = "dc_sweep";
                    pt.sweep_value = value;
                    // voltages
                    for (const auto& [name, idx] : circuit_.node_map) {
                        NodeValue nv;
                        nv.name = name;
                        nv.type = "voltage";
                        nv.real = x(idx).real();
                        pt.values.push_back(std::move(nv));
                    }
                    // currents
                    auto vs_index = builder_.build_vs_index(SimMode::DC);
                    for (const auto& [name, idx] : vs_index) {
                        NodeValue nv;
                        nv.name = name + "#I";
                        nv.type = "current";
                        nv.real = x(idx).real();
                        pt.values.push_back(std::move(nv));
                    }
                    result.data.push_back(std::move(pt));
                    value += dc.step;
                }
            }
            result.success = true;
        }
        catch (const std::exception& e) {
            result.success = false;
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
