# components/base.

class Component:
    # Format:
    def stamp(self, A, z, ctx):
        # A: Ma tran bien x
        # z: Vector dong dien tuong duong
        # ctx: Context cua component
        raise NotImplementedError("stamp() not implemented")