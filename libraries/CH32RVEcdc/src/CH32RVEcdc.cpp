/* CH32RVEcdc: the ECDC's two data paths (CH32H417RM 41.4.2). The register
 * orders below are what the hardware answers with the standard vectors
 * (measured on CH32H417, 2026-11-19): the key's first four bytes, read
 * big-endian, in the highest key register the key length uses (KEY_127T96
 * for 128 bits, KEY_255T224 for 256), the counter's in IV_127T96, a block's
 * in SGSD_127T96 and its result's in SGRT_127T96 - in single mode with
 * DAT_MOD (big-endian), which the manual requires there. Memory to memory
 * takes the bytes as they lie with DAT_MOD clear; SRAM_LEN counts 16-byte
 * blocks. The counter counts up per block in memory to memory; single mode
 * takes it once, at the key expansion, so CTR there is done as ECB on the
 * counter.
 *
 * Memory to memory ignores bit 4 of the destination: 16 bytes past a 32-byte
 * boundary it wrote 16 bytes before (4 and 8 past were right; measured
 * 2026-11-19), over whatever lay there. The EVT asks for 32-byte alignment
 * (ECDC_SetSRC_BaseAddr), so only buffers aligned so go that way. It also
 * works in 32 bytes: three blocks wrote four, the fourth a copy of the third
 * over the next 16 bytes. So it gets an even number of blocks, and an odd
 * one's last goes through the registers. */
#include "CH32RVEcdc.h"

#include <string.h>

#include "internal/ch32rv_registers.h"

#if defined(CH32RV_ECDC_BASE)

#define ECDC(o)        CH32RV_REG32(CH32RV_ECDC_BASE + (o))
#define ECDC_CTRL      ECDC(0x00u)
#define ECDC_INT_FG    ECDC(0x04u)
#define ECDC_KEY(i)    ECDC(0x08u + 4u * (i))      /* 0: 255..224 .. 7: 31..0 */
#define ECDC_IV(i)     ECDC(0x28u + 4u * (i))      /* 0: 127..96 .. 3: 31..0 */
#define ECDC_SGSD(i)   ECDC(0x40u + 4u * (i))
#define ECDC_SGRT(i)   ECDC(0x50u + 4u * (i))
#define ECDC_SRC_ADDR  ECDC(0x60u)
#define ECDC_DST_ADDR  ECDC(0x64u)
#define ECDC_SRAM_LEN  ECDC(0x68u)

#define KEYEX_EN     (1u << 0)
#define NORMAL_EN    (1u << 1)
#define MODE_DECRYPT (1u << 3)
#define CLKDIV(n)    ((uint32_t)(n) << 4)
#define WRSRAM_EN    (1u << 7)
#define ALGRM_AES    (1u << 8)
#define CIPHER_CTR   (1u << 9)
#define KLEN(n)      ((uint32_t)(n) << 10)
#define DAT_BIG      (1u << 13)
#define CLOCK_PLL    (1u << 24)
#define CLOCK_EN     (1u << 25)
#define IF_SINGLE    (1u << 17)
#define IF_WRSRAM    (1u << 18)

/* The module's clock: the PLL itself up to 400 MHz, otherwise divided (2 ..
 * 7) to 400 MHz or below; never below HCLK (the manual's condition). */
static uint32_t clockBits()
{
#if CH32RV_CLOCK_SYSCLK_HZ <= 400000000
    return CLOCK_PLL;
#else
    return CLKDIV((CH32RV_CLOCK_SYSCLK_HZ + 399999999u) / 400000000u);
#endif
}

static uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

static bool wait(uint32_t flag)
{
    for (uint32_t t = 0; t < 10000000u; t++) {
        if (ECDC_INT_FG & flag) {
            ECDC_INT_FG = flag;
            return true;
        }
    }
    return false;
}

bool CH32RVEcdc::begin(Algorithm algorithm, const uint8_t *key, size_t keyLen)
{
    if (!key || !(keyLen == 16 || (algorithm == AES && (keyLen == 24 || keyLen == 32)))) {
        return false;
    }
    memcpy(_key, key, keyLen);
    _keyLen = (uint8_t)keyLen;
    _algorithm = algorithm;
    ch32rv_clock_enable(ECDC);
    _begun = true;
    return true;
}

void CH32RVEcdc::end()
{
    if (!_begun) {
        return;
    }
    ECDC_CTRL = 0;
    ch32rv_clock_disable(ECDC);
    memset(_key, 0, sizeof _key);
    _begun = false;
}

/* The mode and the key set, the key expanded (KEYEX_EN high, then low). The
 * expansion is a few dozen of the module's cycles; the wait is the EVT's
 * bound (64 of them per divider step) with room to spare. */
