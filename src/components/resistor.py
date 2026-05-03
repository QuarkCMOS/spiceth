# components/resistor.py

from components.base import Component

class Resistor(Component):
    def __init__(self, name, node_i, node_j, value):
        self.name = name
        self.i = node_i
        self.j = node_j
        self.R = value


    def stamp(self, A, z, ctx):
        if self.R == 0:
            raise ValueError(f"{self.name}: Resistance cannot be zero")

        g = 1 / self.R

        # A matrix
        if self.i is not None:
            A[self.i, self.i] += g

        if self.j is not None:
            A[self.j, self.j] += g

        if self.i is not None and self.j is not None:
            A[self.i, self.j] -= g
            A[self.j, self.i] -= g

          
    # Debug
    def __repr__(self):
        return f"Resistor({self.name}, {self.i}, {self.j}, {self.R}) at {hex(id(self))}"
