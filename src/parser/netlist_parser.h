#pragma once
#include "mna/mna_builder.h"   // Circuit, AnalysisConfig
#include "core/types.h"
#include "components/basic/resistor.h"
#include "components/basic/capacitor.h"
#include "components/basic/inductor.h"
#include "components/basic/voltage_source.h"
#include "components/basic/current_source.h"
#include "components/controlled/controlled_sources.h"
#include "components/nonlinear/diode.h"

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>
#include <cmath>
#include <complex>

namespace CircuitEngine {

// ──────────────────────────────────────────────────────────────────
// Value parsing helpers
// ──────────────────────────────────────────────────────────────────

/// Parse SPICE suffix multipliers: k, meg, g, t, m, u, n, p
inline double parse_value(const std::string& raw)
{
    std::string s = raw;

    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    static const std::pair<std::string, double> suffixes[] = {
        {"meg", 1e6},
        {"t",   1e12},
        {"g",   1e9},
        {"k",   1e3},
        {"m",   1e-3},
        {"u",   1e-6},
        {"n",   1e-9},
        {"p",   1e-12},
    };

    for (const auto& [sfx, mul] : suffixes) {

        if (s.size() > sfx.size() &&
            s.substr(s.size() - sfx.size()) == sfx)
        {
            std::string num =
                s.substr(0, s.size() - sfx.size());

            size_t pos = 0;

            double v = std::stod(num, &pos);

            if (pos != num.size())
                throw std::runtime_error(
                    "Invalid numeric value: " + raw);

            return v * mul;
        }
    }

    size_t pos = 0;

    double v = std::stod(s, &pos);

    if (pos != s.size())
        throw std::runtime_error(
            "Invalid numeric value: " + raw);

    return v;
}

inline bool is_number(const std::string& s)
{
    try { std::stod(s); return true; }
    catch (...) { return false; }
}

inline std::vector<std::string> split(const std::string& line)
{
    std::istringstream ss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

inline std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ──────────────────────────────────────────────────────────────────
// Transient signal builder helpers
// ──────────────────────────────────────────────────────────────────

/// Parse PULSE v1 v2 td tr tf pw per  (7 values after the keyword)
inline TransientSignal parse_pulse(const std::vector<std::string>& tokens, size_t idx)
{
    if (idx + 7 > tokens.size())
        throw std::runtime_error("PULSE requires 7 parameters: v1 v2 td tr tf pw per");

    PulseParams p;
    p.v1  = parse_value(tokens[idx]);
    p.v2  = parse_value(tokens[idx+1]);
    p.td  = parse_value(tokens[idx+2]);
    p.tr  = parse_value(tokens[idx+3]);
    p.tf  = parse_value(tokens[idx+4]);
    p.pw  = parse_value(tokens[idx+5]);
    p.per = parse_value(tokens[idx+6]);

    TransientSignal sig;
    sig.type  = SignalType::PULSE;
    sig.pulse = p;
    return sig;
}

/// Parse SIN vo va freq td theta phase  (6 values after keyword)
inline TransientSignal parse_sin(const std::vector<std::string>& tokens, size_t idx)
{
    if (idx + 6 > tokens.size())
        throw std::runtime_error("SIN requires 6 parameters: vo va freq td theta phase");

    SinParams s;
    s.vo        = parse_value(tokens[idx]);
    s.va        = parse_value(tokens[idx+1]);
    s.freq      = parse_value(tokens[idx+2]);
    s.td        = parse_value(tokens[idx+3]);
    s.theta     = parse_value(tokens[idx+4]);
    s.phase_deg = parse_value(tokens[idx+5]);

    TransientSignal sig;
    sig.type = SignalType::SIN;
    sig.sin  = s;
    return sig;
}

// ──────────────────────────────────────────────────────────────────
// Source (V/I) parser
// ──────────────────────────────────────────────────────────────────

struct SourceSpec {
    double dc_value = 0.0;
    std::complex<double> ac_value = {0.0, 0.0};
    std::optional<TransientSignal> transient;
};

inline SourceSpec parse_source_spec(const std::string& name,
                                    const std::vector<std::string>& tokens,
                                    size_t start)
{
    SourceSpec spec;
    size_t idx = start;

    // Simple: name n1 n2 value  (4 tokens total)
    if (tokens.size() == 4) {
        spec.dc_value = parse_value(tokens[3]);
        return spec;
    }

    while (idx < tokens.size()) {
        std::string key = to_lower(tokens[idx]);

        if (key == "dc") {
            if (idx + 1 >= tokens.size() || !is_number(tokens[idx+1]))
                throw std::runtime_error(name + ": invalid DC value");
            spec.dc_value = parse_value(tokens[idx+1]);
            idx += 2;
        }
        else if (key == "ac") {
            if (idx + 1 >= tokens.size() || !is_number(tokens[idx+1]))
                throw std::runtime_error(name + ": invalid AC magnitude");
            double mag = parse_value(tokens[idx+1]);
            double phase_deg = 0.0;
            if (idx + 2 < tokens.size() && is_number(tokens[idx+2])) {
                phase_deg = parse_value(tokens[idx+2]);
                idx += 3;
            } else {
                idx += 2;
            }
            constexpr double PI = 3.14159265358979323846;
            double phase_rad = phase_deg * PI / 180.0;
            spec.ac_value = mag * std::exp(std::complex<double>{0.0, phase_rad});
        }
        else if (key == "pulse") {
            spec.transient = parse_pulse(tokens, idx + 1);
            idx += 8;  // keyword + 7 params
        }
        else if (key == "sin") {
            spec.transient = parse_sin(tokens, idx + 1);
            idx += 7;  // keyword + 6 params
        }
        else {
            throw std::runtime_error(name + ": unknown token '" + tokens[idx] + "'");
        }
    }
    return spec;
}

// ──────────────────────────────────────────────────────────────────
// Main parser
// ──────────────────────────────────────────────────────────────────

/// Parse a SPICE-like netlist file and populate a Circuit object.
inline Circuit parse_netlist(const std::string& filename)
{
    std::ifstream f(filename);
    if (!f.is_open())
        throw std::runtime_error("Cannot open netlist file: " + filename);

    Circuit circuit;
    std::unordered_set<std::string>    used_names;  // duplicate check
    std::string line;

    // We need unordered_set
    // (add include at top level)

    while (std::getline(f, line)) {
        // Strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.empty() || line[0] == '*') continue; // blank / comment

        auto tokens = split(line);
        if (tokens.empty()) continue;

        std::string name = tokens[0];
        char prefix = static_cast<char>(std::toupper(name[0]));

        // ── Directives (.ac / .tran / .ic / .model) ───────────────
        if (name[0] == '.') {
            std::string dir = to_lower(tokens[0]);

            if (dir == ".ac") {
                // .AC DEC 10 1 1e6
                if (tokens.size() != 5)
                    throw std::runtime_error("Invalid .AC syntax: " + line);

                std::string sweep_str = tokens[1];
                std::transform(sweep_str.begin(), sweep_str.end(), sweep_str.begin(), ::toupper);

                circuit.analysis.type    = AnalysisType::AC;
                circuit.analysis.points  = std::stoi(tokens[2]);
                circuit.analysis.f_start = parse_value(tokens[3]);
                circuit.analysis.f_end   = parse_value(tokens[4]);
                circuit.has_analysis     = true;

                if      (sweep_str == "DEC") circuit.analysis.sweep = SweepType::DEC;
                else if (sweep_str == "LIN") circuit.analysis.sweep = SweepType::LIN;
                else if (sweep_str == "OCT") circuit.analysis.sweep = SweepType::OCT;
                else throw std::runtime_error("Unknown sweep type: " + sweep_str);

                if (circuit.analysis.f_start <= 0)
                    throw std::runtime_error(".AC: f_start must be > 0");
                // Allow f_end == f_start only for single-point sweeps (points == 1)
                if (circuit.analysis.f_end < circuit.analysis.f_start)
                    throw std::runtime_error(".AC: f_end must be >= f_start");
                if (circuit.analysis.f_end == circuit.analysis.f_start && circuit.analysis.points != 1)
                    throw std::runtime_error(".AC: f_end must be > f_start when points > 1");
                if (circuit.analysis.points <= 0)
                    throw std::runtime_error(".AC: points must be > 0");
            }
            else if (dir == ".tran") {
                // .TRAN tstep tstop [tstart [tmax]]
                if (tokens.size() < 3)
                    throw std::runtime_error("Invalid .TRAN syntax: " + line);

                circuit.analysis.type   = AnalysisType::TRAN;
                circuit.analysis.tstep  = parse_value(tokens[1]);
                circuit.analysis.tstop  = parse_value(tokens[2]);
                circuit.analysis.tstart = (tokens.size() >= 4) ? parse_value(tokens[3]) : 0.0;
                circuit.has_analysis    = true;

                if (circuit.analysis.tstep <= 0)
                    throw std::runtime_error(".TRAN: tstep must be > 0");
                if (circuit.analysis.tstop <= 0)
                    throw std::runtime_error(".TRAN: tstop must be > 0");
                if (circuit.analysis.tstop <= circuit.analysis.tstart)
                    throw std::runtime_error(".TRAN: tstop must be > tstart");
            }
            else if (dir == ".ic") {

                if (tokens.size() < 2)
                    throw std::runtime_error("Invalid .IC syntax");

                for (size_t i = 1; i < tokens.size(); ++i) {

                    const std::string& expr = tokens[i];

                    auto eq_pos = expr.find('=');

                    if (eq_pos == std::string::npos)
                        throw std::runtime_error(
                            ".IC requires '=' : " + expr);

                    std::string lhs = expr.substr(0, eq_pos);
                    std::string rhs = expr.substr(eq_pos + 1);

                    double value = parse_value(rhs);

                    // Must be something like V(n1)
                    auto lp = lhs.find('(');
                    auto rp = lhs.find(')');

                    if (lp == std::string::npos ||
                        rp == std::string::npos ||
                        rp <= lp + 1)
                    {
                        throw std::runtime_error(
                            "Invalid .IC target: " + lhs);
                    }

                    char kind = std::toupper(lhs[0]);

                    if (kind != 'V' && kind != 'I')
                        throw std::runtime_error(
                            ".IC only supports V() or I()");

                    std::string target =
                        lhs.substr(lp + 1, rp - lp - 1);

                    circuit.initial_conditions.emplace_back(
                        kind,
                        target,
                        value
                    );
                }
            }
            else if (dir == ".model") {
                // .MODEL DTEST D IS=1e-14 N=1 TEMP=300

                if (tokens.size() < 3)
                    throw std::runtime_error("Invalid .MODEL syntax: " + line);

                std::string mname = tokens[1];
                std::string mtype = tokens[2];

                std::transform(mtype.begin(), mtype.end(),
                            mtype.begin(), ::toupper);

                std::unordered_map<std::string, double> params;

                for (size_t i = 3; i < tokens.size(); ++i) {
                    auto pos = tokens[i].find('=');

                    if (pos == std::string::npos)
                        throw std::runtime_error(
                            ".MODEL parameter must be key=value: " + tokens[i]);

                    std::string key = to_lower(tokens[i].substr(0, pos));
                    std::string val = tokens[i].substr(pos + 1);

                    params[key] = parse_value(val);
                }

                if (mtype == "D") {
                    DiodeModel dm;

                    dm.name = mname;
                    dm.Is   = params.count("is")   ? params["is"]   : 1e-14;
                    dm.n    = params.count("n")    ? params["n"]    : 1.0;
                    dm.temp = params.count("temp") ? params["temp"] : 300.0;

                    circuit.models[mname] = dm;
                }
                else {
                    throw std::runtime_error(
                        "Unsupported model type: " + mtype);
                }
            }
            // ignore unknown directives silently (e.g. .END)
            continue;
        }

        // ── Components ────────────────────────────────────────────
        if (used_names.count(name))
            throw std::runtime_error("Duplicate component name: " + name);
        used_names.insert(name);

        // R / C / L: name n1 n2 value
        if (prefix == 'R' || prefix == 'C' || prefix == 'L') {
            if (tokens.size() != 4)
                throw std::runtime_error("Expected: " + name + " n1 n2 value");
            auto ni = circuit.node_opt(tokens[1]);
            auto nj = circuit.node_opt(tokens[2]);
            double val = parse_value(tokens[3]);

            if      (prefix == 'R') circuit.add_component(std::make_shared<Resistor> (name, ni, nj, val));
            else if (prefix == 'C') circuit.add_component(std::make_shared<Capacitor>(name, ni, nj, val));
            else                    circuit.add_component(std::make_shared<Inductor>  (name, ni, nj, val));
        }
        // V / I: name n1 n2 [DC val] [AC mag [phase]] [PULSE(...)] [SIN(...)]
        else if (prefix == 'V' || prefix == 'I') {
            if (tokens.size() < 4)
                throw std::runtime_error("Invalid source syntax: " + line);
            auto ni = circuit.node_opt(tokens[1]);
            auto nj = circuit.node_opt(tokens[2]);
            auto spec = parse_source_spec(name, tokens, 3);

            if (prefix == 'V')
                circuit.add_component(std::make_shared<VoltageSource>(
                    name, ni, nj, spec.dc_value, spec.ac_value, spec.transient));
            else
                circuit.add_component(std::make_shared<CurrentSource>(
                    name, ni, nj, spec.dc_value, spec.ac_value, spec.transient));
        }
        // G / E: name np nm ncp ncm gain   (VCCS / VCVS)
        else if (prefix == 'G' || prefix == 'E') {
            if (tokens.size() != 6)
                throw std::runtime_error("Expected: " + name + " np nm ncp ncm gain");
            auto np  = circuit.node_opt(tokens[1]);
            auto nm  = circuit.node_opt(tokens[2]);
            auto ncp = circuit.node_opt(tokens[3]);
            auto ncm = circuit.node_opt(tokens[4]);
            double gain = parse_value(tokens[5]);

            if (prefix == 'G')
                circuit.add_component(std::make_shared<VCCS>(name, np, nm, ncp, ncm, gain));
            else
                circuit.add_component(std::make_shared<VCVS>(name, np, nm, ncp, ncm, gain));
        }
        // F / H: name np nm Vctrl gain   (CCCS / CCVS)
        else if (prefix == 'F' || prefix == 'H') {
            if (tokens.size() != 5)
                throw std::runtime_error("Expected: " + name + " np nm Vctrl gain");
            auto np  = circuit.node_opt(tokens[1]);
            auto nm  = circuit.node_opt(tokens[2]);
            std::string vctrl = tokens[3];
            double gain = parse_value(tokens[4]);

            if (prefix == 'F')
                circuit.add_component(std::make_shared<CCCS>(name, np, nm, vctrl, gain));
            else
                circuit.add_component(std::make_shared<CCVS>(name, np, nm, vctrl, gain));
        }
        // D: name anode cathode model_name
        else if (prefix == 'D') {
            if (tokens.size() != 4)
                throw std::runtime_error("Expected: " + name + " anode cathode model_name");
            auto a = circuit.node_opt(tokens[1]);
            auto c = circuit.node_opt(tokens[2]);
            std::string mname = tokens[3];
            if (!circuit.models.count(mname))
                throw std::runtime_error("Unknown model: " + mname);
            circuit.add_component(std::make_shared<Diode>(name, a, c, circuit.models.at(mname)));
        }
        else {
            throw std::runtime_error("Unknown component prefix: " + name);
        }
    }

    return circuit;
}

} // namespace CircuitEngine
