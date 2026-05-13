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

        #Vlim = n * Vt * 40  # ~1.04V với n=1, Vt=0.02585
        #if Vd > Vlim:
        #    Vd = Vlim

        Id = Is * (np.exp(Vd / (n * Vt)) - 1)
        Gd = Is / (n * Vt) * np.exp(Vd / (n * Vt))
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