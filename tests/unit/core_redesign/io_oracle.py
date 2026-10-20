"""Independent lease/transaction ledger for IO-01..04; no physical bus model."""


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


class IoLedger:
    def __init__(self):
        self.opened = [False, False]
        self.generation = [0, 0]
        self.transactions = [None, None]
        self.owners = [None, None]
        self.data = [bytearray(32), bytearray(32)]
        self.last_read = 99
        self.last_notice = 999
        self.last_completion = [0, 0, 0]

    def apply(self, command):
        op, *args = command.split()
        s, *args = map(int, args)
        transaction = self.transactions[s] if op not in {"write", "read"} else None
        result = False
        if op == "write":
            offset, value = args
            result = self.owners[s] is None and offset < 32
            if result:
                self.data[s][offset] = value & 255
        elif op == "read":
            offset, = args
            result = self.owners[s] is None and offset < 32
            if result:
                self.last_read = self.data[s][offset]
        elif op == "open":
            result = not self.opened[s] and transaction is None
            if result:
                self.opened[s] = True
        elif op == "end":
            result = self.opened[s]
            if result:
                self.opened[s] = False
                if transaction:
                    transaction["notice"] = False
                    if transaction["completion"] is None:
                        transaction["cancel"] = True
        elif op == "start":
            buffer, count, mode, address = args
            if not self.opened[s]: result = 4
            elif transaction is not None: result = 1
            elif mode not in [0, 1]: result = 3
            elif buffer == -1 or not 0 < count <= 32 or address > 127: result = 2
            elif self.owners[buffer] is not None: result = 1
            elif self.generation[s] == 0xFFFFFFFF: result = 5
            else:
                result = 0
                self.generation[s] += 1
                self.owners[buffer] = s
                self.transactions[s] = dict(buffer=buffer, count=count, mode=mode,
                                            cancel=False, notice=False, completion=None)
        elif op == "peek":
            result = bool(transaction and self.opened[s] and transaction["notice"])
            if result:
                self.last_notice = self.generation[s]
        else:
            token, *args = args
            match = bool(transaction and token == self.generation[s] and token)
            terminal = bool(match and transaction["completion"] is not None)
            if op == "cancel":
                if not match: result = 3
                elif terminal: result = 2
                elif transaction["cancel"]: result = 1
                else:
                    result = 0
                    transaction["cancel"] = True
            elif op == "ack":
                result = bool(terminal and transaction["notice"] and self.opened[s])
                if result:
                    transaction["notice"] = False
            elif op == "take":
                result = terminal
                if result:
                    self.last_completion = transaction["completion"][:]
                    self.owners[transaction["buffer"]] = None
                    self.transactions[s] = None
            else:
                assert op == "finish"
                outcome, count, fill = args
                result = bool(match and not terminal and outcome in [1, 2, 3] and
                              count <= transaction["count"] and
                              (outcome != 1 or count == transaction["count"]) and
                              (outcome != 3 or transaction["cancel"]))
                if result:
                    data = self.data[transaction["buffer"]]
                    if transaction["mode"] == 1:
                        data[:count] = bytes((fill + i) & 255 for i in range(count))
                    transaction["completion"] = [outcome, count, fnv(data[:count])]
                    transaction["notice"] = self.opened[s]
        row = [int(result), self.last_read, self.last_notice, *self.last_completion]
        for i, t in enumerate(self.transactions):
            row.extend([int(self.opened[i]), int(t is not None),
                        int(t is not None and t["completion"] is not None),
                        int(t is not None and t["cancel"]), int(t is not None and t["notice"]),
                        self.generation[i], t["completion"][0] if t and t["completion"] else 0])
        for owner, data in zip(self.owners, self.data):
            row.extend([int(owner is None), fnv(data) if owner is None else 0])
        return row
