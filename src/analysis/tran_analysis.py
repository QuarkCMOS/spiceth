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
        size = len(self.circuit.node_map) + len(vs_index)
        x_prev = np.zeros(size) # Default .IC = 0 

        for kind, name, value in self.circuit.initial_conditions:
            if kind == "V":
                if name not in self.circuit.node_map:
                    raise ValueError(f".IC unknown node: {name}")
                idx = self.circuit.node_map[name]
                x_prev[idx] = value

            if kind == "I":
                if name not in self.circuit.node_map:
                    raise ValueError(f".IC unknown branch current: {name}")
                idx = vs_index[name]
                x_prev[idx] = value

        results = [(tstart, x_prev.copy())]

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