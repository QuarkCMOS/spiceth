#pragma once
#include "mna/mna_builder.h"
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

// Value parsing helpers

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
            std::string num = s.substr(0, s.size() - sfx.size());
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
        throw std::runtime_error("Invalid numeric value: " + raw);
    return v;
}

inline bool is_number(const std::string& s)
{
    try { std::stod(s); return true; }
    catch (...) { return false; }
}

inline std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    int paren_depth = 0;

    for (char c : line) {
        if (c == '(') ++paren_depth;
        if (c == ')') --paren_depth;

        // Split only outside parentheses
        if (std::isspace(static_cast<unsigned char>(c)) && paren_depth == 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
        else {
            current += c;
        }
    }
    if (!current.empty())
        tokens.push_back(current);

    if (paren_depth != 0)
        throw std::runtime_error("Mismatched parentheses");

    return tokens;
}

inline std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

inline bool looks_like_component(const std::vector<std::string>& tokens)
{
    if (tokens.empty())
        return false;

    char prefix = static_cast<char>(std::toupper(tokens[0][0]));
    size_t n = tokens.size();

    if (prefix == 'R' || prefix == 'C' || prefix == 'L')
        return n == 4;
    if (prefix == 'V' || prefix == 'I')
        return n >= 4;
    if (prefix == 'G' || prefix == 'E')
        return n == 6;
    if (prefix == 'F' || prefix == 'H')
        return n == 5;
    if (prefix == 'D')
        return n == 4;

    return false;
}

// Transient signal builder helpers
// Function-call parser
// Example:
// SIN(0 1 1k)
// PULSE(0 5 1n 1n 1n 10n 20n)

struct FunctionCall {
    std::string name;
    std::vector<std::string> args;
};

inline FunctionCall parse_function(const std::string& text)
{
    auto l = text.find('(');
    auto r = text.rfind(')');

    if (l == std::string::npos || r == std::string::npos || r <= l)
        throw std::runtime_error("Invalid function syntax: " + text);

    FunctionCall f;
    f.name = to_lower(text.substr(0, l));

    std::string inside = text.substr(l + 1, r - l - 1);
    std::stringstream ss(inside);
    std::string tok;

    while (ss >> tok)
        f.args.push_back(tok);

    return f;
}

// Safe optional arg helper
inline double get_arg(
    const std::vector<std::string>& args,
    size_t idx,
    double default_value)
{
    if (idx >= args.size())
        return default_value;
    return parse_value(args[idx]);
}

// Parse PULSE
// PULSE(V1 V2 TD TR TF PW PER NP)
// Required:
//   V1 V2
// Optional:
//   TD  default = 0
//   TR  default = 0
//   TF  default = 0
//   PW  default = 0
//   PER default = 0
//   NP  default = unlimited (-1)
// NOTE: Real ngspice uses TSTEP/TSTOP defaults, but we keep simple defaults here.
inline TransientSignal parse_pulse(const FunctionCall& f)
{
    if (f.args.size() < 2) {
        throw std::runtime_error("PULSE requires at least 2 args:\n"
            "PULSE(V1 V2 [TD] [TR] [TF] [PW] [PER] [NP])");
    }
    PulseParams p;
    p.v1  = get_arg(f.args, 0, 0.0);
    p.v2  = get_arg(f.args, 1, 0.0);
    p.td  = get_arg(f.args, 2, 0.0);
    p.tr  = get_arg(f.args, 3, 0.0);
    p.tf  = get_arg(f.args, 4, 0.0);
    p.pw  = get_arg(f.args, 5, 0.0);
    p.per = get_arg(f.args, 6, 0.0);

    // Optional NP
    // int np = (f.args.size() >= 8) ? static_cast<int>(parse_value(f.args[7])) : -1;

    TransientSignal sig;
    sig.type  = SignalType::PULSE;
    sig.pulse = p;
    return sig;
}

// Parse SIN
// SIN(VO VA FREQ TD THETA PHASE)
// Required:
//   VO VA FREQ
// Optional:
//   TD      default = 0
//   THETA   default = 0
//   PHASE   default = 0
inline TransientSignal parse_sin(const FunctionCall& f)
{
    if (f.args.size() < 3) {
        throw std::runtime_error(
            "SIN requires at least 3 args:\n"
            "SIN(VO VA FREQ [TD] [THETA] [PHASE])");
    }
    SinParams s;
    s.vo        = get_arg(f.args, 0, 0.0);
    s.va        = get_arg(f.args, 1, 0.0);
    s.freq      = get_arg(f.args, 2, 0.0);
    s.td        = get_arg(f.args, 3, 0.0);
    s.theta     = get_arg(f.args, 4, 0.0);
    s.phase_deg = get_arg(f.args, 5, 0.0);

    if (s.freq <= 0.0)
        throw std::runtime_error("SIN frequency must be > 0");

    TransientSignal sig;
    sig.type = SignalType::SIN;
    sig.sin  = s;
    return sig;
}

