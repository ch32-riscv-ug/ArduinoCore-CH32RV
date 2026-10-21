/* TLS over NetworkClient, as ESP32's NetworkClientSecure: Mbed TLS 4.1, TLS
 * 1.2 and 1.3 (the server picks), the server verified against a root certificate (setCACert), a root
 * bundle in ESP-IDF's format (setCACertBundle - ESP32CertBundle's
 * x509_crt_bundle works as is), or not at all (setInsecure). An optional client
 * certificate and key for services that ask for one.
 *
 * Certificate dates are checked once the clock is set (configTime(), or
 * settimeofday() from an RTC): before that the board cannot know them, and
 * a certificate is not refused for it. */
#pragma once
#include <Network.h>

struct NetworkTlsSession;

class NetworkClientSecure : public NetworkClient {
public:
    NetworkClientSecure();
    ~NetworkClientSecure();
    NetworkClientSecure(const NetworkClientSecure &) = delete;
    NetworkClientSecure &operator=(const NetworkClientSecure &) = delete;

    int connect(IPAddress ip, uint16_t port) override;
    int connect(IPAddress ip, uint16_t port, int32_t timeout_ms);
    int connect(const char *host, uint16_t port) override;
    int connect(const char *host, uint16_t port, int32_t timeout_ms);
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    void flush() override {}
    void stop() override;
    uint8_t connected() override;
    operator bool() override { return connected(); }

    /* What the server is checked against. Each replaces the others. The PEM
     * text and the bundle are kept by pointer: they must stay valid. */
    void setCACert(const char *rootCA);
    void setCACertBundle(const uint8_t *bundle, size_t size);
    void setInsecure();                      /* no check at all: test use only */
    /* A client certificate and its key, PEM; kept by pointer. */
    void setCertificate(const char *clientCA);
    void setPrivateKey(const char *privateKey);
    /* The versions to offer: TLS 1.2 to 1.3 by default, and the server picks
     * the newest it has. Narrow them for a server that must not fall back, or
     * one that mishandles TLS 1.3. */
    enum class TlsVersion : uint16_t { TLS1_2 = 0x0303, TLS1_3 = 0x0304 };
    void setTlsVersions(TlsVersion min, TlsVersion max) { _minVersion = min; _maxVersion = max; }
    /* The version the current connection agreed on: 0x0303 (TLS 1.2), 0x0304 (TLS 1.3), 0 when none. */
    uint16_t tlsVersion() const { return _version; }
    /* The handshake's whole budget; ESP32's default, as the public-key
     * operations take seconds on these chips. */
    void setHandshakeTimeout(unsigned long seconds) { _handshakeSeconds = seconds; }
    /* The Mbed TLS error that ended the last connect() or transfer (0 none), and
     * the certificate verification flags (MBEDTLS_X509_BADCERT_...). */
    int lastError() const { return _lastError; }
    uint32_t verifyFlags() const { return _verifyFlags; }

private:
    int startTls(const char *host);
    friend struct NetworkTlsGlue;

    NetworkTlsSession *_tls = nullptr;
    const char *_caPem = nullptr;
    const uint8_t *_bundle = nullptr;
    size_t _bundleSize = 0;
    const char *_certPem = nullptr;
    const char *_keyPem = nullptr;
    bool _insecure = false;
    unsigned long _handshakeSeconds = 120;
    int _lastError = 0;
    uint32_t _verifyFlags = 0;
    int _peeked = -1;
    TlsVersion _minVersion = TlsVersion::TLS1_2, _maxVersion = TlsVersion::TLS1_3;
    uint16_t _version = 0;
};
