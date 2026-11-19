# MutualTlsClient

[日本語](README.ja.md)

TLS with a client certificate (mutual TLS): the board checks the server against your CA, and the server checks the
board's own certificate. It talks to a local server ([mtls_server.py](mtls_server.py), or `openssl s_server -Verify 1 -www`) and prints whether the
handshake passed, the TLS version, and the server's first reply line - or why it failed.

## What you need and the wiring

- A board with an Ethernet MAC and an RNG: CH32V307 / V317, V407 / V467, H415 to H417.
- The board's RJ45 on a LAN with DHCP and a way out to the internet (SNTP sets the clock), and a PC on the same LAN.
- The serial monitor is `Serial`, 115200 baud.
- **CH32V307 / V317:** Tools → Flash / RAM Split → 192 KB + 128 KB (the handshake needs 30 to 40 KB of heap). With
  Stack Check on Reset on overflow, set Stack Size to 8 KB (a handshake with a client certificate takes about 4.2 KB of
  stack).

## Certificates and the server (PC)

Make a test CA, a server certificate (naming the PC's IP) and the board's certificate with openssl (the PC is
192.168.1.10 here).

```sh
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 3650 -subj "/CN=test CA" -out ca.pem
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -key server.key -subj "/CN=192.168.1.10" -out server.csr
printf "subjectAltName=IP:192.168.1.10,DNS:192.168.1.10\nextendedKeyUsage=serverAuth\n" > server.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 825 -extfile server.ext -out server.pem
openssl ecparam -name prime256v1 -genkey -noout -out board.key
openssl req -new -key board.key -subj "/CN=board 1" -out board.csr
printf "extendedKeyUsage=clientAuth\n" > board.ext
openssl x509 -req -in board.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 825 -extfile board.ext -out board.pem
openssl pkcs8 -topk8 -nocrypt -in board.key -out board8.key
python3 mtls_server.py 192.168.1.10
```

In [certs.h](certs.h) set `SERVER_HOST` to the PC's IP, `CA_CERT` to ca.pem, `CLIENT_CERT` to board.pem and
`CLIENT_KEY` to board8.key (one `"...\n"` per line).

## What to do and what to expect

```text
Address 192.168.1.81
mutual TLS 0x304 in 1286 ms
server says: HTTP/1.0 200 ok
```

The PC prints `TLSv1.3 client {'commonName': 'board 1'}`. With a `CLIENT_CERT` from another CA, the server refuses
(`refused: ... certificate verify failed` on the PC). Under TLS 1.3 the board's side of the handshake ends first, so the
board prints `mutual TLS 0x304` and then `server says: (nothing)` (closed without a reply). Both checked on a CH32V307
(2026-11-20).

## The API and the flow

| API | Its use here |
|---|---|
| `setCACert(CA_CERT)` | The CA the server's certificate is checked against. |
| `setCertificate(CLIENT_CERT)` / `setPrivateKey(CLIENT_KEY)` | The board's certificate and key (PEM, the key unencrypted): shown when the server asks, and the key signs. |
| `connect(host, port)` / `lastError()` / `verifyFlags()` | The connection, and on failure the Mbed TLS error and the verification flags. |

All three are kept by pointer: keep them valid while connected. The key sits in the flash in the clear; for a key
that must not be read out, consider read protection (ch32rv's `target option`).