// Source (V/I) parser
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
    // Simple source:
    // V1 in 0 5
    if (tokens.size() == start + 1) {
        const std::string& tok = tokens[start];
        // Numeric source
        if (tok.find('(') == std::string::npos) {
            spec.dc_value = parse_value(tok);
            return spec;
        }
        // Function source
        FunctionCall f = parse_function(tok);
        if (f.name == "sin")
            spec.transient = parse_sin(f);
        else if (f.name == "pulse")
            spec.transient = parse_pulse(f);
        else
            throw std::runtime_error(name + ": unsupported source function '" + f.name + "'");
        return spec;
    }
    size_t idx = start;
    while (idx < tokens.size()) {
        std::string tok = tokens[idx];
        std::string key = to_lower(tok);
        // DC
        if (key == "dc") {
            if (idx + 1 >= tokens.size())
                throw std::runtime_error(name + ": missing DC value");
            spec.dc_value = parse_value(tokens[idx + 1]);
            idx += 2;
        }
        // AC
        // AC mag
        // AC mag phase
        else if (key == "ac") {
            if (idx + 1 >= tokens.size())
                throw std::runtime_error(name + ": missing AC magnitude");

            double mag = parse_value(tokens[idx + 1]);
            double phase_deg = 0.0;
            bool has_phase = false;

            if (idx + 2 < tokens.size()) {
                std::string next = tokens[idx + 2];
                // If next token is function-like, then it's not AC phase.
                if (next.find('(') == std::string::npos) {
                    has_phase = true;
                    phase_deg = parse_value(next);
                }
            }
            constexpr double PI = 3.14159265358979323846;
            double phase_rad = phase_deg * PI / 180.0;
            spec.ac_value = mag * std::exp(std::complex<double>(0.0, phase_rad));

            idx += has_phase ? 3 : 2;
        }
        // Keyword-style transient (space-separated, ngspice style)
        // SIN  VO VA FREQ [TD [THETA [PHASE]]]
        // PULSE V1 V2 [TD [TR [TF [PW [PER [NP]]]]]]
        else if (key == "sin") {
            FunctionCall f; f.name = "sin";
            size_t i = idx + 1;
            while (i < tokens.size()) {
                std::string t2 = tokens[i];
                std::string k2 = to_lower(t2);
                if (k2 == "dc" || k2 == "ac" || k2 == "pulse" || k2 == "sin") break;
                if (t2.find('(') != std::string::npos) break;
                f.args.push_back(t2);   // raw string, parse_sin will call parse_value
                ++i;
            }
            spec.transient = parse_sin(f);
            idx = i;
        }

        else if (key == "pulse") {
            FunctionCall f; f.name = "pulse";
            size_t i = idx + 1;
            while (i < tokens.size()) {
                std::string t2 = tokens[i];
                std::string k2 = to_lower(t2);
                if (k2 == "dc" || k2 == "ac" || k2 == "pulse" || k2 == "sin") break;
                if (t2.find('(') != std::string::npos) break;
                f.args.push_back(t2);   // raw string
                ++i;
            }
            spec.transient = parse_pulse(f);
            idx = i;
        }

        // Function-style transient (single token with parens)
        // SIN(...)
        // PULSE(...)
        else if (tok.find('(') != std::string::npos) {
            FunctionCall f = parse_function(tok);
            if (f.name == "sin")
                spec.transient = parse_sin(f);
            else if (f.name == "pulse")
                spec.transient = parse_pulse(f);
            else
                throw std::runtime_error(name + ": unsupported source function '" + f.name + "'");

            idx += 1;
        }
        // Unknown token
        else
            throw std::runtime_error(name + ": unknown token '" + tok + "'");
    }

    return spec;
}

// Main parse

