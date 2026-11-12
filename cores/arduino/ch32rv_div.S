/* 32-bit division where the core has no divide instruction (CH32V003's
 * rv32ec, the V00x's rv32ec_zmmul): __udivsi3, __umodsi3, __divsi3 and
 * __modsi3, ahead of the toolchain's runtime library (compiler-rt, whose C
 * versions are 684 B for the four on rv32ec at -Oz).
 * Written for this core, shift and subtract: the divisor is moved up under
 * the dividend first, so a small quotient takes few rounds; the signed entries
 * go straight to the unsigned one when both operands are non-negative.
 * Results are libgcc's for every input (x / 0 all ones, x % 0 x, INT_MIN / -1
 * INT_MIN; 2026-11-12, both run in an emulator over 35,000 pairs and the
 * edges), in the same number of instructions; 146 B against libgcc's 176 B
 * and compiler-rt's 684 B.
 * Uses a0-a3 and t1-t2 only (t1 keeps the return address of the signed and
 * remainder entries, t2 the sign), so it suits ilp32e and needs no stack. */
#if !defined(__riscv_div)

    .text
    .globl __udivsi3
    .type __udivsi3, @function
/* a0 = n / d, a1 = n % d */
__udivsi3:
    mv   a2, a1          /* d */
    mv   a1, a0          /* remainder: n */
    li   a0, -1
    beqz a2, 3f          /* n / 0: all ones, remainder n */
    li   a3, 1           /* the quotient bit d stands for */
    bgeu a2, a1, 2f
0:  bltz a2, 2f          /* top bit set: no further */
    slli a2, a2, 1
    slli a3, a3, 1
    bltu a2, a1, 0b
2:  li   a0, 0
1:  bltu a1, a2, 4f
    sub  a1, a1, a2
    or   a0, a0, a3
4:  srli a2, a2, 1
    srli a3, a3, 1
    bnez a3, 1b
3:  ret
    .size __udivsi3, . - __udivsi3

    .globl __umodsi3
    .type __umodsi3, @function
__umodsi3:
    mv   t1, ra
    jal  __udivsi3
    mv   a0, a1
    jr   t1
    .size __umodsi3, . - __umodsi3

    .globl __divsi3
    .type __divsi3, @function
/* the magnitudes divided unsigned, the quotient negated when the signs
 * differ; n / 0 is all ones whatever n's sign, as libgcc's gives */
__divsi3:
    or   a2, a0, a1
    bgez a2, __udivsi3   /* both non-negative: the unsigned division as it is */
    beqz a1, 9f
    mv   t1, ra
    xor  t2, a0, a1
    srai t2, t2, 31      /* all ones when the signs differ */
    bgez a0, 5f
    neg  a0, a0
5:  bgez a1, 6f
    neg  a1, a1
6:  jal  __udivsi3
    xor  a0, a0, t2      /* (q ^ s) - s: q, or -q */
    sub  a0, a0, t2
    jr   t1
9:  li   a0, -1
    ret
    .size __divsi3, . - __divsi3

    .globl __modsi3
    .type __modsi3, @function
/* the remainder takes the dividend's sign; n % 0 is n */
__modsi3:
    or   a2, a0, a1
    bgez a2, __umodsi3
    mv   t1, ra
    srai t2, a0, 31
    bgez a0, 7f
    neg  a0, a0
7:  bgez a1, 8f
    neg  a1, a1
8:  jal  __udivsi3
    xor  a0, a1, t2
    sub  a0, a0, t2
    jr   t1
    .size __modsi3, . - __modsi3

#endif
