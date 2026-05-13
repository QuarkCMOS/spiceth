# components/basic/inductor.py

from components.base import Component

class Inductor(Component):
    def __init__(self, name, node_i, node_j, value):
        self.name = name
        self.i = node_i
        self.j = node_j
        self.L = value

    
    def stamp(self, A, z, ctx):

        

        # DC (short circuit)
        if ctx.mode == "dc":
            if self.name not in ctx.vs_index:
                raise ValueError(f"{self.name}: Inductor not indexed")

            k = ctx.vs_index[self.name]

            if self.i is not None:
                A[self.i, k] += 1
                A[k, self.i] += 1

            if self.j is not None:
                A[self.j, k] -= 1
                A[k, self.j] -= 1
        
        # AC   Y = 1/(jωL)
        elif ctx.mode == "ac":
            Yl = 1 / (1j * ctx.omega * self.L)

            if self.i is not None:
                A[self.i, self.i] += Yl

            if self.j is not None:
                A[self.j, self.j] += Yl

            if self.i is not None and self.j is not None:
                A[self.i, self.j] -= Yl
                A[self.j, self.i] -= Yl

        # TRAN   i = i_prev + dt/L * v
        elif ctx.mode == "tran":
            if self.name not in ctx.vs_index:
                raise ValueError(f"{self.name}: Inductor not indexed")

            k = ctx.vs_index[self.name]

            g = self.L / ctx.dt

            i_prev = ctx.x_prev[k]

            # Ma tran A
            if self.i is not None:
                A[self.i, k] += 1
                A[k, self.i] += 1

            if self.j is not None:
                A[self.j, k] -= 1
                A[k, self.j] -= 1

            A[k, k] -= g

            # RHS
            z[k] -= g *i_prev


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"Inductor({self.name}, {self.i}, {self.j}, {self.L}) at {hex(id(self))}"