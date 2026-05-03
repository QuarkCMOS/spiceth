# analysis/ac_analysis.py

import numpy as np
from mna_builder.stamp_context import StampContext

class ACAnalysis:
    def __init__(self, circuit, builder, solver):
        self.circuit = circuit
        self.builder = builder
        self.solver = solver

    
    # Tao cac diem tinh tren mien tan so
    def generate_frequencies(self, analysis):
        sweep = analysis["sweep"]
        points = analysis["points"]
        f_start = analysis["f_start"]
        f_end = analysis["f_end"]

        # Decade
        if sweep == "DEC":
            decades = np.log10(f_end) - np.log10(f_start)
            num_points = int(points * decades) + 1
            return np.logspace(np.log10(f_start), np.log10(f_end), num_points)
        
        # Linear
        elif sweep == "LIN":
            return np.linspace(f_start, f_end, points)
    
        # 
        elif sweep == "OCT":
            octaves = np.log2(f_end / f_start)
            num_points = int(points * octaves) + 1
            return f_start * 2 ** np.linspace(0, octaves, num_points)

        else:
            raise ValueError("Unkown sweep type")
        

    # Ham chay
    def run(self):
        analysis = self.circuit.circuit_analysis
        freqs = self.generate_frequencies(analysis)

        results = []

        # Stamp va solve tren thang tan so
        for f in freqs:
            omega = 2 * np.pi * f

            ctx = StampContext(
                mode="ac",
                omega=omega
            )

            A, z = self.builder.build(ctx)
            x = self.solver.solve_linear(A, z)

            results.append((f, x))

        return results
