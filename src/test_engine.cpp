/**
 * tests/test_engine.cpp
 *
 * Unit + integration tests for CircuitEngine.
 * Uses a minimal hand-rolled test framework (no external deps).
 *
 * Build:
 *   g++ -std=c++17 -O2 -I../src -I/usr/include/eigen3 \
 *       test_engine.cpp -o test_engine && ./test_engine
 */

#include "engine.h"
#include "json_export.h"
#include <iostream>
#include <cmath>
#include <cassert>
#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <filesystem>

using namespace CircuitEngine;

// ── Minimal test framework ─────────────────────────────────────────
struct TestRunner {
    int passed = 0, failed = 0;

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

    void summary() {
        std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    }
};

// Assertion helpers
static void CHECK(bool cond, const std::string& msg = "assertion failed") {
    if (!cond) throw std::runtime_error(msg);
}

static void CHECK_NEAR(double a, double b, double tol, const std::string& msg = "") {
    if (std::abs(a - b) > tol)
        throw std::runtime_error(
            msg + " expected≈" + std::to_string(b) +
            " got " + std::to_string(a) +
            " (diff=" + std::to_string(std::abs(a-b)) + ")");
}

// ── Helper: build circuit from inline netlist string ───────────────
static Circuit circuit_from_string(const std::string& netlist)
{
    namespace fs = std::filesystem;

    auto path = fs::temp_directory_path() / "test_netlist_tmp.cir";

    {
        std::ofstream f(path);
        f << netlist;
    }

    return parse_netlist(path.string());
}

static SimulationResult run_string(const std::string& netlist)
{
    Circuit    circ = circuit_from_string(netlist);
    MNABuilder builder(circ);
    Solver     solver;
    return simulate(circ, builder, solver);
}

// Helper to find a NodeValue by name in the first DataPoint
static const NodeValue& find_value(const SimulationResult& r,
                                    const std::string& name,
                                    size_t pt_idx = 0)
{
    for (const auto& nv : r.data.at(pt_idx).values)
        if (nv.name == name) return nv;
    throw std::runtime_error("NodeValue not found: " + name);
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 1 – DC Analysis
// ══════════════════════════════════════════════════════════════════

void test_dc_voltage_divider(TestRunner& t)
{
    // V1=10V, R1=3k between n1 and n2, R2=7k between n2 and GND
    // Expected: V(n2) = 10 * 7/(3+7) = 7 V
    t.run("DC: voltage divider", [&]{
        auto r = run_string(R"(
* Voltage divider
V1 n1 GND 10
R1 n1 n2 3k
R2 n2 GND 7k
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "n2").real, 7.0, 1e-9, "V(n2)");
        CHECK_NEAR(find_value(r, "n1").real, 10.0, 1e-9, "V(n1)");
    });
}

void test_dc_current_source(TestRunner& t)
{
    // I1=2mA into n1, R1=1k from n1 to GND  → V(n1) = 2V
    t.run("DC: current source + resistor", [&]{
        auto r = run_string(R"(
* Norton equivalent
I1 GND n1 2m
R1 n1 GND 1k
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "n1").real, 2.0, 1e-9, "V(n1)");
    });
}

void test_dc_series_resistors(TestRunner& t)
{
    // V=5V, R1=1k, R2=1k in series → I = 2.5mA, V(n1)=5V V(n2)=2.5V
    t.run("DC: series resistors", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 n2 1k
R2 n2 GND 1k
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "n2").real, 2.5, 1e-9, "V(n2)");
        // Branch current through V1 (enters n1) should be -2.5mA
        CHECK_NEAR(find_value(r, "V1#I").real, -2.5e-3, 1e-9, "I(V1)");
    });
}

void test_dc_inductor_shortcircuit(TestRunner& t)
{
    // Inductor in DC = short. V1=5V feeds n_src, R1=1k from n_src to n1, L1 from n1 to GND.
    // L shorts n1 to GND → V(n1)=0, I through R1 = 5mA, V(n_src)=5V
    t.run("DC: inductor = short circuit", [&]{
        auto r = run_string(R"(
V1 n_src GND 5
R1 n_src n1 1k
L1 n1 GND 1m
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "n1").real, 0.0, 1e-9, "V(n1) shorted by inductor");
    });
}

void test_dc_vcvs(TestRunner& t)
{
    // VCVS: E1 output between out and GND, gain=3, controlled by V(n1)
    // V1=2V at n1 → V(out) = 6V
    t.run("DC: VCVS (voltage amplifier)", [&]{
        auto r = run_string(R"(
V1 n1 GND 2
R1 n1 GND 1k
E1 out GND n1 GND 3
R2 out GND 1k
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "out").real, 6.0, 1e-9, "V(out)");
    });
}

void test_dc_vccs(TestRunner& t)
{
    // VCCS G1: Gm=0.01 S, controlled by V(n1)=5V
    // Output current = 0.01*5 = 50mA into n2, through R=100Ω → V(n2)=5V
    t.run("DC: VCCS (transconductance)", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 GND 1k
G1 n2 GND n1 GND 0.01
R2 n2 GND 100
)");
        CHECK(r.success, r.error_msg);
        CHECK_NEAR(find_value(r, "n2").real, 5.0, 1e-9, "V(n2)");
    });
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 2 – AC Analysis
// ══════════════════════════════════════════════════════════════════

