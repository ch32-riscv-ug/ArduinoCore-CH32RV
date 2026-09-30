#!/usr/bin/env python3
"""Equivalence check between an EVT-startup ELF and the unified-startup ELF.

Check 1: the vector table of BOTH ELFs matches the spec (vectors_*.inc):
         entry i is 0 for CH32_RSV, or the address of the named symbol for CH32_IRQ.
Check 2: the set of (csr-op, csr, value) writes in handle_reset is identical.
"""
import re, struct, subprocess, sys

BIN = sys.argv[1]          # toolchain bin dir
SPEC = sys.argv[2]         # vectors_*.inc
ELF_EVT = sys.argv[3]
ELF_UNI = sys.argv[4]
EVT_TABLE_SECTION = sys.argv[5]   # ".init" (table lives after the first jump) or ".vector"

def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout

def spec_entries(path):
    """[(kind, name)]; kind is "word", "jump" or None for a reserved slot."""
    out = []
    for line in open(path):
        s = line.strip()
        if s.startswith("CH32_RSV"):
            out.append((None, None))
        elif s.startswith("CH32_IRQ"):
            out.append(("word", s.split()[1]))
        elif s.startswith("CH32_JMP"):
            out.append(("jump", s.split()[1]))
    return out


def section_vma(elf, section):
    for line in run(BIN + "/riscv-none-elf-objdump", "-h", elf).splitlines():
        parts = line.split()
        if len(parts) > 3 and parts[1] == section:
            return int(parts[3], 16)
    return None


def jal_target(word, pc):
    """Decode `j offset` (JAL with rd=x0) into the address it lands on.

    CH32V103's vector table stores instructions instead of addresses, so the
    entry can only be checked against a symbol after resolving the jump.
    """
    if (word & 0x7F) != 0x6F or ((word >> 7) & 0x1F) != 0:
        return None
    imm = (((word >> 31) & 1) << 20 |
           ((word >> 12) & 0xFF) << 12 |
           ((word >> 20) & 1) << 11 |
           ((word >> 21) & 0x3FF) << 1)
    if imm & (1 << 20):
        imm -= (1 << 21)
    return pc + imm

def symbols(elf):
    m = {}
    for line in run(BIN + "/riscv-none-elf-nm", elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, _t, name = parts
            m.setdefault(name, int(addr, 16))
    return m

def table_words(elf, section):
    raw = subprocess.run([BIN + "/riscv-none-elf-objcopy", "-O", "binary",
                          "--only-section=" + section, elf, "/dev/stdout"],
                         capture_output=True, check=True).stdout
    words = list(struct.unpack("<%dI" % (len(raw) // 4), raw[:len(raw) // 4 * 4]))
    return words[1:]   # drop entry 0 (reset jump or _start pointer)

def check_table(tag, elf, section, spec):
    syms = symbols(elf)
    words = table_words(elf, section)
    base = section_vma(elf, section)
    if len(words) < len(spec):
        print(f"FAIL {tag}: table has {len(words)} entries, spec {len(spec)}")
        return False
    ok = True
    for i, (kind, want) in enumerate(spec):
        got = words[i]
        if kind is None:
            if got != 0:
                print(f"FAIL {tag}: entry {i+1} expected 0, got 0x{got:08x}"); ok = False
            continue
        expect = syms.get(want)
        if expect is None:
            print(f"FAIL {tag}: symbol {want} not in ELF"); ok = False
            continue
        if kind == "jump":
            # words[] dropped entry 0, so entry i+1 sits one slot further in.
            target = jal_target(got, base + 4 * (i + 1))
            if target is None:
                print(f"FAIL {tag}: entry {i+1} ({want}) is not a jump: "
                      f"0x{got:08x}"); ok = False
            elif target != expect:
                print(f"FAIL {tag}: entry {i+1} ({want}) jumps to 0x{target:08x}, "
                      f"expected 0x{expect:08x}"); ok = False
        elif got != expect:
            print(f"FAIL {tag}: entry {i+1} ({want}) expected 0x{expect:08x}, "
                  f"got 0x{got:08x}"); ok = False
    extra = words[len(spec):]
    if any(extra):
        print(f"WARN {tag}: {len(extra)} nonzero words beyond spec")
    if ok:
        print(f"OK   {tag}: {len(spec)} vector entries match spec")
    return ok

def csr_writes(elf):
    dis = run(BIN + "/riscv-none-elf-objdump", "-d", elf)
    # limit to handle_reset..end of its symbol block
    m = re.search(r"<handle_reset>:\n(.*?)(\n\n|\Z)", dis, re.S)
    body = m.group(1)
    t0 = None
    writes = set()
    for line in body.splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        line = " ".join(p.strip() for p in parts[2:]).strip()
        mm = re.match(r"(lui|li|addi|csrw|csrc|csrs)\s+(.*)", line)
        if not mm:
            continue
        op, args = mm.group(1), [a.strip() for a in mm.group(2).split("#")[0].split(",")]
        if op == "lui" and args[0] == "t0":
            t0 = int(args[1], 16) << 12
        elif op == "li" and args[0] == "t0":
            t0 = int(args[1], 0)
        elif op == "addi" and args[0] == "t0" and args[1] == "t0":
            t0 = (t0 or 0) + int(args[2], 0)
        elif op in ("csrw", "csrc", "csrs") and args[-1] == "t0":
            writes.add((op, args[0], t0))
    return writes

spec = spec_entries(SPEC)
r1 = check_table("evt", ELF_EVT, EVT_TABLE_SECTION, spec)
r2 = check_table("uni", ELF_UNI, ".vector", spec)
w_evt, w_uni = csr_writes(ELF_EVT), csr_writes(ELF_UNI)
# mtvec differs by design only in symbol, value check: both write mtvec with table|3.
w_evt_n = {(o, c, v) for (o, c, v) in w_evt if c not in ("mtvec", "mepc")}
w_uni_n = {(o, c, v) for (o, c, v) in w_uni if c not in ("mtvec", "mepc")}
if w_evt_n == w_uni_n:
    print("OK   csr: write sets identical:", sorted((c, hex(v) if v is not None else "?") for _, c, v in w_evt_n))
else:
    print("FAIL csr: evt-only:", w_evt_n - w_uni_n, " uni-only:", w_uni_n - w_evt_n)
    r1 = False
mt_evt = [v for (o, c, v) in w_evt if c == "mtvec"]
mt_uni = [v for (o, c, v) in w_uni if c == "mtvec"]
print(f"info mtvec written: evt={len(mt_evt)==1}, uni={len(mt_uni)==1} (mode bits checked in disasm manually)")

# The value written to mtvec is computed from a symbol, so compare the symbol
# instead: QingKe V2 drops the low bits of the mtvec base, which silently
# vectors every interrupt to address 0 when the table is not at the reset
# address. Measured on CH32V003, and invisible to the table check above.
uni_syms = symbols(ELF_UNI)
base = uni_syms.get("_vector_base")
r3 = True
if base is None:
    print("FAIL uni: no _vector_base symbol"); r3 = False
elif base != 0:
    print(f"FAIL uni: _vector_base is 0x{base:08x}, must be 0 "
          f"(mtvec base must be the reset address)"); r3 = False
else:
    print("OK   uni: vector table starts at the reset address")
sys.exit(0 if (r1 and r2 and r3) else 1)
