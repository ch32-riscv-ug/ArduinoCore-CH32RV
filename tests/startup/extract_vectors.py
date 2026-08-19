import re, sys
# Extract the vector table entries (after the reset slot) from an EVT startup .S
# Emits: one line per entry: ".word <name-or-0>" preserving order.
src = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
in_table = False
entries = []
for line in src:
    s = line.strip()
    if not in_table:
        if s.startswith(".option") and "norvc" in s:
            in_table = True
        continue
    if s.startswith(".option") and "rvc" in s and "norvc" not in s:
        break
    m = re.match(r"\.word\s+([A-Za-z0-9_]+)", s)
    if m:
        entries.append(m.group(1)); continue
    m = re.match(r"j\s+([A-Za-z0-9_]+)", s)
    if m:
        entries.append("@J@" + m.group(1)); continue
# Drop the first entry if it is the reset slot (j handle_reset or _start address)
if entries and (entries[0] in ("_start", "0") or entries[0].startswith("@J@")):
    entries = entries[1:]
out = open(sys.argv[2], "w")
for e in entries:
    if e.startswith("@J@"):
        out.write("    CH32_JMP %s\n" % e[3:])
    else:
        out.write(("    CH32_RSV\n") if e == "0" else ("    CH32_IRQ %s\n" % e))
print("entries:", len(entries))
