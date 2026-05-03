# components/current_source.py

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


    def stamp(self, A, z, ctx):
        if ctx.mode == "dc":
            I = self.dc if self.dc is not None else 0.0
        elif ctx.mode == "ac":
            I = self.ac if self.ac is not None else 0.0
        elif ctx.mode == "tran":
            # (Hien tai dung tam DC value)
            I = self.dc if self.dc is not None else 0.0
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