# components/voltage_source.py

from components.base import Component

class VoltageSource(Component):
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

    def stamp(self, A, z, ctx):
        if self.name not in ctx.vs_index:
            raise ValueError(f"{self.name}: Voltage source not indexed")

        k = ctx.vs_index[self.name]

        # chon gia tri theo mode
        if ctx.mode == "ac":
            V = self.ac
        elif ctx.mode == "tran":
            # Hien tai dung tam DC value
            if self.tran:
                V = self.tran(ctx.time)
            else:
                V = self.dc
        else:
            V = self.dc

        # Stamp A
        if self.i is not None:
            A[self.i, k] += 1
            A[k, self.i] += 1

        if self.j is not None:
            A[self.j, k] -= 1
            A[k, self.j] -= 1

        # Stamp z
        z[k] += V


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"VoltageSource({self.name}, {self.i}, {self.j}, {self.dc}, {self.ac}, {self.tran}) at {hex(id(self))}"