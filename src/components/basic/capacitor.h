#pragma once
#include "components/component.h"
#include <optional>

namespace CircuitEngine {

class Capacitor : public Component {
public:
    Capacitor(std::string name,
              std::optional<int> node_i,
              std::optional<int> node_j,
              double capacitance)
        : Component(std::move(name)), i_(node_i), j_(node_j), C_(capacitance) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        switch (ctx.mode) {
        case SimMode::DC:
            return; // open circuit

        case SimMode::AC: {
            std::complex<double> Yc = {0.0, ctx.omega * C_};
            addG(A, i_, j_, Yc);
            break;
        }

        case SimMode::TRAN: {
            double g = C_ / ctx.dt;
            double v_i_prev = (i_ && ctx.x_prev) ? (*ctx.x_prev)(i_.value()) : 0.0;
            double v_j_prev = (j_ && ctx.x_prev) ? (*ctx.x_prev)(j_.value()) : 0.0;
            double Ieq = g * (v_i_prev - v_j_prev);

            addG(A, i_, j_, {g, 0.0});
            // Norton equivalent current
            if (i_) z(i_.value()) += Ieq;
            if (j_) z(j_.value()) -= Ieq;
            break;
        }
        }
    }

    std::string type_name() const override { return "Capacitor"; }
    double capacitance() const { return C_; }

private:
    std::optional<int> i_, j_;
    double C_;
};

} // namespace CircuitEngine
