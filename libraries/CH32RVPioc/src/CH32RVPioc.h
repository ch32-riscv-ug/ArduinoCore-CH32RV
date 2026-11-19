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
 * SB_INT_REQ, the request done() / waitDone() wait for; reading cmd.out
 * clears both. d0 .. d31 (SFR_DATA_REG0 ..) are shared bytes either side
 * reads and writes at any time.
 *
 * Prototype (2026-11-19): the per-pin AF families (H41x). X035's remap and
 * the SRAM X035 / V205 share with the sketch's RAM come next. */
#pragma once

#include <Arduino.h>
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

    /* cmd.in: the byte the program reads from SFR_CTRL_WR. */
    void command(uint8_t value);
    /* The program has asked for attention (SB_INT_REQ). */
    bool done() const;
    /* Until done() or timeoutMs: cmd.out (which clears the request), -1 on
     * the time-out. */
    int waitDone(uint32_t timeoutMs);
    /* cmd.out now (clears the request). */
    uint8_t result();

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
    int8_t _pads[2] = {-1, -1};
    bool _begun = false;
};
