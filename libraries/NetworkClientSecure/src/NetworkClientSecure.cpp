#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "NetworkClientSecure.h"

#include <string.h>
#include <time.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/psa_util.h"
#include "psa/crypto.h"

#include "NetworkCore.h"

#ifdef NETWORK_HOST_TEST
#include <stdio.h>
#else
#include <ch32rv_rng.h>
#endif

/* ---- entropy: the chip's RNG, or no TLS at all */
extern "C" int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits,
                                            unsigned char *output, size_t output_size)
{
    (void)flags;
#ifdef NETWORK_HOST_TEST
    FILE *f = fopen("/dev/urandom", "rb");
    const bool ok = f && fread(output, 1, output_size, f) == output_size;
    if (f) {
        fclose(f);
    }
#else
    const bool ok = ch32rv_rng_read(output, output_size);
#endif
    if (!ok) {
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    }
    *estimate_bits = 8 * output_size;
    return 0;
}

/* Mbed TLS's millisecond clock (timeouts, ticket ages): millis(), widened. */
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void)
{
    static uint32_t last, high;
    const uint32_t now = millis();
    if (now < last) {
        high++;
    }
    last = now;
    return (mbedtls_ms_time_t)high << 32 | now;
}

/* ---- the bundle: ESP-IDF's x509_crt_bundle format
 *   u32 offsets[n] (little endian; offsets[0] / 4 == n), then per root,
 *   sorted by name: u16 name length, u16 key length, the subject Name (DER),
 *   the SubjectPublicKeyInfo (DER). */