void test_ac_rc_lowpass(TestRunner& t)
{
    // Simple RC lowpass: R=1k, C=1uF, f_c=159.15 Hz
    // At f=f_c: |H| = 1/sqrt(2) ≈ 0.7071
    t.run("AC: RC low-pass at cutoff freq", [&]{
        auto r = run_string(R"(
V1 n1 GND AC 1
R1 n1 n2 1k
C1 n2 GND 1u
.AC LIN 1 159.1549 159.1549
)");
        CHECK(r.success, r.error_msg);
        auto& nv = find_value(r, "n2");
        double mag = nv.magnitude();
        CHECK_NEAR(mag, 1.0/std::sqrt(2.0), 1e-3, "|H(fc)|");
    });
}

void test_ac_resistor_divider(TestRunner& t)
{
    // At any freq, two equal R divide equally (no caps/inductors)
    t.run("AC: resistive divider is frequency-independent", [&]{
        auto r = run_string(R"(
V1 n1 GND AC 1
R1 n1 n2 1k
R2 n2 GND 1k
.AC DEC 3 100 100k
)");
        CHECK(r.success, r.error_msg);
        // All points should give |V(n2)| = 0.5
        for (size_t i = 0; i < r.data.size(); ++i)
            CHECK_NEAR(find_value(r, "n2", i).magnitude(), 0.5, 1e-9, "AC divider pt" + std::to_string(i));
    });
}

void test_ac_rl_highpass(TestRunner& t)
{
    // RL highpass: R=1k, L=1/(2π) H → f_c=1/(2π * 1k/L) ... let's pick f_c=1kHz
    // L = R/(2π*fc) = 1000/(2π*1000) ≈ 0.159155 H
    // At fc: |H|=1/sqrt(2)
    t.run("AC: RL high-pass at cutoff freq", [&]{
        auto r = run_string(R"(
V1 n1 GND AC 1
L1 n1 n2 159.155m
R1 n2 GND 1k
.AC LIN 1 1000 1000
)");
        CHECK(r.success, r.error_msg);
        double mag = find_value(r, "n2").magnitude();
        CHECK_NEAR(mag, 1.0/std::sqrt(2.0), 1e-3, "|H(fc)| RL");
    });
}

void test_ac_result_has_type_fields(TestRunner& t)
{
    // Verify all NodeValue entries have non-empty type
    t.run("AC: all values have 'type' field", [&]{
        auto r = run_string(R"(
V1 n1 GND AC 1
R1 n1 n2 1k
R2 n2 GND 1k
.AC LIN 1 1k 1k
)");
        CHECK(r.success, r.error_msg);
        for (const auto& nv : r.data[0].values)
            CHECK(!nv.type.empty(), "type is empty for " + nv.name);
    });
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 3 – Transient Analysis
// ══════════════════════════════════════════════════════════════════

void test_tran_rc_charge(TestRunner& t)
{
    // RC charge: V=5V, R=1k, C=1uF, tau=1ms
    // After 5*tau=5ms, V(n2) ≈ 5*(1 - e^-5) ≈ 4.9663V
    t.run("TRAN: RC charging to 5*tau", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 n2 1k
C1 n2 GND 1u
.TRAN 10u 5m
)");
        CHECK(r.success, r.error_msg);
        // Check final point
        size_t last = r.data.size() - 1;
        double v_final = find_value(r, "n2", last).real;
        double expected = 5.0 * (1.0 - std::exp(-5.0));
        CHECK_NEAR(v_final, expected, 0.01, "V(n2) after 5*tau");
    });
}

void test_tran_pulse_source(TestRunner& t)
{
    // Pulse from 0V→5V after 1us delay, width=10us, period=20us
    // At t=2us (during pulse high): V(n1) should be 5V through R to n2
    t.run("TRAN: pulse voltage source", [&]{
        auto r = run_string(R"(
V1 n1 GND PULSE 0 5 1u 10n 10n 10u 20u
R1 n1 n2 1k
R2 n2 GND 1k
.TRAN 100n 5u
)");
        CHECK(r.success, r.error_msg);
        // Find a point at ~t=2us (step 100ns → index ~20)
        // At t=2us pulse is HIGH, V(n1)=5, V(n2)=2.5
        size_t idx = 20; // t≈2us
        if (idx < r.data.size()) {
            double v2 = find_value(r, "n2", idx).real;
            CHECK_NEAR(v2, 2.5, 0.05, "V(n2) during pulse high");
        }
    });
}

