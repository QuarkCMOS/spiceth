#pragma once
#include "components/component.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <optional>
#include <string>

namespace CircuitEngine {

struct DiodeModel {
    std::string name;
    double Is   = 1e-14;
    double n    = 1.0;
    double temp = 300.0;

    double Vt() const {
        constexpr double kB = 1.380649e-23;
        constexpr double q  = 1.602176634e-19;
        return kB * temp / q;
    }
};

class Diode : public Component {
public:
    Diode(std::string name,
          std::optional<int> anode,
          std::optional<int> cathode,
          DiodeModel model)
        : Component(std::move(name)), a_(anode), c_(cathode),
          model_(std::move(model)) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        double Is = model_.Is;
        double n  = model_.n;
        double Vt = model_.Vt();

        double v_a = (a_ && ctx.x) ? (*ctx.x)(a_.value()) : 0.0;
        double v_c = (c_ && ctx.x) ? (*ctx.x)(c_.value()) : 0.0;
        double Vd  = v_a - v_c;

        double v_a_prev = (a_ && ctx.x_prev) ? (*ctx.x_prev)(a_.value()) : 0.0;
        double v_c_prev = (c_ && ctx.x_prev) ? (*ctx.x_prev)(c_.value()) : 0.0;
        double Vd_prev  = v_a_prev - v_c_prev;

        // Voltage limiting: clamp Newton step
        //double V_LIMIT = 3.0 * n * Vt;
        double V_LIMIT = 0.5;
        double dv = std::clamp(Vd - Vd_prev, -V_LIMIT, V_LIMIT);
        Vd = Vd_prev + dv;

        // Exponential clamping (SPICE Vcrit)
        double V_CRIT = n * Vt * std::log(n * Vt / (std::sqrt(2.0) * Is));
        double arg;
        if (Vd > V_CRIT) {
            double exp_crit = std::exp(V_CRIT / (n * Vt));
            arg = exp_crit * (1.0 + (Vd - V_CRIT) / (n * Vt));
        } else {
            arg = std::exp(std::clamp(Vd / (n * Vt), -500.0, 500.0));
        }

        double Id  = Is * (arg - 1.0);
        double Gd  = std::max(Is / (n * Vt) * arg, 1e-12);
        double Ieq = Id - Gd * Vd;

        // Stamp conductance
        if (a_) A(a_.value(), a_.value()) += Gd;
        if (c_) A(c_.value(), c_.value()) += Gd;
        if (a_ && c_) {
            A(a_.value(), c_.value()) -= Gd;
            A(c_.value(), a_.value()) -= Gd;
        }

        // Stamp Norton current
        if (a_) z(a_.value()) -= Ieq;
        if (c_) z(c_.value()) += Ieq;
    }

    std::string type_name() const override { return "Diode"; }
    const DiodeModel& model() const { return model_; }

private:
    std::optional<int> a_, c_;
    DiodeModel model_;
};

} // namespace CircuitEngine
