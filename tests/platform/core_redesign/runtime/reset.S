/* Independent EXP-01 startup comparison. IRQs remain disabled. */
.section .reset,"ax",@progbits
.global _start, experiment_vectors
.option push
.option norvc
.option norelax
_start:
    j experiment_reset
#if TARGET_X035
.section .vectors,"a",@progbits
experiment_vectors:
    .word _start
#else
experiment_vectors = _start
#endif
    .word 0
    .word experiment_unhandled
    .word experiment_unhandled
    .word 0
#if TARGET_X035
    .word experiment_unhandled
#else
    .word 0
#endif
    .word 0
    .word 0
#if TARGET_X035
    .word experiment_unhandled
    .word experiment_unhandled
#else
    .word 0
    .word 0
#endif
    .word 0
    .word 0
    .word experiment_unhandled
    .word 0
    .word experiment_unhandled
    .word 0
    .set vector_index, 16
    .rept VECTOR_COUNT-16
#if TARGET_X035
    .if vector_index == 19
    .word 0
    .else
    .word experiment_unhandled
    .endif
#else
    .word experiment_unhandled
#endif
    .set vector_index, vector_index + 1
    .endr
.option pop
.section .text.experiment_reset,"ax",@progbits
.global experiment_reset, experiment_unhandled
experiment_reset:
    csrci mstatus, 8
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    la sp, __stack_top
    la t0, experiment_vectors
    ori t0, t0, 3
    csrw mtvec, t0
    la a0, __data_load
    la a1, __data_start
    la a2, __data_end
.Lcopy_check:
    bgeu a1, a2, .Lzero_begin
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j .Lcopy_check
.Lzero_begin:
    la a0, __bss_start
    la a1, __bss_end
.Lzero_check:
    bgeu a0, a1, .Larrays
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_check
.Larrays:
#if CRT_ARRAYS
    la s0, __preinit_start
    la s1, __preinit_end
.Lpreinit:
    bgeu s0, s1, .Linit_begin
    lw t0, 0(s0)
    addi s0, s0, 4
    jalr ra, 0(t0)
    j .Lpreinit
.Linit_begin:
    la s0, __init_start
    la s1, __init_end
.Linit:
    bgeu s0, s1, .Lmain
    lw t0, 0(s0)
    addi s0, s0, 4
    jalr ra, 0(t0)
    j .Linit
#endif
.Lmain:
    call experiment_main
experiment_unhandled:
    j experiment_unhandled
