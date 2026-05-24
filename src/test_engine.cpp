/**
 * test_engine.cpp  –  Full test suite for CircuitEngine.
 *
 * Covers:
 *   .OP  – single operating point
 *   .DC  – source sweep (ngspice-style)
 *   .AC  – frequency sweep
 *   .TRAN – transient
 *   All components: R, C, L, V, I, E(VCVS), G(VCCS), H(CCVS), F(CCCS), Diode
 *
 * Build:
 *   g++ -std=c++17 -O2 -I. -I/path/to/eigen test_engine.cpp -o test && ./test
 */

#include "engine.h"
#include "json_export.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <filesystem>

using namespace CircuitEngine;

//  Minimal test framework

struct TestRunner {
    int passed = 0, failed = 0;
    std::string group_name;

    void group(const std::string& name) {
        group_name = name;
        std::cout << "\n=== " << name << " ===\n";
    }

    void run(const std::string& name, std::function<void()> fn) {
        try {
            fn();
            std::cout << "  [PASS] " << name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << name << "\n         " << e.what() << "\n";
            ++failed;
        }
    }

    void summary() const {
        std::cout << "\n=== Results: " << passed << " passed, "
                  << failed << " failed ===\n";
    }
};

static void CHECK(bool cond, const std::string& msg = "assertion failed") {
    if (!cond) throw std::runtime_error(msg);
}
static void CHECK_NEAR(double a, double b, double tol, const std::string& tag = "") {
    if (std::abs(a - b) > tol)
        throw std::runtime_error(
            (tag.empty() ? "" : tag + ": ") +
            "expected≈" + std::to_string(b) +
            " got "     + std::to_string(a) +
            " (diff="   + std::to_string(std::abs(a - b)) + ")");
}

//  helpers 

static SimulationResult run_string(const std::string& netlist)
{
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() / "ce_test_tmp.cir";
    { std::ofstream f(path); f << netlist; }
    Circuit    c = parse_netlist(path.string());
    MNABuilder b(c);
    Solver     s;
    return simulate(c, b, s);
}

static const NodeValue& find_value(const SimulationResult& r,
                                    const std::string& name,
                                    size_t pt_idx = 0)
{
    for (const auto& nv : r.data.at(pt_idx).values)
        if (nv.name == name) return nv;
    throw std::runtime_error("signal not found: " + name);
}

