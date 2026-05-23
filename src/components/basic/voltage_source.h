#pragma once
#include "components/component.h"
#include "components/basic/signal_eval.h"
#include "core/types.h"
#include <optional>
#include <complex>
#include <stdexcept>

namespace CircuitEngine {

class VoltageSource : public Component {
public:
    VoltageSource(std::string name,
                  std::optional<int> node_i,
                  std::optional<int> node_j,
                  double dc_value,
                  std::complex<double> ac_value,
                  std::optional<TransientSignal> transient = std::nullopt)
        : Component(std::move(name)),
          i_(node_i), j_(node_j),
          dc_(dc_value), ac_(ac_value), tran_(std::move(transient)) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        if (!ctx.vs_index || ctx.vs_index->find(name_) == ctx.vs_index->end())
            throw std::runtime_error(name_ + ": VoltageSource not indexed");

        int k = ctx.vs_index->at(name_);
        std::complex<double> V;

        switch (ctx.mode) {
        case SimMode::DC:   V = dc_; break;
        case SimMode::AC:   V = ac_; break;
        case SimMode::TRAN:
            V = tran_ ? eval_transient(*tran_, ctx.time) : dc_;
            break;
        }

        if (i_) { A(i_.value(), k) += 1.0; A(k, i_.value()) += 1.0; }
        if (j_) { A(j_.value(), k) -= 1.0; A(k, j_.value()) -= 1.0; }
        z(k) += V;
    }

    std::string type_name() const override { return "VoltageSource"; }
    double dc_value() const { return dc_; }
    std::complex<double> ac_value() const { return ac_; }

private:
    std::optional<int> i_, j_;
    double dc_;
    std::complex<double> ac_;
    std::optional<TransientSignal> tran_;
};

} // namespace CircuitEngine
