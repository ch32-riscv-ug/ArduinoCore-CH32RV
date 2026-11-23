/* The DFSDM of CH32H41x (digital filter for sigma-delta modulators): a 1-bit
 * stream - a PDM microphone, an isolated sigma-delta modulator - turned into
 * samples by a sinc filter. No Arduino or arduino-esp32 counterpart
 * (arduino-esp32's PDM microphones go through its I2S peripheral).
 *
 *     CH32RVDfsdm mic;                       // filter 0
 *     mic.begin(PE9, PD6, 2048000, 64);      // CKOUT to the microphone, its data on DATIN1; 2.048 MHz / 64
 *     int32_t s = mic.read();                // 32 kHz samples, -fullScale() .. fullScale()
 *
 * The clock pin is the CKOUT pad (the DFSDM clocks the modulator: clockHz is
 * HCLK / 2 .. 256, the nearest at or under it; clock() says which) or the
 * channel's own CKIN pad (the modulator clocks itself: clockHz is only what
 * sampleRate() counts from). The data pin is DATIN0 or DATIN1, which picks
 * the channel. Pads are the variant's CH32RV_DFSDM_PADS.
 *
 * The filter: sinc order 1 .. 5 (0: FastSinc), oversampling 1 .. 1024, then
 * the integrator summing 1 .. 256 of those. A sample is +fullScale() for a
 * stream of ones and -fullScale() for zeros: oversample ^ order x integrator
 * (2 x oversample ^ 2 for FastSinc), shifted right to stay in 24 bits.
 * Filter 0 and 1 each take one channel; the two can run together - on two
 * data pads, or on one (the second filter then reads the pad through the
 * other channel: two microphones on one line, the second with
 * setFallingEdge(true)). They share CKOUT: the second must ask its rate.
 * The channels' settings take a write only with the whole DFSDM off, so
 * the second begin() stops the first filter for a moment (a few samples
 * of it are lost while its sinc filter settles again).
 *
 * Samples come by the filter's interrupt into a ring of 64 (setBuffer() for
 * another size before begin()); overruns() counts the ones that found it
 * full. The data is taken on SCK's rising edge (setFallingEdge() before
 * begin() for the falling one). */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_DFSDM_PADS)
#define CH32RV_HAS_DFSDM 1
#else
#define CH32RV_HAS_DFSDM 0
#endif

class CH32RVDfsdm {
public:
    explicit CH32RVDfsdm(uint8_t filter = 0) : _filter(filter) {}
    ~CH32RVDfsdm() { end(); }

    bool setBuffer(size_t samples);
    void setFallingEdge(bool falling) { _falling = falling; }

    bool begin(int clockPin, int dataPin, uint32_t clockHz, uint16_t oversample = 64, uint8_t order = 3,
               uint16_t integrator = 1);
    void end();

    int available() const;
    /* The next sample (waits for one; 0 when not begun). */
    int32_t read();
    /* Up to n samples now in the ring; how many. */
    size_t read(int32_t *samples, size_t n);

    uint32_t clock() const { return _clock_hz; }
    uint32_t sampleRate() const { return _rate; }
    int32_t fullScale() const { return _full; }
    uint32_t overruns() const { return _overruns; }

    /* For the interrupt. */
    void onInterrupt();

private:
    bool pad(int pin, uint8_t role, bool output);

    uint8_t _filter;
    bool _falling = false;
    bool _begun = false;
    int8_t _channel = -1;
    int _pins[2] = {-1, -1};
    uint32_t _clock_hz = 0;
    uint32_t _rate = 0;
    int32_t _full = 0;
    volatile uint32_t _overruns = 0;
    int32_t *_ring = nullptr;
    size_t _size = 64;
    volatile size_t _head = 0, _tail = 0;
};
