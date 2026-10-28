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

## Own formatter levels (own/, cases.py -> own/cases.h and own/expected.txt, build.sh, run.py)

own_printf.h: OWN_LEVEL 0 the first draft, 1 + precision ('.N' for d i u x X s), '*', '+' and ' '; 2 + %f the way
Print::printFloat does it (0.5 added at the last digit from a table, "ovf" past 4294967040). own-size.txt (mode 1:
ints and strings, 2: one %f, 3: a sketch that already prints doubles, 4: that plus one %f, 5: no formatter),
own-run.txt (every case against C's results, stack depth per case, 20 calls' time). The first %f draft divided
(1/v, rounding /= 10) and used 64-bit integers: size-f2 and the first lines of size.txt.

Level 3: %f converted to float once (varargs always carry a double) and computed in float. own-size-float.txt adds
mode 6 (a sketch with float math, no float printing) and 7 (6 plus one %f); own-run-float.txt.

## Print::printFloat in float (printfloat/)

Print-float.diff: the experiment's printFloat, converting to float once and computing in float (a table for the
rounding, no division). pp/: a sketch printing one float with Serial.print; ppf/: that plus one %f (own level 3).
pv/ and cap.py: values printed with the current core (pv-double.txt) and the float one (pv-float.txt) on V003.
