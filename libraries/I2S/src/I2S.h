/* I2S: audio in or out on an SPI in I2S mode (I2S2, I2S3 - CH32V30x,
 * CH32V4x7, CH32H41x). The shape of arduino-esp32's ESP_I2S, so a sketch
 * written for it reads the same here:
 *
 *     I2SClass i2s;
 *     i2s.setPins(PB10, PB9, PB15);                 // BCLK, WS, DOUT
 *     i2s.begin(I2S_MODE_STD, 48000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
 *     int16_t frame[2] = {left, right};
 *     i2s.write((const uint8_t *)frame, sizeof(frame));
 *
 * The controller (master) side only, standard (two-slot) mode only: the
 * SPI's I2S has no TDM or PDM. PDM microphones (I2S_MODE_PDM_RX) go through
 * CH32H41x's DFSDM instead (setPinsPdmRx(), below). One direction per instance - the SPI's I2S is
 * half duplex, so DOUT or DIN, not both (two instances on I2S2 and I2S3 do
 * both at once). The pins pick the instance: CK, WS, SD (and MCK) have to be
 * pads of I2S2 or I2S3 (the variant's CH32RV_I2Sn_PADS) - any of them on
 * CH32H41x, whose pads each take their own function; on one AFIO route on
 * CH32V30x / V4x7.
 *
 * Samples in memory as ESP_I2S has them: little-endian, left then right in
 * stereo; 16-bit as int16_t, 24-bit as three bytes, 32-bit as int32_t. In
 * mono each sample goes out on the slots of slot_mask (both by default) and
 * comes in from the left slot (or the right, with I2S_STD_SLOT_RIGHT).
 *
 * The clock is SYSCLK divided by 2 x I2SDIV + ODD (I2SDIV 2..255): the rate
 * comes out within a fraction of a percent of the one asked for, and
 * sampleRate() says which it is. A rate the divider cannot reach (below
 * SYSCLK / 16352 with 16-bit slots: 8.8 kHz at 144 MHz, 24.5 kHz on
 * CH32H41x's 400 MHz) fails begin().
 *
 * Out: write() copies into a buffer of two halves that the DMA plays in
 * turn, and waits while both are full. A half the sketch did not fill in
 * time plays as silence (underruns() counts them), so stopping writing
 * stops the sound. In: the DMA fills a ring that read() / readBytes() empty;
 * what the sketch does not take in time is overwritten (overruns()).
 */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_I2S2_PADS) || defined(CH32RV_I2S3_PADS)
#define CH32RV_HAS_I2S 1
#else
#define CH32RV_HAS_I2S 0
#endif

typedef enum {
    I2S_MODE_STD = 0,
    I2S_MODE_TDM,
    I2S_MODE_PDM_TX,
    I2S_MODE_PDM_RX,
} i2s_mode_t;

typedef enum {
    I2S_DATA_BIT_WIDTH_8BIT = 8,
    I2S_DATA_BIT_WIDTH_16BIT = 16,
    I2S_DATA_BIT_WIDTH_24BIT = 24,
    I2S_DATA_BIT_WIDTH_32BIT = 32,
} i2s_data_bit_width_t;

typedef enum {
    I2S_SLOT_MODE_MONO = 1,
    I2S_SLOT_MODE_STEREO = 2,
} i2s_slot_mode_t;

typedef enum {
    I2S_STD_SLOT_LEFT = 1,
    I2S_STD_SLOT_RIGHT = 2,
    I2S_STD_SLOT_BOTH = 3,
} i2s_std_slot_mask_t;

/* The frame format (setFormat(), before begin()). Not in ESP_I2S, whose
 * standard mode is Philips: the SPI's I2S has these five. */
enum CH32RVI2SFormat : uint8_t {
    I2S_FORMAT_PHILIPS = 0,   /* WS low = left, MSB one BCLK after WS changes */
    I2S_FORMAT_MSB,           /* left-justified: WS high = left, MSB at the change */
    I2S_FORMAT_LSB,           /* right-justified: the word ends at the slot's end */
    I2S_FORMAT_PCM_SHORT,     /* PCM, a one-BCLK frame sync */
    I2S_FORMAT_PCM_LONG,      /* PCM, a frame sync 13 BCLKs long */
};

/* lastError() */
enum : int {
    I2S_ERROR_NONE = 0,
    I2S_ERROR_ARG,            /* a mode, width or rate this hardware has not got */
    I2S_ERROR_PINS,           /* the pins are no route of a free I2S */
    I2S_ERROR_DMA,            /* the DMA channel is someone else's */
    I2S_ERROR_MEMORY,         /* the buffer could not be allocated */
    I2S_ERROR_STATE,          /* not begun, or begun in the other direction */
};