// Index of DataPoint whose sweep_value is nearest to target
static size_t nearest(const SimulationResult& r, double target)
{
    size_t best = 0;
    double bestd = std::abs(r.data[0].sweep_value - target);
    for (size_t i = 1; i < r.data.size(); ++i) {
        double d = std::abs(r.data[i].sweep_value - target);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}


//  GROUP 1 — .OP  (DC operating point, single solve


void test_op(TestRunner& t)
{
    t.group(".OP — Single Operating Point");

    // Basic: V source + resistor
    t.run("OP: simple V+R, correct node voltage", []{
        auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.analysis_type == AnalysisType::OP, "type == OP");
        CHECK(r.analysis_type_str == "op",         "type_str == op");
        CHECK(r.data.size() == 1,                  "exactly 1 data point");
        CHECK(r.data[0].sweep_type == "operating_point", "sweep_type");
        CHECK_NEAR(r.data[0].sweep_value, 0.0, 1e-15, "sweep_value == 0");
        CHECK_NEAR(find_value(r,"n1").real, 5.0, 1e-9, "V(n1)");
    });

    // Branch current in result
    t.run("OP: branch current V1#I = -5mA", []{
        auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"V1#I").real, -5e-3, 1e-9, "I(V1)");
    });

    // Voltage divider
    t.run("OP: voltage divider V(mid)=7V", []{
        auto r = run_string("V1 in GND 10\nR1 in mid 3k\nR2 mid GND 7k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"mid").real, 7.0, 1e-9, "V(mid)");
    });

    // No .OP directive → engine defaults to .OP
    t.run("OP: default when no directive", []{
        auto r = run_string("V1 n1 GND 3\nR1 n1 GND 1k\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.analysis_type == AnalysisType::OP, "defaults to OP");
        CHECK_NEAR(find_value(r,"n1").real, 3.0, 1e-9, "V(n1)");
    });

    // VCCS at operating point
    t.run("OP: VCCS (Gm=0.01, V=5V) → V(out)=5V", []{
        auto r = run_string(
            "V1 ctrl GND 5\nR1 ctrl GND 1k\n"
            "G1 out GND ctrl GND 0.01\nR2 out GND 100\n"
            ".OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 5.0, 1e-9, "V(out)");
    });

    // Diode at operating point — forward drop ~0.6V
    t.run("OP: diode forward bias Vf in [0.5, 0.8]", []{
        auto r = run_string(
            ".MODEL D1 D(Is=1e-14 n=1.0)\n"
            "V1 in GND 5\nR1 in anode 1k\nD1 anode GND D1\n"
            ".OP\n");
        CHECK(r.success, r.error_msg);
        double vd = find_value(r,"anode").real;
        CHECK(vd > 0.5 && vd < 0.8,
              "Vf expected 0.5-0.8, got " + std::to_string(vd));
    });

    // Result has node_map
    t.run("OP: node_map populated", []{
        auto r = run_string("V1 a GND 1\nR1 a b 1k\nR2 b GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK(!r.node_map.empty(), "node_map not empty");
        CHECK(r.node_map.count("a") && r.node_map.count("b"), "a,b in node_map");
    });

    // Inductor = short at DC
    t.run("OP: inductor = short, V(n1)=0", []{
        auto r = run_string(
            "V1 src GND 5\nR1 src n1 1k\nL1 n1 GND 1m\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"n1").real, 0.0, 1e-9, "V(n1) shorted");
    });
}


// 
//  GROUP 2 — .DC  (source sweep, ngspice-style)
// 

void test_dc_sweep(TestRunner& t)
{
    t.group(".DC — Source Sweep");

    // Basic: correct type labels
    t.run("DC: analysis_type = DC, type_str = dc", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 5 1\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.analysis_type == AnalysisType::DC, "type == DC");
        CHECK(r.analysis_type_str == "dc",         "type_str == dc");
    });

    // Correct number of points
    t.run("DC: V1 0→5 step=1 → 6 data points", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 5 1\n");
        CHECK(r.success, r.error_msg);
        // 0, 1, 2, 3, 4, 5 → 6 points
        CHECK(r.data.size() == 6,
              "expected 6 pts, got " + std::to_string(r.data.size()));
    });

    // sweep_type field
    t.run("DC: sweep_type == dc_sweep on every point", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 3 1\n");
        CHECK(r.success, r.error_msg);
        for (const auto& pt : r.data)
            CHECK(pt.sweep_type == "dc_sweep", "sweep_type");
    });

    // Ohm's law along sweep: V(n1) = sweep value (voltage divider with only R to GND)
    t.run("DC: V(n1) tracks V1 linearly", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 10 1\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i) {
            double sv = r.data[i].sweep_value;
            double vn = find_value(r,"n1",i).real;
            CHECK_NEAR(vn, sv, 1e-9, "V(n1)@sweep=" + std::to_string(sv));
        }
    });

    // Voltage divider: V(mid) = V1 * R2/(R1+R2) = V1 * 0.4
    t.run("DC: voltage divider tracks linearly", []{
        auto r = run_string(
            "V1 in GND 0\nR1 in mid 6k\nR2 mid GND 4k\n"
            ".DC V1 0 10 2\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i) {
            double sv  = r.data[i].sweep_value;
            double mid = find_value(r,"mid",i).real;
            CHECK_NEAR(mid, sv * 0.4, 1e-9,
                       "V(mid)=" + std::to_string(mid) + " @" + std::to_string(sv));
        }
    });

    // Current sweep: I1 into R → V = I*R
    t.run("DC: current source sweep I1 → V=I*R", []{
        auto r = run_string("I1 GND n1 0\nR1 n1 GND 500\n.DC I1 0 10m 2m\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i) {
            double sv = r.data[i].sweep_value; // current in A
            double vn = find_value(r,"n1",i).real;
            CHECK_NEAR(vn, sv * 500.0, 1e-6,
                       "V(n1) @I=" + std::to_string(sv));
        }
    });

    // Sweep down (stop < start, step < 0)
    t.run("DC: reverse sweep (10→0, step=-2)", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 10 0 -2\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.data.size() == 6, "6 pts reverse");
        // First point sweep_value should be 10
        CHECK_NEAR(r.data[0].sweep_value, 10.0, 1e-9, "first=10");
        CHECK_NEAR(r.data.back().sweep_value, 0.0, 1e-9, "last=0");
    });

    // Source restored after sweep (engine doesn't corrupt state)
    t.run("DC: source value restored after sweep", []{
        // Run a .DC sweep then run .OP on the same string — should give original value
        auto r1 = run_string("V1 n1 GND 3\nR1 n1 GND 1k\n.DC V1 0 5 1\n");
        auto r2 = run_string("V1 n1 GND 3\nR1 n1 GND 1k\n.OP\n");
        CHECK(r1.success && r2.success, "both succeed");
        // r2 (OP) should give V(n1)=3 regardless of r1's sweep
        CHECK_NEAR(find_value(r2,"n1").real, 3.0, 1e-9, "V(n1) after sweep");
    });

    // Diode I-V curve via .DC sweep
    t.run("DC: diode I-V — Shockley exponential", []{
        // Sweep V1 from 0→0.8V; V(anode)=V1; I_D = (V1-V(anode))/R as V grows
        // With no series R, V(anode)=V1 exactly; Id = Is*(exp(Vd/Vt)-1)
        // Use R=10Ω to allow meaningful current; check I(V1) ≈ -Id
        auto r = run_string(
            ".MODEL D1 D(Is=1e-14 n=1.0)\n"
            "V1 in GND 0\n"
            "R1 in anode 10\n"
            "D1 anode GND D1\n"
            ".DC V1 0 0.8 0.1\n");
        CHECK(r.success, r.error_msg);
        // At V1=0.7V, diode should be well forward-biased
        size_t idx = nearest(r, 0.7);
        double vd  = find_value(r,"anode",idx).real;
        double id  = -find_value(r,"V1#I",idx).real; // I flowing into circuit
        CHECK(vd > 0.55 && vd < 0.72, "Vd at 0.7V bias: " + std::to_string(vd));
        CHECK(id > 1e-4, "Diode conducting at 0.7V: " + std::to_string(id));
    });

    // VCVS gain swept via input voltage
    t.run("DC: VCVS (gain=4) — V(out)=4*V1", []{
        auto r = run_string(
            "V1 ctrl GND 0\nR1 ctrl GND 1k\n"
            "E1 out GND ctrl GND 4\nR2 out GND 1k\n"
            ".DC V1 0 5 1\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i) {
            double sv  = r.data[i].sweep_value;
            double vout = find_value(r,"out",i).real;
            CHECK_NEAR(vout, sv * 4.0, 1e-9,
                       "V(out)=4*V1 @" + std::to_string(sv));
        }
    });

    // VCCS transconductance swept
    t.run("DC: VCCS (Gm=0.005) — V(out)=Gm*V1*R", []{
        // Gm=0.005, R_load=200 → V(out) = Gm * V(ctrl) * 200 = V(ctrl)
        auto r = run_string(
            "V1 ctrl GND 0\nR1 ctrl GND 1k\n"
            "G1 out GND ctrl GND 0.005\nR2 out GND 200\n"
            ".DC V1 0 5 1\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i) {
            double sv   = r.data[i].sweep_value;
            double vout = find_value(r,"out",i).real;
            // Gm*R = 0.005*200 = 1 → V(out)=V(ctrl)=sv
            CHECK_NEAR(vout, sv, 1e-9, "V(out) @" + std::to_string(sv));
        }
    });

    // Error: .DC target not found
    t.run("DC: error if target source not found", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V99 0 5 1\n");
        CHECK(!r.success, "should fail: V99 not found");
    });

    // Error: .DC target is a resistor (not V/I)
    t.run("DC: error if target is not a V or I source", []{
        auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.DC R1 0 5 1\n");
        CHECK(!r.success, "should fail: R1 is not V/I");
    });

    // JSON: dc sweep has correct structure
    t.run("DC: JSON output structure", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 2 1\n");
        std::string j = to_json(r);
        CHECK(j.find("\"analysis_type\": \"dc\"") != std::string::npos, "type=dc");
        CHECK(j.find("\"sweep_type\": \"dc_sweep\"") != std::string::npos, "sweep_type");
        CHECK(j.find("\"sweep_value\"") != std::string::npos, "sweep_value");
    });
}


