#pragma once
#include <Eigen/Dense>
#include <stdexcept>
#include <string>

namespace CircuitEngine {

/// Linear solver wrapping Eigen's LU decomposition.
/// Provides both complex and real variants.
class Solver {
public:
    /// Solve A*x = b  (complex, used for AC and general MNA).
    static Eigen::VectorXcd solve_linear(const Eigen::MatrixXcd& A,
                                         const Eigen::VectorXcd& b)
    {
        Eigen::FullPivLU<Eigen::MatrixXcd> lu(A);
        if (!lu.isInvertible())
            throw std::runtime_error(
                "Singular matrix – check circuit for floating nodes or short circuits");
        return lu.solve(b);
    }

    /// Solve A*x = b  (real, used internally by TRAN Newton loop).
    static Eigen::VectorXd solve_linear_real(const Eigen::MatrixXd& A,
                                              const Eigen::VectorXd& b)
    {
        Eigen::FullPivLU<Eigen::MatrixXd> lu(A);
        if (!lu.isInvertible())
            throw std::runtime_error(
                "Singular matrix – check circuit for floating nodes or short circuits");
        return lu.solve(b);
    }
};

} // namespace CircuitEngine