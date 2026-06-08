#pragma once
#include <string>
#include <Eigen/Dense>
#include "mna/stamp_context.h"

namespace CircuitEngine {

/// Abstract base for every circuit element.
class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;

    const std::string& name() const { return name_; }

    /// Stamp this component into the MNA matrix A and RHS vector z.
    /// For AC analysis A/z are complex; cast via the template helpers below.
    virtual void stamp(Eigen::MatrixXcd& A,
                       Eigen::VectorXcd& z,
                       const StampContext& ctx) const = 0;

    virtual std::string type_name() const = 0;  ///< "Resistor", "Capacitor", …

protected:
    std::string name_;

    // ── Helpers so derived classes don't repeat null-checks ────────
    static void addG(Eigen::MatrixXcd& A,
                     std::optional<int> i, std::optional<int> j,
                     std::complex<double> g)
    {
        if (i) A(i.value(), i.value()) += g;
        if (j) A(j.value(), j.value()) += g;
        if (i && j) {
            A(i.value(), j.value()) -= g;
            A(j.value(), i.value()) -= g;
        }
    }

    static void addCurrentSource(Eigen::VectorXcd& z,
                                 std::optional<int> i, std::optional<int> j,
                                 std::complex<double> I)
    {
        if (i) z(i.value()) -= I;
        if (j) z(j.value()) += I;
    }
};

} // namespace CircuitEngine