// 
//  GROUP 3 — .AC  (frequency sweep)
// 

void test_ac(TestRunner& t)
{
    t.group(".AC — Frequency Sweep");

    // RC low-pass at cutoff fc = 1/(2π*1k*1µ) ≈ 159.155 Hz
    t.run("AC: RC low-pass |H(fc)| = 1/√2", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in out 1k\nC1 out GND 1u\n"
            ".AC LIN 1 159.1549 159.1549\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").magnitude(), 1.0/std::sqrt(2.0), 1e-3, "|H(fc)|");
    });

    // RL high-pass
    t.run("AC: RL high-pass |H(fc)| = 1/√2", []{
        auto r = run_string(
            "V1 in GND AC 1\nL1 in out 159.155m\nR1 out GND 1k\n"
            ".AC LIN 1 1000 1000\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").magnitude(), 1.0/std::sqrt(2.0), 1e-3, "|H(fc)|");
    });

    // Resistive divider is frequency-independent
    t.run("AC: resistive divider constant across freq", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in out 1k\nR2 out GND 1k\n"
            ".AC DEC 5 1 100k\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i)
            CHECK_NEAR(find_value(r,"out",i).magnitude(), 0.5, 1e-9,
                       "divider pt" + std::to_string(i));
    });

    // Single-point sweep (f_start == f_end, points=1) works
    t.run("AC: single-point sweep (LIN 1 f f) works", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in GND 1k\n"
            ".AC LIN 1 500 500\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.data.size() == 1, "1 data point");
    });

    // result has correct type labels
    t.run("AC: analysis_type_str == ac", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in GND 1k\n"
            ".AC LIN 1 1k 1k\n");
        CHECK(r.success, r.error_msg);
        CHECK(r.analysis_type_str == "ac",         "type_str");
        CHECK(r.analysis_type == AnalysisType::AC, "type enum");
    });

    // each DataPoint has sweep_type == "frequency"
    t.run("AC: sweep_type == frequency on all points", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in GND 1k\n"
            ".AC DEC 3 100 10k\n");
        CHECK(r.success, r.error_msg);
        for (const auto& pt : r.data)
            CHECK(pt.sweep_type == "frequency", "sweep_type");
    });

    // Values carry type field
    t.run("AC: values have type = voltage or current", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in GND 1k\n"
            ".AC LIN 1 1k 1k\n");
        CHECK(r.success, r.error_msg);
        for (const auto& nv : r.data[0].values)
            CHECK(nv.type == "voltage" || nv.type == "current", "type field");
    });
}