static bool bundle_find(const uint8_t *b, size_t size, const uint8_t *name, size_t nlen, const uint8_t **key,
                        size_t *klen)
{
    if (size < 4) {
        return false;
    }
    auto u32 = [b](size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; };
    const uint32_t count = u32(0) / 4;
    if (count == 0 || (size_t)count * 4 > size) {
        return false;
    }
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const uint32_t at = u32(mid * 4);
        if ((size_t)at + 4 > size) {
            return false;
        }
        const size_t l = b[at] | b[at + 1] << 8, k = b[at + 2] | b[at + 3] << 8;
        if ((size_t)at + 4 + l + k > size) {
            return false;
        }
        int c = memcmp(b + at + 4, name, l < nlen ? l : nlen);
        if (c == 0) {
            c = l < nlen ? -1 : l > nlen ? 1 : 0;
        }
        if (c == 0) {
            *key = b + at + 4 + l;
            *klen = k;
            return true;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

/* A root nobody holds the key to: Mbed TLS will not verify a chain with no CA
 * at all, and with a bundle the real roots come from bundle_verify(). */
static const unsigned char placeholder_ca[] = {
    0x30, 0x82, 0x01, 0xba, 0x30, 0x82, 0x01, 0x5f, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x14, 0x60, 0xec, 0x2f, 0x38, 0x71, 0x97, 0xd9, 0x3e, 0x35,
    0x11, 0xc3, 0xac, 0x89, 0xa3, 0x8e, 0x1b, 0x31, 0xa1, 0xb4, 0x1a, 0x30,
    0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30,
    0x31, 0x31, 0x2f, 0x30, 0x2d, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x26,
    0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x43, 0x6c, 0x69, 0x65, 0x6e,
    0x74, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x62, 0x75, 0x6e, 0x64,
    0x6c, 0x65, 0x20, 0x70, 0x6c, 0x61, 0x63, 0x65, 0x68, 0x6f, 0x6c, 0x64,
    0x65, 0x72, 0x30, 0x20, 0x17, 0x0d, 0x32, 0x36, 0x31, 0x30, 0x32, 0x31,
    0x30, 0x36, 0x32, 0x37, 0x34, 0x31, 0x5a, 0x18, 0x0f, 0x32, 0x31, 0x32,
    0x36, 0x30, 0x39, 0x32, 0x37, 0x30, 0x36, 0x32, 0x37, 0x34, 0x31, 0x5a,
    0x30, 0x31, 0x31, 0x2f, 0x30, 0x2d, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c,
    0x26, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x43, 0x6c, 0x69, 0x65,
    0x6e, 0x74, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x62, 0x75, 0x6e,
    0x64, 0x6c, 0x65, 0x20, 0x70, 0x6c, 0x61, 0x63, 0x65, 0x68, 0x6f, 0x6c,
    0x64, 0x65, 0x72, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03,
    0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x90, 0x7a, 0xfd, 0x2d, 0xf1, 0xf2,
    0xb4, 0xee, 0x4d, 0x44, 0x6e, 0x84, 0xff, 0x8c, 0x1c, 0x16, 0x1f, 0x11,
    0xf1, 0xf0, 0x41, 0x56, 0xc0, 0x1d, 0x19, 0x83, 0xf8, 0x60, 0x9b, 0xf7,
    0xcf, 0x94, 0x35, 0xc0, 0xe9, 0xc7, 0x58, 0xa4, 0xe4, 0x02, 0xd9, 0x63,
    0x3b, 0x5d, 0x7a, 0x2e, 0x1f, 0xfa, 0x84, 0x40, 0xb9, 0x6d, 0x17, 0x3a,
    0x43, 0x95, 0xe9, 0x27, 0x13, 0x34, 0x86, 0x48, 0x63, 0x75, 0xa3, 0x53,
    0x30, 0x51, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04,
    0x14, 0xfb, 0x26, 0xf0, 0xde, 0x37, 0x94, 0xca, 0x7c, 0xfb, 0x01, 0xc1,
    0x45, 0xe5, 0x6c, 0x27, 0x67, 0x33, 0x8f, 0xb0, 0x09, 0x30, 0x1f, 0x06,
    0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xfb, 0x26,
    0xf0, 0xde, 0x37, 0x94, 0xca, 0x7c, 0xfb, 0x01, 0xc1, 0x45, 0xe5, 0x6c,
    0x27, 0x67, 0x33, 0x8f, 0xb0, 0x09, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d,
    0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30,
    0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03,
    0x49, 0x00, 0x30, 0x46, 0x02, 0x21, 0x00, 0xdf, 0xc7, 0x8d, 0xbd, 0x81,
    0x67, 0x54, 0x07, 0x48, 0x47, 0x05, 0xc5, 0xbe, 0x01, 0x26, 0x8f, 0x24,
    0x79, 0x46, 0x97, 0x25, 0x03, 0xc4, 0x92, 0x02, 0x67, 0xf9, 0xb2, 0xe8,
    0x44, 0x86, 0xfc, 0x02, 0x21, 0x00, 0xff, 0x67, 0x5e, 0xf0, 0x87, 0x4a,
    0xec, 0x62, 0x68, 0x83, 0x7f, 0x2f, 0x44, 0xbb, 0xfb, 0x09, 0x4f, 0x05,
    0xc3, 0x2d, 0xb5, 0x56, 0xf4, 0x89, 0xde, 0x2b, 0x57, 0xc5, 0x69, 0x74,
    0xa4, 0xd8
};

static bool clock_is_set()
{
    return time(nullptr) >= 1735689600;   /* 2025-01-01 */
}

struct NetworkTlsSession {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_x509_crt own;
    mbedtls_pk_context key;
    NetworkClientSecure *client;
};

struct NetworkTlsGlue {
    static int send(void *arg, const unsigned char *buf, size_t len)
    {
        auto *c = static_cast<NetworkClientSecure *>(arg);
        const size_t n = c->NetworkClient::write(buf, len);
        if (n) {
            return (int)n;
        }
        return c->NetworkClient::connected() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    static int recv(void *arg, unsigned char *buf, size_t len)
    {
        auto *c = static_cast<NetworkClientSecure *>(arg);
        if (c->NetworkClient::available() > 0) {
            return c->NetworkClient::read(buf, len);
        }
        return c->NetworkClient::connected() ? MBEDTLS_ERR_SSL_WANT_READ : 0;   /* 0: the peer closed */
    }
    /* Called for each certificate, the top of the chain first. Dates are
     * judged only with a set clock; with a bundle, a chain whose top is not
     * trusted is checked against the bundle root named as its issuer. */
    static int verify(void *arg, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
    {
        auto *c = static_cast<NetworkClientSecure *>(arg);
        (void)depth;
        if (!clock_is_set()) {
            *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
        }
        if (c->_bundle && (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)) {
            const uint8_t *key;
            size_t klen;
            if (bundle_find(c->_bundle, c->_bundleSize, crt->issuer_raw.p, crt->issuer_raw.len, &key, &klen)) {
                mbedtls_pk_context pk;
                mbedtls_pk_init(&pk);
                unsigned char hash[64];
                size_t hlen = 0;
                const psa_algorithm_t alg = mbedtls_md_psa_alg_from_type(crt->sig_md);
                if (mbedtls_pk_parse_public_key(&pk, key, klen) == 0 &&
                    psa_hash_compute(alg, crt->tbs.p, crt->tbs.len, hash, sizeof hash, &hlen) == PSA_SUCCESS &&
                    mbedtls_pk_verify_ext(crt->sig_pk, &pk, crt->sig_md, hash, hlen, crt->sig.p, crt->sig.len) == 0) {
                    *flags &= ~(uint32_t)MBEDTLS_X509_BADCERT_NOT_TRUSTED;
                }
                mbedtls_pk_free(&pk);
            }
        }
        c->_verifyFlags |= *flags;
        return 0;
    }
};

NetworkClientSecure::NetworkClientSecure() {}

NetworkClientSecure::~NetworkClientSecure()
{
    stop();
}

void NetworkClientSecure::setCACert(const char *rootCA)
{
    _caPem = rootCA;
    _bundle = nullptr;
    _insecure = false;
}

void NetworkClientSecure::setCACertBundle(const uint8_t *bundle, size_t size)
{
    _bundle = size ? bundle : nullptr;
    _bundleSize = size;
    _caPem = nullptr;
    _insecure = false;
}

void NetworkClientSecure::setInsecure()
{
    _insecure = true;
    _caPem = nullptr;
    _bundle = nullptr;
}

void NetworkClientSecure::setCertificate(const char *clientCA)
{
    _certPem = clientCA;
}

void NetworkClientSecure::setPrivateKey(const char *privateKey)
{
    _keyPem = privateKey;
}

int NetworkClientSecure::connect(IPAddress ip, uint16_t port)
{
    return connect(ip, port, -1);
}

int NetworkClientSecure::connect(IPAddress ip, uint16_t port, int32_t timeout_ms)
{
    stop();
    if (!(timeout_ms < 0 ? NetworkClient::connect(ip, port) : NetworkClient::connect(ip, port, timeout_ms))) {
        return 0;
    }
    char host[16];
    snprintf(host, sizeof host, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return startTls(host);
}

int NetworkClientSecure::connect(const char *host, uint16_t port)
{
    return connect(host, port, -1);
}

int NetworkClientSecure::connect(const char *host, uint16_t port, int32_t timeout_ms)
{
    stop();
    if (!(timeout_ms < 0 ? NetworkClient::connect(host, port) : NetworkClient::connect(host, port, timeout_ms))) {
        return 0;
    }
    return startTls(host);
}

int NetworkClientSecure::startTls(const char *host)
{
    static bool psa_ready;
    _lastError = 0;
    _verifyFlags = 0;
    _peeked = -1;
    _version = 0;
    if (!psa_ready) {
        if (psa_crypto_init() != PSA_SUCCESS) {
            _lastError = MBEDTLS_ERR_SSL_HW_ACCEL_FAILED;
            NetworkClient::stop();
            return 0;
        }
        psa_ready = true;
    }
    NetworkTlsSession *s = new NetworkTlsSession;
    if (!s) {
        NetworkClient::stop();
        return 0;
    }
    _tls = s;
    s->client = this;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca);
    mbedtls_x509_crt_init(&s->own);
    mbedtls_pk_init(&s->key);
    int ret = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0) {
        mbedtls_ssl_conf_min_tls_version(&s->conf, (mbedtls_ssl_protocol_version)_minVersion);
        mbedtls_ssl_conf_max_tls_version(&s->conf, (mbedtls_ssl_protocol_version)_maxVersion);
        if (_insecure) {
            mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
        } else {
            mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            if (_caPem) {
                ret = mbedtls_x509_crt_parse(&s->ca, reinterpret_cast<const unsigned char *>(_caPem), strlen(_caPem) + 1);
            } else if (_bundle) {
                ret = mbedtls_x509_crt_parse_der(&s->ca, placeholder_ca, sizeof placeholder_ca);
            } else {
                ret = MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;   /* nothing to verify against: set one, or setInsecure() */
            }
            mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, nullptr);
            mbedtls_ssl_conf_verify(&s->conf, NetworkTlsGlue::verify, this);
        }
    }
    if (ret == 0 && _certPem && _keyPem) {
        ret = mbedtls_x509_crt_parse(&s->own, reinterpret_cast<const unsigned char *>(_certPem), strlen(_certPem) + 1);
        if (ret == 0) {
            ret = mbedtls_pk_parse_key(&s->key, reinterpret_cast<const unsigned char *>(_keyPem), strlen(_keyPem) + 1,
                                       nullptr, 0);
        }
        if (ret == 0) {
            ret = mbedtls_ssl_conf_own_cert(&s->conf, &s->own, &s->key);
        }
    }
    if (ret == 0) {
        ret = mbedtls_ssl_setup(&s->ssl, &s->conf);
    }
    if (ret == 0) {
        ret = mbedtls_ssl_set_hostname(&s->ssl, host);
    }
    if (ret == 0) {
        mbedtls_ssl_set_bio(&s->ssl, this, NetworkTlsGlue::send, NetworkTlsGlue::recv, nullptr);
        const uint32_t start = millis();
        while ((ret = mbedtls_ssl_handshake(&s->ssl)) == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if ((uint32_t)(millis() - start) >= _handshakeSeconds * 1000u) {
                ret = MBEDTLS_ERR_SSL_TIMEOUT;
                break;
            }
            ch32rv_net::pump();
        }
    }
    if (ret != 0) {
        _lastError = ret;
        stop();
        return 0;
    }
    _version = (uint16_t)mbedtls_ssl_get_version_number(&s->ssl);
    return 1;
}

size_t NetworkClientSecure::write(uint8_t data)
{
    return write(&data, 1);
}

size_t NetworkClientSecure::write(const uint8_t *buf, size_t size)
{
    if (!_tls) {
        return 0;
    }
    size_t done = 0;
    while (done < size) {
        const int ret = mbedtls_ssl_write(&_tls->ssl, buf + done, size - done);
        if (ret > 0) {
            done += (size_t)ret;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
            ch32rv_net::pump();
        } else {
            _lastError = ret;
            break;
        }
    }
    return done;
}

int NetworkClientSecure::available()
{
    if (!_tls) {
        return 0;
    }
    /* A zero-length read processes what has arrived, without taking any. */
    const int ret = mbedtls_ssl_read(&_tls->ssl, nullptr, 0);
    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
        ret != MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
        if (ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            _lastError = ret;
        }
    }
    return (int)mbedtls_ssl_get_bytes_avail(&_tls->ssl) + (_peeked >= 0 ? 1 : 0);
}

int NetworkClientSecure::read()
{
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
}

int NetworkClientSecure::read(uint8_t *buf, size_t size)
{
    if (!_tls || !size) {
        return -1;
    }
    size_t got = 0;
    if (_peeked >= 0) {
        buf[got++] = (uint8_t)_peeked;
        _peeked = -1;
        if (got == size) {
            return 1;
        }
    }
    int ret;
    do {   /* a TLS 1.3 server may send session tickets after the handshake: not data */
        ret = mbedtls_ssl_read(&_tls->ssl, buf + got, size - got);
    } while (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (ret > 0) {
        return (int)got + ret;
    }
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY && ret != 0) {
        _lastError = ret;
    }
    return got ? (int)got : -1;
}

int NetworkClientSecure::peek()
{
    if (_peeked < 0) {
        uint8_t b;
        int ret = 0;
        if (_tls) {
            do {
                ret = mbedtls_ssl_read(&_tls->ssl, &b, 1);
            } while (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
        }
        if (ret == 1) {
            _peeked = b;
        }
    }
    return _peeked;
}

void NetworkClientSecure::stop()
{
    if (_tls) {
        if (NetworkClient::connected()) {
            mbedtls_ssl_close_notify(&_tls->ssl);
        }
        mbedtls_ssl_free(&_tls->ssl);
        mbedtls_ssl_config_free(&_tls->conf);
        mbedtls_x509_crt_free(&_tls->ca);
        mbedtls_x509_crt_free(&_tls->own);
        mbedtls_pk_free(&_tls->key);
        delete _tls;
        _tls = nullptr;
    }
    _peeked = -1;
    _version = 0;
    NetworkClient::stop();
}

uint8_t NetworkClientSecure::connected()
{
    if (!_tls) {
        return 0;
    }
    if (available() > 0) {
        return 1;
    }
    return NetworkClient::connected();
}
