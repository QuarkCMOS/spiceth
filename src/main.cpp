/**
 * main.cpp  –  CLI entry point for CircuitEngine.
 *
 * Usage:
 *   circuit_engine <netlist.cir>              → JSON to stdout  (C# / pipe mode)
 *   circuit_engine <netlist.cir> --human      → DC table + ASCII waveforms, no JSON
 *   circuit_engine <netlist.cir> --plot       → write <netlist>.html then open browser
 *   circuit_engine <netlist.cir> --plot out.html → write to specific file
 *
 * C# / WPF integration:
 *   var proc = Process.Start("circuit_engine.exe", netlistPath);
 *   string json = proc.StandardOutput.ReadToEnd();
 *   var result = JsonSerializer.Deserialize<SimulationResult>(json);
 */

#include "engine.h"
#include "json_export.h"
#include "html_export.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
  #include <windows.h>   // ShellExecuteA
  #define OPEN_CMD(p) ShellExecuteA(nullptr,"open",(p).c_str(),nullptr,nullptr,SW_SHOW)
#else
  #define OPEN_CMD(p) (void)system(("xdg-open \"" + (p) + "\" &").c_str())
#endif

using namespace CircuitEngine;

// ANSI colours (disabled on Windows without VT mode)
#ifdef _WIN32
  static void enable_ansi() {
      HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
      DWORD mode = 0;
      if (GetConsoleMode(h, &mode))
          SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
  #define COL_RESET  "\033[0m"
  #define COL_CYAN   "\033[36m"
  #define COL_GREEN  "\033[32m"
  #define COL_YELLOW "\033[33m"
  #define COL_RED    "\033[31m"
  #define COL_BOLD   "\033[1m"
#else
  static void enable_ansi() {}
  #define COL_RESET  "\033[0m"
  #define COL_CYAN   "\033[36m"
  #define COL_GREEN  "\033[32m"
  #define COL_YELLOW "\033[33m"
  #define COL_RED    "\033[31m"
  #define COL_BOLD   "\033[1m"
#endif

// Engineering suffix
static std::string eng(double v, const char* unit)
{
    char buf[64];
    double av = std::abs(v);
    if      (av == 0)    snprintf(buf, sizeof(buf), "0 %s", unit);
    else if (av >= 1e9)  snprintf(buf, sizeof(buf), "%.4g G%s", v/1e9,  unit);
    else if (av >= 1e6)  snprintf(buf, sizeof(buf), "%.4g M%s", v/1e6,  unit);
    else if (av >= 1e3)  snprintf(buf, sizeof(buf), "%.4g k%s", v/1e3,  unit);
    else if (av >= 1.0)  snprintf(buf, sizeof(buf), "%.6g %s",  v,      unit);
    else if (av >= 1e-3) snprintf(buf, sizeof(buf), "%.4g m%s", v*1e3,  unit);
    else if (av >= 1e-6) snprintf(buf, sizeof(buf), "%.4g us%s",v*1e6,  unit);
    else if (av >= 1e-9) snprintf(buf, sizeof(buf), "%.4g n%s", v*1e9,  unit);
    else                 snprintf(buf, sizeof(buf), "%.4g p%s", v*1e12, unit);
    return buf;
}

// .OP: single operating point table
static void print_op(const SimulationResult& r)
{
    if (r.data.empty()) return;
    const auto& pt = r.data[0];

    std::vector<const NodeValue*> volts, amps;
    for (const auto& nv : pt.values) {
        if (nv.type == "voltage") volts.push_back(&nv);
        else                      amps .push_back(&nv);
    }

    std::cout << "\n" << COL_BOLD << COL_CYAN
              << "+==========================================+\n"
              << "|  DC Operating Point  (.OP)               |\n"
              << "+==========================================+" << COL_RESET << "\n\n";

    std::cout << COL_BOLD << "  Node Voltages\n" << COL_RESET;
    std::cout << "  " << std::string(44, '-') << "\n";
    for (const auto* v : volts) {
        std::string val = eng(v->real, "V");
        std::cout << COL_GREEN << "  V(" << std::left << std::setw(12) << (v->name + ")")
                  << COL_RESET << "  " << COL_BOLD << std::right << std::setw(16) << val
                  << COL_RESET << "\n";
    }

    if (!amps.empty()) {
        std::cout << "\n" << COL_BOLD << "  Branch Currents\n" << COL_RESET;
        std::cout << "  " << std::string(44, '-') << "\n";
        for (const auto* a : amps) {
            std::string val = eng(a->real, "A");
            std::cout << COL_YELLOW << "  I(" << std::left << std::setw(12) << (a->name + ")")
                      << COL_RESET << "  " << COL_BOLD << std::right << std::setw(16) << val
                      << COL_RESET << "\n";
        }
    }
    std::cout << "\n";
}

// .DC: ASCII sweep table (source value → node voltages) 
static void print_dc_sweep(const SimulationResult& r)
{
    if (r.data.empty()) return;

    // Collect voltage node names from first point
    std::vector<std::string> vnames, inames;
    for (const auto& nv : r.data[0].values) {
        if (nv.type == "voltage") vnames.push_back(nv.name);
        else                      inames.push_back(nv.name);
    }

    std::cout << "\n" << COL_BOLD << COL_CYAN
              << "+==========================================+\n"
              << "|  DC Sweep  (.DC)                         |\n"
              << "+==========================================+" << COL_RESET << "\n\n";

    // Header
    std::cout << COL_BOLD;
    std::cout << "  " << std::setw(12) << "Sweep";
    for (const auto& n : vnames) std::cout << "  " << std::setw(12) << ("V("+n+")");
    for (const auto& n : inames) std::cout << "  " << std::setw(12) << ("I("+n+")");
    std::cout << COL_RESET << "\n";
    std::cout << "  " << std::string(14 + 14*(vnames.size()+inames.size()), '-') << "\n";

    // Print every point (cap at 40 rows for readability)
    size_t total = r.data.size();
    size_t step  = std::max(size_t(1), total / 40);

    for (size_t i = 0; i < total; i += step) {
        const auto& pt = r.data[i];
        std::cout << "  " << COL_CYAN << std::setw(12) << eng(pt.sweep_value, "") << COL_RESET;

        for (const auto& name : vnames) {
            double val = 0;
            for (const auto& nv : pt.values)
                if (nv.name == name) { val = nv.real; break; }
            std::cout << COL_GREEN << "  " << std::setw(12) << eng(val, "V") << COL_RESET;
        }
        for (const auto& name : inames) {
            double val = 0;
            for (const auto& nv : pt.values)
                if (nv.name == name) { val = nv.real; break; }
            std::cout << COL_YELLOW << "  " << std::setw(12) << eng(val, "A") << COL_RESET;
        }
        std::cout << "\n";
    }
    if (step > 1)
        std::cout << "  " << COL_YELLOW << "(showing " << (total/step) << " of "
                  << total << " points)" << COL_RESET << "\n";
    std::cout << "\n";
}

//  ASCII bar for AC
static void print_ac_ascii(const SimulationResult& r)
{
    if (r.data.empty()) return;
    // Collect all voltage node names
    std::vector<std::string> nodes;
    for (const auto& nv : r.data[0].values)
        if (nv.type == "voltage") nodes.push_back(nv.name);

    for (const auto& node : nodes) {
        std::vector<double> freqs, dbs;
        for (const auto& pt : r.data) {
            for (const auto& nv : pt.values) {
                if (nv.name == node) {
                    freqs.push_back(pt.sweep_value);
                    double m = nv.magnitude();
                    dbs.push_back(20.0 * std::log10(m > 1e-30 ? m : 1e-30));
                }
            }
        }
        if (dbs.empty()) continue;

        const int W = 50;
        double mx = *std::max_element(dbs.begin(), dbs.end());
        double mn = *std::min_element(dbs.begin(), dbs.end());
        double rng = mx - mn; if (rng < 1.0) rng = 1.0;

        std::cout << COL_BOLD << COL_CYAN
                  << "\n  AC  |V(" << node << ")|  [dB]\n" << COL_RESET;
        std::cout << "  " << std::string(W + 22, '-') << "\n";

        int step = std::max(1, (int)freqs.size() / 28);
        for (int i = 0; i < (int)freqs.size(); i += step) {
            double f = freqs[i];
            int bars = static_cast<int>((dbs[i] - mn) / rng * W);
            char buf[32];
            if      (f >= 1e6) snprintf(buf, sizeof(buf), "%7.2f MHz", f/1e6);
            else if (f >= 1e3) snprintf(buf, sizeof(buf), "%7.2f kHz", f/1e3);
            else               snprintf(buf, sizeof(buf), "%7.2f Hz ", f);
            std::cout << "  " << buf << " |" << COL_GREEN
                      << std::string(bars, '#') << std::string(W - bars, ' ')
                      << COL_RESET << "| " << std::fixed << std::setprecision(1) << dbs[i] << " dB\n";
        }
        std::cout << "  " << std::string(W + 22, '-') << "\n";
    }
}

//  ASCII waveform for TRAN 
static void print_tran_ascii(const SimulationResult& r)
{
    if (r.data.empty()) return;
    std::vector<std::string> nodes;
    for (const auto& nv : r.data[0].values)
        if (nv.type == "voltage") nodes.push_back(nv.name);

    for (const auto& node : nodes) {
        std::vector<double> times, vals;
        for (const auto& pt : r.data)
            for (const auto& nv : pt.values)
                if (nv.name == node) { times.push_back(pt.sweep_value); vals.push_back(nv.real); }
        if (vals.empty()) continue;

        const int W = 60, H = 12;
        double vmax = *std::max_element(vals.begin(), vals.end());
        double vmin = *std::min_element(vals.begin(), vals.end());
        double vr   = vmax - vmin; if (vr < 1e-15) vr = 1.0;

        std::vector<std::string> grid(H, std::string(W, ' '));
        for (int col = 0; col < W; ++col) {
            int idx = (int)((double)col / W * vals.size());
            if (idx >= (int)vals.size()) idx = (int)vals.size()-1;
            int row = H-1 - (int)((vals[idx]-vmin)/vr*(H-1));
            row = std::max(0, std::min(H-1, row));
            grid[row][col] = '*';
        }

        std::cout << COL_BOLD << COL_CYAN
                  << "\n  TRAN  V(" << node << ")  vs  time\n" << COL_RESET;
        std::cout << "  " << std::string(W+12, '-') << "\n";
        for (int row = 0; row < H; ++row) {
            double ylvl = vmax - (double)row/(H-1)*vr;
            char buf[20]; snprintf(buf, sizeof(buf), "%9.3f V ", ylvl);
            std::cout << "  " << buf << "|" << COL_GREEN << grid[row] << COL_RESET << "|\n";
        }
        // time axis
        auto tfmt = [](double t) -> std::string {
            char b[20];
            if (t >= 1.0) snprintf(b,sizeof(b),"%.3fs",t);
            else if (t >= 1e-3) snprintf(b,sizeof(b),"%.3fms",t*1e3);
            else if (t >= 1e-6) snprintf(b,sizeof(b),"%.3fus",t*1e6);
            else snprintf(b,sizeof(b),"%.3fns",t*1e9);
            return b;
        };
        std::string t0s = tfmt(times.front()), t1s = tfmt(times.back());
        std::cout << "            +" << t0s
                  << std::string(W - (int)t0s.size() - (int)t1s.size(), '-')
                  << t1s << "\n\n";
    }
}

//  main 
int main(int argc, char* argv[])
{
    enable_ansi();

    if (argc < 2) {
        std::cerr << "Usage: circuit_engine <netlist.cir> [--human | --plot [out.html]]\n";
        return 1;
    }

    std::string path = argv[1];

    bool do_plot  = false;
    bool do_human = false;
    std::string html_out;

    for (int i = 2; i < argc; ++i) {
        std::string f = argv[i];
        if (f == "--plot")  { do_plot = true; }
        else if (f == "--human") { do_human = true; }
        else if (do_plot && f[0] != '-') { html_out = f; }
    }

    SimulationResult result = simulate_file(path);

    //  --human / --plot both show terminal output 
    if (do_human || do_plot) {
        std::cout << COL_BOLD << "\n  CircuitEngine  |  " << path << "\n" << COL_RESET;
        if (!result.success) {
            std::cerr << COL_RED << "\n  Error: " << result.error_msg << COL_RESET << "\n";
            return 2;
        }
        if (result.analysis_type == AnalysisType::OP)   print_op(result);
        else if (result.analysis_type == AnalysisType::DC)   print_dc_sweep(result);
        else if (result.analysis_type == AnalysisType::AC)   print_ac_ascii(result);
        else if (result.analysis_type == AnalysisType::TRAN) print_tran_ascii(result);
    }

    //  --plot: write HTML and open browser 
    if (do_plot) {
        // Default output name: replace .cir with .html
        if (html_out.empty()) {
            html_out = path;
            auto dot = html_out.rfind('.');
            if (dot != std::string::npos) html_out = html_out.substr(0, dot);
            html_out += ".html";
        }
        std::ofstream f(html_out);
        if (!f) {
            std::cerr << COL_RED << "  Cannot write: " << html_out << COL_RESET << "\n";
            return 1;
        }
        // Use just the filename as title
        std::string title = path;
        auto slash = title.find_last_of("/\\");
        if (slash != std::string::npos) title = title.substr(slash+1);

        f << to_html(result, title);
        f.close();
        std::cout << COL_GREEN << "  Plot written: " << html_out << COL_RESET << "\n";
        OPEN_CMD(html_out);
        return result.success ? 0 : 2;
    }

    //  default: JSON to stdout (C# / pipe mode) 
    if (!do_human) {
        std::cout << to_json(result);
    }

    return result.success ? 0 : 2;
}