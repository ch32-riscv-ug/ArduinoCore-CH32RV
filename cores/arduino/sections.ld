/* Core-owned section layout, shared by every variant (ADR-0003).
 * The MEMORY block (FLASH/RAM origin and length) is variant-owned and lives in
 * variants/<VARIANT>/<device>.ld, which INCLUDEs this file.
 */
/* W-3 prototype: own-written common section layout for crt0_ch32.S.
 * Included from the per-SKU memory scripts (ch32v006_62k_8k.ld etc.). */

__stack_size = DEFINED(__stack_size) ? __stack_size : 512;
PROVIDE( _stack_size = __stack_size );

SECTIONS
{
    .init :
    {
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
    } >FLASH

    .vector :
    {
        . = ALIGN(4);
        KEEP(*(.vector))
        . = ALIGN(64);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array* .dtors*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* Code the user asks to run from RAM, e.g.
     *   void f(void) __attribute__((section(".highcode")));
     * crt0_ch32.S copies it when CH32_HIGHCODE is defined. Empty by default,
     * so the copy loop moves zero bytes on families that do not use it. */
    .highcode :
    {
        . = ALIGN(4);
        PROVIDE(_highcode_vma_start = .);
        *(.highcode)
        *(.highcode.*)
        . = ALIGN(4);
        PROVIDE(_highcode_vma_end = .);
    } >RAM AT>FLASH
    PROVIDE(_highcode_lma = LOADADDR(.highcode));

    .dalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
    } >RAM AT>FLASH

    .dlalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_lma = .);
    } >FLASH

    .data :
    {
        *(.gnu.linkonce.r.*)
        *(.data*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );
        *(.sdata*)
        *(.srodata*)
        . = ALIGN(4);
        PROVIDE( _edata = . );
    } >RAM AT>FLASH

    .bss :
    {
        . = ALIGN(4);
        PROVIDE( _sbss = . );
        *(.sbss*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        PROVIDE( _ebss = . );
    } >RAM

    PROVIDE( _end = _ebss );
    PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        PROVIDE( _heap_end = . );
        . = ALIGN(4);
        PROVIDE( _susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = . );
    } >RAM
}
