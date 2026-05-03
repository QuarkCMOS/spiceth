# solver/solver.py
import numpy as np

CONDITION_NUMBER = 1e12
TOLERANCE = 1e-10

class Solver:
    def solve_linear(self, G, b):

        # Check condition number
        #cond = np.linalg.cond(G)
        #if cond > CONDITION_NUMBER:
        #    print(f"Warning: ill-conditioned matrix (cond={cond:.2e})")
            
        # Solve
        try:
            x = np.linalg.solve(G, b)
            return x
        except np.linalg.LinAlgError:
            raise ValueError("Singular matrix (invalid circuit)")
            
        #    '''
        #    # Neu fail thi phan tich rank
        #    rank_G = np.linalg.matrix_rank(G, tol=TOLERANCE)
        #   rank_Ab = np.linalg.matrix_rank(np.c_[G, b], tol=TOLERANCE)

        #    if rank_G < rank_Ab:
        #        raise ValueError("Inconsistent circuit (no solution)")
        #    elif rank_G < G.shape[0]:
        #        raise ValueError("Underdetermined circuit (infinite solutions)")
        #    else:
        #        raise ValueError("Numerically singular (ill-conditioned)")
        #    '''
