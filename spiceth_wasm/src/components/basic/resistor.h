#pragma once
#include "components/component.h"
#include <optional>
#include <stdexcept>

namespace CircuitEngine {

class Resistor : public Component {
public:
    Resistor(std::string name,
             std::optional<int> node_i,
             std::optional<int> node_j,
             double resistance)
        : Component(std::move(name)), i_(node_i), j_(node_j), R_(resistance) {}

    void stamp(Eigen::MatrixXcd& A, Eigen::VectorXcd& z,
               const StampContext& ctx) const override
    {
        if (R_ == 0.0)
            throw std::invalid_argument(name_ + ": Resistance cannot be zero");
        addG(A, i_, j_, {1.0 / R_, 0.0});
    }

    std::string type_name() const override { return "Resistor"; }
    double resistance() const { return R_; }

private:
    std::optional<int> i_, j_;
    double R_;
};

} // namespace CircuitEngine
