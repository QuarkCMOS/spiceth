# components/nonlinear/diode.py

import numpy as np

from components.base import Component

class Diode(Component):
    def __init__(self, name, anode, cathode, model):
        self.name = name
        self.a = anode
        self.c = cathode
        self.model = model


    def stamp(self, A, z, ctx):
        Is = self.model.Is
        n = self.model.n
        Vt = self.model.Vt

        v_a = ctx.x[self.a] if self.a is not None else 0.0
        v_c = ctx.x[self.c] if self.c is not None else 0.0
        Vd  = v_a - v_c

        v_a_prev = ctx.x_prev[self.a]
        v_c_prev = ctx.x_prev[self.c]
        Vd_prev = v_a_prev - v_c_prev

        # Voltage limiting
        # Giới hạn bước nhảy Vd theo từng Newton iteration
        V_LIMIT = 3 * n * Vt
        dv = np.clip(Vd - Vd_prev, -V_LIMIT, V_LIMIT)
        Vd = Vd_prev + dv

        # Exponential limiting: tuyến tính hoá khi Vd quá lớn
        # Ngưỡng này phải đủ cao để diode mô hình đúng (~0.7V trở lên)
        V_CRIT = n * Vt * np.log(n * Vt / (np.sqrt(2) * Is))  # SPICE Vcrit
        if Vd > V_CRIT:
            # Linearise exp quanh Vcrit để tránh overflow
            exp_crit = np.exp(V_CRIT / (n * Vt))
            arg = exp_crit * (1 + (Vd - V_CRIT) / (n * Vt))
        else:
            arg = np.exp(np.clip(Vd / (n * Vt), -500, 500))
        # arg = np.exp(Vd/(n*Vt))

        Id  = Is * (arg - 1)
        Gd  = Is / (n * Vt) * arg
        Gd  = max(Gd, 1e-12)   # tránh Gd = 0
        Ieq = Id - Gd * Vd

        # Stamp A
        if self.a is not None:
            A[self.a, self.a] += Gd
        if self.c is not None:
            A[self.c, self.c] += Gd
        A[self.a, self.c] -= Gd
        A[self.c, self.a] -= Gd

        # Stamp z
        if self.a is not None:
            z[self.a] -= Ieq
        if self.c is not None:
            z[self.c] += Ieq


    def __repr__(self):
        return f"Diode({self.name}, {self.a}, {self.c}, ({self.model.name}, {self.model.Is}, {self.model.n}, {self.model.Vt})) at {hex(id(self))}"