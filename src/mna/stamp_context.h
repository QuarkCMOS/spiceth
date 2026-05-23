#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <Eigen/Dense>

namespace CircuitEngine {

enum class SimMode { DC = 0, AC = 1, TRAN = 2 };

/// Context passed to every Component::stamp() call.
struct StampContext {
    SimMode mode    = SimMode::DC;
    double  omega   = 0.0;   ///< Angular frequency for AC (rad/s)
    double  time    = 0.0;   ///< Current simulation time for TRAN
    double  dt      = 0.0;   ///< Time step for TRAN

    /// Map: component name → row/column index in the MNA matrix
    /// (shared and filled by MNABuilder before stamping)
    const std::unordered_map<std::string, int>* vs_index = nullptr;

    /// Current Newton-iteration guess  (TRAN / nonlinear)
    const Eigen::VectorXd* x      = nullptr;
    /// Solution from previous time step
    const Eigen::VectorXd* x_prev = nullptr;
};

} // namespace CircuitEngine
