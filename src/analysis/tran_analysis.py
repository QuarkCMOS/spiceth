# analysis/tran_analysis.py

import numpy as np

from mna_builder.stamp_context import StampContext

MAX_ITER = 50
TOLERANCE = 1e-8

class TransientAnalysis:
    def __init__(self, circuit, builder, solver):
        self.circuit = circuit
        self.builder = builder
        self.solver = solver


    def run(self):
        analysis = self.circuit.circuit_analysis

        tstart = analysis["tstart"]
        tstop  = analysis["tstop"]
        dt     = analysis["tstep"]

        time_points = np.arange(tstart + dt, tstop + dt, dt)

        # Init state
        vs_index = self.builder.build_vs_index("tran")

        # DC Operating point
        ctx_dc = StampContext(
            vs_index=vs_index,
            mode="dc"
        )

        A_dc, z_dc = self.builder.build(ctx_dc)
        x = self.solver.solve_linear(A_dc, z_dc)
        x_prev = np.zeros_like(x) # .IC = 0 

        results = []

        # Backward Euler loop 
        for t in time_points:
            ctx = StampContext(
                vs_index=vs_index,
                mode="tran",
                time=t,
                dt=dt,
                x_prev=x_prev
            )

            A, z = self.builder.build(ctx)

            # Linear solve (Tạm thời chưa giải bằng Newton)
            x = self.solver.solve_linear(A, z)

            x_prev = x.copy()
            results.append((t, x.copy()))

        return results