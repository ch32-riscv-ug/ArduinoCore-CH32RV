"""Conservative EXP-04 media model; no hardware fault or durability guarantee.

Each program unit can be attempted once after a completed erase. A torn erase
does not make any of its units reusable. This hidden model history is not a
persisted allocation bitmap and cannot be reconstructed by scanning for 0xff.
"""


class PowerCut(RuntimeError):
    pass


class Medium:
    def __init__(self, program_unit, erase_unit, erase_units):
        for value in [program_unit, erase_unit, erase_units]:
            if type(value) is not int or value <= 0:
                raise ValueError("positive integer geometry required")
        if erase_unit % program_unit:
            raise ValueError("program unit must divide erase unit")
        self.program_unit, self.erase_unit = program_unit, erase_unit
        self.data = bytearray([255]) * (erase_unit * erase_units)
        self.attempted = [False] * (len(self.data) // program_unit)
        self.operations = []

    def read(self, offset, length):
        self._bounds(offset, length)
        return bytes(self.data[offset:offset + length])

    def _bounds(self, offset, length):
        if type(offset) is not int or type(length) is not int or offset < 0 or length < 0 or offset > len(self.data) or length > len(self.data) - offset:
            raise ValueError("out of region")

    @staticmethod
    def _cut_mask(mask, length):
        if mask is None:
            return bytes([255]) * length
        if not isinstance(mask, bytes) or len(mask) != length:
            raise ValueError("one cut-mask byte per operation byte")
        return mask

    def program(self, offset, payload, cut_mask=None):
        if not isinstance(payload, bytes):
            raise ValueError("immutable bytes required")
        self._bounds(offset, len(payload))
        if offset % self.program_unit or len(payload) != self.program_unit:
            raise ValueError("one aligned program unit required")
        mask = self._cut_mask(cut_mask, self.program_unit)
        slot = offset // self.program_unit
        if self.attempted[slot] or any(b != 255 for b in self.data[offset:offset + len(payload)]):
            raise ValueError("fresh program unit required")
        self.attempted[slot] = True  # Even an all-0xff attempt consumes the model unit.
        for i, value in enumerate(payload):
            self.data[offset + i] &= value | (255 ^ mask[i])
        self.operations.append(("program", offset, len(payload), cut_mask is not None))
        if cut_mask is not None:
            raise PowerCut("selected program-bit subset; no further operation executed")

    def erase(self, offset, cut_mask=None):
        self._bounds(offset, self.erase_unit)
        if offset % self.erase_unit:
            raise ValueError("one aligned erase unit required")
        mask = self._cut_mask(cut_mask, self.erase_unit)
        begin = offset // self.program_unit
        count = self.erase_unit // self.program_unit
        # Once erase starts, no freshness is inferred until successful completion.
        self.attempted[begin:begin + count] = [cut_mask is not None] * count
        for i, bits in enumerate(mask):
            self.data[offset + i] |= bits
        self.operations.append(("erase", offset, self.erase_unit, cut_mask is not None))
        if cut_mask is not None:
            raise PowerCut("selected erase-bit subset; no further operation executed")
