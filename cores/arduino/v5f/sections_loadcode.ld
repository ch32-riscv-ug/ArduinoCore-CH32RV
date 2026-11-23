/* The CH32H41x V5F layout (CPU Core menu: V5F; CH32RV_RUN_V5F), found ahead of
 * the core's own sections_loadcode.ld through build.ld_layout_flags.
 *
 * As the EVT's V5F layout (EXAM/SRC/Ld/V5F/Link_v5f.ld): the vectors, code
 * and read-only data in the V5F's ITCM, data, .bss and the stack in its DTCM,
 * nothing the V5F runs on in the shared SRAM. Whatever does not fit the ITCM
 * (128 KB) goes into the shared SRAM image after it (ld
 * --enable-non-contiguous-regions takes each input section to the first
 * place it fits) - the vectors and constructor tables come first so that
 * they never do.
 *
 * Why: with the V5F reading its vectors / rodata or writing .bss in the
 * shared SRAM, a stream of USART interrupts and replies left it running but
 * no longer haltable, every access stuck, within tens to hundreds of
 * replies (H417, 2026-11-21..23, TODO.ja.md): .bss in the DTCM (2026-11-22)
 * made one image run 10,000, the next one (signals, 2026-11-23) stuck at 22
 * and 162; with the image in the ITCM as well, 10,000 and 3 x 3,000 with
 * the sketch writing the heap all the while ran without a stop. 64 KB of
 * code ran 2.5 times slower from the shared SRAM than from ITCM once it no
 * longer fitted the 32 KB instruction cache (2026-11-11).
 *
 * The DTCM starts with the HPE's save area (HW_POPDM_ADDR, 0x200C0000 at
 * reset), left free here. The heap takes the shared SRAM after the image
 * and the CH32RV_DMA_MEM buffers (.bss.ch32rv_dma*, ch32rv_dma_mem.h): every
 * DMA reaches the TCMs too, but writes into them are slower and take their
 * port from the V5F, so buffers that stream stay out of them.
 *
 * crt0 copies the ITCM part, the image (empty unless the ITCM overflowed)
 * and the DTCM data before it leaves flash. The variant's MEMORY names ITCM
 * and DTCM.
 */
/* __stack_size: platform.txt's --defsym, from the Stack Size menu (boards.txt). */
PROVIDE( _stack_size = __stack_size );

PROVIDE( _ch32rv_flash_origin = ORIGIN(FLASH) );
PROVIDE( _ch32rv_flash_length = LENGTH(FLASH) );

/* The HPE's save area at the DTCM's start (HW_POPDM_ADDR's reset value): the
 * EVT leaves 768 bytes; 1 KiB here. */
_dtcm_hpe_size = 0x400;

SECTIONS
{
    /* The reset address: _start (j handle_reset) and the copy. */
    .reset :
    {
        KEEP(*(.reset))
        . = ALIGN(4);
    } >FLASH

    .itcm :
    {
        PROVIDE(_itcm_vma = .);
        /* first what must not spill: the vectors, init, the constructor tables */
        KEEP(*(.vector))
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array* .dtors*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        *(.text*)
        *(.highcode)
        *(.highcode.*)
        *(.gnu.linkonce.t.*)
        *(.rodata*)
        *(.gnu.linkonce.r.*)
        *(.srodata*)
        . = ALIGN(4);
        PROVIDE(_itcm_end = .);
    } >ITCM AT>FLASH
    PROVIDE(_itcm_lma = LOADADDR(.itcm));

    .image :
    {
        PROVIDE(_image_vma = .);
        *(.text*)                      /* what ITCM could not take */
        *(.rodata*)
        *(.highcode)
        *(.highcode.*)
        *(.gnu.linkonce.t.*)
        *(.gnu.linkonce.r.*)
        *(.srodata*)
        . = ALIGN(4);
        PROVIDE( _edata = . );         /* the end of crt0's image copy */
    } >RAM AT>FLASH
    PROVIDE(_image_lma = LOADADDR(.image));

    /* Buffers a peripheral's DMA streams through (CH32RV_DMA_MEM). Listed
     * before .bss so that this rule, not *(.bss*), takes them; crt0
     * clears them. */
    .dma_bss (NOLOAD) :
    {
        . = ALIGN(16);
        PROVIDE( _dma_sbss = . );
        *(.bss.ch32rv_dma*)
        . = ALIGN(16);
        PROVIDE( _dma_ebss = . );
    } >RAM

    /* The heap: the rest of the shared SRAM (sbrk, ch32rv_sbrk.c). */
    PROVIDE( _end = _dma_ebss );
    PROVIDE( end = _dma_ebss );
    PROVIDE( _heap_end = ORIGIN(RAM) + LENGTH(RAM) );

    .dtcm_data ORIGIN(DTCM) + _dtcm_hpe_size :
    {
        PROVIDE(_dtcm_data_vma = .);
        *(.data*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );
        *(.sdata*)
        . = ALIGN(4);
        PROVIDE(_dtcm_data_end = .);
    } >DTCM AT>FLASH
    PROVIDE(_dtcm_data_lma = LOADADDR(.dtcm_data));
    PROVIDE( _ch32rv_image_end = MAX(MAX(LOADADDR(.image) + SIZEOF(.image), LOADADDR(.itcm) + SIZEOF(.itcm)),
                                     LOADADDR(.dtcm_data) + SIZEOF(.dtcm_data)) );

    /* Kept through a reset that leaves RAM powered - a software or watchdog
     * reset, the restart deepSleep() ends with: startup neither copies nor
     * clears it. After a power-on it holds whatever the RAM held. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } >DTCM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE( _sbss = . );
        *(.sbss*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        PROVIDE( _ebss = . );
    } >DTCM

    /* NOLOAD: GNU ld makes a section of only symbols and a moved `.` NOBITS on
     * its own, lld makes it PROGBITS - a RAM-sized gap in the .bin. */
    .stack ORIGIN(DTCM) + LENGTH(DTCM) - __stack_size (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE( _susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = . );
    } >DTCM

    /* Storage kept across resets (CH32RV_FLASH_STORE, ch32rv_flash.h), packed
     * against the end of FLASH. Only what something references survives
     * --gc-sections, so it is 0 B unless used; NOLOAD keeps it out of the
     * image. Overlapping code fails the link. */
    .ch32rv_store ORIGIN(FLASH) + LENGTH(FLASH) - SIZEOF(.ch32rv_store) (NOLOAD) :
    {
        PROVIDE( _ch32rv_store_start = . );
        *(.ch32rv_store*)
        PROVIDE( _ch32rv_store_end = . );
    } >FLASH
}

ASSERT(_start == ORIGIN(FLASH), "_start must be at the reset address")
ASSERT(_image_vma == ORIGIN(RAM), "the vector table must start the RAM image")
ASSERT(_ebss <= _susrstack, "DTCM: .data, .bss and the stack do not fit")
