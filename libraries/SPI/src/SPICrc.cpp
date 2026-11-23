/* CH32RVSPIClass::setCRC() / transferCRC() (SPI.h): the SPI's CRC unit, the
 * STM32F1's - CRCPR the polynomial, TCRCR / RCRCR what it computed over the
 * frames sent / received, CRCNEXT set right after the last data frame is
 * written makes the next frame the CRC, and CRCERR says the frame received
 * then was not RCRCR (EVT SPI/SPI_CRC). Its own file: a sketch that never
 * calls them links none of it. */
#include "SPI.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

bool CH32RVSPIClass::setCRC(uint8_t polynomial)
{
    _crc_poly = polynomial;
    return polynomial != 0;
}

bool CH32RVSPIClass::transferCRC(void *buf, size_t count)
{
    if (!_started || _periph || !_crc_poly || !buf || count == 0) {
        return false;
    }
    const uint32_t b = _base;
    uint8_t *p = static_cast<uint8_t *>(buf);
    const uint16_t ctlr1 = CH32RV_SPI_CTLR1(b) & (uint16_t)~(CH32RV_SPI_CTLR1_CRCEN | CH32RV_SPI_CTLR1_CRCNEXT);
    const uint16_t off = (uint16_t)(ctlr1 & ~CH32RV_SPI_CTLR1_SPE);
    /* Wait for the bus to be idle, then the CRC from 0: CRCEN (re)set with
     * the SPI off clears both CRC registers. */
    while (CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_BSY) {
    }
    CH32RV_SPI_CTLR1(b) = off;
    CH32RV_SPI_CRCR(b) = _crc_poly;
    CH32RV_SPI_CTLR1(b) = (uint16_t)(off | CH32RV_SPI_CTLR1_CRCEN);
    CH32RV_SPI_CTLR1(b) = (uint16_t)(off | CH32RV_SPI_CTLR1_CRCEN | CH32RV_SPI_CTLR1_SPE);
    CH32RV_SPI_STATR(b) = (uint16_t)~CH32RV_SPI_STATR_CRCERR;
    (void)CH32RV_SPI_DATAR(b);
    for (size_t i = 0; i < count; i++) {
        while ((CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_TXE) == 0u) {
        }
        CH32RV_SPI_DATAR(b) = p[i];
        if (i + 1 == count) {
            /* right after the last data frame: the CRC follows it */
            CH32RV_SPI_CTLR1(b) |= CH32RV_SPI_CTLR1_CRCNEXT;
        }
        while ((CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_RXNE) == 0u) {
        }
        p[i] = (uint8_t)CH32RV_SPI_DATAR(b);
    }
    /* the CRC frame: the byte that came back with it */
    while ((CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_RXNE) == 0u) {
    }
    _crc_rx = (uint8_t)CH32RV_SPI_DATAR(b);
    while (CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_BSY) {
    }
    _crc_tx = (uint8_t)CH32RV_SPI_TCRCR(b);
    const bool ok = (CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_CRCERR) == 0u;
    CH32RV_SPI_STATR(b) = (uint16_t)~CH32RV_SPI_STATR_CRCERR;
    /* the CRC off again for the transfers after */
    CH32RV_SPI_CTLR1(b) = off;
    CH32RV_SPI_CTLR1(b) = ctlr1;
    return ok;
}

}   // namespace arduino
