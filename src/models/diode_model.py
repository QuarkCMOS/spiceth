# models/diode_model.py

class DiodeModel:
    def __init__(self,
                name,
                Is=1e-14,
                n=1.0,
                temp=300):
        
        self.name = name
        self.Is = Is
        self.n = n
        self.temp = temp

        self.Vt = 1.380649e-23 * temp / 1.602176634e-19