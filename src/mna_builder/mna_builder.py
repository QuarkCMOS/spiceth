# mna_builder/mna_builder.py

import numpy as np

from components.basic.voltage_source import VoltageSource
from components.basic.inductor import Inductor
from components.controlled.vcvs import VCVS
from components.controlled.ccvs import CCVS


class MNABuilder:
    def __init__(self, circuit):
        self.circuit = circuit
        self.node_map = circuit.node_map
        self.vs_index = {}

    def prepare(self, mode):
        self.vs_index = self.build_vs_index(mode)
        return self.vs_index

    def build_vs_index(self, mode):
        vs_list = []
        vs_index = {}

        # Liet ke cac nguon ap (VoltageSource, CCVS, VCVS) va Inductor
        for comp in self.circuit.components:
            if isinstance(comp, (VoltageSource, VCVS, CCVS)):
                vs_list.append(comp)

            elif isinstance(comp, Inductor) and mode in ["dc", "tran"]:
                vs_list.append(comp)

        # Tao index cho nguon ap trong vector x
        base = len(self.node_map) # So node khong tinh GND
        for k, vs in enumerate(vs_list):
            vs_index[vs.name] = base + k

        return vs_index


    # Ma tran MNA
    def build(self, ctx):
        self.vs_index = self.build_vs_index(ctx.mode)

        n = len(self.node_map) # So node (khong tinh GND)
        m = len(self.vs_index)
        size = n + m

        dtype = complex if ctx.mode == "ac" else float

        # Tao ma tran A, vector z
        A = np.zeros((size, size), dtype=dtype)
        z = np.zeros(size, dtype=dtype)

        ctx.vs_index = self.vs_index
        
        for comp in self.circuit.components:
            comp.stamp(A, z, ctx)

        return A, z