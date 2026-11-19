/* The encryption module of CH32H415 / H416 / H417 (ECDC, CH32H417RM chapter 41):
 * AES with a 128, 192 or 256-bit key and SM4 (128-bit key), in ECB or CTR,
 * done by the hardware. Bytes are in the standards' order: a FIPS-197 or
 * GB/T 32907 test vector goes in and comes out as printed.
 *
 *   CH32RVEcdc ecdc;
 *   ecdc.begin(CH32RVEcdc::AES, key, 16);     // 16, 24 or 32 bytes (SM4: 16)
 *   ecdc.encrypt(plain, cipher, 64);          // ECB, len a multiple of 16
 *   ecdc.decrypt(cipher, plain, 64);
 *   ecdc.ctr(iv, plain, cipher, 100);         // CTR: any length; the same call decrypts
 *
 * Pairs of blocks between buffers on 32-byte boundaries (alignas(32)) go
 * memory to memory: the module reads and writes RAM itself while the CPU
 * waits. Other buffers, an odd block and a CTR tail shorter than a block go
 * one block at a time through its registers - the same result, more of the CPU's time. in and out may be the same buffer. The CTR counter is the whole
 * 16-byte iv, counted up by one per block, big-endian (as NIST SP 800-38A's
 * examples). Where the series has no ECDC (CH32RV_ECDC_BASE) begin() returns
 * false. */
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class CH32RVEcdc {
public:
    enum Algorithm : uint8_t { SM4 = 0, AES = 1 };

    /* The key (keyLen 16, 24 or 32 for AES, 16 for SM4) expanded and kept;
     * false for another length or without the module. */
    bool begin(Algorithm algorithm, const uint8_t *key, size_t keyLen);
    /* The module's clock off. */
    void end();

    /* ECB: len a multiple of 16, false otherwise. */
    bool encrypt(const uint8_t *in, uint8_t *out, size_t len);
    bool decrypt(const uint8_t *in, uint8_t *out, size_t len);
    /* CTR from the 16-byte counter iv (left as it was), any len. */
    bool ctr(const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len);

private:
    bool run(bool ctrMode, bool decrypting, const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len);
    void expand(bool ctrMode, bool sram, bool little, const uint8_t *iv);
    bool memoryToMemory(const uint8_t *in, uint8_t *out, size_t blocks);
    bool single(const uint8_t *in, uint8_t *out);

    uint8_t _key[32] = {};
    uint8_t _keyLen = 0;
    Algorithm _algorithm = AES;
    bool _begun = false;
    uint32_t _mode = 0;          /* CTRL as expand() left it, the run bits off */
};
