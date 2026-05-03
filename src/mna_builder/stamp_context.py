class StampContext:
    def __init__(self,
                vs_index=None,
                mode="dc",
                omega=0.0,
                time=0.0,
                dt=0.0,
                x=None,
                x_prev=None):

        self.vs_index = vs_index
        self.mode = mode
        self.omega = omega
        self.time = time
        self.dt = dt
        self.x = x
        self.x_prev = x_prev
