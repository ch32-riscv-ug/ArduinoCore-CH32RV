# HttpsGet

[日本語](README.ja.md)

Fetch a page over HTTPS with the server's certificate checked against one root certificate, and print the agreed TLS version, the handshake time, the HTTP status and the body size - or, when the connection fails, the Mbed TLS error and the certificate verification flags.

## Before running

- A board with an Ethernet MAC and a hardware RNG: CH32V307 / V317, V407 / V467, H415-H417.
- The board's RJ45 on a LAN with DHCP and a route to the internet.
- The serial monitor at 115200 baud on `Serial`.
- **CH32V307 / V317:** the handshake needs 30-40 KB of heap. With the factory 64 KB of RAM it does not fit; choose Tools → Flash / RAM Split → 192 KB + 128 KB. When the heap is short, `connect()` fails and the error is -0x7F00 or -141 (out of memory).

## Operation

After `Address ...`, the sketch waits up to 10 s for SNTP (the certificates' dates are checked once the clock is set), then fetches the page once a minute:

```text
Address 192.168.1.88
TLS 0x304 in 1830 ms
HTTP 200, 2345 bytes
```

`0x304` is TLS 1.3, `0x303` TLS 1.2. The times and sizes vary with the server and the series.

## API

- `setCACert(pem)` verifies the server against one root certificate. The PEM is kept by pointer, so it must stay valid while connected; here it is a constant array.
- `connect(host, port)` connects by host name: the certificate is checked against it, so do not connect by IP address.
- `lastError(buf, size)` returns Mbed TLS's error number and writes its text; `verifyFlags()` gives the verification flags (non-zero when the chain or the name was refused).
- `tlsVersion()` is the version agreed.

## Adapting it

- Another host: set `HOST`, and `ROOT_CA` to the root its chain ends at (`openssl s_client -connect host:443 -showcerts` shows the chain). A wrong root fails with verify flags set; that is the check working.
- Several roots: `setCACertBundle()` takes a bundle in ESP-IDF's format (ESP32CertBundle's `x509_crt_bundle` works as is).
- `setInsecure()` skips the check. Use it only for testing: anyone on the path can then read and change the traffic.
