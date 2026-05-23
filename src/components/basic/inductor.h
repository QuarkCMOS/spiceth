#pragma once
#include "components/component.h"
#include <optional>
#include <stdexcept>
#include <complex>

namespace CircuitEngine {

class Inductor : public Component {
public:
    Inductor(std::string name,
             std::optional<int> node_i,
             std::optional<int> node_j,
             double inductance)
        : Component(std::move(name)), i_(node_i), j_(node_j), L_(inductance) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        switch (ctx.mode) {
        case SimMode::DC:
            // Short-circuit: model as voltage source with V=0
            stampVoltageRows(A, z, ctx, 0.0);
            break;

        case SimMode::AC: {
            // Admittance  Y = 1 / (j*omega*L)
            std::complex<double> Yl = 1.0 / (std::complex<double>{0.0, ctx.omega * L_});
            addG(A, i_, j_, Yl);
            break;
        }

        case SimMode::TRAN: {
            // Backward-Euler: i_L is a state variable (extra row k)
            if (!ctx.vs_index || ctx.vs_index->find(name_) == ctx.vs_index->end())
                throw std::runtime_error(name_ + ": Inductor not indexed");
            int k = ctx.vs_index->at(name_);
            double g = L_ / ctx.dt;
            double i_prev = ctx.x_prev ? (*ctx.x_prev)(k) : 0.0;

            stampVoltageRows(A, z, ctx, 0.0);
            A(k, k) -= g;
            z(k)    -= g * i_prev;
            break;
        }
        }
    }

    std::string type_name() const override { return "Inductor"; }
    double inductance() const { return L_; }

private:
    std::optional<int> i_, j_;
    double L_;

    void stampVoltageRows(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
                          const StampContext& ctx, double V) const
    {
        if (!ctx.vs_index || ctx.vs_index->find(name_) == ctx.vs_index->end())
            throw std::runtime_error(name_ + ": Inductor not indexed");
        int k = ctx.vs_index->at(name_);
        if (i_) { A(i_.value(), k) += 1.0; A(k, i_.value()) += 1.0; }
        if (j_) { A(j_.value(), k) -= 1.0; A(k, j_.value()) -= 1.0; }
        z(k) += V;
    }
};

} // namespace CircuitEngine
