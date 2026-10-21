# NetworkClientSecure

TLS over the Network library's `NetworkClient`, with the API of ESP32's Arduino core 3.x `NetworkClientSecure`. It runs Mbed TLS 4.1 (LTS to March 2029); why is in [docs/network-stack.ja.md](../../docs/network-stack.ja.md).

## Targets and status

- CH32V307/V317, V407/V467 and H415-H417: flash, RAM and a hardware RNG. Other series stop with an error.
- Tested on the host against openssl servers (`tests/unit/network`):
  - Connects with ECDSA and RSA chains, a root certificate, a bundle and a client certificate.
  - Refuses a wrong root, a wrong name, a root missing from the bundle, and an impostor root (same name, another key).
- Not yet run on a chip (no Ethernet driver yet).
- **V307 RAM:** about 30 KB static plus 30-40 KB of heap in a handshake may not fit in 64 KB. The V307 can trade flash for RAM in its option bytes, which the core does not offer yet.

## Use

```cpp
#include <NetworkClientSecure.h>

NetworkClientSecure client;

void setup() {
  // after the link is up (ETH ...)
  configTime(9 * 3600, 0, "pool.ntp.org");   // for certificate dates (below)
  client.setCACert(rootCaPem);               // or setCACertBundle(x509_crt_bundle, x509_crt_bundle_len)
  if (client.connect("example.com", 443)) {
    client.print("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
  }
}
```

- `setCACert(pem)` verifies against a root certificate.
- `setCACertBundle(bundle, size)` verifies against a bundle in ESP-IDF's format (root names and public keys). ESP32CertBundle's `x509_crt_bundle` works as is.
- `setInsecure()` skips verification (testing only).
- `setCertificate(pem)` / `setPrivateKey(pem)` are for services that ask for a client certificate (AWS IoT ...).
- Strings and bundles are kept by pointer, so they must stay valid while connected.
- `lastError()` gives the Mbed TLS error, and `verifyFlags()` the certificate verification flags.

## Behaviour

- TLS 1.2: ECDHE (X25519, P-256, P-384), ECDSA and RSA certificates, AES-GCM and ChaCha20-Poly1305.
- **Dates** are checked once the clock is set (after 2025), by `configTime()` SNTP or `settimeofday()` from an RTC. Before that a certificate is not refused for its dates, as the board cannot judge them.
- **Randomness:** keys come from the chip's RNG. If the RNG keeps failing, the connection fails instead of carrying on with weak randomness.
- **Time:** the handshake's public-key work takes about 1-6 s at 144 MHz, depending on the certificates. The limit is 120 s by default (`setHandshakeTimeout()`), and the network keeps running meanwhile.
- The V407 and H41x use the faster elliptic-curve settings: about 6 KB more flash and 2.5 KB more heap, with verification about 1.5x faster.

## Size (2026-10-21, build only)

An HTTPS GET sketch, Network library included:

- V307: 192 KB of flash, 29.5 KB of static RAM.
- H417: 199 KB of flash.
- TLS adds 30-40 KB of heap during a handshake.

## License

Mbed TLS is Apache-2.0 (`src/mbedtls-src/LICENSE`); the vendored release and file hashes are in `vendor/mbedtls.lock.toml`.
