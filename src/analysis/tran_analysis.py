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

        time_points = np.arange(tstart, tstop + dt, dt)

        # Init state
        vs_index = self.builder.build_vs_index("tran")
        n = len(self.builder.node_map)
        m = len(vs_index)
        size = n + m

        x = np.zeros(size)
        x_prev = x.copy()

        results = []

        # Backward Euler loop 
        for t in time_points:
            # Predictor
            x_guess = x_prev.copy()

            # Newton loop method
            for _ in range(MAX_ITER):
                ctx = StampContext(
                    mode="tran",
                    time=t,
                    dt=dt,
                    x=x_guess,
                    x_prev=x_prev
                )

                A, z = self.builder.build(ctx)\
                
                Jac = A
                Res = A @ x_guess - z

                try:
                    dx = self.solver.solve_linear(Jac, -Res)
                except Exception as e:
                    raise RuntimeError(f"Linear solve failed at t = {t}: {e}")
                
                x_guess = x_guess + dx

                # Convergence
                if np.linalg.norm(dx, np.inf) < TOLERANCE:
                    break

            else:
                raise RuntimeError(f"Newton failed to converge at t = {t}")
            
            # Accept step
            x = x_guess
            x_prev = x.copy()

            results.append((t, x.copy()))

        return results