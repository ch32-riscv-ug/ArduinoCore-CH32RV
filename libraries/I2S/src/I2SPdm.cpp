/* I2SClass::setPinsPdmRx() and I2S_MODE_PDM_RX (I2S.h): PDM microphones on
 * CH32H41x's DFSDM (the library CH32RVDfsdm) - filter 0 the left microphone
 * (data on CKOUT's rising edge), filter 1 the right (falling edge, reading
 * the same pad through the other channel), sinc3 / 64. Its own file: begin()
 * reaches it only through the hooks setPinsPdmRx() sets, so a sketch that
 * never calls setPinsPdmRx() links none of it, nor CH32RVDfsdm. */
#include "I2S.h"
#include <CH32RVDfsdm.h>   /* always: the IDE finds a library by its include (stubs where there is no DFSDM) */

#if CH32RV_HAS_I2S && CH32RV_HAS_DFSDM

#include <new>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint16_t OVERSAMPLE = 64;
constexpr uint8_t ORDER = 3;   /* full scale 64^3 = 2^18 */
constexpr uint8_t FULL_BITS = 18;

struct Pdm {
    CH32RVDfsdm filter[2] = {CH32RVDfsdm(0), CH32RVDfsdm(1)};
    bool on[2] = {false, false};   /* left, right */
};
}   // namespace

struct I2SPdm {
    static Pdm &pdm(I2SClass &i2s) { return *static_cast<Pdm *>(i2s._pdm); }

    static bool begin(I2SClass &i2s, uint32_t rate)
    {
        if (!i2s._pdm) {
            void *mem = malloc(sizeof(Pdm));
            if (!mem) {
                i2s._error = I2S_ERROR_MEMORY;
                return false;
            }
            i2s._pdm = new (mem) Pdm();
        }
        Pdm &p = pdm(i2s);
        p.on[0] = i2s._slots == 2 || (i2s._mask & I2S_STD_SLOT_LEFT);
        p.on[1] = i2s._slots == 2 || (!(i2s._mask & I2S_STD_SLOT_LEFT) && (i2s._mask & I2S_STD_SLOT_RIGHT));
        const size_t samples = i2s._half_bytes / (i2s._bits / 8u) < 64u ? 64u : i2s._half_bytes / (i2s._bits / 8u);
        for (uint8_t k = 0; k < 2; k++) {
            if (!p.on[k]) {
                continue;
            }
            CH32RVDfsdm &f = p.filter[k];
            f.setBuffer(samples);
            f.setFallingEdge(k == 1);
            if (!f.begin(i2s._pdm_clk, i2s._pdm_din, rate * OVERSAMPLE, OVERSAMPLE, ORDER)) {
                end(i2s);
                i2s._error = I2S_ERROR_PINS;
                return false;
            }
            i2s._rate = f.sampleRate();
        }
        return true;
    }

    static void end(I2SClass &i2s)
    {
        if (!i2s._pdm) {
            return;
        }
        Pdm &p = pdm(i2s);
        p.filter[1].end();
        p.filter[0].end();
        p.~Pdm();
        free(i2s._pdm);
        i2s._pdm = nullptr;
    }

    static size_t frames(I2SClass &i2s)
    {
        Pdm &p = pdm(i2s);
        size_t n = SIZE_MAX;
        for (uint8_t k = 0; k < 2; k++) {
            if (p.on[k]) {
                const size_t a = (size_t)p.filter[k].available();
                n = a < n ? a : n;
            }
        }
        return n == SIZE_MAX ? 0 : n;
    }

    /* One frame into _peek: each sample scaled from 2^18 to the width, the
     * full-scale positive one (+2^18) held one under the top. */
    static bool fill(I2SClass &i2s)
    {
        if (frames(i2s) == 0) {
            return false;
        }
        Pdm &p = pdm(i2s);
        const uint8_t bytes = (uint8_t)(i2s._bits / 8u);
        uint8_t at = 0;
        uint32_t lost = 0;
        for (uint8_t k = 0; k < 2; k++) {
            if (!p.on[k]) {
                continue;
            }
            int32_t v;
            p.filter[k].read(&v, 1);
            lost += p.filter[k].overruns();
            const uint8_t bits = i2s._bits;
            int64_t w = bits >= FULL_BITS + 1 ? (int64_t)v << (bits - 1 - FULL_BITS) : (int64_t)v >> (FULL_BITS + 1 - bits);
            const int64_t top = ((int64_t)1 << (bits - 1)) - 1;
            w = w > top ? top : w;
            memcpy(i2s._peek + at, &w, bytes);   /* little-endian: the low bytes */
            at += bytes;
        }
        i2s._overruns = lost;
        i2s._peeked = at;
        i2s._peek_pos = 0;
        return true;
    }
};

namespace {
const I2SClass::PdmOps ops = {I2SPdm::begin, I2SPdm::end, I2SPdm::fill, I2SPdm::frames};
}

void I2SClass::setPinsPdmRx(int clk, int din0, int din1, int din2, int din3)
{
    _pdm_clk = clk;
    _pdm_din = din0;
    /* more lines than one: not here (two filters, both on din0) */
    _pdm_ops = (din1 < 0 && din2 < 0 && din3 < 0) ? &ops : nullptr;
}

#elif CH32RV_HAS_I2S

void I2SClass::setPinsPdmRx(int, int, int, int, int)
{
    _pdm_ops = nullptr;   /* no DFSDM: begin(I2S_MODE_PDM_RX) fails (I2S_ERROR_ARG) */
}

#endif
