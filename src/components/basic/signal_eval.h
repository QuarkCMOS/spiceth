#pragma once
#include "core/types.h"
#include <cmath>
#include <algorithm>

namespace CircuitEngine {

/// Evaluate a transient waveform at time t.
inline double eval_transient(const TransientSignal& sig, double t)
{
    switch (sig.type) {
    case SignalType::DC_CONST:
        return 0.0;

    case SignalType::PULSE: {
        const auto& p = sig.pulse.value();
        if (t < p.td) return p.v1;

        double tr  = std::max(p.tr,  1e-15);
        double tf  = std::max(p.tf,  1e-15);
        double t_mod = std::fmod(t - p.td, p.per);

        if      (t_mod < tr)              return p.v1 + t_mod * (p.v2 - p.v1) / tr;
        else if (t_mod < tr + p.pw)       return p.v2;
        else if (t_mod < tr + p.pw + tf)  return p.v2 - (t_mod - (tr + p.pw)) * (p.v2 - p.v1) / tf;
        else                              return p.v1;
    }

    case SignalType::SIN: {
        const auto& s = sig.sin.value();
        constexpr double PI = 3.14159265358979323846;
        double phase_rad = s.phase_deg * PI / 180.0;
        if (t < s.td)
            return s.vo + s.va * std::sin(phase_rad);
        double tau = t - s.td;
        return s.vo + s.va * std::exp(-s.theta * tau) * std::sin(2.0 * PI * s.freq * tau + phase_rad);
    }
    }
    return 0.0;
}

} // namespace CircuitEngine