/// Parse a SPICE-like netlist file and populate a Circuit object.
inline Circuit parse_netlist(const std::string& filename)
{
    std::ifstream f(filename);
    if (!f.is_open())
        throw std::runtime_error("Cannot open netlist file: " + filename);

    Circuit circuit;
    std::unordered_set<std::string>    used_names;  // duplicate check
    std::string line;

    // unordered_set
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

        // Skip the first non-comment line if it looks like a SPICE title card.
        // In SPICE, the first line may be a free-form title, not a component.
        if (used_names.empty() && name[0] != '.' && name[0] != '*') {
            if (!looks_like_component(tokens))
                continue;
        }
        //  Directives (.op / .dc / .ac / .tran / .ic / .model)
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
            // .OP
            else if (dir == ".op") {
                if (tokens.size() != 1)
                    throw std::runtime_error(".OP takes no arguments");

                circuit.analysis.type = AnalysisType::OP;
                circuit.has_analysis = true;
            }
            // .DC
            // .dc V1 0 10 0.1
            else if (dir == ".dc") {
                if (tokens.size() != 5)
                    throw std::runtime_error(
                        "Invalid .DC syntax:\n"
                        ".DC SRC START STOP STEP");

                circuit.analysis.type = AnalysisType::DC;
                circuit.analysis.dc.target = tokens[1];
                circuit.analysis.dc.start = parse_value(tokens[2]);
                circuit.analysis.dc.stop = parse_value(tokens[3]);
                circuit.analysis.dc.step = parse_value(tokens[4]);
                circuit.has_analysis = true;

                if (circuit.analysis.dc.step == 0.0)
                    throw std::runtime_error(".DC step must not be zero");
                // Prevent infinite sweep direction
                bool forward = circuit.analysis.dc.stop >= circuit.analysis.dc.start;
                if (forward && circuit.analysis.dc.step < 0)
                    throw std::runtime_error(".DC step must be positive");
                if (!forward && circuit.analysis.dc.step > 0)
                    throw std::runtime_error(".DC step must be negative");
            }
            else if (dir == ".ic") {
                if (tokens.size() < 2)
                    throw std::runtime_error("Invalid .IC syntax");

                for (size_t i = 1; i < tokens.size(); ++i) {
                    const std::string& expr = tokens[i];
                    auto eq_pos = expr.find('=');

                    if (eq_pos == std::string::npos)
                        throw std::runtime_error(".IC requires '=' : " + expr);

                    std::string lhs = expr.substr(0, eq_pos);
                    std::string rhs = expr.substr(eq_pos + 1);

                    double value = parse_value(rhs);

                    // Must be something like V(n1)
                    auto lp = lhs.find('(');
                    auto rp = lhs.find(')');

                    if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1)
                        throw std::runtime_error("Invalid .IC target: " + lhs);

                    char kind = std::toupper(lhs[0]);

                    if (kind != 'V' && kind != 'I')
                        throw std::runtime_error(".IC only supports V() or I()");

                    std::string target = lhs.substr(lp + 1, rp - lp - 1);
                    circuit.initial_conditions.emplace_back(kind, target, value);
                }
            }
            else if (dir == ".model") {
                // Accepts both:
                //   .MODEL name D Is=1e-14 n=1.0        (space-separated)
                //   .MODEL name D(Is=1e-14 n=1.0)       (parenthesised, ngspice style)

                if (tokens.size() < 3)
                    throw std::runtime_error("Invalid .MODEL syntax: " + line);

                std::string mname = tokens[1];
                std::string mtype_raw = tokens[2];

                // Strip leading parenthesised form: "D(Is=..." → mtype="D", inject param tokens
                // Check if mtype_raw contains '('
                std::string mtype;
                std::vector<std::string> param_tokens;

                auto paren_pos = mtype_raw.find('(');
                if (paren_pos != std::string::npos) {
                    // e.g. "D(Is=1e-14" → mtype="D", first param fragment = "Is=1e-14"
                    mtype = mtype_raw.substr(0, paren_pos);
                    // Everything after '(' and before ')' across all remaining tokens is params
                    // Rebuild a flat string from tokens[2..] stripping parens
                    std::string flat;
                    for (size_t i = 2; i < tokens.size(); ++i) {
                        flat += " " + tokens[i];
                    }
                    // Remove parens
                    flat.erase(std::remove(flat.begin(), flat.end(), '('), flat.end());
                    flat.erase(std::remove(flat.begin(), flat.end(), ')'), flat.end());
                    // Re-tokenise
                    std::istringstream ss(flat);
                    std::string tok;
                    bool first_tok = true;
                    while (ss >> tok) {
                        if (first_tok) { first_tok = false; continue; } // skip mtype
                        param_tokens.push_back(tok);
                    }
                } else {
                    mtype = mtype_raw;
                    for (size_t i = 3; i < tokens.size(); ++i)
                        param_tokens.push_back(tokens[i]);
                }

                std::transform(mtype.begin(), mtype.end(), mtype.begin(), ::toupper);
                std::unordered_map<std::string, double> params;

                for (const auto& pt : param_tokens) {
                    auto pos = pt.find('=');
                    if (pos == std::string::npos) continue; // ignore stray tokens
                    std::string key = to_lower(pt.substr(0, pos));
                    std::string val = pt.substr(pos + 1);
                    if (!val.empty())
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
                    throw std::runtime_error("Unsupported model type: " + mtype);
                }
            }
            // .END — stop parsing immediately (SPICE standard)
            else if (dir == ".end") {
                break;
            }
            // ignore other unknown directives silently (.probe, .save, .param, ...)
            continue;
        }

        // Components
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

    if (!circuit.has_analysis) {
        circuit.analysis.type = AnalysisType::OP;
        circuit.has_analysis = true;
    }

    return circuit;
}

} // namespace CircuitEngine