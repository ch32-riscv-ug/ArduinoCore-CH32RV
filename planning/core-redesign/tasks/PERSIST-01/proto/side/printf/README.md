# Where printf on a Print should go (2026-10-28)

pfm/pfm.ino builds one candidate per build (-DCAND=n); build.sh builds it with the repository core (size: one call
site, test: every case), run.py flashes, sends g and checks each case's line. Results: size.txt, run.txt.
Candidate 5/6 use eyalroz/printf v6.2.0 (MIT, https://github.com/eyalroz/printf/archive/refs/tags/v6.2.0.tar.gz):
build.sh expects printf.c / printf.h beside it, with printf.c's include changed to "printf.h"; not kept here.

| | candidate |
|---|---|
| 0 | the core now: ch32rv_vprintf (small[64] on the stack, longer lines on the heap, newlib-nano vsnprintf) |
| 1 | small[32] + heap |
| 2 | heap only (vsnprintf(NULL, 0) for the length first) |
| 3 | funopen() stream into the Print, newlib vfprintf |
| 4 | own small formatter streaming into the Print: %[-0][width][l](d i u x X c s %), no precision, no floats |
| 5 | eyalroz/printf vfctprintf, integers only (no decimal/exponential, long long, %n) |
| 6 | eyalroz/printf with floats |
