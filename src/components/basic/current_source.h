#pragma once
#include "components/component.h"
#include "components/basic/signal_eval.h"
#include "core/types.h"
#include <optional>
#include <complex>

namespace CircuitEngine {

class CurrentSource : public Component {
public:
    CurrentSource(std::string name,
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
        std::complex<double> I;
        switch (ctx.mode) {
        case SimMode::DC:   I = dc_; break;
        case SimMode::AC:   I = ac_; break;
        case SimMode::TRAN:
            I = tran_ ? eval_transient(*tran_, ctx.time) : dc_;
            break;
        }
        // Positive current flows from i to j (out of node i)
        if (i_) z(i_.value()) -= I;
        if (j_) z(j_.value()) += I;
    }

    std::string type_name() const override { return "CurrentSource"; }
    void   set_dc_value(double v) { dc_ = v; }
    double dc_value()       const { return dc_; }

private:
    std::optional<int> i_, j_;
    double dc_;
    std::complex<double> ac_;
    std::optional<TransientSignal> tran_;
};

} // namespace CircuitEngine