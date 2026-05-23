#pragma once
/**
 * json_export.h  –  Lightweight JSON serialiser for SimulationResult.
 *
 * No external JSON library required; output is always valid UTF-8 JSON.
 * C# side can deserialise with System.Text.Json or Newtonsoft.Json.
 *
 * Schema:
 * {
 *   "success": true,
 *   "error_msg": "",
 *   "analysis_type": "dc",           // "dc" | "ac" | "tran"
 *   "node_map": { "n1": 0, "n2": 1 },
 *   "data": [
 *     {
 *       "sweep_type": "time",         // "time" | "frequency" | "operating_point"
 *       "sweep_value": 1e-6,
 *       "values": [
 *         { "name": "n1", "type": "voltage", "real": 3.3, "imag": 0.0 },
 *         { "name": "V1#I", "type": "current", "real": -0.001, "imag": 0.0 }
 *       ]
 *     }
 *   ]
 * }
 */

#include "core/types.h"
#include <sstream>
#include <string>
#include <cmath>

namespace CircuitEngine {

namespace detail {

inline std::string esc(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n')  { out += "\\n"; }
        else if (c == '\r')  { out += "\\r"; }
        else if (c == '\t')  { out += "\\t"; }
        else out += c;
    }
    out += '"';
    return out;
}

inline std::string num(double v)
{
    if (std::isnan(v))  return "null";
    if (std::isinf(v))  return v > 0 ? "1e308" : "-1e308";
    std::ostringstream os;
    os.precision(17);
    os << v;
    return os.str();
}

} // namespace detail

/// Serialise a SimulationResult to a JSON string.
inline std::string to_json(const SimulationResult& res)
{
    std::ostringstream j;
    j << "{\n";
    j << "  \"success\": " << (res.success ? "true" : "false") << ",\n";
    j << "  \"error_msg\": " << detail::esc(res.error_msg) << ",\n";
    j << "  \"analysis_type\": " << detail::esc(res.analysis_type_str) << ",\n";

    // node_map
    j << "  \"node_map\": {";
    bool first = true;
    for (const auto& [name, idx] : res.node_map) {
        if (!first) j << ", ";
        j << detail::esc(name) << ": " << idx;
        first = false;
    }
    j << "},\n";

    // data array
    j << "  \"data\": [\n";
    for (size_t di = 0; di < res.data.size(); ++di) {
        const auto& pt = res.data[di];
        j << "    {\n";
        j << "      \"sweep_type\": "  << detail::esc(pt.sweep_type)  << ",\n";
        j << "      \"sweep_value\": " << detail::num(pt.sweep_value) << ",\n";
        j << "      \"values\": [\n";
        for (size_t vi = 0; vi < pt.values.size(); ++vi) {
            const auto& nv = pt.values[vi];
            j << "        {"
              << "\"name\": " << detail::esc(nv.name) << ", "
              << "\"type\": " << detail::esc(nv.type) << ", "
              << "\"real\": " << detail::num(nv.real) << ", "
              << "\"imag\": " << detail::num(nv.imag)
              << "}";
            if (vi + 1 < pt.values.size()) j << ",";
            j << "\n";
        }
        j << "      ]\n";
        j << "    }";
        if (di + 1 < res.data.size()) j << ",";
        j << "\n";
    }
    j << "  ]\n";
    j << "}\n";
    return j.str();
}

} // namespace CircuitEngine
