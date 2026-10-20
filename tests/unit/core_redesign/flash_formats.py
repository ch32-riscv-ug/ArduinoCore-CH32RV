"""EXP-04 host-only format candidates; no MCU RAM/time or power-loss guarantee.

CRC candidates assume detectable corruption for the tested injection set. A CRC
collision, out-of-region damage, concurrent access, or loss of older bank data
can invalidate the old/new result. Region ownership is a caller precondition.
"""
import struct
import zlib


HEADER = 16
FOOTER = 12
LIMIT = 0xffffffff


class Refused(ValueError):
    pass


def align(value, unit):
    return (value + unit - 1) // unit * unit


class Store:
    def __init__(self, medium, kind="blob", policy="overwrite", commit=False, blob_size=8, max_keys=2):
        if kind not in ["raw", "blob", "keys"] or policy not in ["overwrite", "two-bank"] or type(commit) is not bool:
            raise Refused("unknown candidate")
        if kind == "raw" and (policy != "overwrite" or commit):
            raise Refused("raw has no generation or commit metadata")
        for value in [blob_size, max_keys]:
            if type(value) is not int or not 0 <= value <= 65535:
                raise Refused("bounded payload required")
        self.medium, self.kind, self.policy, self.commit = medium, kind, policy, commit
        self.blob_size, self.max_keys = blob_size, max_keys
        self.active = None
        self.closed = False
        self.banks = 2 if policy == "two-bank" else 1
        units = len(medium.data) // medium.erase_unit
        self.bank_bytes = (units // self.banks) * medium.erase_unit
        self.footer_bytes = align(FOOTER, medium.program_unit) if commit else 0
        self.header_bytes = 0 if kind == "raw" else HEADER
        self.usable = units % self.banks == 0 and self.bank_bytes > 0
        self.capacity = max(0, self.bank_bytes - self.footer_bytes - self.header_bytes) if self.usable else 0
        self.operations = []
        self.position = 0

    def layout(self):
        payload = self.blob_size if self.kind != "keys" else self.max_keys * 6
        minimum_record = align(self.header_bytes + payload, self.medium.program_unit) + self.footer_bytes
        fits = self.usable and minimum_record <= self.bank_bytes
        return dict(kind=self.kind, policy=self.policy, commit=self.commit, banks=self.banks,
                    reserved_bytes=len(self.medium.data), bank_bytes=self.bank_bytes,
                    header_bytes=self.header_bytes, footer_allocated_bytes=self.footer_bytes,
                    configured_payload_bytes=payload, max_payload_bytes=self.capacity,
                    record_program_bytes=minimum_record, supported=fits,
                    protection="unvalidated" if self.kind == "raw" else "CRC-conditioned host model")

    def _encode_value(self, value):
        if self.kind != "keys":
            if type(value) is not bytes or len(value) != self.blob_size:
                raise Refused("exact immutable blob required")
            return value
        if type(value) is not dict or len(value) > self.max_keys:
            raise Refused("bounded numeric key map required")
        for key, item in value.items():
            if type(key) is not int or not 0 <= key <= 65535 or type(item) is not int or not 0 <= item <= LIMIT:
                raise Refused("uint16 key and uint32 value required")
        return b"".join(struct.pack("<HI", key, value[key]) for key in sorted(value))

    def _decode_value(self, payload):
        if self.kind != "keys":
            if len(payload) != self.blob_size:
                raise Refused("unexpected blob size")
            return payload
        if len(payload) % 6 or len(payload) // 6 > self.max_keys:
            raise Refused("invalid key extent")
        result = {}; previous = -1
        for key, value in struct.iter_unpack("<HI", payload):
            if key <= previous:
                raise Refused("keys must be distinct and ordered")
            result[key] = value; previous = key
        return result

    def _bank(self, index):
        offset = index * self.bank_bytes
        data = self.medium.read(offset, self.bank_bytes)
        if all(value == 255 for value in data):
            return dict(status="empty", generation=0, bank=index, value=None)
        invalid = dict(status="corrupt", generation=0, bank=index, value=None)
        if len(data) < HEADER:
            return invalid
        magic, generation, size, version, checksum = struct.unpack("<4sIHHI", data[:HEADER])
        expected_magic = b"KEY1" if self.kind == "keys" else b"BLB1"
        if magic != expected_magic or not generation or version != 1 or size > self.capacity:
            return invalid
        payload = data[HEADER:HEADER + size]
        if zlib.crc32(data[:12] + payload) & LIMIT != checksum:
            return invalid
        if self.commit:
            footer = data[self.bank_bytes - self.footer_bytes:self.bank_bytes - self.footer_bytes + FOOTER]
            if footer != struct.pack("<4sII", b"END1", generation, checksum):
                return invalid
        try:
            value = self._decode_value(payload)
        except Refused:
            return invalid
        return dict(status="valid", generation=generation, bank=index, value=value)

    def load(self):
        if not self.layout()["supported"]:
            return dict(status="no-space", generation=0, bank=None, value=None)
        if self.kind == "raw":
            return dict(status="unvalidated", generation=0, bank=0,value=self.medium.read(0, self.blob_size))
        candidates = [self._bank(i) for i in range(self.banks)]
        valid = [row for row in candidates if row["status"] == "valid"]
        if valid:
            valid.sort(key=lambda row: row["generation"], reverse=True)
            if len(valid) == 2 and valid[0]["generation"] == valid[1]["generation"]:
                return dict(status="ambiguous", generation=0, bank=None, value=None)
            return valid[0]
        return dict(status="empty" if all(row["status"] == "empty" for row in candidates) else "corrupt",
                    generation=0, bank=None, value=None)

    def begin(self, value, recover_corrupt=False):
        if type(recover_corrupt) is not bool:
            raise Refused("explicit recovery flag required")
        if self.closed or self.active is not None:
            raise Refused("closed or busy")
        payload = self._encode_value(value)
        if not self.layout()["supported"] or len(payload) > self.capacity:
            raise Refused("no space for configured format")
        current = self.load()
        if current["status"] == "ambiguous" or (current["status"] == "corrupt" and not recover_corrupt):
            raise Refused("explicit corrupt-region replacement required")
        generation = current["generation"] + 1
        if generation > LIMIT:
            raise Refused("generation exhausted")
        bank = 1 - current["bank"] if self.banks == 2 and current["status"] == "valid" else 0
        base = bank * self.bank_bytes
        if self.kind == "raw":
            body = payload; checksum = 0
        else:
            magic = b"KEY1" if self.kind == "keys" else b"BLB1"
            prefix = struct.pack("<4sIHH", magic, generation, len(payload), 1)
            checksum = zlib.crc32(prefix + payload) & LIMIT
            body = prefix + struct.pack("<I", checksum) + payload
        unit = self.medium.program_unit
        body += bytes([255]) * (align(len(body), unit) - len(body))
        operations = [("erase", offset, b"") for offset in range(base, base + self.bank_bytes, self.medium.erase_unit)]
        operations += [("program", base + i, body[i:i + unit]) for i in range(0, len(body), unit)]
        if self.commit:
            footer = struct.pack("<4sII", b"END1", generation, checksum)
            footer += bytes([255]) * (self.footer_bytes - FOOTER)
            start = base + self.bank_bytes - self.footer_bytes
            operations += [("program", start + i, footer[i:i + unit]) for i in range(0, len(footer), unit)]
        self.active = dict(generation=generation,bank=bank)
        self.operations = operations; self.position = 0

    def step(self, cut_mask=None):
        if self.closed or self.active is None:
            raise Refused("no live update")
        operation, offset, data = self.operations[self.position]
        try:
            if operation == "erase":self.medium.erase(offset, cut_mask)
            else:self.medium.program(offset, data, cut_mask)
        except Exception:
            # Host fault operation has ended. A new scanner is needed; this is
            # not a claim that an MCU peripheral is safe after a timeout.
            self.closed = True;self.active = None
            raise
        self.position += 1
        if self.position == len(self.operations):
            self.active = None
            return "done"
        return "pending"

    def abandon(self):
        # Drop volatile progress at an operation boundary, with no rollback.
        self.closed = True;self.active = None

    def change(self, key, value):
        if self.kind != "keys":
            raise Refused("numeric key candidate required")
        current = self.load()
        if current["status"] not in ["valid", "empty"]:
            raise Refused("key map is not readable")
        mapping = {} if current["value"] is None else dict(current["value"])
        if type(key) is not int or not 0 <= key <= 65535:
            raise Refused("uint16 key required")
        if value is None:mapping.pop(key, None)
        else:mapping[key] = value
        self.begin(mapping)
