#pragma once
#include "mna/mna_builder.h"
#include "solver/solver.h"
#include "core/types.h"
#include <stdexcept>
#include <cmath>

namespace CircuitEngine {

constexpr int    NEWTON_MAX_ITER = 50;
constexpr double NEWTON_TOL      = 1e-8;

class TransientAnalysis {
public:
    TransientAnalysis(Circuit& circuit, MNABuilder& builder, Solver& solver)
        : circuit_(circuit), builder_(builder), solver_(solver) {}

    SimulationResult run()
    {
        SimulationResult result;
        result.analysis_type     = AnalysisType::TRAN;
        result.analysis_type_str = "tran";
        result.node_map          = circuit_.node_map;

        try {
            const AnalysisConfig& cfg = circuit_.analysis;

            double tstart = cfg.tstart;
            double tstop  = cfg.tstop;
            double dt     = cfg.tstep;

            // Build vs_index for TRAN (includes inductors)
            auto vs_index = builder_.build_vs_index(SimMode::TRAN);

            int n_nodes = static_cast<int>(circuit_.node_map.size());
            int n_extra = static_cast<int>(vs_index.size());
            int size    = n_nodes + n_extra;

            // Initial condition vector (default = 0)
            // ==========================================================
            // DC Operating Point initialization
            Eigen::VectorXd x_prev = Eigen::VectorXd::Zero(size);
            {
                Eigen::VectorXd x_guess = Eigen::VectorXd::Zero(size);

                bool converged = false;
                for (int iter = 0; iter < NEWTON_MAX_ITER; ++iter) {
                    StampContext ctx;
                    ctx.mode   = SimMode::DC;
                    ctx.x      = &x_guess;
                    ctx.x_prev = &x_guess;

                    auto [A, z] = builder_.build(ctx);

                    Eigen::VectorXcd res_c = A * x_guess.cast<std::complex<double>>() - z;
                    Eigen::VectorXd res = res_c.real();

                    Eigen::MatrixXd A_real = A.real();
                    Eigen::VectorXd dx = Solver::solve_linear_real(A_real, -res);

                    x_guess += dx;

                    if (dx.lpNorm<Eigen::Infinity>() < NEWTON_TOL) {
                        converged = true;
                        break;
                    }
                }

                if (!converged)
                    throw std::runtime_error(
                        "DC operating point failed to converge");

                x_prev = x_guess;
            }

            // Apply .IC directives
            for (const auto& [kind, name, value] : circuit_.initial_conditions) {
                if (kind == 'V') {
                    auto it = circuit_.node_map.find(name);
                    if (it == circuit_.node_map.end())
                        throw std::runtime_error(".IC unknown node: " + name);
                    x_prev(it->second) = value;
                }
                else if (kind == 'I') {
                    auto it = vs_index.find(name);
                    if (it == vs_index.end())
                        throw std::runtime_error(".IC unknown branch: " + name);
                    x_prev(it->second) = value;
                }
            }

            // Record t=0 point
            append_point(result, "time", tstart, x_prev, vs_index);

            // Time stepping (Backward Euler)
            // Build time vector: tstart+dt, tstart+2dt, ..., tstop
            int n_steps = static_cast<int>(std::ceil((tstop - tstart) / dt));
            for (int step = 1; step <= n_steps; ++step) {
                double t = tstart + step * dt;
                if (t > tstop + dt * 1e-9) break; // guard float drift

                // Newton-Raphson solve
                Eigen::VectorXd x_guess = x_prev; // predictor = previous

                bool converged = false;
                for (int iter = 0; iter < NEWTON_MAX_ITER; ++iter) {
                    // Cast x vectors to VectorXd pointers for StampContext
                    StampContext ctx;
                    ctx.mode   = SimMode::TRAN;
                    ctx.time   = t;
                    ctx.dt     = dt;
                    ctx.x      = &x_guess;
                    ctx.x_prev = &x_prev;

                    auto [A, z] = builder_.build(ctx);

                    // Residual F(x) = A*x - z
                    Eigen::VectorXcd res_c = A * x_guess.cast<std::complex<double>>() - z;
                    Eigen::VectorXd  res   = res_c.real();   // imaginary ~0 for TRAN

                    Eigen::MatrixXd A_real = A.real();
                    Eigen::VectorXd dx;
                    try {
                        dx = Solver::solve_linear_real(A_real, -res);
                    } catch (const std::exception& e) {
                        throw std::runtime_error(
                            std::string("Linear solve failed at t=") +
                            std::to_string(t) + ": " + e.what());
                    }

                    x_guess += dx;

                    if (dx.lpNorm<Eigen::Infinity>() < NEWTON_TOL) {
                        converged = true;
                        break;
                    }
                }

                if (!converged)
                    throw std::runtime_error(
                        "Newton-Raphson did not converge at t=" + std::to_string(t));

                x_prev = x_guess;
                append_point(result, "time", t, x_prev, vs_index);
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

    void append_point(SimulationResult& result,
                      const std::string& sweep_type,
                      double sweep_val,
                      const Eigen::VectorXd& x,
                      const std::unordered_map<std::string, int>& vs_index) const
    {
        DataPoint pt;
        pt.sweep_type  = sweep_type;
        pt.sweep_value = sweep_val;

        // Node voltages
        for (const auto& [name, idx] : circuit_.node_map) {
            NodeValue nv;
            nv.name = name;
            nv.type = "voltage";
            nv.real = x(idx);
            nv.imag = 0.0;
            pt.values.push_back(std::move(nv));
        }

        // Branch currents (voltage sources + inductors)
        for (const auto& [name, idx] : vs_index) {
            NodeValue nv;
            nv.name = name + "#I";
            nv.type = "current";
            nv.real = x(idx);
            nv.imag = 0.0;
            pt.values.push_back(std::move(nv));
        }

        result.data.push_back(std::move(pt));
    }
};

} // namespace CircuitEngine
