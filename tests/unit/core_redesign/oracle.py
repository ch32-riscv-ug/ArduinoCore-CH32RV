"""Independent host specification: unbounded Python values and transaction ledgers.

This module is never used by the C++ control or cross-compiled to the target.
The screen oracle materializes whole frames; the control renders 32-byte chunks.
"""

from collections import deque
import struct


def clamp(value, low, high):
    return max(low, min(high, value))


class KnobOracle:
    def __init__(self):
        self.position, self.step_size, self.low, self.high = 0, 1, -1000, 1000
        self.sequence, self.status, self.delta = 0, 0, 0
        self.receiving = None
        self.config = None
        self.public = bytes(8)
        self.snapshot = None
        self.cursor, self.reply, self.accepted = 0, -1, -1

    def apply(self, line):
        op, *args = line.split()
        args = list(map(int, args))
        if op == "input":
            total = self.delta + args[0]
            self.delta = clamp(total, -32768, 32767)
            self.status |= int(total != self.delta)
        elif op == "begin_write":
            self.status |= 4 if self.receiving is not None else 0
            self.snapshot = None
            self.accepted = int(self.config is None)
            self.status |= 0 if self.accepted else 2
            self.receiving = [] if self.accepted else None
            self.length = 0
        elif op == "byte" and self.receiving is not None:
            self.receiving.append(args[0])
        elif op == "stop":
            if self.receiving is not None:
                if len(self.receiving) == 8:
                    self.config = struct.unpack("<Hhhh", bytes(self.receiving))
                else:
                    self.status |= 4
            self.receiving = None
            self.snapshot = None
        elif op in ("restart", "begin_read"):
            self.status |= 4 if self.receiving is not None else 0
            self.receiving = None
            self.snapshot = self.public if op == "begin_read" else None
            if op == "begin_read":
                self.cursor = 0
        elif op == "read":
            self.reply = self.snapshot[self.cursor] if self.snapshot is not None and self.cursor < 8 else 0
            self.cursor = min(8, self.cursor + 1)
        elif op == "step":
            if self.config is not None:
                seq, step, low, high = self.config
                if 1 <= step <= 127 and low <= high:
                    self.sequence, self.step_size, self.low, self.high = seq, step, low, high
                else:
                    self.status |= 4
                self.config = None
            self.position = clamp(self.position + self.delta * self.step_size, self.low, self.high)
            self.delta = 0
            self.public = struct.pack("<iHBB", self.position, self.sequence, self.status, 0)
        return [self.position, self.step_size, self.low, self.high, self.sequence,
                self.status, self.delta, int(self.config is not None),
                int(self.snapshot is not None), self.cursor, self.reply, self.accepted]


class PpsOracle:
    def __init__(self):
        self.connection = 0
        self.online = False
        self.desired = 5000
        self.confirmed = None
        self.last_confirmed = 0
        self.commands = deque()
        self.selected = None
        self.transactions = []
        self.current = None
        self.accepted = self.rejected = 0
        self.time = 0
        self.mv = self.ma = 0
        self.stale = True

    def apply(self, line):
        op, *args = line.split()
        args = list(map(int, args))
        if op == "attach":
            self.connection += 1
            self.online = True
            self.confirmed = self.selected = None
        elif op == "detach":
            self.online = False
            self.confirmed = self.selected = None
            if self.current is not None and self.current["outcome"] is None:
                self.current["outcome"] = False
        elif op == "key":
            if len(self.commands) < 4:
                self.commands.append((args[0], self.connection if self.online else 0)); self.accepted += 1
            else:
                self.rejected += 1
        elif op == "sample":
            ok, mv, ma = args
            if ok:
                self.mv, self.ma = mv, ma
            self.stale = not bool(ok)
        elif op == "time":
            self.time = args[0]
            if self.current and self.current["outcome"] is None and self.current["due"] <= self.time:
                self.current["outcome"] = True
        elif op == "finish":
            request, session, success = args
            if self.current and (request, session) == (self.current["id"], self.current["session"]):
                if self.current["outcome"] is None:
                    self.current["outcome"] = bool(success)
        elif op == "step":
            for key, connection in list(self.commands):
                if key:
                    self.desired = clamp(self.desired + 20 * key, 5000, 11000)
                elif self.online and connection == self.connection:
                    self.selected = self.desired
            self.commands.clear()
            if self.current and self.current["outcome"] is not None:
                if self.online and self.current["session"] == self.connection and self.current["outcome"]:
                    self.confirmed = self.current["value"]
                    self.last_confirmed = self.confirmed
                self.current["returned"] = True
                self.current = None
            if self.online and self.current is None and self.selected is not None:
                self.current = dict(id=len(self.transactions) + 1, session=self.connection,
                                    value=self.selected, outcome=None, due=self.time + 5, returned=False)
                self.transactions.append(self.current)
                self.selected = None
        return [self.connection, int(self.online), self.desired, int(self.confirmed is not None),
                self.last_confirmed, self.transactions[-1]["value"] if self.transactions else 0,
                len(self.transactions), int(self.current is not None),
                int(self.current is not None and self.current["outcome"] is not None),
                len(self.transactions), sum(t["returned"] for t in self.transactions),
                self.accepted, self.rejected, self.mv, self.ma, int(self.stale)]


