# main.py

import numpy as np

from parser.netlist_parser import parse_netlist
from mna_builder.mna_builder import MNABuilder
from solver.solver import Solver
from analysis.ac_analysis import ACAnalysis
from analysis.tran_analysis import TransientAnalysis
from mna_builder.stamp_context import StampContext
from analysis.plot import plot_bode
from analysis.plot import plot_tran

PLOT_TRAN_V1ST = 0
PLOT_TRAN_V2ND = 1

PLOT_AC_IN = 0
PLOT_AC_OUT = 1


def main():
    circuit = parse_netlist("netlist.cir")

    print("===== NODE MAP =====")
    for name, idx in circuit.node_map.items():
        print(f"{name} -> {idx}")

    print("\n===== COMPONENTS =====")
    for comp in circuit.components:
        print(comp)

    print("\n===== ANALYSIS =====")

    builder = MNABuilder(circuit)
    solver = Solver()

    analysis = circuit.circuit_analysis

    if not analysis:
        print("Warning: No analysis specified. Auto DC mode")
        analysis = {"type": "dc"}

    #  DC 
    if analysis["type"] == "dc":
        print(">>> RUN DC ANALYSIS")

        ctx = StampContext(mode="dc")
        A, z = builder.build(ctx)

        x = solver.solve_linear(A, z)

        print("x =", x)
        return

    #  AC ANALYSIS
    if analysis["type"] == "ac":
        print(">>> RUN AC ANALYSIS")

        ac = ACAnalysis(circuit, builder, solver)
        results = ac.run()

        print(">>> DONE:", len(results), "points")
        for f, x in results[:5]:
            print(f"f = {f:.6f} Hz, x = {x}")
        print("...")

        print(">>> PLOT BODE")
        plot_bode(results, PLOT_AC_IN, PLOT_AC_OUT)
        return

    #  TRANSIENT ANALYSIS
    if analysis["type"] == "tran":
        print(">>> RUN TRANSIENT ANALYSIS")

        tran = TransientAnalysis(circuit, builder, solver)
        results = tran.run()

        print(">>> DONE:", len(results), "points")

        for t, x in results[:10]:
            print(f"t = {t:.6f}, x = {x}")
        print("...")

        print(">>> PLOT TRANSIENT")
        plot_tran(results, PLOT_TRAN_V1ST, PLOT_TRAN_V2ND)
        return


if __name__ == "__main__":
    main()
