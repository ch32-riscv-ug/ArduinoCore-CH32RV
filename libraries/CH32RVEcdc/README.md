# CH32RVEcdc

[日本語](README.ja.md)

The encryption module of CH32H415 / H416 / H417 (ECDC, CH32H417RM chapter 41): AES (128, 192 or 256-bit key) and SM4
(128-bit), in ECB and CTR, done by the hardware. Bytes are in the standards' order: FIPS-197 and GB/T 32907 test
vectors go through as printed.

```cpp
#include <CH32RVEcdc.h>
CH32RVEcdc ecdc;
ecdc.begin(CH32RVEcdc::AES, key, 16);     // 16, 24 or 32 bytes (SM4: 16)
ecdc.encrypt(plain, cipher, 64);          // ECB, len a multiple of 16
ecdc.decrypt(cipher, plain, 64);
ecdc.ctr(iv, plain, cipher, 100);         // CTR, any length; the same call decrypts
```

## API

| Function | What it does |
|---|---|
| `begin(algorithm, key, keyLen)` | Keeps the key and clocks the module. AES takes 16, 24 or 32 bytes, SM4 16; another length, or a series without the ECDC, returns false |
| `end()` | The clock off and the kept key cleared |
| `encrypt(in, out, len)` / `decrypt(in, out, len)` | ECB; `len` a multiple of 16 (false otherwise) |
| `ctr(iv, in, out, len)` | CTR; `iv` is the 16-byte counter, one up per block, big-endian (as NIST SP 800-38A's examples); `iv` is left as it was |

`in` and `out` may be the same buffer.

## Speed and placement

Pairs of blocks between buffers on 32-byte boundaries (`alignas(32)`) go memory to memory: the module reads and
writes RAM itself while the CPU waits. Other buffers, an odd block and a CTR tail shorter than a block go one block at
a time through its registers: the same result, more of the CPU's time.

That is down to what the hardware did on the bench (CH32H417, 2026-11-19):
- Memory to memory ignores bit 4 of the destination: given an address 16 bytes past a 32-byte boundary, it wrote
  16 bytes before it.
- It works in 32 bytes: three blocks wrote four, the fourth a copy of the third.
- Block by block, CTR takes the counter once, at the key expansion, and does not count it. So CTR on that path is ECB
  on the counter, XORed by the CPU.

## Test

`selfapi::test_ecdc` (tests/instrumented/selfapi) checks FIPS-197 C.1 .. C.3 (AES-128 / 192 / 256) and GB/T 32907 A.1
(SM4) both ways, ECB over three blocks, and NIST SP 800-38A F.5.1 (AES-128 CTR) on 32-byte boundaries, one byte off,
16 bytes off, at 50 bytes and in place. It passes on H417 (2026-11-19).
