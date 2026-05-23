#pragma once
#include "mna/mna_builder.h"
#include "solver/solver.h"
#include "core/types.h"
#include <cmath>
#include <stdexcept>
#include <vector>
#include <numbers>

constexpr double PI = 3.14159265358979323846;

namespace CircuitEngine {

class ACAnalysis {
public:
    ACAnalysis(Circuit& circuit, MNABuilder& builder, Solver& solver)
        : circuit_(circuit), builder_(builder), solver_(solver) {}

    SimulationResult run()
    {
        SimulationResult result;
        result.analysis_type     = AnalysisType::AC;
        result.analysis_type_str = "ac";
        result.node_map          = circuit_.node_map;

        try {
            const AnalysisConfig& cfg = circuit_.analysis;
            auto freqs = generate_frequencies(cfg);

            for (double f : freqs) {
                double omega = 2.0 * PI * f;

                StampContext ctx;
                ctx.mode  = SimMode::AC;
                ctx.omega = omega;

                auto [A, z] = builder_.build(ctx);
                Eigen::VectorXcd x = Solver::solve_linear(A, z);

                DataPoint pt;
                pt.sweep_type  = "frequency";
                pt.sweep_value = f;

                // Node voltages (complex)
                for (const auto& [name, idx] : circuit_.node_map) {
                    NodeValue nv;
                    nv.name = name;
                    nv.type = "voltage";
                    nv.real = x(idx).real();
                    nv.imag = x(idx).imag();
                    pt.values.push_back(std::move(nv));
                }

                // Branch currents
                auto vs_index = builder_.build_vs_index(SimMode::AC);
                for (const auto& [name, idx] : vs_index) {
                    NodeValue nv;
                    nv.name = name + "#I";
                    nv.type = "current";
                    nv.real = x(idx).real();
                    nv.imag = x(idx).imag();
                    pt.values.push_back(std::move(nv));
                }

                result.data.push_back(std::move(pt));
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

    std::vector<double> generate_frequencies(const AnalysisConfig& cfg) const
    {
        std::vector<double> freqs;
        int    points  = cfg.points;
        double f_start = cfg.f_start;
        double f_end   = cfg.f_end;

        switch (cfg.sweep) {
        case SweepType::DEC: {
            double decades = std::log10(f_end) - std::log10(f_start);
            int    n       = static_cast<int>(points * decades) + 1;
            for (int i = 0; i < n; ++i) {
                double t = static_cast<double>(i) / (n - 1);
                freqs.push_back(std::pow(10.0, std::log10(f_start) + t * decades));
            }
            break;
        }
        case SweepType::LIN: {
            if (points == 1 || f_start == f_end) {
                freqs.push_back(f_start);
            } else {
                for (int i = 0; i < points; ++i) {
                    double t = static_cast<double>(i) / (points - 1);
                    freqs.push_back(f_start + t * (f_end - f_start));
                }
            }
            break;
        }
        case SweepType::OCT: {
            double octaves = std::log2(f_end / f_start);
            int    n       = static_cast<int>(points * octaves) + 1;
            for (int i = 0; i < n; ++i) {
                double t = static_cast<double>(i) / (n - 1);
                freqs.push_back(f_start * std::pow(2.0, t * octaves));
            }
            break;
        }
        }
        return freqs;
    }
};

} // namespace CircuitEngine
