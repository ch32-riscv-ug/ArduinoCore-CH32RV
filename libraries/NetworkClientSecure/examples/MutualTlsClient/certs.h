// en: Put your own here: the server's address (a name or an IP its certificate names), your CA, and this board's
//     certificate and key (PEM, the key unencrypted). README.md shows how to make a test set with openssl.
// ja: 自分のものを入れる: サーバーのアドレス（名前か、その証明書にある IP）、自分の CA、このボードの証明書と鍵
//     （PEM。鍵は暗号化しない）。試験用の一式を openssl で作る手順は README.ja.md にある。
#pragma once

static const char *SERVER_HOST = "192.168.1.10";

static const char CA_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "...your CA...\n"
    "-----END CERTIFICATE-----\n";

static const char CLIENT_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "...this board's certificate...\n"
    "-----END CERTIFICATE-----\n";

static const char CLIENT_KEY[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "...this board's key...\n"
    "-----END PRIVATE KEY-----\n";
