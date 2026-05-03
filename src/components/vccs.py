# components/vccs.py
# Voltage-Controlled Current Source (VCCS)

from components.base import Component

class VCCS(Component):
    # Khai bao VCCS giua node i va j, dieu khien boi dien ap giua node k va l
    def __init__(self, name, np, nm, ncp, ncm, value):
        self.name = name
        self.n_p = np    # output +
        self.n_m = nm    # output -
        self.nc_p = ncp  # control +
        self.nc_m = ncm  # control -
        self.Gm = value  # Transconductance (S)


    def stamp(self, A, z, ctx):
        # Stamp A
        if self.n_p is not None and self.nc_p is not None:
            A[self.n_p, self.nc_p] += self.Gm

        if self.n_p is not None and self.nc_m is not None:
            A[self.n_p, self.nc_m] -= self.Gm

        if self.n_m is not None and self.nc_p is not None:
            A[self.n_m, self.nc_p] -= self.Gm

        if self.n_m is not None and self.nc_m is not None:
            A[self.n_m, self.nc_m] += self.Gm


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"VCCS({self.name}, {self.n_p}, {self.n_m}, {self.nc_p}, {self.nc_m}, {self.Gm}) at {hex(id(self))}"