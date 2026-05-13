# components/controlled/ccvs.py
# Current-Controlled Voltage Source (CCVS)

from components.base import Component

class CCVS(Component):
    # Khai bao CCVS giua node i va j, dieu khien boi dong dien giua node k va l
    def __init__(self, name, np, nm, Vctrl, value):
        self.name = name
        self.n_p = np
        self.n_m = nm
        self.Vctrl = Vctrl # Ten nguon ap dieu khien
        self.Rm = value # Tranresistance


    def stamp(self, A, z, ctx):
        if self.Vctrl not in ctx.vs_index:
            raise ValueError(f"{self.name}: control source {self.Vctrl} not found")
        if self.name not in ctx.vs_index:
            raise ValueError(f"{self.name}: CCVS not indexed")
        if self.Vctrl == self.name:
            raise ValueError(f"{self.name}: CCVS cannot control itself")
        
        k_ctrl = ctx.vs_index[self.Vctrl]  # dong dien khien
        k = ctx.vs_index[self.name]        # bien dong

        # Stamp A
        if self.n_p is not None:
            A[self.n_p, k] += 1
            A[k, self.n_p] += 1

        if self.n_m is not None:
            A[self.n_m, k] -= 1
            A[k, self.n_m] -= 1

        A[k, k_ctrl] -= self.Rm


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"CCVS({self.name}, {self.n_p}, {self.n_m}, {self.Vctrl}, {self.Rm}) at {hex(id(self))}"