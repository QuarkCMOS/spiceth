#pragma once
#include "components/component.h"
#include "components/basic/voltage_source.h"
#include "components/basic/inductor.h"
#include "components/controlled/controlled_sources.h"
#include "core/types.h"
#include "mna/stamp_context.h"
#include "components/nonlinear/diode.h"

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <tuple>
#include <stdexcept>
#include <Eigen/Dense>

namespace CircuitEngine {

// ──────────────────────────────────────────────────────────────────
//  Circuit – holds topology + parsed directives
// ──────────────────────────────────────────────────────────────────
struct AnalysisConfig {
    AnalysisType type  = AnalysisType::DC;
    // DC: nothing extra
    // AC
    SweepType sweep    = SweepType::DEC;
    int       points   = 10;
    double    f_start  = 1.0;
    double    f_end    = 1e6;
    // TRAN
    double    tstep    = 1e-6;
    double    tstop    = 1e-3;
    double    tstart   = 0.0;
};

using InitialCondition = std::tuple<char, std::string, double>; // (kind, name, value)

class Circuit {
public:
    std::unordered_map<std::string, int> node_map;
    std::vector<std::shared_ptr<Component>> components;
    AnalysisConfig analysis;
    bool has_analysis = false;
    std::vector<InitialCondition> initial_conditions;
    std::unordered_map<std::string, DiodeModel> models;

    int get_or_create_node(const std::string& name) {
        if (name == "GND" || name == "0") return -1; // GND sentinel
        auto it = node_map.find(name);
        if (it != node_map.end()) return it->second;
        int idx = static_cast<int>(node_map.size());
        node_map[name] = idx;
        return idx;
    }

    std::optional<int> node_opt(const std::string& name) {
        int idx = get_or_create_node(name);
        if (idx < 0) return std::nullopt;
        return idx;
    }

    void add_component(std::shared_ptr<Component> c) {
        components.push_back(std::move(c));
    }
};

// ──────────────────────────────────────────────────────────────────
//  MNABuilder
// ──────────────────────────────────────────────────────────────────
class MNABuilder {
public:
    explicit MNABuilder(Circuit& circuit) : circuit_(circuit) {}

    /// Build vs_index: assigns extra MNA rows for V/VCVS/CCVS/Inductors.
    std::unordered_map<std::string, int> build_vs_index(SimMode mode) const
    {
        std::unordered_map<std::string, int> vs_index;
        int base = static_cast<int>(circuit_.node_map.size());
        int k = 0;

        for (const auto& comp : circuit_.components) {
            bool is_vs = dynamic_cast<VoltageSource*>(comp.get()) != nullptr
                      || dynamic_cast<VCVS*>(comp.get())         != nullptr
                      || dynamic_cast<CCVS*>(comp.get())         != nullptr;
            bool is_l  = dynamic_cast<Inductor*>(comp.get())     != nullptr;

            if (is_vs || (is_l && (mode == SimMode::DC || mode == SimMode::TRAN))) {
                vs_index[comp->name()] = base + k;
                ++k;
            }
        }
        return vs_index;
    }

    /// Assemble MNA matrices for the given context.
    /// Returns (A, z) as complex; for DC/TRAN the imaginary parts are zero.
    std::pair<Eigen::MatrixXcd, Eigen::VectorXcd>
    build(StampContext& ctx) const
    {
        if (circuit_.components.empty()) {
            throw std::runtime_error(
                "Empty circuit: no components");
        }

        auto vs_index = build_vs_index(ctx.mode);
        ctx.vs_index  = &vs_index;

        int n    = static_cast<int>(circuit_.node_map.size());
        int m    = static_cast<int>(vs_index.size());
        int size = n + m;

        if (size == 0) {
            throw std::runtime_error(
                "Invalid circuit: empty MNA matrix");
        }

        Eigen::MatrixXcd A =
            Eigen::MatrixXcd::Zero(size, size);

        Eigen::VectorXcd z =
            Eigen::VectorXcd::Zero(size);

        for (const auto& comp : circuit_.components)
            comp->stamp(A, z, ctx);

        return {A, z};
    }

private:
    Circuit& circuit_;
};

} // namespace CircuitEngine