GLYPHS = {
    "0": [31, 17, 31], "1": [18, 31, 16], "2": [29, 21, 23], "3": [21, 21, 31],
    "4": [7, 4, 31], "5": [23, 21, 29], "6": [31, 21, 29], "7": [1, 1, 31],
    "8": [31, 21, 31], "9": [23, 21, 31], "-": [4, 4, 4],
    "D": [31, 17, 14], "C": [31, 17, 17], "V": [15, 16, 15],
    "A": [30, 5, 30], "S": [23, 21, 29], "L": [31, 16, 16], " ": [0, 0, 0],
}


def render_frame(desired=5000, confirmed=0, mv=0, ma=0, valid=False, stale=True, online=False):
    def number(value):
        return "-----" if value < 0 else f"{value % 100000:05}"
    lines = ["D" + number(desired), "C" + (number(confirmed) if valid else "-----"),
             "V" + number(mv), "A" + number(ma), "", f"L{int(online)} S{int(stale)} "]
    frame = bytearray(1024)
    for page, text in enumerate(lines):
        for cell, char in enumerate(text):
            frame[128 * page + 4 * cell:128 * page + 4 * cell + 3] = bytes(GLYPHS[char])
    return bytes(frame)


def fnv(data, initial=2166136261):
    value = initial
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


class ScreenOracle:
    def __init__(self):
        self.latest = render_frame()
        self.revision, self.frame_revision, self.displayed_revision = 1, 0, 0
        self.frame = None
        self.offset = self.now = self.due = self.next_frame = self.frames = 0
        self.transfer = None
        self.outcome = None
        self.tx = bytes(32)
        self.displayed_hash = 0

    def apply(self, line):
        op, *args = line.split()
        args = list(map(int, args))
        if op == "view":
            desired, confirmed, valid = args
            self.latest = render_frame(desired, confirmed, 5000, 1000, bool(valid), False, True)
            self.revision += 1
        elif op == "time":
            self.now = args[0]
            if self.transfer is not None and self.now >= self.due and self.outcome is None:
                self.outcome = True
        elif op == "fail":
            if self.transfer is not None and self.outcome is None:
                self.outcome = False
        elif op == "step":
            if self.transfer is not None and self.outcome is not None:
                self.transfer = None
                if not self.outcome:
                    self.frame = None
                    self.next_frame = self.now + 100
                else:
                    self.offset += 32
                    if self.offset == 1024:
                        self.displayed_revision = self.frame_revision
                        self.displayed_hash = fnv(self.frame)
                        self.frames += 1
                        self.frame = None
                self.outcome = None
            if self.frame is None and self.revision != self.displayed_revision and self.now >= self.next_frame:
                self.frame = self.latest
                self.frame_revision = self.revision
                self.offset = 0
                self.next_frame = self.now + 100
            if self.frame is not None and self.transfer is None:
                self.tx = self.frame[self.offset:self.offset + 32]
                self.transfer = self.tx
                self.due = self.now + 2
        return [self.revision, self.frame_revision, self.displayed_revision, self.offset,
                int(self.transfer is not None), int(self.outcome is not None), fnv(self.tx),
                self.displayed_hash, self.frames]
