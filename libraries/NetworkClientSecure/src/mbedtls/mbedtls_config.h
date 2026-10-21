/* Mbed TLS (the TLS side) for NetworkClientSecure: a TLS 1.2 client with
 * ECDHE and ECDSA / RSA certificates, SNI, and a client certificate for
 * services that ask for one (AWS IoT and the like). Ours, in place of the
 * release's default (tools/vendor/vendor_mbedtls.py); the crypto side is
 * psa/crypto_config.h. Fixed per build: a sketch's defines do not reach it. */
#pragma once

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
/* Servers send records of up to 16 KB unless they agree to less, and few do. */
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