class I2SClass : public Stream {
public:
    I2SClass();
    ~I2SClass();

    /* -1 for a pin that is not used. DOUT makes an output, DIN an input. */
    void setPins(int bclk, int ws, int dout, int din = -1, int mclk = -1);
    /* PDM microphones, I2S_MODE_PDM_RX (CH32H41x: its DFSDM, the library
     * CH32RVDfsdm, not the SPI): clk a DFSDM CKOUT pad, din0 a DATIN pad.
     * Stereo is two microphones on din0, the left one sending on the rising
     * edge and the right on the falling (each microphone's L/R or SELECT);
     * mono takes the left, or with I2S_STD_SLOT_RIGHT the right. The clock
     * is 64 x rate (HCLK divided: sampleRate() says what it came to), a
     * sinc3 filter / 64, full scale the width's. din1 .. din3 (more lines):
     * -1 only. Its own file - a sketch that never calls it links none of it. */
    void setPinsPdmRx(int clk, int din0, int din1 = -1, int din2 = -1, int din3 = -1);
    /* BCLK's idle level high (data change on its falling edge as usual,
     * sampled on the rising one, read the other way round); WS inverted
     * swaps the slots. MCK cannot be inverted (false only). */
    void setInverted(bool bclk, bool ws, bool mclk = false);
    void setFormat(CH32RVI2SFormat format);
    /* Bytes of each half of the DMA buffer (default 1024; a multiple of a
     * frame is kept). Before begin(). */
    void setBufferSize(size_t bytes);

    bool begin(i2s_mode_t mode, uint32_t rate, i2s_data_bit_width_t bits, i2s_slot_mode_t ch,
               int8_t slot_mask = -1);
    bool end();

    /* The rate the divider gives (0 when not begun). */
    uint32_t sampleRate() const { return _rate; }
    int lastError() const { return _error; }
    uint32_t underruns() const { return _underruns; }
    uint32_t overruns() const { return _overruns; }

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t readBytes(char *buffer, size_t length);
    size_t readBytes(uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }

    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    int availableForWrite() override;
    using Print::write;

    /* The DMA's half / full interrupt: the I2S library's own. */
    void dmaInterrupt();

    /* PDM_RX's (I2SPdm.cpp), set by setPinsPdmRx() */
    struct PdmOps {
        bool (*begin)(I2SClass &, uint32_t rate);
        void (*end)(I2SClass &);
        bool (*fill)(I2SClass &);
        size_t (*frames)(I2SClass &);
    };

private:
    friend struct I2SPdm;
    bool rxBegun() const { return (_port >= 0 && !_tx) || _pdm_on; }
    size_t frameBytes() const;
    size_t hwPerFrame() const;
    void packFrame(const uint8_t *in, uint16_t *out) const;
    void unpackFrame(const uint16_t *in, uint8_t *out) const;
    size_t rxReady();
    bool fill();

    int _bclk = -1, _ws = -1, _dout = -1, _din = -1, _mclk = -1;
    bool _inv_bclk = false, _inv_ws = false;
    CH32RVI2SFormat _format = I2S_FORMAT_PHILIPS;
    size_t _half_bytes = 1024;

    int8_t _port = -1;   /* index into the instance table, -1 when not begun */
    bool _tx = false;
    uint8_t _bits = 16, _slots = 2, _mask = 3;
    uint32_t _rate = 0;
    int _error = I2S_ERROR_NONE;

    uint16_t *_ring = nullptr;   /* the DMA's: two halves of _half halfwords */
    uint32_t _half = 0;

    /* out: the half the DMA plays now, the half write() fills and how far */
    volatile uint8_t _playing = 0;
    volatile uint8_t _w_half = 1;
    volatile uint32_t _w_ofs = 0;
    volatile uint32_t _fill[2] = {0, 0};
    volatile uint32_t _underruns = 0;
    uint8_t _stage[8];   /* a frame write() has part of */
    uint8_t _staged = 0;

    /* in: halves the DMA finished, and the halfwords read() took */
    volatile uint32_t _rx_halves = 0;
    uint32_t _rx_read = 0;   /* halves read to the end */
    uint32_t _rx_off = 0;    /* halfwords into the half being read */
    volatile uint32_t _overruns = 0;
    uint8_t _peek[8];
    uint8_t _peeked = 0, _peek_pos = 0;

    const PdmOps *_pdm_ops = nullptr;
    void *_pdm = nullptr;   /* I2SPdm.cpp's: the DFSDM filters */
    int _pdm_clk = -1, _pdm_din = -1;
    bool _pdm_on = false;
};