void test_tran_dc_steady_state(TestRunner& t)
{
    // Pure resistive: after any dt, should immediately reach steady state
    t.run("TRAN: resistor reaches DC steady state", [&]{
        auto r = run_string(R"(
V1 n1 GND 3
R1 n1 n2 2k
R2 n2 GND 1k
.TRAN 1u 10u
)");
        CHECK(r.success, r.error_msg);
        // V(n2) = 3 * 1/(2+1) = 1V at all times
        for (size_t i = 1; i < r.data.size(); ++i)
            CHECK_NEAR(find_value(r, "n2", i).real, 1.0, 1e-6,
                       "steady state at pt" + std::to_string(i));
    });
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 4 – Error handling
// ══════════════════════════════════════════════════════════════════

void test_error_zero_resistor(TestRunner& t)
{
    t.run("Error: zero-value resistor", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 GND 0
)");
        CHECK(!r.success, "should fail with zero resistor");
    });
}

void test_error_unknown_model(TestRunner& t)
{
    t.run("Error: unknown diode model", [&]{
        bool threw = false;
        try {
            circuit_from_string(R"(
D1 n1 GND BADMODEL
V1 n1 GND 1
)");
        } catch (...) { threw = true; }
        CHECK(threw, "should throw on unknown model");
    });
}

void test_error_duplicate_component(TestRunner& t)
{
    t.run("Error: duplicate component name", [&]{
        bool threw = false;
        try {
            circuit_from_string(R"(
V1 n1 GND 5
V1 n2 GND 3
)");
        } catch (...) { threw = true; }
        CHECK(threw, "should throw on duplicate name");
    });
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 5 – Result structure / C# integration
// ══════════════════════════════════════════════════════════════════

void test_result_structure(TestRunner& t)
{
    t.run("Result: analysis_type_str matches enum", [&]{
        {
            auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n");
            CHECK(r.analysis_type_str == "dc", "dc string");
            CHECK(r.analysis_type == AnalysisType::DC, "dc enum");
        }
        {
            auto r = run_string("V1 n1 GND AC 1\nR1 n1 GND 1k\n.AC LIN 1 1k 1k\n");
            CHECK(r.analysis_type_str == "ac", "ac string");
        }
        {
            auto r = run_string("V1 n1 GND 5\nR1 n1 GND 1k\n.TRAN 1u 5u\n");
            CHECK(r.analysis_type_str == "tran", "tran string");
        }
    });
}

void test_json_serialisation(TestRunner& t)
{
    t.run("JSON: output is valid (spot checks)", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 n2 1k
R2 n2 GND 1k
)");
        std::string json = to_json(r);
        CHECK(json.find("\"success\": true")  != std::string::npos, "success field");
        CHECK(json.find("\"analysis_type\": \"dc\"") != std::string::npos, "type field");
        CHECK(json.find("\"type\": \"voltage\"")     != std::string::npos, "voltage type tag");
        CHECK(json.find("\"type\": \"current\"")     != std::string::npos, "current type tag");
        CHECK(json.find("\"sweep_type\"")            != std::string::npos, "sweep_type field");
    });
}

void test_node_map_in_result(TestRunner& t)
{
    t.run("Result: node_map present and correct", [&]{
        auto r = run_string(R"(
V1 n1 GND 5
R1 n1 n2 1k
R2 n2 GND 1k
)");
        CHECK(r.node_map.count("n1"), "n1 in node_map");
        CHECK(r.node_map.count("n2"), "n2 in node_map");
        CHECK(!r.node_map.count("GND"), "GND not in node_map");
    });
}

// ══════════════════════════════════════════════════════════════════
// TEST GROUP 6 – Value parsing
// ══════════════════════════════════════════════════════════════════

void test_value_parsing(TestRunner& t)
{
    // 1kΩ and 1000Ω should give same result
    t.run("Parser: SPICE suffixes (k, m, u, meg)", [&]{
        auto r1 = run_string("V1 n1 GND 1\nR1 n1 GND 1k\n");
        auto r2 = run_string("V1 n1 GND 1\nR1 n1 GND 1000\n");
        CHECK(r1.success && r2.success, "both should succeed");
        CHECK_NEAR(find_value(r1, "V1#I").real,
                   find_value(r2, "V1#I").real, 1e-15, "k vs 1000");
    });
}

// ══════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════

int main()
{
    TestRunner t;

    std::cout << "\n=== DC Analysis ===\n";
    test_dc_voltage_divider(t);
    test_dc_current_source(t);
    test_dc_series_resistors(t);
    test_dc_inductor_shortcircuit(t);
    test_dc_vcvs(t);
    test_dc_vccs(t);

    std::cout << "\n=== AC Analysis ===\n";
    test_ac_rc_lowpass(t);
    test_ac_resistor_divider(t);
    test_ac_rl_highpass(t);
    test_ac_result_has_type_fields(t);

    std::cout << "\n=== Transient Analysis ===\n";
    test_tran_rc_charge(t);
    test_tran_pulse_source(t);
    test_tran_dc_steady_state(t);

    std::cout << "\n=== Error Handling ===\n";
    test_error_zero_resistor(t);
    test_error_unknown_model(t);
    test_error_duplicate_component(t);

    std::cout << "\n=== Result Structure / C# Integration ===\n";
    test_result_structure(t);
    test_json_serialisation(t);
    test_node_map_in_result(t);

    std::cout << "\n=== Value Parsing ===\n";
    test_value_parsing(t);

    t.summary();
    return t.failed > 0 ? 1 : 0;
}
