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

        # Initial conditions
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

        # Backward Euler loop (time stepping)
        for t in time_points:
            # Predictor
            x_guess = x_prev.copy()

            # Newton loop (nonlinear solve)
            for iter_count in range(MAX_ITER):
                ctx = StampContext(
                    vs_index=vs_index,
                    mode="tran",
                    time=t,
                    dt=dt,
                    x=x_guess,
                    x_prev=x_prev
                )

                A, z = self.builder.build(ctx)

                # Residual:
                # F(x) = A(x) - z(x)
                Res = A @ x_guess - z

                try:
                    dx = self.solver.solve_linear(A, -Res)
                except Exception as e:
                    raise RuntimeError(f"Linear solve failed at t={t}: {e}")

                # Damping giảm dần theo iteration
                # alpha = 1.0 / (1 + iter_count * 0.1)   # hoặc đơn giản hơn:
                # alpha = min(1.0, 0.1 + iter_count * 0.05)
                # x_guess += alpha * dx
                x_guess += dx

                # Convergence check
                if np.linalg.norm(dx, np.inf) < TOLERANCE:
                    break

            else:
                raise RuntimeError(f"Newton failed at t={t}")
            
            # Accept timestep
            x_prev = x_guess.copy()

            results.append((t, x_prev.copy()))

        return results