Side findings while doing PERSIST-01 (2026-10-28), not part of persistence. Results are in TODO.ja.md (the persistence item).

- hcfn/: a function in .highcode; line.py flashes and prints the "hc" line. crt0-one-loop.diff: copy .highcode and .data
  in one loop (with a linker ASSERT that they sit alike in flash and RAM).
- pf/, pfh/ (-DFILL=n static bytes), pfs/: Serial.printf line lengths, a short heap, and stack depth; pf.py drives them.
