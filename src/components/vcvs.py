# components/vcvs.py
# Voltage-Controlled Voltage Source (VCVS)

from components.base import Component

class VCVS(Component):
    # Khai bao VCVS giua node i va j, dieu khien boi dien ap giua node k va l
    def __init__(self, name, np, nm, ncp, ncm, value):
        self.name = name
        self.n_p = np   # output +
        self.n_m = nm   # output -
        self.nc_p = ncp # control +
        self.nc_m = ncm # control -
        self.A = value  # Voltage gain


    def stamp(self, A, z, ctx):
        if self.name not in ctx.vs_index:
            raise ValueError(f"{self.name}: VCVS not indexed")
        
        k = ctx.vs_index[self.name]

        # Stamp A
        if self.n_p is not None:
            A[self.n_p, k] += 1
            A[k, self.n_p] += 1

        if self.n_m is not None:
            A[self.n_m, k] -= 1
            A[k, self.n_m] -= 1

        if self.nc_p is not None:
            A[k, self.nc_p] -= self.A

        if self.nc_m is not None:
            A[k, self.nc_m] += self.A


    # Hien thi thong tin linh kien (cho debug)
    def __repr__(self):
        return f"VCVS({self.name}, {self.n_p}, {self.n_m}, {self.nc_p}, {self.nc_m}, {self.A}) at {hex(id(self))}"