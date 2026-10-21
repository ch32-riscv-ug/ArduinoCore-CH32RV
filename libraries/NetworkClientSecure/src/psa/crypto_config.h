/* TF-PSA-Crypto (the crypto side of Mbed TLS 4) for NetworkClientSecure.
 * Ours, in place of the release's default (tools/vendor/vendor_mbedtls.py).
 *
 * Key exchange ECDHE on X25519, P-256 and P-384; certificates signed with
 * ECDSA (P-256 / P-384) or RSA (PKCS#1 v1.5, PSS); records AES-GCM and
 * ChaCha20-Poly1305; SHA-256/384/512. PEM for setCACert() and the client
 * certificate and key. Entropy from the chip's RNG (mbedtls_platform_get_entropy
 * in NetworkClientSecure.cpp); the clock (time()) for certificate dates.
 *
 * The series with more RAM take the faster elliptic-curve settings (a 4-bit
 * window and fixed-point tables: about 6 KB more flash and 2.5 KB more heap
 * in a handshake, and P-256 / P-384 verification about 1.5x faster). */
#ifndef PSA_CRYPTO_CONFIG_H
#define PSA_CRYPTO_CONFIG_H

#define PSA_WANT_ALG_ECDH 1
#define PSA_WANT_ALG_ECDSA 1
#define PSA_WANT_ALG_DETERMINISTIC_ECDSA 1
#define PSA_WANT_ALG_RSA_PKCS1V15_SIGN 1
#define PSA_WANT_ALG_RSA_PSS 1
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_ALG_CHACHA20_POLY1305 1
#define PSA_WANT_ALG_SHA_256 1
#define PSA_WANT_ALG_SHA_384 1
#define PSA_WANT_ALG_SHA_512 1
#define PSA_WANT_ALG_HMAC 1
#define PSA_WANT_ALG_TLS12_PRF 1
#define PSA_WANT_ECC_SECP_R1_256 1
#define PSA_WANT_ECC_SECP_R1_384 1
#define PSA_WANT_ECC_MONTGOMERY_255 1
#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_KEY_TYPE_CHACHA20 1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY 1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC 1   /* ECDHE_RSA needs it in 4.1, even in a client */
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_IMPORT 1  /* a client certificate's RSA key */

#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_MS_TIME_ALT             /* mbedtls_ms_time() from millis(), NetworkClientSecure.cpp */
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA512_SMALLER
#define MBEDTLS_MPI_MAX_SIZE 512                 /* RSA-4096 roots */
#define MBEDTLS_ECP_NIST_OPTIM

#if !defined(CH32RV_SERIES_CH32V307) && !defined(CH32RV_SERIES_CH32V317) && !defined(CH32RV_SERIES_CH32V407) && \
    !defined(CH32RV_SERIES_CH32V467) && !defined(CH32RV_SERIES_CH32H415) && !defined(CH32RV_SERIES_CH32H416) && \
    !defined(CH32RV_SERIES_CH32H417) && !defined(NETWORK_HOST_TEST)
#error "NetworkClientSecure is for CH32V307/V317, V407/V467 and H415-H417 (flash, RAM and a hardware RNG): docs/network-stack.ja.md"
#endif

#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417) || defined(NETWORK_HOST_TEST)
#define MBEDTLS_ECP_WINDOW_SIZE 4
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 1
#else
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#endif

#endif
