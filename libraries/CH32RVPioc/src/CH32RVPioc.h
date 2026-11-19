/* The PIOC: WCH's RISC8B I/O processor (CH32X035 / X033, V205, H41x), an
 * 8-bit core with its own 4 KiB program SRAM and two pads (IO0, IO1) that it
 * drives cycle by cycle. Its program is written in the sketch's (or a
 * library's) .S with the core's macros (<ch32rv_pioc.inc>) and assembled by
 * the same clang; this class copies it into the PIOC's SRAM, routes the pads
 * and talks to it through the PIOC's shared registers:
 *
 *   // blink.S:  #include <ch32rv_pioc.inc>
 *   //           PIOC_BEGIN blink ... PIOC_END blink
 *   #include <CH32RVPioc.h>
 *   PIOC_IMAGE(blink);
 *   CH32RVPioc pioc;
 *   pioc.begin(PIOC_IMAGE_OF(blink), PA0);      // IO0 on PA0, IO1 unused
 *   pioc.command(1);                            // cmd.in (SFR_CTRL_WR)
 *   int status = pioc.waitDone(100);            // cmd.out (SFR_CTRL_RD), -1 on time-out
 *
 * The handshake is the PIOC's own (its manual, EVT's host code): the host's
 * write of cmd.in sets SB_DATA_MW_SR for the program to see; the program's
 * write of cmd.out (SFR_CTRL_RD) sets SB_DATA_SW_MR and, if it sets
 * SB_INT_REQ, the request done() / waitDone() wait for; the host reads
 * cmd.out (clearing SB_DATA_SW_MR) and writes it (clearing SB_INT_REQ). d0 .. d31 (SFR_DATA_REG0 ..) are shared bytes either side
 * reads and writes at any time.
 *
 * On X035 / X033 and V205 the program SRAM is 4 KiB of the sketch's RAM
 * (X035: the top, below which the stack then sits; V205: the bottom): a
 * sketch that uses the PIOC has that much less (sections.ld), one that does
 * not keeps it all. */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>
#include <stddef.h>
#include <stdint.h>

/* An image a .S made with PIOC_BEGIN name / PIOC_END name: declare it once
 * (at file scope), then PIOC_IMAGE_OF(name) is what begin() takes. */
#define PIOC_IMAGE(name)                                                    \
    extern "C" const uint16_t name[];                                       \
    extern "C" const uint32_t name##_size
#define PIOC_IMAGE_OF(name) CH32RVPioc::Image{ name, name##_size }

class CH32RVPioc {
public:
    struct Image {
        const uint16_t *words;
        uint32_t bytes;
    };

    /* Stops the PIOC, copies the image into its SRAM, routes IO0 / IO1 to
     * those pads (-1: not that one), and starts it at word 1 with both pads
     * granted to it. False when the part has no PIOC, the image is empty or
     * larger than 4 KiB, or a pad is not one the PIOC reaches. */
    bool begin(const Image &image, int io0 = -1, int io1 = -1);
    /* Stops it (held in reset, its clock off) and gives the pads back. */
    void end();
    /* IO0 (io 0) or IO1 (1) open drain after begin(): the pad then pulls low
     * for an output 0 and lets go for a 1 (a bus with a pull-up: 1-Wire, a
     * shared line). begin() makes the pads push-pull, and the PIOC's
     * SFR_PORT_DIR does not reach a pad routed through the GPIO's AF: a
     * program lets a line go by writing 1, not by making it an input. False
     * before begin() or for a pad not routed. */
    bool setOpenDrain(uint8_t io, bool on = true);

    /* cmd.in: the byte the program reads from SFR_CTRL_WR. */
    void command(uint8_t value);
    /* The program has asked for attention (SB_INT_REQ). */
    bool done() const;
    /* Until done() or timeoutMs: cmd.out (the request taken down), -1 on
     * the time-out. */
    int waitDone(uint32_t timeoutMs);
    /* cmd.out now, and the request taken down (the host's write of cmd.out
     * clears SB_INT_REQ; its read alone does not). */
    uint8_t result();
    /* Each request (SB_INT_REQ) as an interrupt instead: the PIOC's
     * interrupt reads cmd.out (which clears the request) and calls this with
     * it - keep it short, it runs in the interrupt. done() / waitDone() then
     * see nothing; nullptr goes back to them. One PIOC: the last begun object
     * gets the calls. */
    typedef CH32RVCallback<void(uint8_t)> DoneCallback;
    void onDone(DoneCallback callback);
    void service();                    /* the interrupt's, for PIOC_IRQHandler */

    /* Bytes into the program SRAM past the image (offset in bytes from its
     * start; the program reads them with RDCODE, word offset / 2). The SRAM
     * is the PIOC's alone while it runs, so this holds it in reset, copies,
     * and starts it again from its beginning: d0 .. d31 are cleared, write
     * them after this; the pads float meanwhile (no use for a line that must
     * idle high). False when not begun, or the bytes would overlap the
     * image or pass the SRAM's 4 KiB. */
    bool loadSram(uint16_t offset, const void *data, size_t n);

    /* The PIOC's clock: HCLK (the main core's clock but on H41x, where the
     * V5F runs faster than the bus). A program's timing is counted in it. */
    static constexpr uint32_t clockHz()
    {
#if defined(CH32RV_HCLK_HZ)
        return CH32RV_HCLK_HZ;
#else
        return F_CPU;
#endif
    }

    /* d0 .. d31: SFR_DATA_REG0 + index. */
    void writeData(uint8_t index, const void *data, size_t n);
    void readData(uint8_t index, void *data, size_t n) const;
    void write(uint8_t index, uint8_t value) { writeData(index, &value, 1); }
    uint8_t read(uint8_t index) const
    {
        uint8_t v = 0;
        readData(index, &v, 1);
        return v;
    }

private:
    bool route(int pin, uint8_t role);
    static constexpr uint8_t NO_PAD = 0xFF;
    uint8_t _pads[2] = {NO_PAD, NO_PAD};   /* pin numbers reach past 127 (CH32X035's PC7 is 135) */
    uint16_t _imageBytes = 0;
    uint8_t _io = 0;
    bool _begun = false;
    DoneCallback _done;
};
