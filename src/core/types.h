#pragma once
#include <string>
#include <vector>
#include <complex>
#include <optional>
#include <unordered_map>

namespace CircuitEngine {

// ──────────────────────────────────────────────────────────────────
//  Enums (serialise cleanly to int for C# P/Invoke / JSON)
// ──────────────────────────────────────────────────────────────────

enum class AnalysisType { DC = 0, AC = 1, TRAN = 2 };
enum class SweepType    { DEC = 0, LIN = 1, OCT = 2 };
enum class SignalType   { DC_CONST = 0, PULSE = 1, SIN = 2 };

// ──────────────────────────────────────────────────────────────────
//  Simulation result entries  – each carries an explicit "type" tag
//  so that C# can deserialise without reflection tricks.
// ──────────────────────────────────────────────────────────────────

/// One node voltage or branch current at a single time/frequency point.
struct NodeValue {
    std::string name;       ///< Node name (e.g. "n1") or branch (e.g. "V1#I")
    std::string type;       ///< "voltage" | "current"
    double      real  = 0;  ///< Real part (or DC value)
    double      imag  = 0;  ///< Imaginary part (0 for DC/TRAN)

    // Convenience
    std::complex<double> asComplex() const { return {real, imag}; }
    double magnitude()  const { return std::abs(asComplex()); }
    double phase_deg()  const { return std::arg(asComplex()) * 180.0 / 3.14159265358979; }
};

/// A single point in the simulation sweep (time or frequency).
struct DataPoint {
    std::string  sweep_type;   ///< "time" | "frequency"
    double       sweep_value;  ///< seconds or Hz
    std::vector<NodeValue> values;
};

/// Top-level result returned to the UI layer.
struct SimulationResult {
    bool        success     = false;
    std::string error_msg;
    AnalysisType analysis_type;
    std::string  analysis_type_str;  ///< "dc" | "ac" | "tran"  (for JSON)
    std::vector<DataPoint> data;

    // Node index → name, useful for the UI to label plots
    std::unordered_map<std::string, int> node_map;
};

// ──────────────────────────────────────────────────────────────────
//  Transient signal descriptors
// ──────────────────────────────────────────────────────────────────

struct PulseParams {
    double v1, v2, td, tr, tf, pw, per;
};

struct SinParams {
    double vo, va, freq, td, theta, phase_deg;
};

struct TransientSignal {
    SignalType type = SignalType::DC_CONST;
    std::optional<PulseParams> pulse;
    std::optional<SinParams>   sin;
};

} // namespace CircuitEngine
