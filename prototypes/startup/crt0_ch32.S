/*
 * crt0_ch32.S - prototype unified startup for CH32 (W-2 proof of concept)
 * Parameters via -D:
 *   CH32_VECTORS        quoted include file with CH32_IRQ/CH32_RSV/CH32_JMP lines
 *   CH32_MSTATUS_INIT   e.g. 0x1880 (V2), 0x88 (V3/V4), 0x6088 (+FPU), 0x688 (+RVV)
 *   CH32_INTSYSCR_INIT  value for csr 0x804
 *   CH32_CORECFGR       optional, value for csr 0xbc0
 *   CH32_CSR_BC1        optional, value for csr 0xbc1
 *   CH32_CSR805_CLR     optional, bitmask cleared in csr 0x805 (csrc)
 *   CH32_HIGHCODE       optional, copy .highcode from flash to RAM
 */

#ifndef CH32_VECTORS
#error "CH32_VECTORS is required"
#endif
#ifndef CH32_MSTATUS_INIT
#error "CH32_MSTATUS_INIT is required"
#endif
#ifndef CH32_INTSYSCR_INIT
#error "CH32_INTSYSCR_INIT is required"
#endif

    .section  .init, "ax", @progbits
    .globl    _start
    .align    2
_start:
    j       handle_reset

/* ---- vector table (pass 1: table words) ---- */
    .macro CH32_IRQ name
    .word   \name
    .endm
    .macro CH32_RSV
    .word   0
    .endm
    .macro CH32_JMP name
    j       \name
    .endm

    .section  .vector, "ax", @progbits
    .globl    _vector_base
    .align    1
_vector_base:
    .option norvc;
    .word   _start
#include CH32_VECTORS
    .option rvc;

    .purgem CH32_IRQ
    .purgem CH32_RSV
    .purgem CH32_JMP

/* ---- default handler and weak aliases (pass 2) ---- */
    .section  .text.vector_handler, "ax", @progbits
    .weak   Default_Handler
Default_Handler:
1:
    j 1b

    .macro CH32_IRQ name
    .weak   \name
    .set    \name, Default_Handler
    .endm
    .macro CH32_RSV
    .endm
    .macro CH32_JMP name
    .weak   \name
    .set    \name, Default_Handler
    .endm
#include CH32_VECTORS

/* ---- reset ---- */
    .section  .text.handle_reset, "ax", @progbits
    .weak     handle_reset
    .align    1
handle_reset:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    la sp, _eusrstack

#ifdef CH32_HIGHCODE
/* Load highcode section from flash to RAM */
    la a0, _highcode_lma
    la a1, _highcode_vma_start
    la a2, _highcode_vma_end
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
2:
#endif

/* Load data section from flash to RAM */
    la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
2:
/* Clear bss section */
    la a0, _sbss
    la a1, _ebss
    bgeu a0, a1, 2f
1:
    sw zero, (a0)
    addi a0, a0, 4
    bltu a0, a1, 1b
2:

#ifdef CH32_CSR805_CLR
    li t0, CH32_CSR805_CLR
    csrc 0x805, t0
#endif
#ifdef CH32_CORECFGR
    li t0, CH32_CORECFGR
    csrw 0xbc0, t0
#endif
#ifdef CH32_CSR_BC1
    li t0, CH32_CSR_BC1
    csrw 0xbc1, t0
#endif
    li t0, CH32_INTSYSCR_INIT
    csrw 0x804, t0
    li t0, CH32_MSTATUS_INIT
    csrw mstatus, t0

/* interrupt vector table: absolute address mode */
    la t0, _vector_base
    ori t0, t0, 3
    csrw mtvec, t0

    jal SystemInit
    la t0, main
    csrw mepc, t0
    mret
