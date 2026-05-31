#pragma once
#include "mna/mna_builder.h"
#include "solver/solver.h"
#include "core/types.h"
#include <stdexcept>

namespace CircuitEngine {

constexpr int    DC_NEWTON_MAX_ITER = 50;
constexpr double DC_NEWTON_TOL      = 1e-9;

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

    SimulationResult run()
    {
        SimulationResult result;
        result.analysis_type     = AnalysisType::DC;
        result.analysis_type_str = (circuit_.analysis.type == AnalysisType::OP) ? "op" : "dc";
        result.node_map          = circuit_.node_map;

        try {
            auto vs_index = builder_.build_vs_index(SimMode::DC);
            int  size     = static_cast<int>(circuit_.node_map.size())
                          + static_cast<int>(vs_index.size());

            // .OP
            if (circuit_.analysis.type == AnalysisType::OP) {
                Eigen::VectorXd x = newton_solve(vs_index, size);
                result.data.push_back(make_point("operating_point", 0.0, x, vs_index));
            }
            // .DC sweep
            else {
                auto& dc   = circuit_.analysis.dc;
                Component* comp = find_sweep_target(dc.target);

                if (!comp)
                    throw std::runtime_error(".DC target not found: " + dc.target);

                auto* vsrc = dynamic_cast<VoltageSource*>(comp);
                auto* isrc = dynamic_cast<CurrentSource*>(comp);

                if (!vsrc && !isrc)
                    throw std::runtime_error(".DC currently supports only V/I sources");

                // Use solution from previous sweep point as warm start
                Eigen::VectorXd x_prev = Eigen::VectorXd::Zero(size);

                double value = dc.start;
                auto done = [&]() {
                    return dc.step > 0 ? value > dc.stop + 1e-15
                                       : value < dc.stop - 1e-15;
                };
                while (!done()) {
                    if (vsrc) vsrc->set_dc_value(value);
                    if (isrc) isrc->set_dc_value(value);

                    Eigen::VectorXd x = newton_solve(vs_index, size, x_prev);
                    result.data.push_back(make_point("dc_sweep", value, x, vs_index));

                    x_prev = x;   // warm start for next point
                    value += dc.step;
                }
            }

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

    /// Newton-Raphson solver for one DC operating point.
    /// x_init: initial guess (zero vector if omitted — used as warm start for DC sweep).
    Eigen::VectorXd newton_solve(
        const std::unordered_map<std::string, int>& vs_index,
        int size,
        Eigen::VectorXd x_guess = Eigen::VectorXd()) const
    {
        if (x_guess.size() == 0)
            x_guess = Eigen::VectorXd::Zero(size);

        for (int iter = 0; iter < DC_NEWTON_MAX_ITER; ++iter) {
            StampContext ctx;
            ctx.mode   = SimMode::DC;
            ctx.x      = &x_guess;
            ctx.x_prev = &x_guess;

            auto [A, z] = builder_.build(ctx);

            // Residual F(x) = A*x - z
            Eigen::VectorXcd res_c = A * x_guess.cast<std::complex<double>>() - z;
            Eigen::VectorXd  res   = res_c.real();

            Eigen::VectorXd dx = Solver::solve_linear_real(A.real(), -res);
            x_guess += dx;

            if (dx.lpNorm<Eigen::Infinity>() < DC_NEWTON_TOL)
                return x_guess;
        }

        throw std::runtime_error("Newton-Raphson did not converge (DC)");
    }

    DataPoint make_point(
        const std::string& sweep_type,
        double sweep_val,
        const Eigen::VectorXd& x,
        const std::unordered_map<std::string, int>& vs_index) const
    {
        DataPoint pt;
        pt.sweep_type  = sweep_type;
        pt.sweep_value = sweep_val;

        for (const auto& [name, idx] : circuit_.node_map) {
            NodeValue nv;
            nv.name = name;
            nv.type = "voltage";
            nv.real = x(idx);
            pt.values.push_back(std::move(nv));
        }
        for (const auto& [name, idx] : vs_index) {
            NodeValue nv;
            nv.name = name + "#I";
            nv.type = "current";
            nv.real = x(idx);
            pt.values.push_back(std::move(nv));
        }
        return pt;
    }
};

} // namespace CircuitEngine