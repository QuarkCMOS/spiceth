# components/basic/current_source.py

import numpy as np

from components.base import Component

class CurrentSource(Component):
    def __init__(self, name, node_i, node_j,
                dc_value=None,
                ac_value=None,
                transient=None):
        
        self.name = name
        self.i = node_i
        self.j = node_j
        self.dc = dc_value
        self.ac = ac_value
        self.tran = transient


    # Lay cac gia tri cau hinh cho che do transient
    def get_tran_value(self, t):
        tran = self.tran
        if tran is None:
            return self.dc if self.dc is not None else 0.0
        
        # Tao tin hieu xung
        if tran["type"] == "pulse":
            v1 = tran["v1"]
            v2 = tran["v2"]
            td = tran["td"]
            tr = max(tran["tr"], 1e-15)
            tf = max(tran["tf"], 1e-15)
            pw = tran["pw"]
            per = tran["per"]

            if t < td: # Delay
                return v1

            t_mod = (t - td) % per

            if t_mod < tr: # Raise
                return v1 + t_mod * (v2 - v1) / tr
            elif t_mod < tr + pw: # High
                return v2
            elif t_mod < tr + pw + tf: # Fall
                return v2 - (t_mod - (tr + pw)) * (v2 - v1) / tf
            else: # Low
                return v1

        # Tao tin hieu sin
        elif tran["type"] == "sin":
            vo = tran["vo"]
            va = tran["va"]
            freq = tran["freq"]
            td = tran["td"]
            theta = tran["theta"]
            phase = tran["phase"]

            if t < td: # Delay
                return vo + va * np.sin(np.radians(phase))
            else:
                tau = t - td
                return vo + va * np.exp(-theta * (tau)) * np.sin(2 * np.pi * freq * (tau) + np.radians(phase))


    def stamp(self, A, z, ctx):
        if ctx.mode == "dc":
            I = self.dc if self.dc is not None else 0.0
        elif ctx.mode == "ac":
            I = self.ac if self.ac is not None else 0.0
        elif ctx.mode == "tran":
            I = self.get_tran_value(ctx.time)
        else:
            raise ValueError(f"{self.name}: Unknown mode {ctx.mode}")

        # Stamp RHS
        if self.i is not None:
            z[self.i] -= I
        if self.j is not None:
            z[self.j] += I


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"CurrentSource({self.name}, {self.i}, {self.j}, {self.dc}, {self.ac}, {self.tran}) at {hex(id(self))}"