void CH32RVEcdc::expand(bool ctrMode, bool sram, bool little, const uint8_t *iv)
{
    const uint8_t words = (uint8_t)(_keyLen / 4u);
    _mode = clockBits() | CLOCK_EN | (_algorithm == AES ? ALGRM_AES : 0u) | (ctrMode ? CIPHER_CTR : 0u) |
            KLEN(words / 2u - 2u) | (little ? 0u : DAT_BIG) | (sram ? WRSRAM_EN : 0u);
    ECDC_INT_FG = 0xFFFFFFFFu;
    ECDC_CTRL = _mode;
    for (uint8_t i = 0; i < words; i++) {
        ECDC_KEY(8u - words + i) = be32(_key + 4u * i);
    }
    if (iv) {
        for (uint8_t i = 0; i < 4; i++) {
            ECDC_IV(i) = be32(iv + 4u * i);
        }
    }
    ECDC_CTRL = _mode | KEYEX_EN;
    for (volatile uint32_t d = 0; d < 2000u; d++) {
    }
    ECDC_CTRL = _mode;
}

bool CH32RVEcdc::memoryToMemory(const uint8_t *in, uint8_t *out, size_t blocks)
{
    ECDC_SRC_ADDR = (uint32_t)(uintptr_t)in;
    ECDC_DST_ADDR = (uint32_t)(uintptr_t)out;
    ECDC_SRAM_LEN = (uint32_t)blocks;            /* a write starts it */
    const bool ok = wait(IF_WRSRAM);
    __asm__ volatile("fence rw, rw" ::: "memory");
    return ok;
}

bool CH32RVEcdc::single(const uint8_t *in, uint8_t *out)
{
    for (uint8_t i = 0; i < 4; i++) {
        ECDC_SGSD(i) = be32(in + 4u * i);        /* the last write (SGSD_31T0) starts it */
    }
    if (!wait(IF_SINGLE)) {
        return false;
    }
    for (uint8_t i = 0; i < 4; i++) {
        put32(out + 4u * i, ECDC_SGRT(i));
    }
    return true;
}

bool CH32RVEcdc::run(bool ctrMode, bool decrypting, const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len)
{
    if (!_begun || (!in && len) || (!out && len)) {
        return false;
    }
    const size_t blocks = len / 16u;
    const uint32_t direction = decrypting ? MODE_DECRYPT : 0u;
    size_t done = 0;
    uint8_t counter[16];
    if (ctrMode) {
        memcpy(counter, iv, 16);
    }
    const size_t pairs = blocks & ~(size_t)1u;
    if (pairs && ((uintptr_t)in % 32u) == 0 && ((uintptr_t)out % 32u) == 0) {
        expand(ctrMode, true, true, ctrMode ? counter : nullptr);
        ECDC_CTRL = _mode | NORMAL_EN | direction;
        if (!memoryToMemory(in, out, pairs)) {
            return false;
        }
        done = pairs;
        if (ctrMode) {
            /* the counter past the blocks done, for the rest */
            uint32_t carry = (uint32_t)pairs;
            for (int i = 15; i >= 0 && carry; i--) {
                carry += counter[i];
                counter[i] = (uint8_t)carry;
                carry >>= 8;
            }
        }
    }
    if (done == blocks && len % 16u == 0) {
        return true;
    }
    /* Block by block. CTR here is ECB on the counter and the XOR by the CPU:
     * single mode takes the counter at the key expansion and neither counts
     * it nor reads it again (a new IV per block gave the first block's
     * stream again). */
    expand(false, false, false, nullptr);
    ECDC_CTRL = _mode | NORMAL_EN | (ctrMode ? 0u : direction);
    for (size_t b = done; b * 16u < len; b++) {
        uint8_t block[16] = {};
        const size_t n = len - b * 16u < 16u ? len - b * 16u : 16u;
        if (ctrMode) {
            if (!single(counter, block)) {
                return false;
            }
            for (size_t i = 0; i < n; i++) {
                out[b * 16u + i] = (uint8_t)(in[b * 16u + i] ^ block[i]);
            }
            for (int i = 15; i >= 0 && ++counter[i] == 0; i--) {
            }
            continue;
        }
        memcpy(block, in + b * 16u, 16);
        if (!single(block, block)) {
            return false;
        }
        memcpy(out + b * 16u, block, 16);
    }
    return true;
}

bool CH32RVEcdc::encrypt(const uint8_t *in, uint8_t *out, size_t len)
{
    return len % 16u == 0 && run(false, false, nullptr, in, out, len);
}

bool CH32RVEcdc::decrypt(const uint8_t *in, uint8_t *out, size_t len)
{
    return len % 16u == 0 && run(false, true, nullptr, in, out, len);
}

bool CH32RVEcdc::ctr(const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len)
{
    return iv && run(true, false, iv, in, out, len);
}

#else

bool CH32RVEcdc::begin(Algorithm, const uint8_t *, size_t) { return false; }
void CH32RVEcdc::end() {}
bool CH32RVEcdc::encrypt(const uint8_t *, uint8_t *, size_t) { return false; }
bool CH32RVEcdc::decrypt(const uint8_t *, uint8_t *, size_t) { return false; }
bool CH32RVEcdc::ctr(const uint8_t *, const uint8_t *, uint8_t *, size_t) { return false; }

#endif
