# components/basic/capacitor.py

from components.base import Component

class Capacitor(Component):
    def __init__(self, name, node_i, node_j, value):
        self.name = name
        self.i = node_i
        self.j = node_j
        self.C = value


    def stamp(self, A, z, ctx):
        if ctx.mode == "dc":
            return  # DC case: open circuit

        elif ctx.mode == "ac":
            Yc = 1j * ctx.omega * self.C

            if self.i is not None:
                A[self.i, self.i] += Yc

            if self.j is not None:
                A[self.j, self.j] += Yc

            if self.i is not None and self.j is not None:
                A[self.i, self.j] -= Yc
                A[self.j, self.i] -= Yc


        elif ctx.mode == "tran":
            g = self.C / ctx.dt

            v_i_prev = ctx.x_prev[self.i] if self.i is not None else 0.0
            v_j_prev = ctx.x_prev[self.j] if self.j is not None else 0.0
            v_prev = v_i_prev - v_j_prev

            Ieq = g * v_prev

            # Stamp A
            if self.i is not None:
                A[self.i, self.i] += g

            if self.j is not None:
                A[self.j, self.j] += g

            if self.i is not None and self.j is not None:
                A[self.i, self.j] -= g
                A[self.j, self.i] -= g

            # Stamp z
            if self.i is not None:
                z[self.i] += Ieq

            if self.j is not None:
                z[self.j] -= Ieq

        
    def __repr__(self):
        return f"Capacitor({self.name}, {self.i}, {self.j}, {self.C}) at {hex(id(self))}"