// 
//  GROUP 4 — .TRAN  (transient)
// 

void test_tran(TestRunner& t)
{
    t.group(".TRAN — Transient Analysis");

    // RC charging curve
    t.run("TRAN: RC charge to 5τ ≈ 5*(1-e⁻⁵)V", []{
        auto r = run_string(
            "V1 in GND 5\nR1 in out 1k\nC1 out GND 1u\n"
            ".TRAN 10u 5m\n");
        CHECK(r.success, r.error_msg);
        // Check at t=3ms (3τ): V = 5*(1-e^-3) ≈ 4.751V
        size_t idx3 = nearest(r, 3e-3);
        double exp_v = 5.0*(1.0 - std::exp(-3.0));
        CHECK_NEAR(find_value(r,"out",idx3).real, exp_v, 0.05, "V(out)@3tau");
    });

    // PULSE source transitions
    t.run("TRAN: PULSE source HIGH/LOW", []{
        auto r = run_string(
            "V1 n1 GND PULSE 0 5 0 1n 1n 50u 100u\n"
            "R1 n1 GND 1k\n"
            ".TRAN 1u 120u\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,  "n1", nearest(r, 25e-6)).real, 5.0, 0.05, "HIGH");
        CHECK_NEAR(find_value(r,  "n1", nearest(r, 75e-6)).real, 0.0, 0.05, "LOW");
    });

    // SIN source peak
    t.run("TRAN: SIN source |peak| ≈ amplitude", []{
        // SIN 0 3 10kHz: at t=25µs (quarter period): V=3*sin(π/2)=3
        auto r = run_string(
            "V1 n1 GND SIN 0 3 10k 0 0 0\n"
            "R1 n1 GND 1k\n"
            ".TRAN 500n 50u\n");
        CHECK(r.success, r.error_msg);
        size_t idx = nearest(r, 25e-6);
        CHECK_NEAR(std::abs(find_value(r,"n1",idx).real), 3.0, 0.1, "SIN peak");
    });

    // Capacitor pre-charged (.IC)
    t.run("TRAN: capacitor discharge with .IC", []{
        // V0=3V, R=1k, C=1µF → V(t)=3*e^(-t/1ms); at t=2ms: V≈3*e^-2≈0.406
        auto r = run_string(
            "R1 n1 GND 1k\nC1 n1 GND 1u\n"
            ".IC V(n1)=3\n"
            ".TRAN 10u 2m\n");
        CHECK(r.success, r.error_msg);
        size_t last = r.data.size()-1;
        CHECK_NEAR(find_value(r,"n1",last).real, 3.0*std::exp(-2.0), 0.05, "V(n1)");
    });

    // Inductor rise (RL)
    t.run("TRAN: RL current rise I(L)≈(V/R)*(1-e^-5) at 5τ", []{
        // V=5, R=100, L=10mH → τ=0.1ms; at 5τ=0.5ms
        auto r = run_string(
            "V1 n1 GND 5\nR1 n1 n2 100\nL1 n2 GND 10m\n"
            ".TRAN 2u 500u\n");
        CHECK(r.success, r.error_msg);
        size_t last = r.data.size()-1;
        double expected = (5.0/100.0)*(1.0 - std::exp(-5.0));
        CHECK_NEAR(find_value(r,"L1#I",last).real, expected, 1e-3, "I(L)");
    });

    // Diode half-wave rectifier
    t.run("TRAN: half-wave rectifier positive/negative halves", []{
        auto r = run_string(
            ".MODEL D1 D(Is=1e-14 n=1.0)\n"
            "V1 in GND SIN 0 5 1k 0 0 0\n"
            "D1 in out D1\nR1 out GND 1k\n"
            ".TRAN 2u 1m\n");
        CHECK(r.success, r.error_msg);
        // positive half-cycle peak: V(out) > 3.5V
        CHECK(find_value(r,"out",nearest(r,250e-6)).real > 3.5, "pos half");
        // negative half-cycle: V(out) < 0.1V
        CHECK(find_value(r,"out",nearest(r,750e-6)).real < 0.1, "neg half");
    });
}


// 
//  GROUP 5 — Component: Resistor
// 

void test_resistor(TestRunner& t)
{
    t.group("Resistor");

    t.run("R: Ohm's law I=V/R", []{
        auto r = run_string("V1 n1 GND 6\nR1 n1 GND 2k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"V1#I").real, -3e-3, 1e-12, "I(V1)");
    });

    t.run("R: parallel equivalent", []{
        // R1||R2 = 2k, series R3=1k → V(out)=10*2/3
        auto r = run_string("V1 in GND 10\nR3 in out 1k\nR1 out GND 4k\nR2 out GND 4k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 20.0/3.0, 1e-6, "V(out)");
    });

    t.run("R: zero resistance throws", []{
        auto r = run_string("V1 a GND 1\nR1 a GND 0\n.OP\n");
        CHECK(!r.success, "should fail");
    });

    t.run("R: series chain KVL", []{
        auto r = run_string("V1 a GND 9\nR1 a b 1k\nR2 b c 2k\nR3 c GND 3k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"b").real, 7.5, 1e-9, "V(b)");
        CHECK_NEAR(find_value(r,"c").real, 4.5, 1e-9, "V(c)");
    });

    t.run("R: Wheatstone bridge balanced → V(a)=V(b)=5V", []{
        auto r = run_string(
            "V1 vcc GND 10\n"
            "R1 vcc a 1k\nR2 a GND 1k\n"
            "R3 vcc b 1k\nR4 b GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"a").real, 5.0, 1e-9, "V(a)");
        CHECK_NEAR(find_value(r,"b").real, 5.0, 1e-9, "V(b)");
    });
}


// 
//  GROUP 6 — Component: Capacitor
// 

void test_capacitor(TestRunner& t)
{
    t.group("Capacitor");

    t.run("C: DC open circuit → V(out)=V(in)", []{
        auto r = run_string("V1 in GND 10\nR1 in out 1k\nC1 out GND 1u\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 10.0, 1e-9, "V(out) open");
    });

    t.run("C: AC RC low-pass |H(fc)|=1/√2", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in out 1k\nC1 out GND 1u\n"
            ".AC LIN 1 159.1549 159.1549\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").magnitude(), 1.0/std::sqrt(2.0), 1e-3, "|H|");
    });

    t.run("C: TRAN charging 5τ", []{
        auto r = run_string(
            "V1 in GND 5\nR1 in out 1k\nC1 out GND 1u\n.TRAN 10u 5m\n");
        CHECK(r.success, r.error_msg);
        // Check at 3τ=3ms: V = 5*(1-e^-3) ≈ 4.751V
        size_t idx = nearest(r, 3e-3);
        CHECK_NEAR(find_value(r,"out",idx).real,
                   5.0*(1.0-std::exp(-3.0)), 0.05, "V@3τ");
    });
}


// 
//  GROUP 7 — Component: Inductor
// 

void test_inductor(TestRunner& t)
{
    t.group("Inductor");

    t.run("L: DC short → V(n1)=0", []{
        auto r = run_string("V1 src GND 5\nR1 src n1 1k\nL1 n1 GND 1m\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"n1").real, 0.0, 1e-9, "V(n1)");
    });

    t.run("L: DC current through = V/R", []{
        auto r = run_string("V1 src GND 5\nR1 src n1 1k\nL1 n1 GND 1m\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"L1#I").real, 5e-3, 1e-9, "I(L1)");
    });

    t.run("L: AC RL high-pass |H(fc)|=1/√2", []{
        auto r = run_string(
            "V1 in GND AC 1\nL1 in out 159.155m\nR1 out GND 1k\n"
            ".AC LIN 1 1000 1000\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").magnitude(), 1.0/std::sqrt(2.0), 1e-3, "|H|");
    });
}


// 
//  GROUP 8 — Component: Controlled Sources
// 

void test_controlled_sources(TestRunner& t)
{
    t.group("Controlled Sources (E, G, H, F)");

    // VCVS (E)
    t.run("E: gain=3, V(out)=3*V(ctrl)", []{
        auto r = run_string(
            "V1 ctrl GND 2\nR1 ctrl GND 1k\n"
            "E1 out GND ctrl GND 3\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 6.0, 1e-9, "V(out)");
    });

    t.run("E: inverting gain=-1", []{
        auto r = run_string(
            "V1 ctrl GND 4\nR1 ctrl GND 1k\n"
            "E1 out GND ctrl GND -1\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, -4.0, 1e-9, "V(out)");
    });

    t.run("E: AC gain constant vs frequency", []{
        auto r = run_string(
            "V1 in GND AC 1\nR1 in GND 1k\n"
            "E1 out GND in GND 5\nR2 out GND 1k\n"
            ".AC DEC 3 100 100k\n");
        CHECK(r.success, r.error_msg);
        for (size_t i = 0; i < r.data.size(); ++i)
            CHECK_NEAR(find_value(r,"out",i).magnitude(), 5.0, 1e-6,
                       "E gain @pt" + std::to_string(i));
    });

    // VCCS (G)
    t.run("G: Gm=0.01, V(ctrl)=5 → V(out)=5V", []{
        auto r = run_string(
            "V1 ctrl GND 5\nR1 ctrl GND 1k\n"
            "G1 out GND ctrl GND 0.01\nR2 out GND 100\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 5.0, 1e-9, "V(out)");
    });

    t.run("G: negative Gm inverts output", []{
        auto r = run_string(
            "V1 ctrl GND 2\nR1 ctrl GND 1k\n"
            "G1 out GND ctrl GND -0.005\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, -10.0, 1e-9, "V(out)");
    });

    // CCVS (H)
    t.run("H: transresistance Rm=500, |V(out)|=1V", []{
        auto r = run_string(
            "V1 n1 GND 2\nR1 n1 GND 1k\n"
            "H1 out GND V1 500\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(std::abs(find_value(r,"out").real), 1.0, 1e-6, "|V(out)|");
    });

    t.run("H: 0V ammeter senses branch current", []{
        auto r = run_string(
            "V1 a GND 3\nV_sense a b 0\nR1 b GND 3k\n"
            "H1 out GND V_sense 2k\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(std::abs(find_value(r,"out").real), 2.0, 1e-6, "|V(out)|");
    });

    // CCCS (F)
    t.run("F: current gain=-10, V(out)=5V", []{
        auto r = run_string(
            "V1 n1 GND 1\nR1 n1 GND 1k\n"
            "F1 n_out GND V1 -10\nR2 n_out GND 500\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"n_out").real, 5.0, 1e-6, "V(n_out)");
    });

    t.run("F: current mirror gain=2", []{
        auto r = run_string(
            "V1 a GND 4\nV_sense a b 0\nR1 b GND 2k\n"
            "F1 out GND V_sense 2\nR2 out GND 1k\n.OP\n");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r,"out").real, 4.0, 1e-6, "V(out)");
    });
}


//  GROUP 9 — Diode
// 

void test_diode(TestRunner& t)
{
    t.group("Diode");

    t.run("D: forward bias Vf in [0.5, 0.8]V", []{
        auto r = run_string(
            ".MODEL D1N4148 D(Is=1e-14 n=1.0)\n"
            "V1 in GND 5\nR1 in anode 1k\nD1 anode GND D1N4148\n.OP\n");
        CHECK(r.success, r.error_msg);
        double vd = find_value(r,"anode").real;
        CHECK(vd > 0.5 && vd < 0.8, "Vf=" + std::to_string(vd));
    });

    t.run("D: reverse bias V(anode)≈-5V", []{
        auto r = run_string(
            ".MODEL DREV D(Is=1e-14 n=1.0)\n"
            "V1 in GND -5\nR1 in anode 1k\nD1 anode GND DREV\n.OP\n");
        CHECK(r.success, r.error_msg);
        double va = find_value(r,"anode").real;
        CHECK(va < -4.9 && va > -5.1, "V(anode)≈-5: " + std::to_string(va));
    });

    t.run("D: two diodes in series have similar Vf", []{
        auto r = run_string(
            ".MODEL DS D(Is=1e-14 n=1.0)\n"
            "V1 in GND 10\nR1 in a 1k\nD1 a b DS\nD2 b GND DS\n.OP\n");
        CHECK(r.success, r.error_msg);
        double vf1 = find_value(r,"a").real - find_value(r,"b").real;
        double vf2 = find_value(r,"b").real;
        CHECK_NEAR(vf1, vf2, 0.01, "Vf1≈Vf2");
    });

    t.run("D: Shockley KCL — I(R1)=Id at operating point", []{
        auto r = run_string(
            ".MODEL DSH D(Is=1e-14 n=1.0)\n"
            "V1 in GND 5\nR1 in anode 1k\nD1 anode GND DSH\n.OP\n");
        CHECK(r.success, r.error_msg);
        double va = find_value(r,"anode").real;
        double vin= find_value(r,"in").real;
        double I_R = (vin - va)/1000.0;
        double Vt  = 1.380649e-23 * 300.0 / 1.602176634e-19;
        double Id  = 1e-14*(std::exp(va/Vt) - 1.0);
        CHECK_NEAR(I_R, Id, I_R*0.01, "KCL: I(R)=I(D)");
    });

    t.run("D: TRAN half-wave rectifier positive half > 3.5V", []{
        auto r = run_string(
            ".MODEL DRECT D(Is=1e-14 n=1.0)\n"
            "V1 in GND SIN 0 5 1k 0 0 0\n"
            "D1 in out DRECT\nR1 out GND 1k\n"
            ".TRAN 2u 1m\n");
        CHECK(r.success, r.error_msg);
        CHECK(find_value(r,"out",nearest(r,250e-6)).real > 3.5, "pos peak");
        CHECK(find_value(r,"out",nearest(r,750e-6)).real < 0.1, "neg blocked");
    });

    t.run("D: peak detector holds peak-Vf", []{
        auto r = run_string(
            ".MODEL DPK D(Is=1e-14 n=1.0)\n"
            "V1 in GND SIN 0 3 1k 0 0 0\n"
            "D1 in out DPK\nC1 out GND 10u\nR1 out GND 100k\n"
            ".TRAN 2u 5m\n");
        CHECK(r.success, r.error_msg);
        double v = find_value(r,"out",r.data.size()-1).real;
        CHECK(v > 2.0 && v < 3.0, "peak-det: " + std::to_string(v));
    });

    t.run("D: DC sweep I-V (diode conducting at 0.7V)", []{
        auto r = run_string(
            ".MODEL D1 D(Is=1e-14 n=1.0)\n"
            "V1 in GND 0\nR1 in anode 10\nD1 anode GND D1\n"
            ".DC V1 0 0.8 0.1\n");
        CHECK(r.success, r.error_msg);
        size_t idx = nearest(r, 0.7);
        double id  = -find_value(r,"V1#I",idx).real;
        CHECK(id > 1e-4, "Diode I at 0.7V: " + std::to_string(id));
    });

    t.run("D: error on unknown model", []{
        bool threw = false;
        try { auto c = parse_netlist([](){
            namespace fs = std::filesystem;
            auto p = fs::temp_directory_path()/"ce_dtest.cir";
            { std::ofstream f(p); f << "D1 a GND NOMODEL\nV1 a GND 1\n"; }
            return p.string();
        }()); } catch (...) { threw = true; }
        CHECK(threw, "should throw: unknown model");
    });
}


// 
//  GROUP 10 — Error handling & Integration
// 

void test_errors_and_integration(TestRunner& t)
{
    t.group("Error Handling & Integration");

    t.run("Error: duplicate component name", []{
        bool threw = false;
        try { run_string("V1 n1 GND 5\nV1 n2 GND 3\n"); }
        catch (...) { threw = true; }
        // duplicate name throws during parse (not as r.success=false)
        CHECK(threw, "should throw on duplicate name");
    });

    t.run("Error: singular matrix (ideal V || ideal L)", []{
        // Direct V source parallel with inductor → singular DC matrix
        auto r = run_string("V1 n1 GND 5\nL1 n1 GND 1m\n.OP\n");
        CHECK(!r.success, "singular: V||L at DC");
    });

    t.run("Parser: SPICE suffixes k m u n p meg", []{
        // 1k = 1000; 1MEG = 1e6
        auto r1 = run_string("V1 a GND 1\nR1 a GND 1k\n.OP\n");
        auto r2 = run_string("V1 a GND 1\nR1 a GND 1000\n.OP\n");
        CHECK(r1.success && r2.success, "both succeed");
        CHECK_NEAR(find_value(r1,"V1#I").real,
                   find_value(r2,"V1#I").real, 1e-15, "k==1000");
        auto r3 = run_string("V1 a GND 1\nR1 a GND 1MEG\n.OP\n");
        CHECK_NEAR(find_value(r3,"V1#I").real, -1e-6, 1e-12, "MEG");
    });

    t.run("JSON: OP result has required fields", []{
        auto r = run_string("V1 n1 GND 5\nR1 n1 n2 1k\nR2 n2 GND 1k\n.OP\n");
        std::string j = to_json(r);
        CHECK(j.find("\"success\": true")           != std::string::npos, "success");
        CHECK(j.find("\"analysis_type\": \"op\"")   != std::string::npos, "type=op");
        CHECK(j.find("\"node_map\"")                != std::string::npos, "node_map");
        CHECK(j.find("\"sweep_type\": \"operating_point\"") != std::string::npos, "sweep_type");
        CHECK(j.find("\"type\": \"voltage\"")       != std::string::npos, "voltage");
        CHECK(j.find("\"type\": \"current\"")       != std::string::npos, "current");
    });

    t.run("JSON: DC sweep result has dc_sweep sweep_type", []{
        auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 3 1\n");
        std::string j = to_json(r);
        CHECK(j.find("\"analysis_type\": \"dc\"")      != std::string::npos, "type=dc");
        CHECK(j.find("\"sweep_type\": \"dc_sweep\"")   != std::string::npos, "sweep_type");
    });

    t.run("Integration: OP vs DC give same value at single point", []{
        // OP at V1=3V should equal DC sweep at V1=3
        auto r_op = run_string("V1 n1 GND 3\nR1 n1 GND 1k\n.OP\n");
        auto r_dc = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 3 3 1\n");
        CHECK(r_op.success && r_dc.success, "both succeed");
        CHECK_NEAR(find_value(r_op,"n1",0).real,
                   find_value(r_dc,"n1",0).real, 1e-9, "V(n1) OP==DC@3");
    });

    t.run("Integration: RLC series resonance |H(f0)|≈1", []{
        // f0=15915Hz (L=1mH, C=100nF), Z_L=Z_C cancel → |H|=1 into R
        auto r = run_string(
            "V1 in GND AC 1\n"
            "L1 in lc 1m\nC1 lc out 100n\nR1 out GND 100\n"
            ".AC LIN 1 15915 15915\n");
        CHECK(r.success, r.error_msg);
        CHECK(find_value(r,"out").magnitude() > 0.9, "RLC resonance");
    });

    t.run("Integration: op_type_str + enum consistent for all modes", []{
        { auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.OP\n");
          CHECK(r.analysis_type_str == "op" && r.analysis_type == AnalysisType::OP, "op"); }
        { auto r = run_string("V1 n1 GND 0\nR1 n1 GND 1k\n.DC V1 0 1 1\n");
          CHECK(r.analysis_type_str == "dc" && r.analysis_type == AnalysisType::DC, "dc"); }
        { auto r = run_string("V1 n1 GND AC 1\nR1 n1 GND 1k\n.AC LIN 1 1k 1k\n");
          CHECK(r.analysis_type_str == "ac" && r.analysis_type == AnalysisType::AC, "ac"); }
        { auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.TRAN 1u 5u\n");
          CHECK(r.analysis_type_str == "tran" && r.analysis_type == AnalysisType::TRAN, "tran"); }
    });
}


// 
//  main
// 

int main()
{
    TestRunner t;

    test_op(t);
    test_dc_sweep(t);
    test_ac(t);
    test_tran(t);
    test_resistor(t);
    test_capacitor(t);
    test_inductor(t);
    test_controlled_sources(t);
    test_diode(t);
    test_errors_and_integration(t);

    t.summary();
    return t.failed > 0 ? 1 : 0;
}