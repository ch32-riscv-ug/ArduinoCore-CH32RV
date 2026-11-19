# EcdcAes

[日本語](README.ja.md)

AES-128 by the H41x encryption module (ECDC): FIPS-197's example block encrypted and decrypted, then a string in CTR and back.

## What you need and wiring

A chip with the ECDC (H415 / H416 / H417). No wiring.

## Operation and expected result

`Serial` prints the lines below; the first is FIPS-197 C.1's value.

```
ECB encrypt: 69C4E0D86A7B0430D8CDB78070B4C55A
ECB decrypt: 00112233445566778899AABBCCDDEEFF
CTR encrypt: ...
CTR decrypt: Hello from the ECDC
```

## API and flow

`begin()` keeps the key; `encrypt()` / `decrypt()` work in ECB and `ctr()` in CTR. The hardware does the work.
