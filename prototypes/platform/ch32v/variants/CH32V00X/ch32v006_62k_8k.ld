/* CH32V006x8: FLASH 62K, SRAM 8K (datasheet facts; to be generated from ch32-device-data) */
ENTRY( _start )

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 62K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 8K
}

INCLUDE sections.ld
