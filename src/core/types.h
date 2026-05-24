#pragma once
#include <string>
#include <vector>
#include <complex>
#include <optional>
#include <unordered_map>

namespace CircuitEngine {


//  Enums (serialise cleanly to int for C# P/Invoke / JSON)
enum class AnalysisType {
    NONE,
    OP,
    DC,
    AC,
    TRAN
};
enum class SweepType {
    NONE,
    LIN,
    DEC,
    OCT
};
enum class SignalType {
    DC_CONST = 0,
    PULSE    = 1,
    SIN      = 2
};

// DC Sweep config
struct DCSweep {
    // Example:
    // .dc V1 0 5 0.1
    std::string target;
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
};

// Analysis config
struct AnalysisConfig {
    AnalysisType type = AnalysisType::NONE;
    // AC analysis
    SweepType sweep = SweepType::NONE;
    int points = 0;
    double f_start = 0.0;
    double f_end   = 0.0;

    // TRAN analysis
    double tstep  = 0.0;
    double tstop  = 0.0;
    double tstart = 0.0;

    // DC sweep
    DCSweep dc;
};

//  Simulation result entries  – each carries an explicit "type" tag
//  so that C# can deserialise without reflection tricks.

// One node voltage or branch current at a single time/frequency point.
struct NodeValue {
    std::string name;       //< Node name (e.g. "n1") or branch (e.g. "V1#I")
    std::string type;       //< "voltage" | "current"
    double      real  = 0;  //< Real part (or DC value)
    double      imag  = 0;  //< Imaginary part (0 for DC/TRAN)

    // Convenience
    std::complex<double> asComplex() const { return {real, imag}; }
    double magnitude()  const { return std::abs(asComplex()); }
    double phase_deg()  const { return std::arg(asComplex()) * 180.0 / 3.14159265358979; }
};

// A single point in the simulation sweep (time or frequency).
struct DataPoint {
    std::string  sweep_type;   //< "time" | "frequency"
    double       sweep_value;  //< seconds or Hz
    std::vector<NodeValue> values;
};

// Top-level result returned to the UI layer.
struct SimulationResult {
    bool        success     = false;
    std::string error_msg;
    AnalysisType analysis_type;
    std::string  analysis_type_str;  //<| "op" | "dc" | "ac" | "tran"  (for JSON)
    std::vector<DataPoint> data;

    // Node index → name, useful for the UI to label plots
    std::unordered_map<std::string, int> node_map;
};

//  Transient signal descriptors
struct PulseParams {
    // PULSE(V1 V2 TD TR TF PW PER NP)
    double v1  = 0.0;
    double v2  = 0.0;
    double td  = 0.0;
    double tr  = 0.0;
    double tf  = 0.0;
    double pw  = 0.0;
    double per = 0.0;
    int np = -1; // unlimited = -1
};

struct SinParams {
    // SIN(VO VA FREQ TD THETA PHASE)
    double vo = 0.0;
    double va = 0.0;
    double freq = 0.0;
    double td = 0.0;
    double theta = 0.0;
    double phase_deg = 0.0;
};

struct TransientSignal {
    SignalType type = SignalType::DC_CONST;
    std::optional<PulseParams> pulse;
    std::optional<SinParams>   sin;
};

} // namespace CircuitEngine
